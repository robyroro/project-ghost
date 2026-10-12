// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser's checks on procedural and style rules, with arbitrary input:
// from 3E, lists arrive between releases.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "ghost/components/cosmetic_filters/procedural.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string input(reinterpret_cast<const char*>(data), size);
  ghost::cosmetic_filters::IsSafeStyle(input);
  ghost::cosmetic_filters::IsMarkerAttribute(input);
  ghost::cosmetic_filters::SafeProceduralFilters({input});
  return 0;
}
