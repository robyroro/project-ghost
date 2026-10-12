// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// adblock-rust through the cxx bridge, with small lists written here.

#include <initializer_list>
#include <string>
#include <vector>

#include "ghost/components/blocking/rust/lib.rs.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

using testing::ElementsAre;
using testing::IsEmpty;

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

std::vector<std::string> Strings(const rust::Vec<rust::String>& strings) {
  std::vector<std::string> result;
  for (const rust::String& s : strings) {
    result.emplace_back(s);
  }
  return result;
}

rust::Vec<rust::String> RustStrings(std::initializer_list<const char*> strings) {
  rust::Vec<rust::String> result;
  for (const char* s : strings) {
    result.push_back(rust::String(s));
  }
  return result;
}

TEST(EngineBridgeTest, SiteRulesApplyToTheirSite) {
  rust::Box<Engine> engine = new_engine("a.test##.site-ad\n");
  EXPECT_THAT(Strings(engine->cosmetic_resources("https://www.a.test/page").hide_selectors),
              ElementsAre(".site-ad"));
  EXPECT_THAT(Strings(engine->cosmetic_resources("https://b.test/").hide_selectors), IsEmpty());
}

TEST(EngineBridgeTest, ComplexGenericRulesComeWithEveryPage) {
  rust::Box<Engine> engine = new_engine("##div[class*=\"Ads\"]\n");
  EXPECT_THAT(Strings(engine->cosmetic_resources("https://b.test/").hide_selectors),
              ElementsAre("div[class*=\"Ads\"]"));
}

TEST(EngineBridgeTest, GenericRulesMatchClassesAndIds) {
  rust::Box<Engine> engine = new_engine("##.generic-ad\n###banner\n");
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"generic-ad", "plain"}),
                                                RustStrings({"banner"}), RustStrings({}))),
              ElementsAre("#banner", ".generic-ad"));
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"plain"}), RustStrings({}),
                                                RustStrings({}))),
              IsEmpty());
}

TEST(EngineBridgeTest, AnExceptionRemovesAGenericRuleOnItsSite) {
  rust::Box<Engine> engine = new_engine("##.generic-ad\na.test#@#.generic-ad\n");
  const CosmeticResources a = engine->cosmetic_resources("https://a.test/");
  EXPECT_THAT(Strings(a.exceptions), ElementsAre(".generic-ad"));
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"generic-ad"}), RustStrings({}),
                                                a.exceptions)),
              IsEmpty());
  const CosmeticResources b = engine->cosmetic_resources("https://b.test/");
  EXPECT_THAT(Strings(engine->generic_selectors(RustStrings({"generic-ad"}), RustStrings({}),
                                                b.exceptions)),
              ElementsAre(".generic-ad"));
}

TEST(EngineBridgeTest, GenerichideTurnsGenericRulesOff) {
  rust::Box<Engine> engine = new_engine(
      "##div[class*=\"Ads\"]\nnohide.test##.site-ad\n@@||nohide.test^$generichide\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://nohide.test/");
  EXPECT_TRUE(resources.generichide);
  // The site's own rules stay; the generic ones go.
  EXPECT_THAT(Strings(resources.hide_selectors), ElementsAre(".site-ad"));
  EXPECT_FALSE(engine->cosmetic_resources("https://b.test/").generichide);
}

TEST(EngineBridgeTest, AnUnparsableUrlHasNoCosmeticRules) {
  rust::Box<Engine> engine = new_engine("##div[class*=\"Ads\"]\n");
  EXPECT_THAT(Strings(engine->cosmetic_resources("not a url").hide_selectors), IsEmpty());
}

}  // namespace
}  // namespace ghost::blocking
