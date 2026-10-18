// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_check.h"

#include "base/test/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/test/navigation_simulator.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

using privacy_policy::ProtectionLevel;

constexpr char kPageUrl[] = "https://www.news.test/a";

class BreakageCheckTest : public ChromeRenderViewHostTestHarness {
 protected:
  const GURL page_{kPageUrl};

  BreakageCheck* Check() {
    BreakageCheck::CreateForWebContents(web_contents());
    return BreakageCheck::FromWebContents(web_contents());
  }

  // Commits the reload TryLowerLevel or RestoreLevel started, to the end of
  // its loading.
  void FinishReload() {
    auto reload = content::NavigationSimulator::CreateFromPending(web_contents()->GetController());
    reload->Commit();
  }

  bool HasChoice() { return privacy_policy::GetChoice(profile(), page_).has_value(); }
};

TEST_F(BreakageCheckTest, LowersOneLevelAndAsksOnceReloaded) {
  NavigateAndCommit(page_);
  privacy_policy::SetLevel(profile(), page_, ProtectionLevel::kStrict);
  int changes = 0;
  auto subscription = Check()->AddChangedCallback(base::BindLambdaForTesting([&] { ++changes; }));

  ASSERT_TRUE(Check()->TryLowerLevel());
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kReloading);
  EXPECT_EQ(Check()->from(), ProtectionLevel::kStrict);
  EXPECT_EQ(Check()->to(), ProtectionLevel::kStandard);
  // Standard is the default: the site's choice is cleared, not written.
  EXPECT_FALSE(HasChoice());

  FinishReload();
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kAsking);
  EXPECT_GE(changes, 2);
}

TEST_F(BreakageCheckTest, KeepingTheLevelHelped) {
  NavigateAndCommit(page_);
  ASSERT_TRUE(Check()->TryLowerLevel());  // Standard, the default, to Off
  FinishReload();
  Check()->KeepLevel();
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kAnswered);
  EXPECT_EQ(Check()->outcome(), Outcome::kLowerHelped);
  EXPECT_EQ(privacy_policy::GetLevel(profile(), page_), ProtectionLevel::kOff);
}

TEST_F(BreakageCheckTest, RestoringPutsTheExactChoiceBack) {
  NavigateAndCommit(page_);
  // No choice: the site had the default, Standard.
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  ASSERT_TRUE(HasChoice());  // Off was written
  Check()->RestoreLevel();
  FinishReload();
  EXPECT_FALSE(HasChoice());
  EXPECT_EQ(Check()->outcome(), Outcome::kLowerDidntHelp);
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kAnswered);

  // An explicit Strict comes back as Strict.
  privacy_policy::SetLevel(profile(), page_, ProtectionLevel::kStrict);
  Check()->Done();
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  Check()->RestoreLevel();
  FinishReload();
  EXPECT_EQ(privacy_policy::GetChoice(profile(), page_), ProtectionLevel::kStrict);
}

TEST_F(BreakageCheckTest, TheSnapshotIsTakenBeforeLowering) {
  NavigateAndCommit(page_);
  PageProtections::RecordBlocked(
      main_rfh()->GetGlobalId(),
      {GURL("https://t.test/x.js"), page_, network::mojom::RequestDestination::kScript, "GET"});
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  ASSERT_EQ(Check()->blocked().size(), 1u);
  EXPECT_EQ(Check()->blocked()[0].url, GURL("https://t.test/x.js"));
}

TEST_F(BreakageCheckTest, AnOffSiteHasNothingLower) {
  NavigateAndCommit(page_);
  privacy_policy::SetLevel(profile(), page_, ProtectionLevel::kOff);
  EXPECT_FALSE(Check()->TryLowerLevel());
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kNone);
}

TEST_F(BreakageCheckTest, AnotherSiteEndsTheCheckAndKeepsTheLevel) {
  NavigateAndCommit(page_);
  ASSERT_TRUE(Check()->TryLowerLevel());
  FinishReload();
  NavigateAndCommit(GURL("https://other.test/"));
  EXPECT_EQ(Check()->step(), BreakageCheck::Step::kNone);
  EXPECT_EQ(privacy_policy::GetLevel(profile(), page_), ProtectionLevel::kOff);
}

TEST_F(BreakageCheckTest, NoStepWhereProtectionsDontApply) {
  NavigateAndCommit(GURL("chrome://no-such-page/"));
  EXPECT_FALSE(Check()->TryLowerLevel());
}

}  // namespace
}  // namespace ghost::protections
