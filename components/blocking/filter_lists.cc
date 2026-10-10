// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/filter_lists.h"

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/threading/scoped_blocking_call.h"

namespace ghost::blocking {

base::FilePath DefaultFilterListsDir() {
  return base::PathService::CheckedGet(base::DIR_ASSETS).AppendASCII("blocking");
}

std::vector<std::string> ReadFilterLists(const base::FilePath& dir) {
  base::ScopedBlockingCall blocking(FROM_HERE, base::BlockingType::MAY_BLOCK);
  std::vector<std::string> lists;
  for (const char* name : kFilterListFiles) {
    std::string text;
    if (base::ReadFileToString(dir.AppendASCII(name), &text) && !text.empty()) {
      lists.push_back(std::move(text));
    } else {
      LOG(ERROR) << "Blocking: can't read the filter list " << dir.AppendASCII(name);
    }
  }
  return lists;
}

}  // namespace ghost::blocking
