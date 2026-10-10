// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/startup/browser_main_extra_parts.h"

#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#include "ghost/browser/blocking/blocking_service.h"

namespace ghost {

BrowserMainExtraParts::BrowserMainExtraParts() = default;

BrowserMainExtraParts::~BrowserMainExtraParts() = default;

void BrowserMainExtraParts::PreEarlyInitialization() {
  // The earliest browser-process stage: BrowserProcessImpl does not exist yet,
  // so nothing that reads these switches has run.
  AppendStartupSwitches(*base::CommandLine::ForCurrentProcess());
}

void BrowserMainExtraParts::PostCreateThreads() {
  // As early as the thread pool allows: the engine compiles the lists in about
  // a second, and requests made before it is ready wait for it.
  blocking::BlockingService::Start();
}

void BrowserMainExtraParts::PostProfileInit(Profile* profile, bool is_initial_profile) {
  // Runs for every profile, those created later included.
  incognito_defaults_.Watch(profile);
}

void AppendStartupSwitches(base::CommandLine& command_line) {
  // The component updater fetches data components from update.googleapis.com.
  // It stays off until our own update server exists (roadmap, Phase 2).
  // Upstream checks this switch everywhere components are consumed: component
  // registration, First-Party Sets, Live Caption and hyphenation. Skipping
  // registration alone would leave First-Party Sets waiting for component
  // data that never arrives.
  if (!command_line.HasSwitch(switches::kDisableComponentUpdate)) {
    command_line.AppendSwitch(switches::kDisableComponentUpdate);
  }
}

}  // namespace ghost
