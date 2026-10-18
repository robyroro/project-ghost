// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The protections button and the panel it opens, against a test server
// reached through host-resolver rules.

#include <memory>
#include <optional>
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
#include "ghost/browser/protections/breakage_check.h"
#include "ghost/browser/ui/protections/protections_button.h"
#include "net/base/url_util.h"
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
        {"||tracker.test^$third-party\n||metrics.news.test^\n"});
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

  // Clicks the element |selector| in the panel's shadow root.
  static bool ClickInPanel(content::WebContents* panel, std::string_view selector) {
    return content::ExecJs(
        panel, content::JsReplace("document.querySelector('protections-app').shadowRoot"
                                  ".querySelector($1).click()",
                                  selector));
  }

  // Waits until the element |selector| exists in the panel.
  static bool WaitForPanelElement(content::WebContents* panel, std::string_view selector) {
    return content::EvalJs(
               panel,
               content::JsReplace(
                   "new Promise(done => { const check = () => "
                   "document.querySelector('protections-app').shadowRoot.querySelector($1) ? "
                   "done(true) : setTimeout(check, 20); check(); })",
                   selector))
        .ExtractBool();
  }

  // Loads one script from |url| in the tab and waits until it has loaded or
  // failed.
  void LoadScript(Browser* in, const GURL& url) {
    EXPECT_EQ(content::EvalJs(Tab(in), content::JsReplace(R"(new Promise(done => {
                  const s = document.createElement('script');
                  s.onload = s.onerror = () => done('done');
                  s.src = $1;
                  document.head.append(s);
                }))",
                                                          url)),
              "done");
  }

  // The panel's page now, once the bubble shows (it may have reopened).
  content::WebContents* PanelOf(Browser* in) {
    ProtectionsButton* button = Button(in);
    views::Widget* widget = button->bubble_manager_for_testing()->GetBubbleWidget();
    if (!widget) {
      return nullptr;
    }
    views::test::WidgetVisibleWaiter(widget).Wait();
    return button->bubble_manager_for_testing()->GetContentsWrapper()->web_contents();
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

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheLinkOpensTheSettings) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ui_test_utils::TabAddedWaiter added(browser());
  ASSERT_TRUE(content::ExecJs(panel, R"(document.querySelector('protections-app')
      .shadowRoot.querySelector('#settings').click())"));
  content::WebContents* settings = added.Wait();
  ASSERT_TRUE(content::WaitForLoadStop(settings));
  EXPECT_EQ(settings->GetLastCommittedURL(), GURL("chrome://settings/protections"));
  EXPECT_EQ(Tab(browser()), settings);
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, AnOffDefaultTurnsTheButtonOff) {
  browser()->GetProfile()->GetPrefs()->SetString(privacy_policy::kDefaultLevelPref, "off");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadImages(browser(), "tracker.test", 3);
  ProtectionsButton* button = Button(browser());
  EXPECT_TRUE(button->shows_off_for_testing());
  EXPECT_EQ(button->badge_text_for_testing(), u"");
  EXPECT_EQ(button->GetTooltipText(), u"Protections: Off");
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, SiteNotWorkingLowersAndAsks) {
  privacy_policy::SetLevel(browser()->GetProfile(), Page("news.test"),
                           privacy_policy::ProtectionLevel::kStrict);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  LoadScript(browser(), Page("metrics.news.test"));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  EXPECT_TRUE(WaitForPanelText(panel, "#offer-reload", "Reload with Standard"));

  content::TestNavigationObserver reload(Tab(browser()));
  ASSERT_TRUE(ClickInPanel(panel, "#offer-reload"));
  reload.Wait();
  // The panel asks once the reload has loaded, reopened if it closed.
  panel = PanelOf(browser());
  ASSERT_TRUE(panel);
  EXPECT_TRUE(WaitForPanelText(panel, "#ask-title", "Is it working now?"));
  EXPECT_EQ(privacy_policy::GetLevel(browser()->GetProfile(), Page("news.test")),
            privacy_policy::ProtectionLevel::kStandard);

  // No: Strict comes back, and the report shows what Strict blocked.
  content::TestNavigationObserver restore(Tab(browser()));
  ASSERT_TRUE(ClickInPanel(panel, "#ask-no"));
  restore.Wait();
  EXPECT_EQ(privacy_policy::GetChoice(browser()->GetProfile(), Page("news.test")),
            privacy_policy::ProtectionLevel::kStrict);
  panel = PanelOf(browser());
  ASSERT_TRUE(panel);
  EXPECT_TRUE(WaitForPanelElement(panel, "#report-open"));
  EXPECT_EQ(PanelText(panel, "#report-site"), "Report news.test");
  const std::string fields = PanelText(panel, "#report-fields");
  EXPECT_NE(fields.find("Standard didn't fix it; back to Strict"), std::string::npos) << fields;
  EXPECT_NE(fields.find("metrics.news.test  ||metrics.news.test^"), std::string::npos) << fields;
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheReportOpensGitHubsFormAndNothingElse) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("www.news.test", "/page?session=s3cr3t")));
  content::WebContents* page_tab = Tab(browser());
  LoadImages(browser(), "tracker.test", 1);
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  content::TestNavigationObserver reload(page_tab);
  ASSERT_TRUE(ClickInPanel(panel, "#offer-reload"));
  reload.Wait();
  panel = PanelOf(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(WaitForPanelElement(panel, "#ask-yes"));
  ASSERT_TRUE(ClickInPanel(panel, "#ask-yes"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  ASSERT_TRUE(content::ExecJs(panel, R"(
      const root = document.querySelector('protections-app').shadowRoot;
      root.querySelector('#report-category').value = '1';
      root.querySelector('#report-category').dispatchEvent(new Event('change'));
      root.querySelector('#report-details').value = 'The video stays black.';
      root.querySelector('#report-details').dispatchEvent(new Event('input'));)"));

  ui_test_utils::TabAddedWaiter added(browser());
  ASSERT_TRUE(ClickInPanel(panel, "#report-open"));
  added.Wait();
  content::WebContents* issue = Tab(browser());
  ASSERT_NE(issue, page_tab);
  const GURL url = issue->GetVisibleURL();
  EXPECT_EQ(url.host(), "github.com");
  EXPECT_EQ(url.path(), "/robyroro/project-ghost/issues/new");
  std::string value;
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "site", &value));
  EXPECT_EQ(value, embedded_test_server()->GetURL("www.news.test", "/page").spec());
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "category", &value));
  EXPECT_EQ(value, "Video or audio");
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "details", &value));
  EXPECT_EQ(value, "The video stays black.");
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "outcome", &value));
  EXPECT_EQ(value, "Off fixed it");
  ASSERT_TRUE(net::GetValueForKeyInQuery(url, "blocked", &value));
  EXPECT_EQ(value, "tracker.test  ||tracker.test^$third-party");
  EXPECT_EQ(url.spec().find("s3cr3t"), std::string::npos);
  // The report ends the step.
  EXPECT_EQ(BreakageCheck::FromWebContents(page_tab)->step(), BreakageCheck::Step::kNone);
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, AnOffSiteGoesStraightToTheReport) {
  privacy_policy::SetLevel(browser()->GetProfile(), Page("news.test"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  const std::string fields = PanelText(panel, "#report-fields");
  EXPECT_NE(fields.find("The site was already Off"), std::string::npos) << fields;
  EXPECT_NE(fields.find("Nothing was blocked on this page."), std::string::npos) << fields;
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, IncognitosReportStaysIncognito) {
  // Incognito's HTTPS-First is strict: an http: page would get its
  // interstitial (4A's finding).
  net::EmbeddedTestServer https(net::EmbeddedTestServer::TYPE_HTTPS);
  https.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  https.RegisterRequestHandler(base::BindRepeating(&ServePage));
  ASSERT_TRUE(https.Start());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, https.GetURL("a.test", "/page")));
  content::WebContents* panel = OpenPanel(incognito);
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  content::TestNavigationObserver reload(Tab(incognito));
  ASSERT_TRUE(ClickInPanel(panel, "#offer-reload"));
  reload.Wait();
  panel = PanelOf(incognito);
  ASSERT_TRUE(panel);
  ASSERT_TRUE(WaitForPanelElement(panel, "#ask-yes"));
  ASSERT_TRUE(ClickInPanel(panel, "#ask-yes"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  content::WebContents* page_tab = Tab(incognito);
  ui_test_utils::TabAddedWaiter added(incognito);
  ASSERT_TRUE(ClickInPanel(panel, "#report-open"));
  added.Wait();
  content::WebContents* issue = Tab(incognito);
  ASSERT_NE(issue, page_tab);
  EXPECT_EQ(issue->GetVisibleURL().host(), "github.com");
  EXPECT_TRUE(issue->GetBrowserContext()->IsOffTheRecord());
}

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, ALongDetailsTextIsABadMessage) {
  // The panel's renderer is driven past its own limit (maxlength): the
  // browser must refuse what a compromised renderer could send.
  privacy_policy::SetLevel(browser()->GetProfile(), Page("news.test"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Page("news.test")));
  content::WebContents* panel = OpenPanel(browser());
  ASSERT_TRUE(panel);
  ASSERT_TRUE(ClickInPanel(panel, "#not-working"));
  ASSERT_TRUE(WaitForPanelElement(panel, "#report-open"));
  content::RenderProcessHostBadMojoMessageWaiter bad(panel->GetPrimaryMainFrame()->GetProcess());
  ASSERT_TRUE(content::ExecJs(panel, R"(
      const root = document.querySelector('protections-app').shadowRoot;
      const details = root.querySelector('#report-details');
      details.removeAttribute('maxlength');
      details.value = 'x'.repeat(2001);
      details.dispatchEvent(new Event('input'));
      // After this script returns: the browser ends the renderer at once.
      setTimeout(() => root.querySelector('#report-open').click());)"));
  const std::optional<std::string> error = bad.Wait();
  ASSERT_TRUE(error.has_value());
  EXPECT_NE(error->find("OpenReport"), std::string::npos) << *error;
  EXPECT_EQ(browser()->tab_strip_model()->count(), 1);
}

}  // namespace
}  // namespace ghost::protections
