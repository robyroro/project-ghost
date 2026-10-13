// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Checks against the lists Shade ships, over arbitrary requests: a page
// chooses its requests' URLs. The input is a line of the performance test's
// corpus (test/request_corpus.tsv: type, page, URL), optionally followed by a
// method and by DNS aliases (comma-separated), so the corpus seeds this
// fuzzer. With aliases, the request's alias URLs go through check_aliases too
// (Phase 4C). The bridge takes any UTF-8, a
// superset of the canonical URLs the browser passes.

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/i18n/icu_util.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/blocking/dns_aliases.h"
#include "ghost/components/blocking/filter_lists.h"
#include "ghost/components/blocking/rust/lib.rs.h"
#include "url/gurl.h"

namespace {

// GURL needs ICU for international host names (as url/gurl_fuzzer.cc).
struct Environment {
  Environment() { CHECK(base::i18n::InitializeICU()); }
  base::AtExitManager at_exit_manager;
};

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
  static const base::NoDestructor<Environment> environment;
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  if (!base::IsStringUTF8AllowingNoncharacters(input)) {
    return 0;
  }
  const std::vector<std::string_view> fields =
      base::SplitStringPiece(input, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (fields.size() < 3) {
    return 0;
  }
  const std::string_view method = fields.size() > 3 ? fields[3] : "GET";
  ShippedEngine().check(Str(fields[2]), Str(fields[1]), Str(fields[0]), Str(method));
  if (fields.size() > 4) {
    const GURL url(fields[2]);
    rust::Vec<rust::String> alias_urls;
    // As the request filter: only a valid http(s) URL is uncloaked (an
    // invalid URL can still have an http scheme).
    if (url.is_valid() && url.SchemeIsHTTPOrHTTPS()) {
      const std::vector<std::string> aliases =
          base::SplitString(fields[4], ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
      for (const std::string& alias : ghost::blocking::AliasesToCheck(url.host(), aliases)) {
        // An alias AliasesToCheck keeps but WithAlias can't place is a finding.
        const GURL alias_url = ghost::blocking::WithAlias(url, alias);
        CHECK(alias_url.is_valid()) << url.possibly_invalid_spec() << " with " << alias;
        CHECK_EQ(alias_url.host(), alias) << url.possibly_invalid_spec();
        alias_urls.push_back(rust::String(alias_url.spec()));
      }
    }
    ShippedEngine().check_aliases(Str(fields[2]), Str(fields[1]), Str(fields[0]), Str(method),
                                  alias_urls);
  }
  return 0;
}
