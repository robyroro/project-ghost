// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/command_line.h"
#include "base/test/task_environment.h"
#include "chrome/browser/component_updater/chrome_component_updater_configurator.h"
#include "chrome/common/chrome_switches.h"
#include "components/prefs/testing_pref_service.h"
#include "components/update_client/update_client.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost {
namespace {

TEST(ComponentUpdateUrlsTest, DisableSwitchBlocksLateRegistrations) {
  base::test::TaskEnvironment task_environment;
  TestingPrefServiceSimple pref_service;
  update_client::RegisterPrefs(pref_service.registry());

  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  command_line.AppendSwitch(switches::kDisableComponentUpdate);
  const auto config = component_updater::MakeChromeComponentUpdaterConfigurator(
      &command_line, &pref_service);

  EXPECT_TRUE(config->UpdateUrl().empty());
  EXPECT_TRUE(config->PingUrl().empty());
}

}  // namespace
}  // namespace ghost
