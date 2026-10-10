// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_strings.h"

#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_ui_data_source.h"

namespace ghost::protections::strings {

namespace {

using privacy_policy::ProtectionLevel;

struct Entry {
  std::string_view name;
  std::string_view text;
};

// The sentences are docs/superpowers/specs/2026-10-10-protections-panel-design.md's.
constexpr Entry kPageStrings[] = {
    {"blockedOne", "request blocked on this page"},
    {"blockedMany", "requests blocked on this page"},
    {"offLabel", "nothing is blocked on this site"},
    {"levelGroup", "Protection level for this site"},
    {"levelOff", "Off"},
    {"levelStandard", "Standard"},
    {"levelStrict", "Strict"},
    {"defaultMark", "default"},
    {"aboutOff",
     "Trackers and ads load. Links keep their tracking parameters. Cookies, HTTPS and GPC "
     "protections still apply."},
    {"aboutStandard",
     "Blocks third-party trackers and ads. Removes click identifiers from links to this site."},
    {"aboutStrict",
     "Also blocks the site's own trackers. Removes campaign parameters. Sends no referrer to "
     "other sites."},
    {"incognitoNote", "Changes here last until you close all Incognito windows."},
    {"notApplicable", "Protections don't apply to this page"},
};

std::string_view LevelName(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return "Off";
    case ProtectionLevel::kStandard:
      return "Standard";
    case ProtectionLevel::kStrict:
      return "Strict";
  }
}

}  // namespace

void AddToDataSource(content::WebUIDataSource& source) {
  for (const Entry& entry : kPageStrings) {
    source.AddString(entry.name, base::UTF8ToUTF16(entry.text));
  }
}

std::u16string ButtonAccessibleName(ProtectionLevel level, int blocked_count) {
  if (level == ProtectionLevel::kOff) {
    return u"Protections: Off";
  }
  return base::UTF8ToUTF16(base::StrCat(
      {"Protections: ", LevelName(level), ", ", base::NumberToString(blocked_count),
       blocked_count == 1 ? " request blocked" : " requests blocked"}));
}

std::u16string NotApplicable() {
  return u"Protections don't apply to this page";
}

}  // namespace ghost::protections::strings
