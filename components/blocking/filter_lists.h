// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_FILTER_LISTS_H_
#define GHOST_COMPONENTS_BLOCKING_FILTER_LISTS_H_

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"

namespace ghost::blocking {

// Where the installer puts the lists: <version directory>/blocking/, beside
// chrome.dll (base::DIR_ASSETS); in a build, <out>/blocking/.
base::FilePath DefaultFilterListsDir();

// The lists the browser ships (components/blocking/data/), in the order they
// are given to the engine.
// In this order; shade.txt, Shade's own fixes (Phase 4D-1), last.
inline constexpr const char* kFilterListFiles[] = {"easylist.txt", "easyprivacy.txt",
                                                   "shade.txt"};

// Reads each list in `dir`; a missing or unreadable one is logged and left
// out. Blocking I/O: call it on a task that may block.
std::vector<std::string> ReadFilterLists(const base::FilePath& dir);

// A list as a breakage report names it: its "! Title:" and "! Version:"
// headers, "EasyList 202610100609"; the title alone without a version;
// "Untitled list" without a title. Only the header, the comment lines before
// the first rule, is read.
std::string ListLabel(std::string_view list);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_FILTER_LISTS_H_
