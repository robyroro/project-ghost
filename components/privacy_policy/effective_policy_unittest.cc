// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/privacy_policy/effective_policy.h"

#include <optional>

#include "ghost/components/privacy_policy/protection_level.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::privacy_policy {
namespace {

TEST(EffectivePolicyTest, OffTurnsEverySiteProtectionOff) {
  const EffectivePolicy policy = PolicyFor(ProtectionLevel::kOff, /*campaign_pref=*/true);
  EXPECT_FALSE(policy.block_requests);
  EXPECT_FALSE(policy.strip_click_identifiers);
  EXPECT_FALSE(policy.strip_campaign_parameters);
  EXPECT_TRUE(policy.cross_site_referrer);
}

TEST(EffectivePolicyTest, StandardBlocksThirdPartiesAndStripsClickIdentifiers) {
  const EffectivePolicy policy = PolicyFor(ProtectionLevel::kStandard, /*campaign_pref=*/false);
  EXPECT_TRUE(policy.block_requests);
  EXPECT_FALSE(policy.check_same_site);
  EXPECT_TRUE(policy.strip_click_identifiers);
  EXPECT_FALSE(policy.strip_campaign_parameters);
  EXPECT_TRUE(policy.cross_site_referrer);
  // The profile's campaign pref (3B) still applies at Standard.
  EXPECT_TRUE(PolicyFor(ProtectionLevel::kStandard, /*campaign_pref=*/true)
                  .strip_campaign_parameters);
}

TEST(EffectivePolicyTest, StrictDoesEverything) {
  const EffectivePolicy policy = PolicyFor(ProtectionLevel::kStrict, /*campaign_pref=*/false);
  EXPECT_TRUE(policy.block_requests);
  EXPECT_TRUE(policy.check_same_site);
  EXPECT_TRUE(policy.strip_click_identifiers);
  EXPECT_TRUE(policy.strip_campaign_parameters);
  EXPECT_FALSE(policy.cross_site_referrer);
}

TEST(EffectivePolicyTest, TheSitesChoiceWinsOverTheMode) {
  EXPECT_EQ(ResolveLevel(ProtectionLevel::kStrict, std::nullopt), ProtectionLevel::kStrict);
  EXPECT_EQ(ResolveLevel(ProtectionLevel::kStrict, ProtectionLevel::kOff), ProtectionLevel::kOff);
  EXPECT_EQ(ResolveLevel(ProtectionLevel::kStandard, ProtectionLevel::kStrict),
            ProtectionLevel::kStrict);
}

TEST(ProtectionLevelTest, PrefStringsRoundTrip) {
  for (ProtectionLevel level :
       {ProtectionLevel::kOff, ProtectionLevel::kStandard, ProtectionLevel::kStrict}) {
    EXPECT_EQ(ParseProtectionLevel(ToPrefString(level)), level);
  }
  EXPECT_EQ(ToPrefString(ProtectionLevel::kOff), "off");
  EXPECT_EQ(ParseProtectionLevel("OFF"), std::nullopt);
  EXPECT_EQ(ParseProtectionLevel(""), std::nullopt);
  EXPECT_EQ(ParseProtectionLevel("strictest"), std::nullopt);
}

}  // namespace
}  // namespace ghost::privacy_policy
