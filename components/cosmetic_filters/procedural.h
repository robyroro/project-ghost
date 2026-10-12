// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_PROCEDURAL_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_PROCEDURAL_H_

#include <string>
#include <string_view>
#include <vector>

namespace ghost::cosmetic_filters {

// The procedural and action rules (adblock-rust's JSON, one per rule) the
// renderer may run: each an object with a non-empty `selector` list, whose
// `action`, if any, is an object with a `type`, and whose `style` action
// passes IsSafeStyle. The others are dropped.
std::vector<std::string> SafeProceduralFilters(const std::vector<std::string>& filters);

// The attributes procedural.js marks elements with: `shade-` then 1 to 64 of
// [a-z0-9-].
bool IsMarkerAttribute(std::string_view name);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_PROCEDURAL_H_
