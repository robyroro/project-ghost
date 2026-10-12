// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_

#include <string>
#include <string_view>
#include <vector>

namespace ghost::cosmetic_filters {

// A user-origin style sheet hiding what the selectors match, one rule per
// selector: in a selector list one invalid selector drops the whole rule.
// Selectors that could end their rule or start another ({, }, ;, @,
// comments, a trailing backslash, newlines) are dropped.
std::string StyleSheetFor(const std::vector<std::string>& selectors);

// Whether a :style() action's declarations stay inside their rule: not
// empty; no {, }, @, comments, backslashes or newlines; no url( or
// image-set(, which would load. `;` separates declarations and is allowed.
bool IsSafeStyle(std::string_view declarations);

// An action rule CSS can express: a selector and its declarations.
struct StyledSelector {
  std::string selector;
  std::string style;
};

// One rule per pair, `selector{style}`; a pair whose selector or style isn't
// safe is dropped.
std::string StyleSheetForActions(const std::vector<StyledSelector>& rules);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_STYLE_SHEET_H_
