// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/update_client/request_scrubber.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/contains.h"

namespace ghost {
namespace {

using Keys = std::initializer_list<std::string_view>;

void KeepOnly(base::DictValue& dict, Keys keys) {
  std::vector<std::string> drop;
  for (const auto [key, value] : dict) {
    if (!base::Contains(keys, key)) {
      drop.push_back(key);
    }
  }
  for (const std::string& key : drop) {
    dict.Remove(key);
  }
}

void KeepOnlyInEach(base::DictValue& parent, std::string_view list, Keys keys) {
  if (base::ListValue* entries = parent.FindList(list)) {
    for (base::Value& entry : *entries) {
      if (entry.is_dict()) {
        KeepOnly(entry.GetDict(), keys);
      }
    }
  }
}

}  // namespace

void ScrubUpdateRequest(base::DictValue& root) {
  KeepOnly(root, {"request"});
  base::DictValue* request = root.FindDict("request");
  if (!request) {
    return;
  }
  KeepOnly(*request, {"protocol", "ismachine", "acceptformat", "sessionid",
                      "requestid", "@updater", "updaterversion", "prodversion",
                      "updaterchannel", "prodchannel", "@os", "arch", "wow64",
                      "dlpref", "os", "apps"});
  if (base::DictValue* os = request->FindDict("os")) {
    KeepOnly(*os, {"platform", "arch", "version"});
  }
  base::ListValue* apps = request->FindList("apps");
  if (!apps) {
    return;
  }
  for (base::Value& value : *apps) {
    if (!value.is_dict()) {
      continue;
    }
    base::DictValue& app = value.GetDict();
    KeepOnly(app, {"appid", "version", "ap", "brand", "release_channel",
                   "enabled", "disabled", "cached_items", "updatecheck",
                   "data"});
    KeepOnlyInEach(app, "disabled", {"reason"});
    KeepOnlyInEach(app, "cached_items", {"sha256"});
    KeepOnlyInEach(app, "data", {"name", "index"});
    if (base::DictValue* check = app.FindDict("updatecheck")) {
      KeepOnly(*check, {"updatedisabled", "rollback_allowed",
                        "sameversionupdate", "targetversionprefix"});
    }
  }
}

}  // namespace ghost
