// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/blocking_engine.h"

#include <string>
#include <vector>

#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
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

// A WebSocket has no request destination of its own: the check names its type.
TEST_F(BlockingEngineTest, ACheckCanNameItsRequestType) {
  BlockingEngine engine;
  engine.Load({"||sockets.test^$websocket\n"});
  CheckRequest socket = {GURL("wss://sockets.test/s"), GURL("https://a.test/"),
                         network::mojom::RequestDestination::kEmpty, "GET"};
  socket.adblock_type = "websocket";
  EXPECT_TRUE(Check(engine, socket).blocked);
  CheckRequest script = {GURL("wss://sockets.test/s"), GURL("https://a.test/"),
                         network::mojom::RequestDestination::kScript, "GET"};
  EXPECT_FALSE(Check(engine, script).blocked);
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

TEST_F(BlockingEngineTest, AnInvalidPageIsNoPage) {
  BlockingEngine engine;
  engine.Load({"||tracker.test^\n"});
  CheckRequest request = TrackerScript();
  request.source_url = GURL("not a url\xff");
  EXPECT_TRUE(Check(engine, std::move(request)).blocked);
}

}  // namespace
}  // namespace ghost::blocking
