// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/page_protections.h"

#include "base/scoped_observation.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/test_renderer_host.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

blocking::CheckRequest Request(const char* url) {
  return {GURL(url), GURL("https://news.test/"), network::mojom::RequestDestination::kScript,
          "GET"};
}

class PageProtectionsTest : public ChromeRenderViewHostTestHarness {
 protected:
  int Count() {
    PageProtections* protections = PageProtections::FromWebContents(web_contents());
    return protections ? protections->blocked_count() : 0;
  }
};

TEST_F(PageProtectionsTest, CountsTheMainFrameAndSubframes) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/x.js"));
  content::RenderFrameHost* child =
      content::RenderFrameHostTester::For(main_rfh())->AppendChild("ad");
  child = content::NavigationSimulator::NavigateAndCommitFromDocument(
      GURL("https://ads.test/frame"), child);
  PageProtections::RecordBlocked(child->GetGlobalId(), Request("https://t.test/x.js"));
  EXPECT_EQ(Count(), 2);
}

TEST_F(PageProtectionsTest, ANavigationResetsTheCount) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/x.js"));
  ASSERT_EQ(Count(), 1);
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(Count(), 0);
}

TEST_F(PageProtectionsTest, IgnoresFramesOfAnotherPage) {
  NavigateAndCommit(GURL("https://news.test/"));
  content::GlobalRenderFrameHostId old_page = main_rfh()->GetGlobalId();
  NavigateAndCommit(GURL("https://other.test/"));
  PageProtections::RecordBlocked(old_page, Request("https://t.test/x.js"));
  PageProtections::RecordBlocked(content::GlobalRenderFrameHostId(), Request("https://t.test/x.js"));
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
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/x.js"));
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(recorder.calls, 2);  // the block, then the reset
}

TEST_F(PageProtectionsTest, KeepsTheFirstBlockedRequestOfEachHost) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/a.js"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/b.js"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://ads.test/c.js"));
  PageProtections* protections = PageProtections::FromWebContents(web_contents());
  ASSERT_EQ(protections->blocked_requests().size(), 2u);
  EXPECT_EQ(protections->blocked_requests()[0].url, GURL("https://t.test/a.js"));
  EXPECT_EQ(protections->blocked_requests()[1].url, GURL("https://ads.test/c.js"));
  EXPECT_EQ(protections->blocked_count(), 3);
  EXPECT_FALSE(protections->more_hosts());
}

TEST_F(PageProtectionsTest, KeepsAtMostFiftyHosts) {
  NavigateAndCommit(GURL("https://news.test/"));
  for (int i = 0; i < 60; ++i) {
    PageProtections::RecordBlocked(
        main_rfh()->GetGlobalId(),
        Request(base::StrCat({"https://t", base::NumberToString(i), ".test/x.js"}).c_str()));
  }
  PageProtections* protections = PageProtections::FromWebContents(web_contents());
  EXPECT_EQ(protections->blocked_requests().size(), PageProtections::kMaxBlockedHosts);
  EXPECT_TRUE(protections->more_hosts());
  EXPECT_EQ(protections->blocked_count(), 60);
}

TEST_F(PageProtectionsTest, ANavigationForgetsTheBlockedRequests) {
  NavigateAndCommit(GURL("https://news.test/"));
  PageProtections::RecordBlocked(main_rfh()->GetGlobalId(), Request("https://t.test/a.js"));
  NavigateAndCommit(GURL("https://other.test/"));
  PageProtections* protections = PageProtections::FromWebContents(web_contents());
  EXPECT_TRUE(protections->blocked_requests().empty());
  EXPECT_FALSE(protections->more_hosts());
}

}  // namespace
}  // namespace ghost::protections
