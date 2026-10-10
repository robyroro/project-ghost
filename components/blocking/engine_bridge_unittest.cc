// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// adblock-rust through the cxx bridge, with small lists written here.

#include "ghost/components/blocking/rust/lib.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

bool Blocked(const Engine& engine, const char* url, const char* source,
             const char* type = "script") {
  return engine.check(url, source, type, "GET").blocked;
}

TEST(EngineBridgeTest, BlocksAThirdPartyTracker) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$third-party\n");
  EXPECT_TRUE(Blocked(*engine, "https://tracker.test/t.js", "https://a.test/"));
  EXPECT_FALSE(Blocked(*engine, "https://tracker.test/t.js",
                       "https://tracker.test/page"));
}

TEST(EngineBridgeTest, SubdomainsOfOneSiteAreFirstParty) {
  // The engine asks Chromium for the registrable domain (registrable_domain.h).
  rust::Box<Engine> engine = new_engine("||cdn.a.test^$third-party\n");
  EXPECT_FALSE(Blocked(*engine, "https://cdn.a.test/x.js", "https://www.a.test/"));
  EXPECT_TRUE(Blocked(*engine, "https://cdn.a.test/x.js", "https://b.test/"));
}

TEST(EngineBridgeTest, DomainOptionLimitsARule) {
  rust::Box<Engine> engine = new_engine("||ads.test^$domain=a.test\n");
  EXPECT_TRUE(Blocked(*engine, "https://ads.test/x.js", "https://a.test/"));
  EXPECT_FALSE(Blocked(*engine, "https://ads.test/x.js", "https://b.test/"));
}

TEST(EngineBridgeTest, RequestTypesMatter) {
  rust::Box<Engine> engine = new_engine("||media.test^$image\n");
  EXPECT_TRUE(Blocked(*engine, "https://media.test/p.png", "https://a.test/", "image"));
  EXPECT_FALSE(Blocked(*engine, "https://media.test/p.js", "https://a.test/", "script"));
}

TEST(EngineBridgeTest, ExceptionsWin) {
  rust::Box<Engine> engine =
      new_engine("||tracker.test^\n@@||tracker.test/allowed.js\n");
  EXPECT_TRUE(Blocked(*engine, "https://tracker.test/t.js", "https://a.test/"));
  EXPECT_FALSE(Blocked(*engine, "https://tracker.test/allowed.js", "https://a.test/"));
}

TEST(EngineBridgeTest, MalformedLinesDontStopTheRest) {
  rust::Box<Engine> engine =
      new_engine("##[\n||$$$\n[Adblock Plus 2.0]\n||tracker.test^\n");
  EXPECT_TRUE(Blocked(*engine, "https://tracker.test/t.js", "https://a.test/"));
}

TEST(EngineBridgeTest, AnEmptyEngineAllowsEverything) {
  rust::Box<Engine> engine = new_engine("");
  EXPECT_FALSE(Blocked(*engine, "https://tracker.test/t.js", "https://a.test/"));
}

TEST(EngineBridgeTest, AnUnparsableUrlIsAllowed) {
  rust::Box<Engine> engine = new_engine("*\n");
  EXPECT_FALSE(Blocked(*engine, "not a url", "https://a.test/"));
}

}  // namespace
}  // namespace ghost::blocking
