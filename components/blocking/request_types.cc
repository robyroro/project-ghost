// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/request_types.h"

namespace ghost::blocking {

const char* ToAdblockType(network::mojom::RequestDestination destination) {
  using enum network::mojom::RequestDestination;
  switch (destination) {
    case kDocument:
      return "document";
    case kFrame:
    case kIframe:
    case kFencedframe:
      return "subdocument";
    // Workers and worklets are scripts, as lists write them ($script).
    case kScript:
    case kWorker:
    case kSharedWorker:
    case kServiceWorker:
    case kAudioWorklet:
    case kPaintWorklet:
    case kSharedStorageWorklet:
    case kXslt:
      return "script";
    case kStyle:
      return "stylesheet";
    case kImage:
      return "image";
    case kFont:
      return "font";
    case kAudio:
    case kVideo:
    case kTrack:
      return "media";
    case kObject:
    case kEmbed:
      return "object";
    case kReport:
      return "ping";
    // fetch(), XMLHttpRequest and sendBeacon() carry no destination.
    case kEmpty:
      return "xmlhttprequest";
    case kManifest:
    case kWebBundle:
    case kWebIdentity:
    case kCompressionDictionary:
    case kSpeculationRules:
    case kJson:
    case kEmailVerification:
    case kText:
      return "other";
  }
  return "other";
}

}  // namespace ghost::blocking
