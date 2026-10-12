// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Cosmetic queries against the lists Shade ships, with arbitrary input: a page
// chooses its URL's path and its class and id names. The first line is a
// document URL; each further line is a name, used both as a class and as an
// id. The bridge takes any UTF-8.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/check.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/blocking/filter_lists.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace {

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
  const std::vector<std::string_view> lines =
      base::SplitStringPiece(input, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  // An empty input splits into no lines at all.
  if (lines.empty()) {
    return 0;
  }
  ghost::blocking::Engine& engine = ShippedEngine();
  const ghost::blocking::CosmeticResources resources =
      engine.cosmetic_resources(rust::Str(lines[0].data(), lines[0].size()));
  rust::Vec<rust::String> names;
  for (size_t i = 1; i < lines.size(); ++i) {
    names.push_back(rust::String(lines[i].data(), lines[i].size()));
  }
  engine.generic_selectors(names, names, resources.exceptions);
  return 0;
}
