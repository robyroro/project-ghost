// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The list parser and Strip() over arbitrary input: the first line is a URL,
// the rest a list. Stripping keeps a valid URL with the same scheme, host and
// port, and a second pass changes nothing (a navigation's throttles can run
// twice).

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string_view>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/i18n/icu_util.h"
#include "base/no_destructor.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "ghost/components/query_filter/query_filter.h"
#include "url/gurl.h"
#include "url/scheme_host_port.h"

namespace ghost::query_filter {
namespace {

// GURL needs ICU's data for internationalized hosts, as the browser loads it
// at startup (url/gurl_fuzzer.cc does the same).
struct Environment {
  Environment() { CHECK(base::i18n::InitializeICU()); }
  base::AtExitManager at_exit_manager;
};

void CheckStrip(const GURL& url, const ParameterList& list, Scope scope) {
  const std::optional<GURL> stripped = Strip(url, list, scope);
  if (!stripped) {
    return;
  }
  CHECK(stripped->is_valid());
  // Scheme, host and port: an origin's tuple. url::Origin itself would differ
  // for every opaque URL (data:, unknown schemes), stripped or not.
  CHECK(url::SchemeHostPort(*stripped) == url::SchemeHostPort(url));
  CHECK(!Strip(*stripped, list, scope));
}

}  // namespace
}  // namespace ghost::query_filter

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using ghost::query_filter::Scope;
  static base::NoDestructor<ghost::query_filter::Environment> environment;
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const size_t newline = input.find('\n');
  const GURL url(input.substr(0, newline));
  const ghost::query_filter::ParameterList list = ghost::query_filter::ParseParameterList(
      newline == std::string_view::npos ? std::string_view() : input.substr(newline + 1));
  for (Scope scope : {Scope::kClick, Scope::kClickAndCampaign}) {
    ghost::query_filter::CheckStrip(url, list, scope);
    ghost::query_filter::CheckStrip(url, ghost::query_filter::ShippedParameterList(), scope);
  }
  return 0;
}
