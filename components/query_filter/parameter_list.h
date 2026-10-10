// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_QUERY_FILTER_PARAMETER_LIST_H_
#define GHOST_COMPONENTS_QUERY_FILTER_PARAMETER_LIST_H_

#include <string>
#include <string_view>
#include <vector>

namespace ghost::query_filter {

// One entry of the list (data/parameters.txt).
struct Parameter {
  std::string name;  // Without the final '*' of a prefix.
  bool prefix = false;
  // Registrable domains the entry is limited to; empty: every site.
  std::vector<std::string> sites;
};

struct ParameterList {
  std::vector<Parameter> click;
  std::vector<Parameter> campaign;
};

// Parses data/parameters.txt's format. A malformed line is left out and, when
// |skipped| is given, appended to it.
ParameterList ParseParameterList(std::string_view text,
                                 std::vector<std::string>* skipped = nullptr);

// The list compiled into the browser, and its text.
const ParameterList& ShippedParameterList();
std::string_view ShippedParameterListText();

}  // namespace ghost::query_filter

#endif  // GHOST_COMPONENTS_QUERY_FILTER_PARAMETER_LIST_H_
