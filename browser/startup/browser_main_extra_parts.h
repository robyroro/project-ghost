// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_STARTUP_BROWSER_MAIN_EXTRA_PARTS_H_
#define GHOST_BROWSER_STARTUP_BROWSER_MAIN_EXTRA_PARTS_H_

#include "chrome/browser/chrome_browser_main_extra_parts.h"

namespace base {
class CommandLine;
}

namespace ghost {

// Ghost's stages of browser-process startup. ChromeBrowserMainParts::Create()
// adds these parts before upstream's (patches/0005), so each stage runs first.
class BrowserMainExtraParts : public ChromeBrowserMainExtraParts {
 public:
  BrowserMainExtraParts();
  BrowserMainExtraParts(const BrowserMainExtraParts&) = delete;
  BrowserMainExtraParts& operator=(const BrowserMainExtraParts&) = delete;
  ~BrowserMainExtraParts() override;

  // ChromeBrowserMainExtraParts:
  void PreEarlyInitialization() override;
  void PostCreateThreads() override;
};

// Adds the switches that turn off upstream services we have no replacement
// for yet. Safe to call on a command line that already has them: a relaunch
// passes the current command line on.
void AppendStartupSwitches(base::CommandLine& command_line);

}  // namespace ghost

#endif  // GHOST_BROWSER_STARTUP_BROWSER_MAIN_EXTRA_PARTS_H_
