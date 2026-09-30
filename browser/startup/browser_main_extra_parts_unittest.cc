// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Browser tests cannot observe these switches: the browser-test launcher adds
// --disable-component-update to every test on its own. The egress audit
// covers the effect on a real build.

#include "ghost/browser/startup/browser_main_extra_parts.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

TEST(StartupSwitchesTest, DisablesTheComponentUpdater) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  AppendStartupSwitches(command_line);
  EXPECT_TRUE(command_line.HasSwitch(switches::kDisableComponentUpdate));
}

// A relaunch starts from the current command line, which already carries the
// switch; appending again would grow argv on every restart.
TEST(StartupSwitchesTest, DoesNotRepeatSwitchesAlreadyPresent) {
  base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  AppendStartupSwitches(command_line);
  const size_t argc = command_line.argv().size();

  AppendStartupSwitches(command_line);
  EXPECT_EQ(command_line.argv().size(), argc);
}

}  // namespace
}  // namespace ghost
