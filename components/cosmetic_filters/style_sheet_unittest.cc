// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/style_sheet.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::cosmetic_filters {
namespace {

TEST(StyleSheetTest, OneRulePerSelector) {
  EXPECT_EQ(StyleSheetFor({".a", "#b > div"}),
            ".a{display:none !important}\n#b > div{display:none !important}\n");
}

TEST(StyleSheetTest, NoSelectorsNoSheet) {
  EXPECT_EQ(StyleSheetFor({}), "");
}

// Lists will arrive by update (3E): a selector that closes its rule would
// inject arbitrary style.
TEST(StyleSheetTest, UnsafeSelectorsAreDropped) {
  for (const char* unsafe : {".a{color:red}", ".a}body{", ".a;", "@import url(x)", ".a/*", ".a*/",
                             ".a\\", ".a\n.b", ".a\r", ".a\f", ""}) {
    SCOPED_TRACE(unsafe);
    EXPECT_EQ(StyleSheetFor({unsafe, ".ok"}), ".ok{display:none !important}\n");
  }
}

}  // namespace
}  // namespace ghost::cosmetic_filters
