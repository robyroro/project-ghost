// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_BLOCKING_BLOCKING_SERVICE_H_
#define GHOST_BROWSER_BLOCKING_BLOCKING_SERVICE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
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

  // The rule that blocks each request and each list's label, from the lists
  // the engine was loaded with, for a breakage report (Phase 4D-2). Reads and
  // compiles the lists on the thread pool; replies on the calling sequence.
  using DescribeCallback = base::OnceCallback<void(std::vector<std::string> rules,
                                                   std::vector<std::string> list_labels)>;
  void DescribeBlocks(std::vector<CheckRequest> requests, DescribeCallback reply);

  // Replaces the engine's lists, for tests.
  void SetListsForTesting(std::vector<std::string> lists);

 private:
  friend class base::NoDestructor<BlockingService>;

  BlockingService();
  ~BlockingService();

  BlockingEngine engine_;
  // SetListsForTesting's lists, which DescribeBlocks then uses instead of
  // the shipped ones.
  std::optional<std::vector<std::string>> lists_for_testing_;
};

}  // namespace ghost::blocking

#endif  // GHOST_BROWSER_BLOCKING_BLOCKING_SERVICE_H_
