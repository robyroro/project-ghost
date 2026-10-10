// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_BLOCKING_BLOCKING_SERVICE_H_
#define GHOST_BROWSER_BLOCKING_BLOCKING_SERVICE_H_

#include <string>
#include <vector>

#include "base/no_destructor.h"
#include "ghost/components/blocking/blocking_engine.h"

namespace ghost::blocking {

// The browser's one blocking engine, for every profile and tab
// (docs/architecture.md). Started once the browser's thread pool runs; never
// destroyed, so request filters may hold its engine for as long as they live.
class BlockingService {
 public:
  // Starts loading the shipped lists (DefaultFilterListsDir()). Idempotent.
  static void Start();

  // The service once Start() has run, else null: before that, requests aren't
  // filtered at all rather than held for an engine that never loads.
  static BlockingService* GetIfStarted();

  BlockingEngine& engine() { return engine_; }

  // Replaces the engine's lists, for tests.
  void SetListsForTesting(std::vector<std::string> lists);

 private:
  friend class base::NoDestructor<BlockingService>;

  BlockingService();

  BlockingEngine engine_;
};

}  // namespace ghost::blocking

#endif  // GHOST_BROWSER_BLOCKING_BLOCKING_SERVICE_H_
