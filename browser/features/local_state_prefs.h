// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_FEATURES_LOCAL_STATE_PREFS_H_
#define GHOST_BROWSER_FEATURES_LOCAL_STATE_PREFS_H_

class PrefRegistrySimple;

namespace ghost {

// Whether Shade sends Global Privacy Control (docs/privacy-model.md). Read
// once, when the feature list is created (AppendFeatureOverrides): a change
// applies at the next start.
inline constexpr char kGpcEnabledPref[] = "ghost.gpc.enabled";

// Shade's browser-wide prefs; chrome's RegisterLocalState() calls this
// (patches/0039).
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);

}  // namespace ghost

#endif  // GHOST_BROWSER_FEATURES_LOCAL_STATE_PREFS_H_
