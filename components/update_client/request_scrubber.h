// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_UPDATE_CLIENT_REQUEST_SCRUBBER_H_
#define GHOST_COMPONENTS_UPDATE_CLIENT_REQUEST_SCRUBBER_H_

#include "base/values.h"

namespace ghost {

// Removes every key the privacy model doesn't allow from a serialized update
// request ({"request": {...}}), including keys a later milestone adds. The
// allow-list is in docs/privacy-model.md and tools/update_server.py; both
// are tested against test/updater/request_*.json. Called by
// ProtocolSerializerJSON::Serialize (patches/0021).
void ScrubUpdateRequest(base::DictValue& root);

}  // namespace ghost

#endif  // GHOST_COMPONENTS_UPDATE_CLIENT_REQUEST_SCRUBBER_H_
