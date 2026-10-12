// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/procedural.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::cosmetic_filters {
namespace {

TEST(ProceduralTest, KeepsWellFormedRules) {
  const std::vector<std::string> rules = {
      R"({"selector":[{"type":"css-selector","arg":".a"},{"type":"has-text","arg":"Ad"}]})",
      R"({"selector":[{"type":"css-selector","arg":".a"}],"action":{"type":"remove"}})",
      R"({"selector":[{"type":"css-selector","arg":".a"},{"type":"upward","arg":"2"}],)"
      R"("action":{"type":"style","arg":"color: red"}})",
  };
  EXPECT_EQ(SafeProceduralFilters(rules), rules);
}

TEST(ProceduralTest, DropsWhatCouldEscape) {
  EXPECT_TRUE(SafeProceduralFilters({
                  "not json",
                  "[]",
                  R"({"selector":[]})",
                  R"({"action":{"type":"remove"}})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],"action":{}})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],"action":"remove"})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],)"
                  R"("action":{"type":"style","arg":"x}body{display:none"}})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],)"
                  R"("action":{"type":"style"}})",
              })
                  .empty());
}

TEST(ProceduralTest, MarkerAttributes) {
  EXPECT_TRUE(IsMarkerAttribute("shade-0-0123456789abcdef"));
  for (const char* name :
       {"", "shade-", "class", "shade-A", "shade-a b", "shade-a]",
        "shade-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}) {
    EXPECT_FALSE(IsMarkerAttribute(name)) << name;
  }
}

}  // namespace
}  // namespace ghost::cosmetic_filters
