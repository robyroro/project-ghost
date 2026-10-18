// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/filter_lists.h"

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
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

std::string ListLabel(std::string_view list) {
  std::string_view title, version;
  for (std::string_view line :
       base::SplitStringPiece(list, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (!line.starts_with('!') && !line.starts_with('[')) {
      break;
    }
    if (line.starts_with("! Title:")) {
      title = base::TrimWhitespaceASCII(line.substr(8), base::TRIM_ALL);
    } else if (line.starts_with("! Version:")) {
      version = base::TrimWhitespaceASCII(line.substr(10), base::TRIM_ALL);
    }
  }
  if (title.empty()) {
    return "Untitled list";
  }
  return version.empty() ? std::string(title) : base::StrCat({title, " ", version});
}

}  // namespace ghost::blocking
