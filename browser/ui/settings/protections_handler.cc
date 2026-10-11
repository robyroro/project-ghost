// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/ui/settings/protections_handler.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/components/privacy_policy/protection_level.h"
#include "url/gurl.h"

namespace ghost::settings {
namespace {

// A page of |site| for site_levels, or an empty URL when |site| isn't a bare
// host (a path, a space, not a string).
GURL SitePage(const base::Value& site) {
  if (!site.is_string() || site.GetString().empty()) {
    return GURL();
  }
  GURL page("https://" + site.GetString() + "/");
  if (!page.is_valid() || page.host() != site.GetString()) {
    return GURL();
  }
  return page;
}

}  // namespace

ProtectionsHandler::ProtectionsHandler(Profile* profile) : profile_(profile) {}

ProtectionsHandler::~ProtectionsHandler() = default;

void ProtectionsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "shadeSetSiteLevel", base::BindRepeating(&ProtectionsHandler::HandleSetSiteLevel,
                                               base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "shadeClearSiteLevel", base::BindRepeating(&ProtectionsHandler::HandleClearSiteLevel,
                                                 base::Unretained(this)));
}

void ProtectionsHandler::HandleSetSiteLevel(const base::ListValue& args) {
  if (args.size() != 2 || !args[1].is_string()) {
    return;
  }
  const GURL page = SitePage(args[0]);
  const std::optional<privacy_policy::ProtectionLevel> level =
      privacy_policy::ParseProtectionLevel(args[1].GetString());
  if (page.is_empty() || !level) {
    return;
  }
  // Keyed by the site's registrable domain, as the panel's choices are.
  privacy_policy::SetLevel(profile_, page, *level);
}

void ProtectionsHandler::HandleClearSiteLevel(const base::ListValue& args) {
  if (args.size() != 1) {
    return;
  }
  const GURL page = SitePage(args[0]);
  if (!page.is_empty()) {
    privacy_policy::ClearLevel(profile_, page);
  }
}

}  // namespace ghost::settings
