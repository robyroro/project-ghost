// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/blocking_engine.h"

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::blocking {
namespace {

CheckRequest TrackerScript() {
  return {GURL("https://tracker.test/t.js"), GURL("https://a.test/"),
          network::mojom::RequestDestination::kScript, "GET"};
}

class BlockingEngineTest : public testing::Test {
 protected:
  Decision Check(BlockingEngine& engine, CheckRequest request) {
    base::test::TestFuture<Decision> decision;
    engine.Check(std::move(request), decision.GetCallback());
    return decision.Take();
  }

  BlockingEngine::State State(BlockingEngine& engine) {
    base::test::TestFuture<BlockingEngine::State> state;
    engine.GetStateForTesting(state.GetCallback());
    return state.Get();
  }

  base::test::TaskEnvironment task_environment_;
};

TEST_F(BlockingEngineTest, LoadsAndBlocks) {
  BlockingEngine engine;
  EXPECT_EQ(State(engine), BlockingEngine::State::kLoading);
  engine.Load({"||tracker.test^$third-party\n"});
  EXPECT_EQ(State(engine), BlockingEngine::State::kReady);
  Decision decision = Check(engine, TrackerScript());
  EXPECT_TRUE(decision.blocked);
}

TEST_F(BlockingEngineTest, ACheckBeforeLoadingIsAnsweredAfterIt) {
  BlockingEngine engine;
  base::test::TestFuture<Decision> decision;
  engine.Check(TrackerScript(), decision.GetCallback());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(decision.IsReady()) << "answered before the lists loaded";
  engine.Load({"||tracker.test^$third-party\n"});
  EXPECT_TRUE(decision.Get().blocked);
}

TEST_F(BlockingEngineTest, WithoutListsTheEngineFailsAndAllows) {
  BlockingEngine engine;
  base::test::TestFuture<Decision> pending;
  engine.Check(TrackerScript(), pending.GetCallback());
  engine.Load({});
  EXPECT_FALSE(pending.Get().blocked);
  EXPECT_EQ(State(engine), BlockingEngine::State::kFailed);
  EXPECT_FALSE(Check(engine, TrackerScript()).blocked);
}

TEST_F(BlockingEngineTest, SeveralListsAreOneEngine) {
  BlockingEngine engine;
  engine.Load({"||ads.test^\n", "||tracker.test^\n"});
  EXPECT_TRUE(Check(engine, TrackerScript()).blocked);
  EXPECT_TRUE(Check(engine, {GURL("https://ads.test/a.png"), GURL("https://a.test/"),
                             network::mojom::RequestDestination::kImage, "GET"})
                  .blocked);
}

// A WebSocket has no request destination of its own: adblock-rust reads its
// type from the ws: or wss: scheme, which the connection filter relies on.
TEST_F(BlockingEngineTest, AWebSocketUrlIsCheckedAsAWebSocket) {
  BlockingEngine engine;
  engine.Load({"||sockets.test^$websocket\n"});
  EXPECT_TRUE(Check(engine, {GURL("wss://sockets.test/s"), GURL("https://a.test/"),
                             network::mojom::RequestDestination::kEmpty, "GET"})
                  .blocked);
  // The control: the same host's script is no WebSocket.
  EXPECT_FALSE(Check(engine, {GURL("https://sockets.test/s.js"), GURL("https://a.test/"),
                              network::mojom::RequestDestination::kScript, "GET"})
                   .blocked);
}

// The bridge takes Rust strings, which must be UTF-8: cxx aborts the process
// on anything else, so nothing that isn't may reach it.
TEST_F(BlockingEngineTest, AListThatIsNotUtf8IsLeftOut) {
  BlockingEngine engine;
  engine.Load({"||ads.test^\n\xff\n", "||tracker.test^\n"});
  EXPECT_EQ(State(engine), BlockingEngine::State::kReady);
  EXPECT_TRUE(Check(engine, TrackerScript()).blocked);
  EXPECT_FALSE(Check(engine, {GURL("https://ads.test/a.png"), GURL("https://a.test/"),
                              network::mojom::RequestDestination::kImage, "GET"})
                   .blocked);
}

TEST_F(BlockingEngineTest, WhenNoListIsUtf8TheEngineFails) {
  BlockingEngine engine;
  engine.Load({"||tracker.test^\n\xc3\x28\n"});
  EXPECT_EQ(State(engine), BlockingEngine::State::kFailed);
}

// The method comes from the renderer, and the request filter sees it before
// the network service validates it.
TEST_F(BlockingEngineTest, AMethodThatIsNotUtf8IsBlocked) {
  BlockingEngine engine;
  engine.Load({"||other.test^\n"});
  CheckRequest request = TrackerScript();
  request.method = "G\xffT";
  EXPECT_TRUE(Check(engine, std::move(request)).blocked);
}

TEST_F(BlockingEngineTest, CosmeticRulesForADocument) {
  BlockingEngine engine;
  engine.Load({"a.test##.site-ad\n##.generic-ad\na.test#@#.generic-ad\n"});
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  EXPECT_THAT(rules.Get().hide_selectors, testing::ElementsAre(".site-ad"));
  EXPECT_THAT(rules.Get().exceptions, testing::ElementsAre(".generic-ad"));
  EXPECT_FALSE(rules.Get().generichide);
}

TEST_F(BlockingEngineTest, GenericSelectorsLessExceptions) {
  BlockingEngine engine;
  engine.Load({"##.generic-ad\n###banner\n"});
  base::test::TestFuture<std::vector<std::string>> selectors;
  engine.GetGenericSelectors({"generic-ad"}, {"banner"}, {".generic-ad"}, selectors.GetCallback());
  EXPECT_THAT(selectors.Get(), testing::ElementsAre("#banner"));
}

TEST_F(BlockingEngineTest, CosmeticQueriesBeforeLoadingAreAnsweredAfterIt) {
  BlockingEngine engine;
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(rules.IsReady()) << "answered before the lists loaded";
  engine.Load({"a.test##.site-ad\n"});
  EXPECT_THAT(rules.Get().hide_selectors, testing::ElementsAre(".site-ad"));
}

TEST_F(BlockingEngineTest, AFailedEngineHidesNothing) {
  BlockingEngine engine;
  engine.Load({});
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  EXPECT_TRUE(rules.Get().hide_selectors.empty());
  base::test::TestFuture<std::vector<std::string>> selectors;
  engine.GetGenericSelectors({"generic-ad"}, {}, {}, selectors.GetCallback());
  EXPECT_TRUE(selectors.Get().empty());
}

// Names come from pages; cxx aborts on a Rust string that isn't UTF-8.
TEST_F(BlockingEngineTest, NamesThatAreNotUtf8AreLeftOut) {
  BlockingEngine engine;
  engine.Load({"##.generic-ad\n"});
  base::test::TestFuture<std::vector<std::string>> selectors;
  engine.GetGenericSelectors({"\xff", "generic-ad"}, {"\xc3\x28"}, {}, selectors.GetCallback());
  EXPECT_THAT(selectors.Get(), testing::ElementsAre(".generic-ad"));
}

TEST_F(BlockingEngineTest, AnInvalidPageIsNoPage) {
  BlockingEngine engine;
  engine.Load({"||tracker.test^\n"});
  CheckRequest request = TrackerScript();
  request.source_url = GURL("not a url\xff");
  EXPECT_TRUE(Check(engine, std::move(request)).blocked);
}

}  // namespace
}  // namespace ghost::blocking
