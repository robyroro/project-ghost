// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// patches/0018: CRX3_WITH_GHOST_PUBLISHER_PROOF accepts only Ghost's
// publisher key, and the Web Store format still requires Google's.

#include "components/crx_file/crx_verifier.h"

#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

base::FilePath Fixture(const char* name) {
  return base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
      .AppendASCII("ghost/test/updater/data")
      .AppendASCII(name);
}

crx_file::VerifierResult Verify(const char* name,
                                crx_file::VerifierFormat format) {
  std::string public_key, crx_id;
  std::vector<uint8_t> verified_contents;
  return crx_file::Verify(Fixture(name), format, {}, {}, &public_key, &crx_id,
                          &verified_contents);
}

TEST(CrxVerifierTest, GhostFormatAcceptsGhostsPublisher) {
  EXPECT_EQ(Verify("ghost_publisher.crx3",
                   crx_file::VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF),
            crx_file::VerifierResult::OK_FULL);
}

TEST(CrxVerifierTest, GhostFormatRejectsOtherPublishers) {
  EXPECT_EQ(Verify("other_publisher.crx3",
                   crx_file::VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}

TEST(CrxVerifierTest, WebStoreFormatStillRequiresGooglesPublisher) {
  EXPECT_EQ(Verify("ghost_publisher.crx3",
                   crx_file::VerifierFormat::CRX3_WITH_PUBLISHER_PROOF),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}

}  // namespace
}  // namespace ghost
