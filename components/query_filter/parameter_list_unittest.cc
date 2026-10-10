// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/parameter_list.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::query_filter {
namespace {

TEST(ParameterListTest, ReadsGroupsEntriesPrefixesAndSites) {
  const ParameterList list = ParseParameterList(
      "# a comment\n"
      "[click]\n"
      "fbclid   # Meta\n"
      "si youtube.com youtu.be\n"
      "\n"
      "[campaign]\n"
      "utm_*\n");
  ASSERT_EQ(list.click.size(), 2u);
  EXPECT_EQ(list.click[0].name, "fbclid");
  EXPECT_FALSE(list.click[0].prefix);
  EXPECT_TRUE(list.click[0].sites.empty());
  EXPECT_EQ(list.click[1].sites, (std::vector<std::string>{"youtube.com", "youtu.be"}));
  ASSERT_EQ(list.campaign.size(), 1u);
  EXPECT_EQ(list.campaign[0].name, "utm_");
  EXPECT_TRUE(list.campaign[0].prefix);
}

TEST(ParameterListTest, SkipsMalformedLinesAndKeepsTheRest) {
  std::vector<std::string> skipped;
  const ParameterList list = ParseParameterList(
      "orphan\n"           // before any group
      "[click]\n"
      "*\n"                // an empty prefix would match everything
      "a*b\n"              // * only at the end
      "x Youtube.com\n"    // domains are lowercase
      "y *.example.com\n"  // and plain
      "z example\n"        // with a dot
      "[unknown]\n"
      "inside_unknown\n"
      "[click]\n"
      "gclid\n",
      &skipped);
  ASSERT_EQ(list.click.size(), 1u);
  EXPECT_EQ(list.click[0].name, "gclid");
  EXPECT_EQ(skipped.size(), 8u);
}

TEST(ParameterListTest, TheShippedListParsesWithoutASkippedLine) {
  std::vector<std::string> skipped;
  const ParameterList list = ParseParameterList(ShippedParameterListText(), &skipped);
  EXPECT_TRUE(skipped.empty()) << skipped.front();
  EXPECT_GT(list.click.size(), 60u);
  EXPECT_EQ(list.campaign.size(), 1u);
}

}  // namespace
}  // namespace ghost::query_filter
