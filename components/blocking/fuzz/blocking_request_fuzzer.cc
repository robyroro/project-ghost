// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Checks against the lists Shade ships, over arbitrary requests: a page
// chooses its requests' URLs. The input is a line of the performance test's
// corpus (test/request_corpus.tsv: type, page, URL), optionally followed by a
// method, so the corpus seeds this fuzzer. The bridge takes any UTF-8, a
// superset of the canonical URLs the browser passes.

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/check.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/blocking/filter_lists.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace {

rust::Str Str(std::string_view s) {
  return rust::Str(s.data(), s.size());
}

ghost::blocking::Engine& ShippedEngine() {
  static rust::Box<ghost::blocking::Engine>* engine = [] {
    std::string lists;
    for (const std::string& list :
         ghost::blocking::ReadFilterLists(ghost::blocking::DefaultFilterListsDir())) {
      lists += list;
      lists += '\n';
    }
    CHECK(!lists.empty()) << "the build copies the lists to <out>/blocking";
    return new rust::Box<ghost::blocking::Engine>(ghost::blocking::new_engine(lists));
  }();
  return **engine;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  if (!base::IsStringUTF8AllowingNoncharacters(input)) {
    return 0;
  }
  const std::vector<std::string_view> fields =
      base::SplitStringPiece(input, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (fields.size() < 3) {
    return 0;
  }
  ShippedEngine().check(Str(fields[2]), Str(fields[1]), Str(fields[0]),
                        Str(fields.size() > 3 ? fields[3] : "GET"));
  return 0;
}
