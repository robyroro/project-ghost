// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/parameter_list.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/query_filter/parameter_list_data.h"

namespace ghost::query_filter {
namespace {

bool IsNameChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '_' || c == '-' || c == '.';
}

bool IsValidName(std::string_view name) {
  return !name.empty() && std::ranges::all_of(name, IsNameChar);
}

bool IsValidSite(std::string_view site) {
  return site.find('.') != std::string_view::npos &&
         std::ranges::all_of(site, [](char c) {
           return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-' || c == '.';
         });
}

std::optional<Parameter> ParseEntry(std::string_view entry) {
  const std::vector<std::string_view> words = base::SplitStringPiece(
      entry, " \t", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  Parameter parameter;
  std::string_view name = words[0];
  if (name.ends_with('*')) {
    parameter.prefix = true;
    name.remove_suffix(1);
  }
  if (!IsValidName(name)) {
    return std::nullopt;
  }
  parameter.name = std::string(name);
  for (size_t i = 1; i < words.size(); ++i) {
    if (!IsValidSite(words[i])) {
      return std::nullopt;
    }
    parameter.sites.emplace_back(words[i]);
  }
  return parameter;
}

}  // namespace

ParameterList ParseParameterList(std::string_view text, std::vector<std::string>* skipped) {
  ParameterList list;
  std::vector<Parameter>* group = nullptr;
  for (std::string_view line :
       base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const std::string_view entry =
        base::TrimWhitespaceASCII(line.substr(0, line.find('#')), base::TRIM_ALL);
    if (entry.empty()) {
      continue;
    }
    if (entry == "[click]" || entry == "[campaign]") {
      group = entry == "[click]" ? &list.click : &list.campaign;
      continue;
    }
    std::optional<Parameter> parameter;
    if (group && !entry.starts_with('[')) {
      parameter = ParseEntry(entry);
    }
    if (!parameter) {
      if (entry.starts_with('[')) {
        group = nullptr;  // An unknown group: its entries are skipped too.
      }
      if (skipped) {
        skipped->emplace_back(line);
      }
      continue;
    }
    group->push_back(std::move(*parameter));
  }
  return list;
}

std::string_view ShippedParameterListText() {
  return kShippedParameterList;
}

const ParameterList& ShippedParameterList() {
  static const base::NoDestructor<ParameterList> list([] {
    std::vector<std::string> skipped;
    ParameterList parsed = ParseParameterList(kShippedParameterList, &skipped);
    for (const std::string& line : skipped) {
      LOG(ERROR) << "Query filter: a line of the shipped list is malformed: " << line;
    }
    return parsed;
  }());
  return *list;
}

}  // namespace ghost::query_filter
