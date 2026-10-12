// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/style_sheet.h"

#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace ghost::cosmetic_filters {
namespace {

bool IsSafe(std::string_view selector) {
  if (selector.empty() || selector.back() == '\\') {
    return false;
  }
  for (std::string_view unsafe : {"{", "}", ";", "@", "/*", "*/", "\n", "\r", "\f"}) {
    if (selector.find(unsafe) != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string StyleSheetFor(const std::vector<std::string>& selectors) {
  std::string sheet;
  for (const std::string& selector : selectors) {
    if (IsSafe(selector)) {
      sheet += selector;
      sheet += "{display:none !important}\n";
    }
  }
  return sheet;
}

bool IsSafeStyle(std::string_view declarations) {
  if (declarations.empty()) {
    return false;
  }
  for (std::string_view unsafe : {"{", "}", "@", "/*", "*/", "\\", "\n", "\r", "\f"}) {
    if (declarations.find(unsafe) != std::string_view::npos) {
      return false;
    }
  }
  const std::string lower = base::ToLowerASCII(declarations);
  return lower.find("url(") == std::string::npos && lower.find("image-set(") == std::string::npos;
}

std::string StyleSheetForActions(const std::vector<StyledSelector>& rules) {
  std::string sheet;
  for (const StyledSelector& rule : rules) {
    if (IsSafe(rule.selector) && IsSafeStyle(rule.style)) {
      base::StrAppend(&sheet, {rule.selector, "{", rule.style, "}\n"});
    }
  }
  return sheet;
}

}  // namespace ghost::cosmetic_filters
