// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/query_filter.h"

#include <optional>
#include <string>

#include "base/no_destructor.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::query_filter {
namespace {

const ParameterList& TestList() {
  static const base::NoDestructor<ParameterList> list(ParseParameterList(
      "[click]\nfbclid\ngclid\nsi youtube.com youtu.be\n[campaign]\nutm_*\n"));
  return *list;
}

// The URL Strip() makes, or "unchanged".
std::string Stripped(std::string_view url, Scope scope = Scope::kClick) {
  std::optional<GURL> result = Strip(GURL(url), TestList(), scope);
  return result ? result->spec() : "unchanged";
}

TEST(QueryFilterTest, RemovesAParameterFirstInTheMiddleAndLast) {
  EXPECT_EQ(Stripped("https://b.test/?fbclid=1&x=2"), "https://b.test/?x=2");
  EXPECT_EQ(Stripped("https://b.test/?x=1&fbclid=2&y=3"), "https://b.test/?x=1&y=3");
  EXPECT_EQ(Stripped("https://b.test/p?x=1&gclid=2"), "https://b.test/p?x=1");
}

TEST(QueryFilterTest, KeepsTheRestByteForByte) {
  EXPECT_EQ(Stripped("https://b.test/?b=%2F%20+&fbclid=1&a=&a=2&&c"),
            "https://b.test/?b=%2F%20+&a=&a=2&&c");
}

TEST(QueryFilterTest, KeepsTheFragmentAndDropsAnEmptiedQuery) {
  EXPECT_EQ(Stripped("https://b.test/p?fbclid=1#top"), "https://b.test/p#top");
  EXPECT_EQ(Stripped("https://b.test/p?fbclid=1&gclid=2"), "https://b.test/p");
}

TEST(QueryFilterTest, MatchesEncodedNamesExactlyAndEveryRepeat) {
  EXPECT_EQ(Stripped("https://b.test/?f%62clid=1&x=2"), "https://b.test/?x=2");
  EXPECT_EQ(Stripped("https://b.test/?fbclid=1&fbclid=2&fbclid"), "https://b.test/");
  EXPECT_EQ(Stripped("https://b.test/?fbclid2=1&xfbclid=2&FBCLID=3"), "unchanged");
  EXPECT_EQ(Stripped("https://b.test/?x=fbclid"), "unchanged");
}

TEST(QueryFilterTest, ASiteTiedEntryOnlyOnItsSiteAndSubdomains) {
  EXPECT_EQ(Stripped("https://www.youtube.com/watch?v=1&si=2"),
            "https://www.youtube.com/watch?v=1");
  EXPECT_EQ(Stripped("https://youtu.be/1?si=2"), "https://youtu.be/1");
  EXPECT_EQ(Stripped("https://b.test/?si=2"), "unchanged");
  EXPECT_EQ(Stripped("https://notyoutube.com/?si=2"), "unchanged");
}

TEST(QueryFilterTest, CampaignParametersOnlyInTheirScope) {
  EXPECT_EQ(Stripped("https://b.test/?utm_source=a&x=1"), "unchanged");
  EXPECT_EQ(Stripped("https://b.test/?utm_source=a&utm_medium=b&x=1", Scope::kClickAndCampaign),
            "https://b.test/?x=1");
}

TEST(QueryFilterTest, NothingToDo) {
  EXPECT_EQ(Stripped("https://b.test/"), "unchanged");
  EXPECT_EQ(Stripped("https://b.test/?"), "unchanged");
  EXPECT_EQ(Stripped("not a url"), "unchanged");
}

TEST(QueryFilterTest, StrippingTwiceChangesNothing) {
  const GURL once = *Strip(GURL("https://b.test/?fbclid=1&x=2"), TestList(), Scope::kClick);
  EXPECT_FALSE(Strip(once, TestList(), Scope::kClick));
}

TEST(QueryFilterTest, OnlyHttpGetNavigationsAreFiltered) {
  EXPECT_TRUE(IsFilteredNavigation(GURL("https://b.test/"), "GET"));
  EXPECT_TRUE(IsFilteredNavigation(GURL("http://b.test/"), "GET"));
  EXPECT_FALSE(IsFilteredNavigation(GURL("https://b.test/"), "POST"));
  EXPECT_FALSE(IsFilteredNavigation(GURL("file:///c:/a?fbclid=1"), "GET"));
  EXPECT_FALSE(IsFilteredNavigation(GURL("chrome://settings/?fbclid=1"), "GET"));
}

TEST(QueryFilterTest, AURLComesFromElsewhere) {
  const GURL url("https://shop.b.test/?fbclid=1");
  EXPECT_TRUE(ComesFromElsewhere(url, std::nullopt));  // The user.
  EXPECT_TRUE(ComesFromElsewhere(url, url::Origin::Create(GURL("https://a.test/"))));
  EXPECT_TRUE(ComesFromElsewhere(url, url::Origin()));  // Opaque.
  EXPECT_FALSE(ComesFromElsewhere(url, url::Origin::Create(GURL("https://www.b.test/"))));
  EXPECT_FALSE(ComesFromElsewhere(url, url::Origin::Create(GURL("http://b.test/"))));
}

TEST(QueryFilterTest, ARedirectCrossesSites) {
  EXPECT_TRUE(CrossesSites(GURL("https://r.test/"), GURL("https://b.test/")));
  EXPECT_FALSE(CrossesSites(GURL("https://a.b.test/"), GURL("https://c.b.test/")));
  EXPECT_TRUE(CrossesSites(GURL("http://127.0.0.1/"), GURL("http://127.0.0.2/")));
}

}  // namespace
}  // namespace ghost::query_filter
