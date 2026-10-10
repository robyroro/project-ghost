// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// adblock-rust's list parser and the engine it builds, over arbitrary list
// text: every list the bridge can be given (UTF-8, as BlockingEngine ensures)
// builds an engine that answers checks. Rust's side is instrumented too
// (//ghost/build/config:rust_fuzz_coverage).

#include <stddef.h>
#include <stdint.h>

#include <string_view>

#include "base/strings/string_util.h"
#include "ghost/components/blocking/rust/lib.rs.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view lists(reinterpret_cast<const char*>(data), size);
  if (!base::IsStringUTF8AllowingNoncharacters(lists)) {
    return 0;
  }
  rust::Box<ghost::blocking::Engine> engine =
      ghost::blocking::new_engine(rust::Str(lists.data(), lists.size()));
  // Matching walks what the lists built: a third-party request of each kind
  // the rules most often name.
  for (const char* type : {"script", "image", "subdocument", "xmlhttprequest", "other"}) {
    engine->check("https://ads.example.com/banner/ad.js?id=x", "https://news.example.org/", type,
                  "GET");
  }
  return 0;
}
