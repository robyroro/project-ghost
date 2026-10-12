// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/dns_aliases.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::blocking {
namespace {

using testing::ElementsAre;
using testing::IsEmpty;

TEST(DnsAliasesTest, NoAliasesNothingToCheck) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {}), IsEmpty());
}

TEST(DnsAliasesTest, TheHostIsNotItsOwnAlias) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"metrics.a.test"}), IsEmpty());
}

TEST(DnsAliasesTest, AnAliasOnTheSameSiteIsNotCloaking) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"edge.a.test", "a.test"}), IsEmpty());
}

TEST(DnsAliasesTest, AnAliasOnAnotherSiteIsChecked) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"metrics.a.test", "a.tracker.test"}),
              ElementsAre("a.tracker.test"));
}

TEST(DnsAliasesTest, AddressesAndNamesThatArentHostsAreSkipped) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test",
                             {"192.0.2.1", "[2001:db8::1]", "2001:db8::1", "", "bad host.test",
                              "UPPER.tracker.test"}),
              IsEmpty());
}

TEST(DnsAliasesTest, EachOnceAndSorted) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test",
                             {"b.tracker.test", "a.tracker.test", "b.tracker.test"}),
              ElementsAre("a.tracker.test", "b.tracker.test"));
}

TEST(DnsAliasesTest, ATrailingDotIsDropped) {
  // A rule's `^` doesn't match a dot: "a.tracker.test." would escape
  // ||tracker.test^.
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"a.tracker.test."}),
              ElementsAre("a.tracker.test"));
  EXPECT_THAT(AliasesToCheck("metrics.a.test.", {"metrics.a.test"}), IsEmpty());
}

TEST(DnsAliasesTest, WithAliasKeepsEverythingButTheHost) {
  EXPECT_EQ(WithAlias(GURL("https://metrics.a.test:8443/p/m.js?q=1#f"), "a.tracker.test"),
            GURL("https://a.tracker.test:8443/p/m.js?q=1#f"));
}

TEST(DnsAliasesTest, WithAnAliasThatIsntAHostIsInvalid) {
  EXPECT_FALSE(WithAlias(GURL("https://metrics.a.test/m.js"), "bad host").is_valid());
  EXPECT_FALSE(WithAlias(GURL("https://metrics.a.test/m.js"), "").is_valid());
}

}  // namespace
}  // namespace ghost::blocking
