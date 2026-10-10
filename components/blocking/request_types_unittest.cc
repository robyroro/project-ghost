// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/request_types.h"

#include <set>
#include <string>

#include "services/network/public/mojom/fetch_api.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

using Destination = network::mojom::RequestDestination;

TEST(RequestTypesTest, CommonDestinations) {
  EXPECT_STREQ(ToAdblockType(Destination::kScript), "script");
  EXPECT_STREQ(ToAdblockType(Destination::kWorker), "script");
  EXPECT_STREQ(ToAdblockType(Destination::kServiceWorker), "script");
  EXPECT_STREQ(ToAdblockType(Destination::kImage), "image");
  EXPECT_STREQ(ToAdblockType(Destination::kStyle), "stylesheet");
  EXPECT_STREQ(ToAdblockType(Destination::kFont), "font");
  EXPECT_STREQ(ToAdblockType(Destination::kIframe), "subdocument");
  EXPECT_STREQ(ToAdblockType(Destination::kFrame), "subdocument");
  EXPECT_STREQ(ToAdblockType(Destination::kFencedframe), "subdocument");
  EXPECT_STREQ(ToAdblockType(Destination::kVideo), "media");
  EXPECT_STREQ(ToAdblockType(Destination::kAudio), "media");
  EXPECT_STREQ(ToAdblockType(Destination::kObject), "object");
  EXPECT_STREQ(ToAdblockType(Destination::kDocument), "document");
  // fetch() and XMLHttpRequest have no destination.
  EXPECT_STREQ(ToAdblockType(Destination::kEmpty), "xmlhttprequest");
  EXPECT_STREQ(ToAdblockType(Destination::kReport), "ping");
}

TEST(RequestTypesTest, EveryDestinationMapsToATypeTheEngineKnows) {
  // adblock-rust reads these names (request.rs); anything else is "other".
  const std::set<std::string> known = {
      "document", "font",  "image",  "media",          "object", "ping",
      "script",   "stylesheet", "subdocument", "xmlhttprequest", "other"};
  for (int value = static_cast<int>(Destination::kMinValue);
       value <= static_cast<int>(Destination::kMaxValue); ++value) {
    EXPECT_TRUE(known.contains(ToAdblockType(static_cast<Destination>(value))))
        << "destination " << value;
  }
}

}  // namespace
}  // namespace ghost::blocking
