// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/features/feature_overrides.h"

#include <algorithm>
#include <vector>

#include "components/prefs/testing_pref_service.h"
#include "ghost/browser/features/local_state_prefs.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features.h"

namespace ghost {
namespace {

bool ForcesGpc(bool enabled) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  local_state.SetBoolean(kGpcEnabledPref, enabled);
  std::vector<base::FeatureList::FeatureOverrideInfo> overrides;
  AppendFeatureOverrides(local_state, &overrides);
  return std::ranges::any_of(overrides, [](const auto& entry) {
    return &entry.first.get() == &blink::features::kGlobalPrivacyControlForce &&
           entry.second == base::FeatureList::OVERRIDE_ENABLE_FEATURE;
  });
}

TEST(FeatureOverridesTest, GpcIsForcedByDefault) {
  TestingPrefServiceSimple local_state;
  RegisterLocalStatePrefs(local_state.registry());
  EXPECT_TRUE(local_state.GetBoolean(kGpcEnabledPref));
  EXPECT_TRUE(ForcesGpc(true));
}

TEST(FeatureOverridesTest, GpcTurnedOffIsLeftToUpstream) {
  EXPECT_FALSE(ForcesGpc(false));
}

}  // namespace
}  // namespace ghost
