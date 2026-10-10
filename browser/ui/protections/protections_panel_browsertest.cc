// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The protections button and the panel it opens.

#include <string>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/bubble/webui_bubble_manager.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/top_chrome/webui_contents_wrapper.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/ui/protections/protections_button.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/interaction/element_tracker_views.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget.h"

namespace ghost::protections {
namespace {

class ProtectionsPanelBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override { host_resolver()->AddRule("*", "127.0.0.1"); }

  ProtectionsButton* Button() {
    return views::ElementTrackerViews::GetInstance()->GetFirstMatchingViewAs<ProtectionsButton>(
        kProtectionsButtonElementId,
        views::ElementTrackerViews::GetContextForWidget(
            BrowserView::GetBrowserViewForBrowser(browser())->GetWidget()));
  }

  void Click(views::Button* button) {
    views::test::ButtonTestApi(button).NotifyClick(
        ui::MouseEvent(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON));
  }

  // Clicks the button and waits until the panel shows; returns its page.
  content::WebContents* OpenPanel() {
    ProtectionsButton* button = Button();
    Click(button);
    views::Widget* widget = button->bubble_manager_for_testing()->GetBubbleWidget();
    if (!widget) {
      return nullptr;
    }
    views::test::WidgetVisibleWaiter(widget).Wait();
    return button->bubble_manager_for_testing()->GetContentsWrapper()->web_contents();
  }
};

IN_PROC_BROWSER_TEST_F(ProtectionsPanelBrowserTest, TheButtonOpensThePanel) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("www.news.test", "/empty.html")));
  ASSERT_TRUE(Button());
  content::WebContents* panel = OpenPanel();
  ASSERT_TRUE(panel);
  EXPECT_EQ(panel->GetLastCommittedURL(), GURL("chrome://protections.top-chrome/"));
  EXPECT_EQ(content::EvalJs(panel, "document.querySelector('protections-app')"
                                   ".shadowRoot.querySelector('#site').textContent"),
            "news.test");
}

}  // namespace
}  // namespace ghost::protections
