// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/explain.h"

#include "base/strings/string_util.h"
#include "ghost/components/blocking/request_types.h"
#include "ghost/components/blocking/rust/lib.rs.h"

namespace ghost::blocking {

std::vector<std::string> ExplainBlocks(const std::vector<std::string>& lists,
                                       const std::vector<CheckRequest>& requests) {
  // As BlockingEngine::Load joins them: lists that aren't UTF-8 are left out
  // (cxx aborts on one), and each ends with a newline.
  std::string text;
  for (const std::string& list : lists) {
    if (!list.empty() && base::IsStringUTF8AllowingNoncharacters(list)) {
      text += list;
      text += '\n';
    }
  }
  std::vector<std::string> rules(requests.size());
  if (text.empty()) {
    return rules;
  }
  rust::Box<Engine> engine = new_explaining_engine(text);
  for (size_t i = 0; i < requests.size(); ++i) {
    const CheckRequest& request = requests[i];
    if (!request.url.is_valid() || !base::IsStringUTF8AllowingNoncharacters(request.method)) {
      continue;
    }
    const std::string page =
        request.source_url.is_valid() ? request.source_url.spec() : std::string();
    const Verdict verdict = engine->check(request.url.spec(), page,
                                          ToAdblockType(request.destination), request.method);
    if (verdict.blocked) {
      rules[i] = std::string(verdict.filter);
    }
  }
  return rules;
}

}  // namespace ghost::blocking
