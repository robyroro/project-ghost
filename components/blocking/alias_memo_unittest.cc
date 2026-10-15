// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/alias_memo.h"

#include <string>
#include <vector>

#include "base/memory/scoped_refptr.h"
#include "net/base/schemeful_site.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::blocking {
namespace {

using ::testing::ElementsAre;
using ::testing::IsNull;
using ::testing::Pointee;

net::NetworkAnonymizationKey KeyFor(const char* site) {
  return net::NetworkAnonymizationKey::CreateSameSite(net::SchemefulSite(GURL(site)));
}

class AliasMemoTest : public ::testing::Test {
 protected:
  scoped_refptr<AliasMemo> memo_ = base::MakeRefCounted<AliasMemo>();
  const net::NetworkAnonymizationKey site_ = KeyFor("https://site.test");
  const base::TimeTicks now_ = base::TimeTicks() + base::Hours(1);
};

TEST_F(AliasMemoTest, RemembersAnAnswerForAMinute) {
  EXPECT_THAT(memo_->Find(site_, "metrics.site.test", false, now_), IsNull());
  memo_->Remember(site_, "metrics.site.test", false, {"site.tracker.test"}, now_);
  EXPECT_THAT(memo_->Find(site_, "metrics.site.test", false, now_ + base::Seconds(59)),
              Pointee(ElementsAre("site.tracker.test")));
  EXPECT_THAT(memo_->Find(site_, "metrics.site.test", false, now_ + AliasMemo::kLifetime),
              IsNull());
}

TEST_F(AliasMemoTest, RemembersThatAHostHasNoAliases) {
  memo_->Remember(site_, "cdn.site.test", false, {}, now_);
  const std::vector<std::string>* aliases = memo_->Find(site_, "cdn.site.test", false, now_);
  ASSERT_NE(aliases, nullptr);
  EXPECT_TRUE(aliases->empty());
}

TEST_F(AliasMemoTest, KeysAsTheResolutionWasAsked) {
  memo_->Remember(site_, "metrics.site.test", false, {"site.tracker.test"}, now_);
  // Another partition, another host, or secure DNS off: not the same answer.
  EXPECT_THAT(memo_->Find(KeyFor("https://other.test"), "metrics.site.test", false, now_),
              IsNull());
  EXPECT_THAT(memo_->Find(site_, "www.site.test", false, now_), IsNull());
  EXPECT_THAT(memo_->Find(site_, "metrics.site.test", true, now_), IsNull());
}

TEST_F(AliasMemoTest, AFullMemoDropsExpiredEntriesFirst) {
  memo_->Remember(site_, "old.site.test", false, {}, now_);
  const base::TimeTicks later = now_ + AliasMemo::kLifetime;
  for (size_t i = 1; i < AliasMemo::kMaxEntries; ++i) {
    memo_->Remember(site_, "host" + std::to_string(i) + ".site.test", false, {}, later);
  }
  ASSERT_EQ(memo_->size(), AliasMemo::kMaxEntries);
  memo_->Remember(site_, "new.site.test", false, {}, later);
  EXPECT_EQ(memo_->size(), AliasMemo::kMaxEntries);
  EXPECT_THAT(memo_->Find(site_, "host1.site.test", false, later), Pointee(ElementsAre()));
}

TEST_F(AliasMemoTest, AFullMemoOfLiveEntriesStartsOver) {
  for (size_t i = 0; i < AliasMemo::kMaxEntries; ++i) {
    memo_->Remember(site_, "host" + std::to_string(i) + ".site.test", false, {}, now_);
  }
  memo_->Remember(site_, "new.site.test", false, {}, now_);
  EXPECT_EQ(memo_->size(), 1u);
  EXPECT_THAT(memo_->Find(site_, "new.site.test", false, now_), Pointee(ElementsAre()));
}

}  // namespace
}  // namespace ghost::blocking
