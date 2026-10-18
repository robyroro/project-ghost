// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_EXPLAIN_H_
#define GHOST_COMPONENTS_BLOCKING_EXPLAIN_H_

#include <string>
#include <vector>

#include "ghost/components/blocking/blocking_engine.h"

namespace ghost::blocking {

// The rule of `lists` that blocks each request, or an empty string for one
// they allow: for breakage reports (Phase 4D-2). Builds an engine that keeps
// its rules' text, so it takes about as long as loading the lists: call it on
// a task that may block, and only when a report is asked for.
std::vector<std::string> ExplainBlocks(const std::vector<std::string>& lists,
                                       const std::vector<CheckRequest>& requests);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_EXPLAIN_H_
