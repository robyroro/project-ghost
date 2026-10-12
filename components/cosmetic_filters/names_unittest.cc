// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/names.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::cosmetic_filters {
namespace {

TEST(NamesTest, AName) {
  EXPECT_TRUE(IsValidName("ad-banner"));
  EXPECT_TRUE(IsValidName(std::string(kMaxNameLength, 'a')));
  EXPECT_FALSE(IsValidName(std::string(kMaxNameLength + 1, 'a')));
  EXPECT_FALSE(IsValidName(""));
  EXPECT_FALSE(IsValidName("\xff"));
}

TEST(NamesTest, ACall) {
  EXPECT_TRUE(AreValidNames(std::vector<std::string>(kMaxNamesPerCall - 1, "c"), {"i"}));
  EXPECT_FALSE(AreValidNames(std::vector<std::string>(kMaxNamesPerCall, "c"), {"i"}));
  EXPECT_FALSE(AreValidNames({"c"}, {""}));
}

}  // namespace
}  // namespace ghost::cosmetic_filters
