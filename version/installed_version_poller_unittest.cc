// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// patches/0013: the installed version is the release version, so finding it
// installed is not an update. Only a build whose chrome/VERSION differs from
// CHROMIUM_VERSION (a release, or the spike) tells this apart from upstream.

#include "chrome/browser/upgrade_detector/installed_version_poller.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/upgrade_detector/build_state.h"
#include "chrome/browser/upgrade_detector/get_installed_version.h"
#include "chrome/browser/upgrade_detector/installed_version_monitor.h"
#include "ghost/version/version.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

class IdleMonitor final : public InstalledVersionMonitor {
 public:
  void Start(Callback callback) override {}
};

TEST(InstalledVersionPollerTest, TheInstalledReleaseIsNotAnUpdate) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  BuildState build_state;
  bool polled = false;
  InstalledVersionPoller poller(
      &build_state,
      base::BindRepeating(
          [](bool* polled, InstalledVersionCallback callback) {
            *polled = true;
            std::move(callback).Run(
                InstalledAndCriticalVersion(ReleaseVersion()));
          },
          &polled),
      std::make_unique<IdleMonitor>(), task_environment.GetMockTickClock());
  task_environment.RunUntilIdle();
  // kNone is also BuildState's initial value, so check the poll happened.
  ASSERT_TRUE(polled);
  EXPECT_EQ(build_state.update_type(), BuildState::UpdateType::kNone);
}

}  // namespace
}  // namespace ghost
