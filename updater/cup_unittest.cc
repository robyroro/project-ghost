// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Chromium's CUP verifier accepts what the build identity's CUP key signs,
// and nothing else (test/updater/cup_vector*.json).

#include "components/client_update_protocol/cup.h"

#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/values.h"
#include "ghost/branding/cup_key.h"
#include "ghost/branding/signing_buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

#if BUILDFLAG(GHOST_SIGNING_IDENTITY_TEST)
constexpr char kVector[] = "ghost/test/updater/cup_vector_test_identity.json";
#else
constexpr char kVector[] = "ghost/test/updater/cup_vector.json";
#endif

base::DictValue Vector() {
  std::string text;
  CHECK(base::ReadFileToString(
      base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
          .AppendASCII(kVector),
      &text));
  return std::move(*base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC));
}

TEST(CupTest, ChromiumAcceptsTheTestServersProof) {
  const base::DictValue vector = Vector();
  client_update_protocol::Cup cup(kCupKeyVersion, kCupPublicKey);
  cup.PrepareRequestParameters(*vector.FindString("request"));
  cup.OverrideNonceForTesting(kCupKeyVersion, *vector.FindInt("nonce"));
  EXPECT_TRUE(cup.ValidateResponse(*vector.FindString("response"),
                                   *vector.FindString("proof")));
}

TEST(CupTest, ChromiumRejectsAnAlteredResponse) {
  const base::DictValue vector = Vector();
  client_update_protocol::Cup cup(kCupKeyVersion, kCupPublicKey);
  cup.PrepareRequestParameters(*vector.FindString("request"));
  cup.OverrideNonceForTesting(kCupKeyVersion, *vector.FindInt("nonce"));
  EXPECT_FALSE(cup.ValidateResponse(*vector.FindString("response") + " ",
                                    *vector.FindString("proof")));
}

}  // namespace
}  // namespace ghost
