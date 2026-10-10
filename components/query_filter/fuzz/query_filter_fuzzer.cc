// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The list parser and Strip() over arbitrary input: the first line is a URL,
// the rest a list. Stripping keeps a valid URL of the same origin, and a
// second pass changes nothing (a navigation's throttles can run twice).

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string_view>

#include "base/check.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "ghost/components/query_filter/query_filter.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::query_filter {
namespace {

void CheckStrip(const GURL& url, const ParameterList& list, Scope scope) {
  const std::optional<GURL> stripped = Strip(url, list, scope);
  if (!stripped) {
    return;
  }
  CHECK(stripped->is_valid());
  CHECK(url::Origin::Create(*stripped).IsSameOriginWith(url::Origin::Create(url)));
  CHECK(!Strip(*stripped, list, scope));
}

}  // namespace
}  // namespace ghost::query_filter

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using ghost::query_filter::Scope;
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
