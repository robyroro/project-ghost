// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_check.h"

#include <utility>

#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/browser/protections/panel_state.h"
#include "ghost/components/site/registrable_domain.h"
#include "url/gurl.h"

namespace ghost::protections {

namespace {

using privacy_policy::ProtectionLevel;

GURL PageOf(content::WebContents& contents) {
  return contents.GetPrimaryMainFrame()->GetLastCommittedURL();
}

}  // namespace

std::optional<ProtectionLevel> OneLower(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kStrict:
      return ProtectionLevel::kStandard;
    case ProtectionLevel::kStandard:
      return ProtectionLevel::kOff;
    case ProtectionLevel::kOff:
      return std::nullopt;
  }
}

BreakageCheck::BreakageCheck(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<BreakageCheck>(*contents) {}

BreakageCheck::~BreakageCheck() = default;

bool BreakageCheck::TryLowerLevel() {
  content::WebContents& contents = GetWebContents();
  const GURL page = PageOf(contents);
  if (!page.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  content::BrowserContext* context = contents.GetBrowserContext();
  const ProtectionLevel level = privacy_policy::GetLevel(context, page);
  const std::optional<ProtectionLevel> lower = OneLower(level);
  if (!lower) {
    return false;
  }
  site_ = std::string(RegistrableDomain(page.host()));
  choice_before_ = privacy_policy::GetChoice(context, page);
  from_ = level;
  to_ = *lower;
  outcome_ = Outcome::kNotTried;
  blocked_.clear();
  more_hosts_ = false;
  if (PageProtections* protections = PageProtections::FromWebContents(&contents)) {
    blocked_ = protections->blocked_requests();
    more_hosts_ = protections->more_hosts();
  }
  reloaded_ = false;
  SetStep(Step::kReloading);
  ChooseLevel(contents, *lower);  // reloads
  return true;
}

void BreakageCheck::KeepLevel() {
  if (step_ != Step::kAsking) {
    return;
  }
  outcome_ = Outcome::kLowerHelped;
  SetStep(Step::kAnswered);
}

void BreakageCheck::RestoreLevel() {
  if (step_ != Step::kAsking) {
    return;
  }
  content::WebContents& contents = GetWebContents();
  const GURL page = PageOf(contents);
  content::BrowserContext* context = contents.GetBrowserContext();
  if (choice_before_) {
    privacy_policy::SetLevel(context, page, *choice_before_);
  } else {
    privacy_policy::ClearLevel(context, page);
  }
  outcome_ = Outcome::kLowerDidntHelp;
  SetStep(Step::kAnswered);
  contents.GetController().Reload(content::ReloadType::NORMAL, /*check_for_repost=*/true);
}

void BreakageCheck::Done() {
  blocked_.clear();
  more_hosts_ = false;
  outcome_ = Outcome::kNotTried;
  SetStep(Step::kNone);
}

base::CallbackListSubscription BreakageCheck::AddChangedCallback(
    base::RepeatingClosure callback) {
  return changed_.Add(std::move(callback));
}

void BreakageCheck::PrimaryPageChanged(content::Page& page) {
  if (step_ == Step::kNone) {
    return;
  }
  const GURL url = page.GetMainDocument().GetLastCommittedURL();
  if (RegistrableDomain(url.host()) != site_) {
    Done();
    return;
  }
  reloaded_ = true;
}

void BreakageCheck::DidStopLoading() {
  if (step_ == Step::kReloading && reloaded_) {
    SetStep(Step::kAsking);
  }
}

void BreakageCheck::SetStep(Step step) {
  step_ = step;
  changed_.Notify();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(BreakageCheck);

}  // namespace ghost::protections
