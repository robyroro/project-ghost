// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/registrable_domain.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

TEST(RegistrableDomainTest, KnownRegistries) {
  EXPECT_EQ(RegistrableDomain("www.example.com"), "example.com");
  EXPECT_EQ(RegistrableDomain("a.b.example.co.uk"), "example.co.uk");
  // A private registry: each user's site is its own.
  EXPECT_EQ(RegistrableDomain("alice.github.io"), "alice.github.io");
}

TEST(RegistrableDomainTest, UnknownRegistriesCountTheirLastLabel) {
  // Test and intranet names: "a.test" and "sub.a.test" are one site, so the
  // engine and the request filter must not see them as third parties.
  EXPECT_EQ(RegistrableDomain("sub.a.test"), "a.test");
  EXPECT_EQ(RegistrableDomain("a.test"), "a.test");
}

TEST(RegistrableDomainTest, HostsWithoutADomainAreThemselves) {
  EXPECT_EQ(RegistrableDomain("localhost"), "localhost");
  EXPECT_EQ(RegistrableDomain("127.0.0.1"), "127.0.0.1");
  EXPECT_EQ(RegistrableDomain("[::1]"), "[::1]");
  EXPECT_EQ(RegistrableDomain("com"), "com");
  EXPECT_EQ(RegistrableDomain(""), "");
}

TEST(RegistrableDomainTest, RangeIsTheDomainsPositionInTheHost) {
  size_t start = 99, end = 99;
  RegistrableDomainRange("api.m.example.com", start, end);
  EXPECT_EQ(start, 6u);
  EXPECT_EQ(end, 17u);
}

}  // namespace
}  // namespace ghost::blocking
