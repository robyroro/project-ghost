// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_PROTECTIONS_BREAKAGE_REPORT_H_
#define GHOST_BROWSER_PROTECTIONS_BREAKAGE_REPORT_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ghost/components/privacy_policy/protection_level.h"
#include "url/gurl.h"

namespace ghost::protections {

// What the guided step found
// (docs/superpowers/specs/2026-10-18-breakage-controls-design.md).
enum class Outcome { kNotTried, kAlreadyOff, kLowerHelped, kLowerDidntHelp };

// What broke, as the user chose it. The issue form receives its label.
enum class Category { kContent, kMedia, kSignIn, kOther };

// One field of the report, as the panel shows it and the issue form receives
// it: `id` is the form's field id (.github/ISSUE_TEMPLATE/breakage.yml).
struct ReportField {
  std::string id;
  std::string label;
  std::string value;
};

struct ReportInputs {
  ReportInputs();
  ReportInputs(const ReportInputs&);
  ReportInputs(ReportInputs&&);
  ReportInputs& operator=(const ReportInputs&);
  ReportInputs& operator=(ReportInputs&&);
  ~ReportInputs();

  GURL page;
  // The level the site broke at, and the one tried (meaningful with
  // kLowerHelped and kLowerDidntHelp).
  privacy_policy::ProtectionLevel level = privacy_policy::ProtectionLevel::kStandard;
  privacy_policy::ProtectionLevel tried = privacy_policy::ProtectionLevel::kOff;
  Outcome outcome = Outcome::kNotTried;
  std::string version;
  std::vector<std::string> lists;
  // Each blocked host with the rule that blocked it (empty if not found).
  std::vector<std::pair<std::string, std::string>> blocked;
  bool more_hosts = false;
};

struct BreakageReport {
  BreakageReport();
  BreakageReport(const BreakageReport&);
  BreakageReport(BreakageReport&&);
  BreakageReport& operator=(const BreakageReport&);
  BreakageReport& operator=(BreakageReport&&);
  ~BreakageReport();

  // The page's registrable domain, for the issue's title.
  std::string site;
  // ReportAddress(page): the panel shows it for the user to shorten.
  std::string address;
  // level, outcome, version, lists, blocked: shown read-only.
  std::vector<ReportField> fields;
};

// The issue URL's longest length; the blocked hosts are cut to fit.
inline constexpr size_t kMaxIssueUrlLength = 8000;

// The form's ids for the fields the user fills in the panel.
inline constexpr char kSiteFieldId[] = "site";
inline constexpr char kCategoryFieldId[] = "category";
inline constexpr char kDetailsFieldId[] = "details";

// |page| without its query, fragment or credentials.
std::string ReportAddress(const GURL& page);

// |edited| as ReportAddress when it is an http(s) URL on |page|'s registrable
// domain; otherwise |page|'s origin. The panel's renderer supplies |edited|.
std::string CheckedAddress(std::string_view edited, const GURL& page);

BreakageReport BuildReport(const ReportInputs& inputs);

// The category's words, as the panel and the issue show them.
std::string_view CategoryLabel(Category category);

// GitHub's new-issue page for the breakage form, filled in with |report|, the
// category, the user's details and the (checked) address.
GURL IssueUrl(const BreakageReport& report,
              Category category,
              std::string_view details,
              std::string_view address);

}  // namespace ghost::protections

#endif  // GHOST_BROWSER_PROTECTIONS_BREAKAGE_REPORT_H_
