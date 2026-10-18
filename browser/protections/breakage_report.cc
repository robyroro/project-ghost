// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_report.h"

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/site/registrable_domain.h"
#include "net/base/url_util.h"

namespace ghost::protections {

namespace {

using privacy_policy::ProtectionLevel;

constexpr char kIssueBase[] = "https://github.com/robyroro/project-ghost/issues/new";
constexpr char kBlockedFieldId[] = "blocked";

std::string_view LevelWord(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::kOff:
      return "Off";
    case ProtectionLevel::kStandard:
      return "Standard";
    case ProtectionLevel::kStrict:
      return "Strict";
  }
}

std::string OutcomeWords(const ReportInputs& inputs) {
  switch (inputs.outcome) {
    case Outcome::kNotTried:
      return "Not tried";
    case Outcome::kAlreadyOff:
      return "The site was already Off";
    case Outcome::kLowerHelped:
      return base::StrCat({LevelWord(inputs.tried), " fixed it"});
    case Outcome::kLowerDidntHelp:
      return base::StrCat(
          {LevelWord(inputs.tried), " didn't fix it; back to ", LevelWord(inputs.level)});
  }
}

std::string BlockedLines(const std::vector<std::pair<std::string, std::string>>& blocked,
                         bool more_hosts) {
  if (blocked.empty()) {
    return "Nothing was blocked on this page.";
  }
  std::vector<std::string> lines;
  for (const auto& [host, rule] : blocked) {
    lines.push_back(base::StrCat({host, "  ", rule.empty() ? "(rule not found)" : rule}));
  }
  if (more_hosts) {
    lines.push_back("(more hosts were blocked)");
  }
  return base::JoinString(lines, "\n");
}

GURL WithParams(const BreakageReport& report,
                std::string_view blocked,
                Category category,
                std::string_view details,
                std::string_view address) {
  GURL url(kIssueBase);
  url = net::AppendQueryParameter(url, "template", "breakage.yml");
  url = net::AppendQueryParameter(url, "title", base::StrCat({"Breakage: ", report.site}));
  url = net::AppendQueryParameter(url, kSiteFieldId, address);
  for (const ReportField& field : report.fields) {
    url = net::AppendQueryParameter(url, field.id,
                                    field.id == kBlockedFieldId ? blocked : field.value);
  }
  url = net::AppendQueryParameter(url, kCategoryFieldId, CategoryLabel(category));
  return net::AppendQueryParameter(url, kDetailsFieldId, details);
}

}  // namespace

ReportInputs::ReportInputs() = default;
ReportInputs::ReportInputs(const ReportInputs&) = default;
ReportInputs::ReportInputs(ReportInputs&&) = default;
ReportInputs& ReportInputs::operator=(const ReportInputs&) = default;
ReportInputs& ReportInputs::operator=(ReportInputs&&) = default;
ReportInputs::~ReportInputs() = default;

BreakageReport::BreakageReport() = default;
BreakageReport::BreakageReport(const BreakageReport&) = default;
BreakageReport::BreakageReport(BreakageReport&&) = default;
BreakageReport& BreakageReport::operator=(const BreakageReport&) = default;
BreakageReport& BreakageReport::operator=(BreakageReport&&) = default;
BreakageReport::~BreakageReport() = default;

std::string ReportAddress(const GURL& page) {
  GURL::Replacements drop;
  drop.ClearQuery();
  drop.ClearRef();
  drop.ClearUsername();
  drop.ClearPassword();
  return page.ReplaceComponents(drop).spec();
}

std::string CheckedAddress(std::string_view edited, const GURL& page) {
  const GURL url(edited);
  if (url.is_valid() && url.SchemeIsHTTPOrHTTPS() && url.has_host() &&
      RegistrableDomain(url.host()) == RegistrableDomain(page.host())) {
    return ReportAddress(url);
  }
  return page.DeprecatedGetOriginAsURL().spec();
}

BreakageReport BuildReport(const ReportInputs& inputs) {
  BreakageReport report;
  report.site = std::string(RegistrableDomain(inputs.page.host()));
  report.address = ReportAddress(inputs.page);
  report.fields = {
      {"level", "Level", std::string(LevelWord(inputs.level))},
      {"outcome", "One level lower", OutcomeWords(inputs)},
      {"version", "Shade", inputs.version},
      {"lists", "Lists", base::JoinString(inputs.lists, ", ")},
      {"blocked", "Blocked on the page", BlockedLines(inputs.blocked, inputs.more_hosts)},
  };
  return report;
}

std::string_view CategoryLabel(Category category) {
  switch (category) {
    case Category::kContent:
      return "Page content missing or broken";
    case Category::kMedia:
      return "Video or audio";
    case Category::kSignIn:
      return "Sign-in or forms";
    case Category::kOther:
      return "Something else";
  }
}

GURL IssueUrl(const BreakageReport& report,
              Category category,
              std::string_view details,
              std::string_view address) {
  std::string blocked;
  for (const ReportField& field : report.fields) {
    if (field.id == kBlockedFieldId) {
      blocked = field.value;
    }
  }
  GURL url = WithParams(report, blocked, category, details, address);
  // Too long: drop whole lines from the end, saying how many.
  std::vector<std::string> lines =
      base::SplitString(blocked, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  size_t dropped = 0;
  while (url.spec().size() > kMaxIssueUrlLength && !lines.empty()) {
    lines.pop_back();
    ++dropped;
    const std::string cut =
        base::StrCat({base::JoinString(lines, "\n"), lines.empty() ? "" : "\n", "+",
                      base::NumberToString(dropped), " more"});
    url = WithParams(report, cut, category, details, address);
  }
  return url;
}

}  // namespace ghost::protections
