// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/cosmetic_filters/names.h"

#include <algorithm>

#include "base/strings/string_util.h"

namespace ghost::cosmetic_filters {

bool IsValidName(std::string_view name) {
  return !name.empty() && name.size() <= kMaxNameLength &&
         base::IsStringUTF8AllowingNoncharacters(name);
}

bool AreValidNames(const std::vector<std::string>& classes, const std::vector<std::string>& ids) {
  return classes.size() + ids.size() <= kMaxNamesPerCall &&
         std::ranges::all_of(classes, IsValidName) && std::ranges::all_of(ids, IsValidName);
}

}  // namespace ghost::cosmetic_filters
