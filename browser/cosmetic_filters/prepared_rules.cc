// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/prepared_rules.h"

#include <utility>

namespace ghost::cosmetic_filters {

PreparedRules::PreparedRules(content::NavigationHandle& navigation,
                             mojom::DocumentRulesPtr rules)
    : rules_(std::move(rules)) {}

PreparedRules::~PreparedRules() = default;

NAVIGATION_HANDLE_USER_DATA_KEY_IMPL(PreparedRules);

}  // namespace ghost::cosmetic_filters
