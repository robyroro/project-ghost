// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/blocking_engine.h"

#include <optional>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "ghost/components/blocking/request_types.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace ghost::blocking {

CosmeticRules::CosmeticRules() = default;
CosmeticRules::CosmeticRules(CosmeticRules&&) = default;
CosmeticRules& CosmeticRules::operator=(CosmeticRules&&) = default;
CosmeticRules::~CosmeticRules() = default;

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
    for (base::OnceClosure& answer : std::exchange(pending_, {})) {
      std::move(answer).Run();
    }
  }

  void Check(CheckRequest request, base::OnceCallback<void(Decision)> reply) {
    WhenLoaded(base::BindOnce(&Core::Answer, base::Unretained(this), std::move(request),
                              std::move(reply)));
  }

  void GetCosmeticRules(GURL document, base::OnceCallback<void(CosmeticRules)> reply) {
    WhenLoaded(base::BindOnce(&Core::AnswerCosmeticRules, base::Unretained(this),
                              std::move(document), std::move(reply)));
  }

  void GetGenericSelectors(std::vector<std::string> classes,
                           std::vector<std::string> ids,
                           std::vector<std::string> exceptions,
                           base::OnceCallback<void(std::vector<std::string>)> reply) {
    WhenLoaded(base::BindOnce(&Core::AnswerGenericSelectors, base::Unretained(this),
                              std::move(classes), std::move(ids), std::move(exceptions),
                              std::move(reply)));
  }

  State state() const { return state_; }

 private:
  // Queries made while the engine loads wait for it, so the first pages after
  // startup don't escape it. Core owns the closures, so Unretained is safe.
  void WhenLoaded(base::OnceClosure answer) {
    if (state_ == State::kLoading) {
      pending_.push_back(std::move(answer));
    } else {
      std::move(answer).Run();
    }
  }

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

  void AnswerCosmeticRules(const GURL& document,
                           base::OnceCallback<void(CosmeticRules)> reply) {
    CosmeticRules rules;
    // Valid URLs' specs are ASCII.
    if (state_ == State::kReady && document.is_valid()) {
      CosmeticResources resources = (*engine_)->cosmetic_resources(document.spec());
      for (const rust::String& selector : resources.hide_selectors) {
        rules.hide_selectors.emplace_back(selector);
      }
      for (const rust::String& selector : resources.exceptions) {
        rules.exceptions.emplace_back(selector);
      }
      rules.generichide = resources.generichide;
    }
    std::move(reply).Run(std::move(rules));
  }

  void AnswerGenericSelectors(const std::vector<std::string>& classes,
                              const std::vector<std::string>& ids,
                              const std::vector<std::string>& exceptions,
                              base::OnceCallback<void(std::vector<std::string>)> reply) {
    std::vector<std::string> selectors;
    if (state_ == State::kReady) {
      for (const rust::String& selector :
           (*engine_)->generic_selectors(ToRust(classes), ToRust(ids), ToRust(exceptions))) {
        selectors.emplace_back(selector);
      }
    }
    std::move(reply).Run(std::move(selectors));
  }

  // The bridge's strings must be UTF-8: cxx aborts the process otherwise.
  static rust::Vec<rust::String> ToRust(const std::vector<std::string>& strings) {
    rust::Vec<rust::String> result;
    for (const std::string& s : strings) {
      if (base::IsStringUTF8AllowingNoncharacters(s)) {
        result.push_back(rust::String(s));
      }
    }
    return result;
  }

  State state_ = State::kLoading;
  std::optional<rust::Box<Engine>> engine_;
  std::vector<base::OnceClosure> pending_;
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

void BlockingEngine::GetCosmeticRules(GURL document,
                                      base::OnceCallback<void(CosmeticRules)> reply) {
  core_.AsyncCall(&Core::GetCosmeticRules)
      .WithArgs(std::move(document), base::BindPostTaskToCurrentDefault(std::move(reply)));
}

void BlockingEngine::GetGenericSelectors(
    std::vector<std::string> classes,
    std::vector<std::string> ids,
    std::vector<std::string> exceptions,
    base::OnceCallback<void(std::vector<std::string>)> reply) {
  core_.AsyncCall(&Core::GetGenericSelectors)
      .WithArgs(std::move(classes), std::move(ids), std::move(exceptions),
                base::BindPostTaskToCurrentDefault(std::move(reply)));
}

void BlockingEngine::GetStateForTesting(base::OnceCallback<void(State)> reply) {
  core_.AsyncCall(&Core::state).Then(std::move(reply));
}

}  // namespace ghost::blocking
