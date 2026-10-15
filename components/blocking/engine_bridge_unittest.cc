// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// adblock-rust through the cxx bridge, with small lists written here.

#include <initializer_list>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "ghost/components/blocking/filter_lists.h"
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

// Phase 4B: scriptlets, procedural and action rules, redirects.

std::string Script(const Engine& engine, const char* url) {
  return std::string(engine.cosmetic_resources(url).injected_script);
}

TEST(EngineBridgeTest, ScriptletsComeWithTheirSite) {
  rust::Box<Engine> engine = new_engine("a.test##+js(set, shadeValue, true)\n");
  const std::string script = Script(*engine, "https://a.test/");
  EXPECT_NE(script.find("function setConstant("), std::string::npos) << script;
  EXPECT_NE(script.find("function shadeSafe("), std::string::npos) << script;
  EXPECT_NE(script.find("setConstant(\"shadeValue\", \"true\")"), std::string::npos) << script;
  EXPECT_TRUE(Script(*engine, "https://b.test/").empty());
}

TEST(EngineBridgeTest, ArgumentsStayStrings) {
  // A list's argument is a JSON string literal in the call, never code.
  rust::Box<Engine> engine = new_engine("a.test##+js(set, a\"b, true)\n");
  const std::string script = Script(*engine, "https://a.test/");
  EXPECT_NE(script.find("setConstant(\"a\\\"b\", \"true\")"), std::string::npos) << script;
}

TEST(EngineBridgeTest, TheLicenseHeaderIsntInjected) {
  rust::Box<Engine> engine = new_engine("a.test##+js(set, shadeValue, true)\n");
  EXPECT_EQ(Script(*engine, "https://a.test/").find("Mozilla Public License"),
            std::string::npos);
}

TEST(EngineBridgeTest, AScriptletWeDontHaveIsSkipped) {
  rust::Box<Engine> engine = new_engine("a.test##+js(json-edit, a)\n");
  EXPECT_TRUE(Script(*engine, "https://a.test/").empty());
}

TEST(EngineBridgeTest, AScriptletExceptionTurnsThemOff) {
  rust::Box<Engine> engine =
      new_engine("a.test##+js(set, shadeValue, true)\na.test#@#+js()\n");
  EXPECT_TRUE(Script(*engine, "https://a.test/").empty());
}

TEST(EngineBridgeTest, ProceduralRulesComeAsJson) {
  rust::Box<Engine> engine = new_engine("a.test#?#.box:has-text(Sponsored)\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://a.test/");
  ASSERT_EQ(resources.procedural_filters.size(), 1u);
  const std::string json(resources.procedural_filters[0]);
  EXPECT_NE(json.find("\"has-text\""), std::string::npos) << json;
  EXPECT_NE(json.find("Sponsored"), std::string::npos) << json;
}

TEST(EngineBridgeTest, AProceduralOperatorInsideHasIsProcedural) {
  // adblock-rust passes :has() through as CSS, operators inside it too (63
  // EasyList rules): Blink would drop the selector, so it goes to the
  // procedural executor as one css-selector it parses itself.
  rust::Box<Engine> engine = new_engine("a.test#?#div:has(p:has-text(Sponsored))\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://a.test/");
  EXPECT_THAT(Strings(resources.hide_selectors), IsEmpty());
  ASSERT_EQ(resources.procedural_filters.size(), 1u);
  EXPECT_EQ(std::string(resources.procedural_filters[0]),
            R"json({"selector":[{"type":"css-selector","arg":"div:has(p:has-text(Sponsored))"}]})json");
}

TEST(EngineBridgeTest, ActionsInPlainCssComeAsStyleRules) {
  rust::Box<Engine> engine = new_engine("a.test##.box:style(color: red)\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://a.test/");
  ASSERT_EQ(resources.style_rules.size(), 1u);
  EXPECT_EQ(std::string(resources.style_rules[0].selector), ".box");
  EXPECT_EQ(std::string(resources.style_rules[0].style), "color: red");
  EXPECT_TRUE(resources.procedural_filters.empty());
}

TEST(EngineBridgeTest, ARedirectComesAsADataUrl) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$script,redirect=noopjs\n");
  const Verdict verdict =
      engine->check("https://tracker.test/t.js", "https://a.test/", "script", "GET");
  EXPECT_TRUE(verdict.blocked);
  EXPECT_TRUE(std::string(verdict.redirect).starts_with("data:application/javascript;base64,"))
      << std::string(verdict.redirect);
}

TEST(EngineBridgeTest, ARedirectToAResourceWeDontHaveJustBlocks) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$script,redirect=nothing.js\n");
  const Verdict verdict =
      engine->check("https://tracker.test/t.js", "https://a.test/", "script", "GET");
  EXPECT_TRUE(verdict.blocked);
  EXPECT_TRUE(verdict.redirect.empty());
}

TEST(EngineBridgeTest, EveryScriptletTheListsUseResolvesExceptJsonEdit) {
  for (const char* name : {"set", "acs", "aost", "rmnt", "no-xhr-if", "no-fetch-if",
                           "set-local-storage-item", "set-cookie", "cookie-remover"}) {
    rust::Box<Engine> engine = new_engine(std::string("a.test##+js(") + name + ", x, true)\n");
    EXPECT_FALSE(Script(*engine, "https://a.test/").empty()) << name;
  }
}

TEST(EngineBridgeTest, ShippedResourcesHaveNoProblem) {
  for (const rust::String& problem : resource_problems()) {
    ADD_FAILURE() << std::string(problem);
  }
}

TEST(EngineBridgeTest, OnlyFunctionStyleScriptletsAreAccepted) {
  // A template pastes a list's argument into code: refused.
  EXPECT_FALSE(
      resource_problem_for_testing("x.js", "application/javascript", "alert({{1}})", false)
          .empty());
  // A scriptlet adblock-rust can't call by name: refused.
  EXPECT_FALSE(
      resource_problem_for_testing("x.js", "fn/javascript", "(function () {})();", true).empty());
  EXPECT_TRUE(resource_problem_for_testing("x.js", "fn/javascript",
                                           "// header\n\nfunction shadeX(a) {}\n", true)
                  .empty());
  // A redirect needn't be a function.
  EXPECT_TRUE(resource_problem_for_testing("x.js", "application/javascript",
                                           "(function () {})();", false)
                  .empty());
}

// Phase 4D-1: Shade's own list, checked rule by rule.
TEST(EngineBridgeTest, ListProblemsNamesTheLinesTheParserRefuses) {
  rust::Vec<rust::String> problems =
      list_problems("! a comment\n[Adblock Plus 2.0]\n\n||a.test^\n||b.test^$nosuchoption\n");
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_TRUE(std::string(problems[0]).starts_with("line 5: ||b.test^$nosuchoption"))
      << std::string(problems[0]);
}

TEST(EngineBridgeTest, ShadesListHasNoProblem) {
  std::string list;
  ASSERT_TRUE(base::ReadFileToString(DefaultFilterListsDir().AppendASCII("shade.txt"), &list));
  for (const rust::String& problem : list_problems(list)) {
    ADD_FAILURE() << std::string(problem);
  }
}

// Phase 4C: a request's DNS aliases, as URLs with the alias as host.

rust::Vec<rust::String> Urls(std::initializer_list<const char*> urls) {
  rust::Vec<rust::String> result;
  for (const char* url : urls) {
    result.push_back(rust::String(url));
  }
  return result;
}

AliasVerdict CheckAliases(const Engine& engine,
                          std::initializer_list<const char*> alias_urls,
                          const char* source = "https://a.test/") {
  return engine.check_aliases("https://metrics.a.test/m.js", source, "script", "GET",
                              Urls(alias_urls));
}

TEST(EngineBridgeTest, AListedAliasBlocksTheRequest) {
  rust::Box<Engine> engine = new_engine("||tracker.test^\n");
  const AliasVerdict verdict =
      CheckAliases(*engine, {"https://cdn.test/m.js", "https://a.tracker.test/m.js"});
  EXPECT_TRUE(verdict.blocked);
  EXPECT_EQ(std::string(verdict.alias_url), "https://a.tracker.test/m.js");
  EXPECT_FALSE(verdict.filter.empty());
}

TEST(EngineBridgeTest, NoListedAliasBlocksNothing) {
  rust::Box<Engine> engine = new_engine("||tracker.test^\n");
  const AliasVerdict verdict = CheckAliases(*engine, {"https://cdn.test/m.js"});
  EXPECT_FALSE(verdict.blocked);
  EXPECT_TRUE(verdict.alias_url.empty());
  EXPECT_FALSE(CheckAliases(*engine, {}).blocked);
}

TEST(EngineBridgeTest, AnExceptionForTheRequestLeavesItsAliasesAlone) {
  // No blocking rule names metrics.a.test: the exception must still count.
  rust::Box<Engine> engine = new_engine("||tracker.test^\n@@||metrics.a.test^\n");
  EXPECT_FALSE(CheckAliases(*engine, {"https://a.tracker.test/m.js"}).blocked);
}

TEST(EngineBridgeTest, AnAliasIsJudgedAgainstThePage) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$third-party\n");
  EXPECT_TRUE(CheckAliases(*engine, {"https://a.tracker.test/m.js"}).blocked);
  EXPECT_FALSE(CheckAliases(*engine, {"https://a.tracker.test/m.js"},
                            "https://www.tracker.test/")
                   .blocked);
}

TEST(EngineBridgeTest, AnAliasCanBeRedirected) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$script,redirect=noopjs\n");
  const AliasVerdict verdict = CheckAliases(*engine, {"https://a.tracker.test/m.js"});
  EXPECT_TRUE(verdict.blocked);
  EXPECT_TRUE(std::string(verdict.redirect).starts_with("data:application/javascript;base64,"));
}

}  // namespace
}  // namespace ghost::blocking
