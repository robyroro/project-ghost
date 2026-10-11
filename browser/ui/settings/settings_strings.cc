// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/settings/settings_strings.h"

#include <string_view>

#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ghost/browser/ui/protections/level_strings.h"
#include "third_party/blink/public/common/global_privacy_control/global_privacy_control_util.h"

namespace ghost::settings {
namespace {

using privacy_policy::ProtectionLevel;

struct Entry {
  std::string_view name;
  std::string_view text;
};

constexpr Entry kStrings[] = {
    {"shadeProtectionsTitle", "Shade protections"},
    {"shadeProtectionsLead",
     "Choose how much Shade blocks on the sites you visit. You can change it for any site from "
     "the shield in the toolbar."},
    {"shadeDefaultLevel", "Default level"},
    {"shadeLevelOffSummary", "Nothing is blocked. Cookie, HTTPS and GPC protections still apply."},
    {"shadeLevelOffWarning", "Trackers and ads load on every site you haven't set."},
    {"shadeLevelStandardSummary", "Balanced for privacy and for sites that work."},
    {"shadeLevelStrictSummary", "Stronger protection; some sites may break."},
    {"shadeRecommended", "Recommended"},
    {"shadeIncognitoNote", "Incognito windows always start at Strict."},
    {"shadeMoreProtections", "More protections"},
    {"shadeGpc", "Send Global Privacy Control"},
    {"shadeGpcSub", "Tells sites not to sell or share your data."},
    {"shadeRelaunch", "Relaunch"},
    {"shadeCampaign", "Remove campaign parameters"},
    {"shadeCampaignSub",
     "utm_source, utm_medium and the rest. Always removed in Incognito and at Strict."},
    {"shadeSites", "Sites with their own level"},
    {"shadeSitesEmpty", "Sites you set from the shield in the toolbar appear here."},
    {"shadeRemoveSite", "Remove $1"},
    {"shadeSiteLevel", "Level for $1"},
};

}  // namespace

void AddSettingsStrings(content::WebUIDataSource* source) {
  for (const Entry& entry : kStrings) {
    source->AddString(entry.name, base::UTF8ToUTF16(entry.text));
  }
  // The panel's words for the levels (level_strings), so the two agree.
  constexpr struct {
    std::string_view name;
    std::string_view details;
    ProtectionLevel level;
  } kLevels[] = {
      {"shadeLevelOff", "shadeLevelOffDetails", ProtectionLevel::kOff},
      {"shadeLevelStandard", "shadeLevelStandardDetails", ProtectionLevel::kStandard},
      {"shadeLevelStrict", "shadeLevelStrictDetails", ProtectionLevel::kStrict},
  };
  for (const auto& level : kLevels) {
    source->AddString(level.name, base::UTF8ToUTF16(protections::strings::LevelName(level.level)));
    source->AddString(level.details,
                      base::UTF8ToUTF16(protections::strings::LevelDetails(level.level)));
  }
  // What GPC is in this process: the switch applies at the next start, so the
  // page offers a relaunch while the pref differs from it.
  source->AddBoolean("shadeGpcEnabledAtStartup", blink::IsGlobalPrivacyControlEnabled());
}

}  // namespace ghost::settings
