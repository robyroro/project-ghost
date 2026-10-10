// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/blocking_engine.h"

#include <optional>
#include <utility>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "ghost/components/blocking/request_types.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace ghost::blocking {

class BlockingEngine::Core {
 public:
  void Load(std::vector<std::string> lists) {
    std::string text;
    for (const std::string& list : lists) {
      // The bridge takes Rust strings, and cxx aborts the process on one that
      // isn't UTF-8: a damaged list is left out, not a crash at startup.
      if (!base::IsStringUTF8AllowingNoncharacters(list)) {
        LOG(ERROR) << "Blocking: a filter list isn't UTF-8; it is left out.";
      } else if (!list.empty()) {
        text += list;
        text += '\n';  // A list without a final newline mustn't join the next.
      }
    }
    if (text.empty()) {
      state_ = State::kFailed;
      LOG(ERROR) << "Blocking: no filter list was loaded; requests are not blocked.";
    } else {
      engine_ = new_engine(text);
      state_ = State::kReady;
    }
    auto pending = std::move(pending_);
    for (auto& [request, reply] : pending) {
      Answer(request, std::move(reply));
    }
  }

  void Check(CheckRequest request, base::OnceCallback<void(Decision)> reply) {
    if (state_ == State::kLoading) {
      pending_.emplace_back(std::move(request), std::move(reply));
      return;
    }
    Answer(request, std::move(reply));
  }

  State state() const { return state_; }

 private:
  void Answer(const CheckRequest& request, base::OnceCallback<void(Decision)> reply) {
    Decision decision;
    // The method comes from the renderer, unvalidated: the network service
    // would refuse one that isn't UTF-8, and it mustn't reach the bridge.
    // Valid URLs' specs are ASCII; an invalid page is no page.
    if (!base::IsStringUTF8AllowingNoncharacters(request.method)) {
      decision.blocked = true;
    } else if (state_ == State::kReady && request.url.is_valid()) {
      const std::string& page =
          request.source_url.is_valid() ? request.source_url.spec() : base::EmptyString();
      Verdict verdict = (*engine_)->check(request.url.spec(), page,
                                          ToAdblockType(request.destination), request.method);
      decision.blocked = verdict.blocked;
      decision.filter = std::string(verdict.filter);
    }
    std::move(reply).Run(std::move(decision));
  }

  State state_ = State::kLoading;
  std::optional<rust::Box<Engine>> engine_;
  std::vector<std::pair<CheckRequest, base::OnceCallback<void(Decision)>>> pending_;
};

BlockingEngine::BlockingEngine()
    : core_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::TaskPriority::USER_BLOCKING,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {}

BlockingEngine::~BlockingEngine() = default;

void BlockingEngine::Load(std::vector<std::string> lists) {
  core_.AsyncCall(&Core::Load).WithArgs(std::move(lists));
}

void BlockingEngine::Check(CheckRequest request, base::OnceCallback<void(Decision)> reply) {
  core_.AsyncCall(&Core::Check)
      .WithArgs(std::move(request), base::BindPostTaskToCurrentDefault(std::move(reply)));
}

void BlockingEngine::GetStateForTesting(base::OnceCallback<void(State)> reply) {
  core_.AsyncCall(&Core::state).Then(std::move(reply));
}

}  // namespace ghost::blocking
