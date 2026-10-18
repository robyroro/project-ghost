// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/explain.h"

#include <string>
#include <vector>

#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::blocking {
namespace {

using network::mojom::RequestDestination;

CheckRequest Script(const char* url) {
  return {GURL(url), GURL("https://a.test/"), RequestDestination::kScript, "GET"};
}

TEST(ExplainTest, NamesTheRuleForEachRequest) {
  const std::vector<std::string> lists = {"||tracker.test^$third-party\n", "||ads.test/banner\n"};
  EXPECT_EQ(ExplainBlocks(lists, {Script("https://tracker.test/t.js"),
                                  Script("https://ads.test/banner.js")}),
            (std::vector<std::string>{"||tracker.test^$third-party", "||ads.test/banner"}));
}

TEST(ExplainTest, ARequestTheListsAllowHasNoRule) {
  EXPECT_EQ(ExplainBlocks({"||tracker.test^\n"}, {Script("https://cdn.test/x.js")}),
            (std::vector<std::string>{""}));
}

TEST(ExplainTest, WithoutListsNothingIsExplained) {
  EXPECT_EQ(ExplainBlocks({}, {Script("https://tracker.test/t.js")}),
            (std::vector<std::string>{""}));
}

}  // namespace
}  // namespace ghost::blocking
