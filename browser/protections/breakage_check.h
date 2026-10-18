// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PROTECTIONS_BREAKAGE_CHECK_H_
#define GHOST_BROWSER_PROTECTIONS_BREAKAGE_CHECK_H_

#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ghost/browser/protections/breakage_report.h"
#include "ghost/components/blocking/blocking_engine.h"
#include "ghost/components/privacy_policy/protection_level.h"

namespace ghost::protections {

// The protections panel's "Site not working?" step for a tab
// (docs/superpowers/specs/2026-10-18-breakage-controls-design.md): one level
// lower and a reload, then the user's answer, which a breakage report states.
// The step ends when the report is opened or the tab leaves the site; the
// lowered level stays then, since the user chose it.
class BreakageCheck : public content::WebContentsObserver,
                      public content::WebContentsUserData<BreakageCheck> {
 public:
  enum class Step {
    kNone,
    // The page reloads at the lower level.
    kReloading,
    // The reload has loaded: the panel asks whether the site works.
    kAsking,
    // The user answered; the report follows.
    kAnswered,
  };

  BreakageCheck(const BreakageCheck&) = delete;
  BreakageCheck& operator=(const BreakageCheck&) = delete;
  ~BreakageCheck() override;

  // Remembers the site's choice and the page's blocked requests, sets the
  // site one level lower and reloads. False, and nothing done, where
  // protections don't apply or the site is Off.
  bool TryLowerLevel();
  // The answers, while asking. Restoring writes the site's old choice back
  // (none, when it had none) and reloads.
  void KeepLevel();
  void RestoreLevel();
  // The report was opened: the step is over.
  void Done();

  Step step() const { return step_; }
  Outcome outcome() const { return outcome_; }
  privacy_policy::ProtectionLevel from() const { return from_; }
  privacy_policy::ProtectionLevel to() const { return to_; }
  // The page's blocked requests when the step began.
  const std::vector<blocking::CheckRequest>& blocked() const { return blocked_; }
  bool more_hosts() const { return more_hosts_; }

  // Runs after each change of step.
  base::CallbackListSubscription AddChangedCallback(base::RepeatingClosure callback);

 private:
  friend class content::WebContentsUserData<BreakageCheck>;

  explicit BreakageCheck(content::WebContents* contents);

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void DidStopLoading() override;

  void SetStep(Step step);

  Step step_ = Step::kNone;
  Outcome outcome_ = Outcome::kNotTried;
  // The site the step is for, its choice before it, and the levels.
  std::string site_;
  std::optional<privacy_policy::ProtectionLevel> choice_before_;
  privacy_policy::ProtectionLevel from_ = privacy_policy::ProtectionLevel::kStandard;
  privacy_policy::ProtectionLevel to_ = privacy_policy::ProtectionLevel::kOff;
  std::vector<blocking::CheckRequest> blocked_;
  bool more_hosts_ = false;
  // The reload has committed: its end of loading asks.
  bool reloaded_ = false;
  base::RepeatingClosureList changed_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// The level one below |level|: Strict to Standard, Standard to Off; none
// below Off.
std::optional<privacy_policy::ProtectionLevel> OneLower(privacy_policy::ProtectionLevel level);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_PROTECTIONS_BREAKAGE_CHECK_H_
