// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/procedural.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"

namespace ghost::cosmetic_filters {
namespace {

bool IsSafe(const std::string& filter) {
  std::optional<base::DictValue> rule =
      base::JSONReader::ReadDict(filter, base::JSON_PARSE_RFC);
  if (!rule) {
    return false;
  }
  const base::ListValue* selector = rule->FindList("selector");
  if (!selector || selector->empty()) {
    return false;
  }
  const base::Value* action = rule->Find("action");
  if (!action) {
    return true;
  }
  if (!action->is_dict()) {
    return false;
  }
  const std::string* type = action->GetDict().FindString("type");
  if (!type) {
    return false;
  }
  if (*type != "style") {
    return true;
  }
  const std::string* style = action->GetDict().FindString("arg");
  return style && IsSafeStyle(*style);
}

}  // namespace

std::vector<std::string> SafeProceduralFilters(const std::vector<std::string>& filters) {
  std::vector<std::string> safe;
  for (const std::string& filter : filters) {
    if (IsSafe(filter)) {
      safe.push_back(filter);
    }
  }
  return safe;
}

bool IsMarkerAttribute(std::string_view name) {
  constexpr std::string_view kPrefix = "shade-";
  if (!name.starts_with(kPrefix) || name.size() <= kPrefix.size() ||
      name.size() > kPrefix.size() + 64) {
    return false;
  }
  for (char c : name.substr(kPrefix.size())) {
    if (!base::IsAsciiLower(c) && !base::IsAsciiDigit(c) && c != '-') {
      return false;
    }
  }
  return true;
}

}  // namespace ghost::cosmetic_filters
