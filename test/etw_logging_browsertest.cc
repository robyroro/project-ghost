// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Chromium registers an ETW provider for its log messages: any trace session
// that enables it receives every message, and lowers the browser's minimum log
// level to the session's. Windows telemetry's DiagTrack sessions enable
// Chromium's provider at the verbose level, and many log messages carry URLs
// and host names. Shade registers no provider (patches/0033).

#include "base/logging_win.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"

namespace ghost {
namespace {

using EtwLoggingBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(EtwLoggingBrowserTest, TheBrowserRegistersNoEtwLogProvider) {
  EXPECT_EQ(logging::LogEventProvider::GetInstance()->registration_handle(), 0u);
}

}  // namespace
}  // namespace ghost
