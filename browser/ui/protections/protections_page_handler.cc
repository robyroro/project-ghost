// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_page_handler.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/protections/breakage_check.h"
#include "ghost/browser/protections/panel_state.h"
#include "ghost/browser/ui/protections/protections_ui.h"
#include "ghost/version/version.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/gurl.h"

namespace ghost::protections {

namespace {

using privacy_policy::ProtectionLevel;

mojom::Level ToMojo(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return mojom::Level::kOff;
    case ProtectionLevel::kStandard:
      return mojom::Level::kStandard;
    case ProtectionLevel::kStrict:
      return mojom::Level::kStrict;
  }
}

constexpr size_t kMaxDetails = 2000;
constexpr size_t kMaxAddress = 2048;

mojom::Step ToMojo(BreakageCheck::Step step) {
  switch (step) {
    case BreakageCheck::Step::kNone:
      return mojom::Step::kNone;
    case BreakageCheck::Step::kReloading:
      return mojom::Step::kReloading;
    case BreakageCheck::Step::kAsking:
      return mojom::Step::kAsking;
    case BreakageCheck::Step::kAnswered:
      return mojom::Step::kAnswered;
  }
}

Category FromMojo(mojom::Category category) {
  switch (category) {
    case mojom::Category::kContent:
      return Category::kContent;
    case mojom::Category::kMedia:
      return Category::kMedia;
    case mojom::Category::kSignIn:
      return Category::kSignIn;
    case mojom::Category::kOther:
      return Category::kOther;
  }
}

ProtectionLevel FromMojo(mojom::Level level) {
  switch (level) {
    case mojom::Level::kOff:
      return ProtectionLevel::kOff;
    case mojom::Level::kStandard:
      return ProtectionLevel::kStandard;
    case mojom::Level::kStrict:
      return ProtectionLevel::kStrict;
  }
}

}  // namespace

ProtectionsPageHandler::ProtectionsPageHandler(mojo::PendingReceiver<mojom::PageHandler> receiver,
                                               mojo::PendingRemote<mojom::Page> page,
                                               content::WebUI* web_ui,
                                               ProtectionsUI* ui)
    : receiver_(this, std::move(receiver)), page_(std::move(page)), web_ui_(web_ui), ui_(ui) {}

ProtectionsPageHandler::~ProtectionsPageHandler() = default;

void ProtectionsPageHandler::GetState(GetStateCallback callback) {
  Follow(ActiveTab());
  std::move(callback).Run(State());
}

void ProtectionsPageHandler::SetLevel(mojom::Level level) {
  if (!web_contents()) {
    return;
  }
  ChooseLevel(*web_contents(), FromMojo(level));
  SendState();
}

void ProtectionsPageHandler::ShowUI() {
  ui_->ShowUI();
}

void ProtectionsPageHandler::OpenSettings() {
  content::WebContents* tab = ActiveTab();
  if (!tab) {
    return;
  }
  tab->OpenURL(content::OpenURLParams(GURL("chrome://settings/protections"), content::Referrer(),
                                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                      ui::PAGE_TRANSITION_LINK,
                                      /*is_renderer_initiated=*/false),
               /*navigation_handle_callback=*/{});
  ui_->CloseUI();
}

void ProtectionsPageHandler::TryLowerLevel() {
  if (web_contents()) {
    BreakageCheck::FromWebContents(web_contents())->TryLowerLevel();
  }
}

void ProtectionsPageHandler::KeepLevel() {
  if (web_contents()) {
    BreakageCheck::FromWebContents(web_contents())->KeepLevel();
  }
}

void ProtectionsPageHandler::RestoreLevel() {
  if (web_contents()) {
    BreakageCheck::FromWebContents(web_contents())->RestoreLevel();
  }
}

void ProtectionsPageHandler::GetReport(GetReportCallback callback) {
  blocking::BlockingService* service = blocking::BlockingService::GetIfStarted();
  if (!web_contents() || !service) {
    std::move(callback).Run(mojom::Report::New());
    return;
  }
  content::WebContents& tab = *web_contents();
  const GURL page = tab.GetPrimaryMainFrame()->GetLastCommittedURL();
  const PanelState panel = GetPanelState(tab);
  BreakageCheck* check = BreakageCheck::FromWebContents(&tab);
  PageProtections* protections = PageProtections::FromWebContents(&tab);

  ReportInputs inputs;
  inputs.page = page;
  inputs.version = DisplayVersion();
  std::vector<blocking::CheckRequest> requests;
  if (check && check->step() != BreakageCheck::Step::kNone) {
    inputs.level = check->from();
    inputs.tried = check->to();
    inputs.outcome = check->outcome();
    requests = check->blocked();
    inputs.more_hosts = check->more_hosts();
  } else {
    inputs.level = panel.level;
    inputs.outcome =
        panel.level == ProtectionLevel::kOff ? Outcome::kAlreadyOff : Outcome::kNotTried;
    if (protections) {
      requests = protections->blocked_requests();
      inputs.more_hosts = protections->more_hosts();
    }
  }
  for (const blocking::CheckRequest& request : requests) {
    inputs.blocked.emplace_back(std::string(request.url.host()), std::string());
  }
  service->DescribeBlocks(
      std::move(requests),
      base::BindOnce(&ProtectionsPageHandler::OnReportDescribed, weak_factory_.GetWeakPtr(), page,
                     std::move(inputs), std::move(callback)));
}

void ProtectionsPageHandler::OnReportDescribed(GURL page,
                                               ReportInputs inputs,
                                               GetReportCallback callback,
                                               std::vector<std::string> rules,
                                               std::vector<std::string> list_labels) {
  for (size_t i = 0; i < inputs.blocked.size() && i < rules.size(); ++i) {
    inputs.blocked[i].second = std::move(rules[i]);
  }
  inputs.lists = std::move(list_labels);
  report_ = BuildReport(inputs);
  report_page_ = page;
  auto report = mojom::Report::New();
  report->site = report_->site;
  report->address = report_->address;
  for (const ReportField& field : report_->fields) {
    report->fields.push_back(mojom::ReportField::New(field.label, field.value));
  }
  std::move(callback).Run(std::move(report));
}

void ProtectionsPageHandler::OpenReport(mojom::Category category,
                                        const std::string& details,
                                        const std::string& address) {
  // The panel's renderer isn't trusted: it chooses words, never the URL.
  if (details.size() > kMaxDetails || address.size() > kMaxAddress ||
      !base::IsStringUTF8(details) || !base::IsStringUTF8(address)) {
    receiver_.ReportBadMessage("OpenReport: details or address too long or not UTF-8");
    return;
  }
  content::WebContents* tab = ActiveTab();
  if (!tab || !report_) {
    return;
  }
  const GURL url =
      IssueUrl(*report_, FromMojo(category), details, CheckedAddress(address, report_page_));
  if (BreakageCheck* check = BreakageCheck::FromWebContents(tab)) {
    check->Done();
  }
  report_.reset();
  tab->OpenURL(content::OpenURLParams(url, content::Referrer(),
                                      WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                      ui::PAGE_TRANSITION_LINK,
                                      /*is_renderer_initiated=*/false),
               /*navigation_handle_callback=*/{});
  ui_->CloseUI();
}

void ProtectionsPageHandler::OnBlockedCountChanged() {
  SendState();
}

void ProtectionsPageHandler::WebContentsDestroyed() {
  // Before the tab's PageProtections and BreakageCheck go with it.
  protections_observation_.Reset();
  check_subscription_ = {};
  Observe(nullptr);
}

content::WebContents* ProtectionsPageHandler::ActiveTab() {
  BrowserWindowInterface* browser = webui::GetBrowserWindowInterface(web_ui_->GetWebContents());
  return browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
}

void ProtectionsPageHandler::Follow(content::WebContents* tab) {
  if (tab == web_contents()) {
    return;
  }
  protections_observation_.Reset();
  check_subscription_ = {};
  Observe(tab);
  if (tab) {
    PageProtections::CreateForWebContents(tab);
    protections_observation_.Observe(PageProtections::FromWebContents(tab));
    BreakageCheck::CreateForWebContents(tab);
    check_subscription_ = BreakageCheck::FromWebContents(tab)->AddChangedCallback(
        base::BindRepeating(&ProtectionsPageHandler::SendState, base::Unretained(this)));
  }
}

mojom::StatePtr ProtectionsPageHandler::State() {
  auto state = mojom::State::New();
  if (!web_contents()) {
    state->level = mojom::Level::kStandard;
    state->mode_default = mojom::Level::kStandard;
    state->check_from = mojom::Level::kStandard;
    state->check_to = mojom::Level::kOff;
    return state;
  }
  PanelState panel = GetPanelState(*web_contents());
  state->applies = panel.applies;
  state->site = panel.site;
  state->level = ToMojo(panel.level);
  state->mode_default = ToMojo(panel.mode_default);
  state->blocked_count = panel.blocked_count;
  state->off_the_record = panel.off_the_record;
  if (BreakageCheck* check = BreakageCheck::FromWebContents(web_contents())) {
    state->step = ToMojo(check->step());
    state->check_from = ToMojo(check->from());
    state->check_to = ToMojo(check->to());
  } else {
    state->check_from = state->level;
    state->check_to = state->level;
  }
  return state;
}

void ProtectionsPageHandler::SendState() {
  page_->OnStateChanged(State());
}

}  // namespace ghost::protections
