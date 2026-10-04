// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// patches/0018: CRX3_WITH_GHOST_PUBLISHER_PROOF accepts the build identity's
// primary and backup publisher keys and nothing else, and the Web Store
// format still requires Google's.

#include "components/crx_file/crx_verifier.h"

#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "ghost/branding/signing_buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

base::FilePath Fixture(const char* name) {
  return base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
      .AppendASCII("ghost/test/updater/data")
      .AppendASCII(name);
}

// A fixture signed by the build identity's keys.
base::FilePath IdentityFixture(const char* name) {
#if BUILDFLAG(GHOST_SIGNING_IDENTITY_TEST)
  return Fixture("test_identity").AppendASCII(name);
#else
  return Fixture(name);
#endif
}

crx_file::VerifierResult Verify(const base::FilePath& path,
                                crx_file::VerifierFormat format) {
  std::string public_key, crx_id;
  std::vector<uint8_t> verified_contents;
  return crx_file::Verify(path, format, {}, {}, &public_key, &crx_id,
                          &verified_contents);
}

constexpr auto kGhost =
    crx_file::VerifierFormat::CRX3_WITH_GHOST_PUBLISHER_PROOF;

TEST(CrxVerifierTest, GhostFormatAcceptsThePrimaryPublisher) {
  EXPECT_EQ(Verify(IdentityFixture("ghost_publisher.crx3"), kGhost),
            crx_file::VerifierResult::OK_FULL);
}

TEST(CrxVerifierTest, GhostFormatAcceptsTheBackupPublisher) {
  EXPECT_EQ(Verify(IdentityFixture("ghost_backup_publisher.crx3"), kGhost),
            crx_file::VerifierResult::OK_FULL);
}

TEST(CrxVerifierTest, GhostFormatRejectsOtherPublishers) {
  EXPECT_EQ(Verify(Fixture("other_publisher.crx3"), kGhost),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}

TEST(CrxVerifierTest, WebStoreFormatStillRequiresGooglesPublisher) {
  EXPECT_EQ(Verify(IdentityFixture("ghost_publisher.crx3"),
                   crx_file::VerifierFormat::CRX3_WITH_PUBLISHER_PROOF),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}

#if BUILDFLAG(GHOST_SIGNING_IDENTITY_TEST)
// The development keys are committed: a test-identity build must not trust
// a package they sign.
TEST(CrxVerifierTest, TestIdentityRejectsTheDevelopmentPublisher) {
  EXPECT_EQ(Verify(Fixture("ghost_publisher.crx3"), kGhost),
            crx_file::VerifierResult::ERROR_REQUIRED_PROOF_MISSING);
}
#endif

}  // namespace
}  // namespace ghost
