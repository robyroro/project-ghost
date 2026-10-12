// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/style_sheet.h"

#include <string_view>

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

}  // namespace ghost::cosmetic_filters
