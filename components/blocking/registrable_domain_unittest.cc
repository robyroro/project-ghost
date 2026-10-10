// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/registrable_domain.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

TEST(RegistrableDomainTest, RangeIsTheDomainsPositionInTheHost) {
  size_t start = 99, end = 99;
  RegistrableDomainRange("api.m.example.com", start, end);
  EXPECT_EQ(start, 6u);
  EXPECT_EQ(end, 17u);
}

}  // namespace
}  // namespace ghost::blocking
