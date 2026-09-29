// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_FEATURES_FEATURE_OVERRIDES_H_
#define GHOST_BROWSER_FEATURES_FEATURE_OVERRIDES_H_

#include <vector>

#include "base/feature_list.h"

namespace ghost {

// Appends the feature states that replace upstream defaults. chrome registers
// these as "extra overrides" during field-trial setup (patches/0003), which
// places them after --enable-features/--disable-features, so the command line
// and chrome://flags still win, and before field trials, so server-side
// experiments cannot undo them.
void AppendFeatureOverrides(
    std::vector<base::FeatureList::FeatureOverrideInfo>* overrides);

}  // namespace ghost

#endif  // GHOST_BROWSER_FEATURES_FEATURE_OVERRIDES_H_
