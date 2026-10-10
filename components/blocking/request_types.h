// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_BLOCKING_REQUEST_TYPES_H_
#define GHOST_COMPONENTS_BLOCKING_REQUEST_TYPES_H_

#include "services/network/public/mojom/fetch_api.mojom-shared.h"

namespace ghost::blocking {

// The request type filter lists name ($script, $image, $subdocument, ...) for
// a Fetch destination, as adblock-rust reads it. Unlisted destinations are
// "other".
const char* ToAdblockType(network::mojom::RequestDestination destination);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_REQUEST_TYPES_H_
