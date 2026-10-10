// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The protections button and the panel it opens, against a test server
// reached through host-resolver rules.

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/ui/protections/protections_button.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget.h"

namespace ghost::protections {
namespace {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

std::unique_ptr<HttpResponse> ServePage(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content("<html><head></head><body>page</body></html>");
  return response;
}

// Reads a part of the panel's page.
constexpr char kPanelText[] = R"((selector => {
  const node = document.querySelector('protections-app').shadowRoot.querySelector(selector);
  return node ? node.textContent.trim() : '';
}))";

class ProtectionsPanelBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(&ServePage));
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(blocking::BlockingService::GetIfStarted());
    blocking::BlockingService::GetIfStarted()->SetListsForTesting(
        {"||tracker.test^$third-party\n"});
  }

  GURL Page(std::string_view host) { return embedded_test_server()->GetURL(host, "/page"); }

  ProtectionsButton* Button(Browser* in) {
    return views::ElementTrackerViews::GetInstance()->GetFirstMatchingViewAs<ProtectionsButton>(
        kProtectionsButtonElementId, views::ElementTrackerViews::GetContextForWidget(
                                         BrowserView::GetBrowserViewForBrowser(in)->GetWidget()));
  }

  content::WebContents* Tab(Browser* in) { return in->tab_strip_model()->GetActiveWebContents(); }

  // Loads |n| images from |host| in the tab and waits until each has loaded
  // or failed.
  void LoadImages(Browser* in, std::string_view host, int n) {
    EXPECT_EQ(content::EvalJs(Tab(in), content::JsReplace(R"(Promise.all(
                  Array.from({length: $2}, (_, i) => new Promise(done => {
                    const img = new Image();
                    img.onload = img.onerror = done;
                    img.src = $1 + '?' + i;
                  }))).then(() => 'done'))",
                                                          Page(host), n)),
              "done");
  }

  // Clicks the button and waits until the panel shows; returns its page.
  content::WebContents* OpenPanel(Browser* in) {
    ProtectionsButton* button = Button(in);
    views::test::ButtonTestApi(button).NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
    views::Widget* widget = button->bubble_manager_for_testing()->GetBubbleWidget();
    if (!widget) {
      return nullptr;
    }
    views::test::WidgetVisibleWaiter(widget).Wait();
    return button->bubble_manager_for_testing()->GetContentsWrapper()->web_contents();
  }

  static std::string PanelText(content::WebContents* panel, std::string_view selector) {
    return content::EvalJs(panel,
                           content::JsReplace(base::StrCat({kPanelText, "($1)"}), selector))
        .ExtractString();
  }

  // Waits until the panel shows |text| in |selector|: its state arrives over
  // Mojo after the browser's.
  static bool WaitForPanelText(content::WebContents* panel,
                               std::string_view selector,
                               std::string_view text) {
    return content::EvalJs(panel, content::JsReplace(
                                      base::StrCat({"new Promise(done => { const read = ",
                                                    kPanelText,
                                                    "; const check = () => read($1) === $2 ? "
                                                    "done(true) : setTimeout(check, 20); "
                                                    "check(); })"}),
                                      selector, text))
        .ExtractBool();
  }
};

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheBadgeCountsTheBlockedRequests) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  ProtectionsButton* button = Button(browser());
  ASSERT_TRUE(button);
  EXPECT_TRUE(button->GetEnabled());
  EXPECT_EQ(button->badge_text_for_testing(), u"");
  LoadImages(browser(), "tracker.test", 3);
  EXPECT_EQ(button->badge_text_for_testing(), u"3");
  EXPECT_EQ(button->GetTooltipText(), u"Protections: Standard, 3 requests blocked");
  EXPECT_EQ(button->GetViewAccessibility().GetCachedName(),
            u"Protections: Standard, 3 requests blocked");
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, ThePanelShowsTheTabsSiteAndCount) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("www.news.test")));
  LoadImages(browser(), "tracker.test", 3);
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  EXPECT_EQ(panel->GetLastCommittedURL(), GURL("chrome://protections.top-chrome/"));
  EXPECT_EQ(PanelText(panel, "#site"), "news.test");
  EXPECT_EQ(PanelText(panel, "#count"), "3");
  EXPECT_EQ(PanelText(panel, "#note"), "");
  // While it is open, the count follows the page.
  LoadImages(browser(), "tracker.test", 2);
  EXPECT_TRUE(WaitForPanelText(panel, "#count", "5"));
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, OffInThePanelReloadsTheSiteUnprotected) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadImages(browser(), "tracker.test", 2);
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  content::TestNavigationObserver reload(Tab(browser()));
  ASSERT_TRUE(content::ExecJs(panel, R"(document.querySelector('protections-app')
      .shadowRoot.querySelector('input[name=level][value="0"]').click())"));
  reload.Wait();

  const base::Value* choice =
      browser()->GetProfile()->GetPrefs()->GetDict(privacy_policy::kSiteLevelsPref).Find("news.test");
  ASSERT_TRUE(choice);
  EXPECT_EQ(*choice, base::Value("off"));
  ProtectionsButton* button = Button(browser());
  EXPECT_TRUE(button->shows_off_for_testing());
  EXPECT_EQ(button->badge_text_for_testing(), u"");
  EXPECT_EQ(button->GetTooltipText(), u"Protections: Off");
  // Off: the tracker loads, and nothing is counted.
  LoadImages(browser(), "tracker.test", 2);
  EXPECT_EQ(button->badge_text_for_testing(), u"");
  EXPECT_TRUE(WaitForPanelText(panel, ".big.off", "Off"));
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheButtonIsDisabledWhereProtectionsDontApply) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));
  ProtectionsButton* button = Button(browser());
  EXPECT_FALSE(button->GetEnabled());
  EXPECT_EQ(button->GetTooltipText(), u"Protections don't apply to this page");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  EXPECT_TRUE(button->GetEnabled());
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, IncognitoIsStrictForTheSession) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, Page("news.test")));
  content::WebContents* panel = OpenPanel(incognito);
  ASSERT_TRUE(panel);
  EXPECT_EQ(PanelText(panel, "label:has(input[value=\"2\"]:checked) .default-mark"),
            "· default");
  EXPECT_EQ(PanelText(panel, "#note"), "Changes here last until you close all Incognito windows.");
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheButtonFollowsTheActiveTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadImages(browser(), "tracker.test", 3);
  ProtectionsButton* button = Button(browser());
  ASSERT_EQ(button->badge_text_for_testing(), u"3");
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), Page("shop.test"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  EXPECT_EQ(button->badge_text_for_testing(), u"");
  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_EQ(button->badge_text_for_testing(), u"3");
}

}  // namespace
}  // namespace ghost::protections
