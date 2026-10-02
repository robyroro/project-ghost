// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/version/version.h"

#include "base/version.h"
#include "components/version_info/version_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

TEST(VersionTest, ADevelopmentBuildShowsTheChromiumRelease) {
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.149"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149");
}

TEST(VersionTest, RespinZeroHasNoSuffix) {
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.14900"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149");
}

TEST(VersionTest, ARespinIsAppended) {
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.14901"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149-1");
  EXPECT_EQ(FormatDisplayVersion(base::Version("152.0.7977.14999"),
                                 base::Version("152.0.7977.149")),
            "152.0.7977.149-99");
}

TEST(VersionTest, TheReleaseVersionIsAReleaseOfTheChromiumRelease) {
  ASSERT_TRUE(ReleaseVersion().IsValid());
  ASSERT_TRUE(ChromiumVersion().IsValid());
  // FormatDisplayVersion() CHECKs that the pair belongs together.
  EXPECT_FALSE(DisplayVersion().empty());
}

// patches/0012: version_info reports CHROMIUM_VERSION, not chrome/VERSION.
// Only a build whose chrome/VERSION differs (a release, or the spike) can
// tell the two apart.
TEST(VersionTest, VersionInfoIsTheChromiumRelease) {
  EXPECT_EQ(version_info::GetVersion(), ChromiumVersion());
}

}  // namespace
}  // namespace ghost
