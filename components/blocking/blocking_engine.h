// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_BLOCKING_ENGINE_H_
#define GHOST_COMPONENTS_BLOCKING_BLOCKING_ENGINE_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/threading/sequence_bound.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "url/gurl.h"

namespace ghost::blocking {

// One request, as the engine needs it.
struct CheckRequest {
  GURL url;
  // The page the request is for (the top-level document): filter lists'
  // third-party and $domain= options are relative to it.
  GURL source_url;
  network::mojom::RequestDestination destination;
  std::string method;
};

struct Decision {
  bool blocked = false;
  // The rule that matched, for the console; may be empty.
  std::string filter;
};

// adblock-rust's engine, on a sequence of its own: building it from the lists
// takes about a second, and every check runs off the thread that asks. One
// engine serves every profile and tab.
class BlockingEngine {
 public:
  enum class State { kLoading, kReady, kFailed };

  BlockingEngine();
  BlockingEngine(const BlockingEngine&) = delete;
  BlockingEngine& operator=(const BlockingEngine&) = delete;
  ~BlockingEngine();

  // Builds the engine from filter-list texts. Without any text it fails, and
  // from then on allows every request: a browser that loads nothing is worse
  // than one that doesn't block.
  void Load(std::vector<std::string> lists);

  // Answers on the calling sequence. A check made while the engine loads
  // waits for it, so the first pages after startup don't escape blocking.
  void Check(CheckRequest request, base::OnceCallback<void(Decision)> reply);

  void GetStateForTesting(base::OnceCallback<void(State)> reply);

 private:
  class Core;
  base::SequenceBound<Core> core_;
};

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_BLOCKING_ENGINE_H_
