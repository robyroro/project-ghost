// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_NAMES_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_NAMES_H_

#include <stddef.h>

#include <string>
#include <string_view>
#include <vector>

namespace ghost::cosmetic_filters {

// The limits on what a renderer's scanner sends in one call
// (mojom::CosmeticFiltersHost): more is a bad message.
inline constexpr size_t kMaxNamesPerCall = 1000;
inline constexpr size_t kMaxNameLength = 256;  // Bytes of UTF-8.

bool IsValidName(std::string_view name);
bool AreValidNames(const std::vector<std::string>& classes, const std::vector<std::string>& ids);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_NAMES_H_
