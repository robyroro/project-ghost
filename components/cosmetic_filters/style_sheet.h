// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_

#include <string>
#include <vector>

namespace ghost::cosmetic_filters {

// A user-origin style sheet hiding what the selectors match, one rule per
// selector: in a selector list one invalid selector drops the whole rule.
// Selectors that could end their rule or start another ({, }, ;, @,
// comments, a trailing backslash, newlines) are dropped.
std::string StyleSheetFor(const std::vector<std::string>& selectors);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_
