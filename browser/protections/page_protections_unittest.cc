// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/page_protections.h"

#include "base/scoped_observation.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

class PageProtectionsTest : public ChromeRenderViewHostTestHarness {
 protected:
  int Count() {
    PageProtections* protections = PageProtections::FromWebContents(web_contents());
    return protections ? protections->blocked_count() : 0;
  }
};

TEST_F(PageProtectionsTest, CountsTheMainFrameAndSubframes) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  content::RenderFrameHost* child =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("ad");
  child = content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("https://ads.test/frame"), child);
  PageProtections::RecordBlocked(child->GetGlobalId());
  EXPECT_EQ(Count(), 2);
}

TEST_F(PageProtectionsTest, ANavigationResetsTheCount) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  ASSERT_EQ(Count(), 1);
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(Count(), 0);
}

TEST_F(PageProtectionsTest, IgnoresFramesOfAnotherPage) {
  NavigateAndCommit(GURL("https://news.test/"));
  content::GlobalRenderFrameHostId old_page = main_rfh()->GetGlobalId();
  NavigateAndCommit(GURL("https://other.test/"));
  PageProtections::RecordBlocked(old_page);
  PageProtections::RecordBlocked(content::GlobalRenderFrameHostId());
  EXPECT_EQ(Count(), 0);
}

TEST_F(PageProtectionsTest, TellsItsObservers) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::CreateForWebContents(web_contents());
  struct Recorder : PageProtections::Observer {
    void OnBlockedCountChanged() override { ++calls; }
    int calls = 0;
  } recorder;
  base::ScopedObservation<PageProtections, PageProtections::Observer> observation(&recorder);
  observation.Observe(PageProtections::FromWebContents(web_contents()));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId());
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(recorder.calls, 2);  // the block, then the reset
}

}  // namespace
}  // namespace ghost::protections
