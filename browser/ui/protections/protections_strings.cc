// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/protections/protections_strings.h"

#include <string_view>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ghost/browser/ui/protections/level_strings.h"

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
    {"defaultMark", "default"},
    {"incognitoNote", "Changes here last until you close all Incognito windows."},
    {"notApplicable", "Protections don't apply to this page"},
    {"settingsLink", "Protection settings"},
    // The breakage step and report (2026-10-18-breakage-controls-design.md).
    // The category words are CategoryLabel's (breakage_report.cc).
    {"notWorking", "Site not working?"},
    {"offerText", "Try $1 on $2: the page reloads with fewer protections."},
    {"offerReload", "Reload with $1"},
    {"cancel", "Cancel"},
    {"askTitle", "Is it working now?"},
    {"askText", "$1 is at $2."},
    {"askNo", "No, put $1 back"},
    {"askYes", "Yes"},
    {"askNote", "Either way, you can tell us about the site."},
    {"reportTitle", "Report $1"},
    {"reportCategory", "What broke"},
    {"categoryContent", "Page content missing or broken"},
    {"categoryMedia", "Video or audio"},
    {"categorySignIn", "Sign-in or forms"},
    {"categoryOther", "Something else"},
    {"reportDetails", "What went wrong? (optional)"},
    {"reportAddress", "Address"},
    {"reportNote",
     "Opens a public issue on GitHub, filled in with the above. Nothing is sent until you "
     "submit it there."},
    {"reportOpen", "Open on GitHub"},
    {"reportLoading", "Preparing the report…"},
};

}  // namespace

void AddToDataSource(content::WebUIDataSource& source) {
  for (const Entry& entry : kPageStrings) {
    source.AddString(entry.name, base::UTF8ToUTF16(entry.text));
  }
  // Shared with Shade's section of chrome://settings (level_strings).
  constexpr struct {
    std::string_view name;
    std::string_view about;
    ProtectionLevel level;
  } kLevels[] = {
      {"levelOff", "aboutOff", ProtectionLevel::kOff},
      {"levelStandard", "aboutStandard", ProtectionLevel::kStandard},
      {"levelStrict", "aboutStrict", ProtectionLevel::kStrict},
  };
  for (const auto& level : kLevels) {
    source.AddString(level.name, base::UTF8ToUTF16(LevelName(level.level)));
    source.AddString(level.about, base::UTF8ToUTF16(LevelDetails(level.level)));
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
