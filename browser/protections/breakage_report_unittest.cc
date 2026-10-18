// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/protections/breakage_report.h"

#include <string>

#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "net/base/url_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::protections {
namespace {

using privacy_policy::ProtectionLevel;

ReportInputs Inputs() {
  ReportInputs inputs;
  inputs.page = GURL("https://www.news.test/world/story?session=s3cr3t#top");
  inputs.level = ProtectionLevel::kStrict;
  inputs.tried = ProtectionLevel::kStandard;
  inputs.outcome = Outcome::kLowerHelped;
  inputs.version = "152.0.7977.169-1";
  inputs.lists = {"EasyList 202610100609", "EasyPrivacy 202610100609", "Shade's fixes"};
  inputs.blocked = {{"metrics.news.test", "||metrics.news.test^"},
                    {"tracker.test", "||tracker.test^$third-party"}};
  return inputs;
}

// The query parameter's value, fully decoded: GitHub decodes the line breaks
// (%0A) that net::GetValueForKeyInQuery leaves escaped.
std::string Param(const GURL& url, std::string_view name) {
  for (net::QueryIterator it(url); !it.IsAtEnd(); it.Advance()) {
    if (it.GetKey() == name) {
      return base::UnescapeBinaryURLComponent(it.GetValue(),
                                              base::UnescapeRule::REPLACE_PLUS_WITH_SPACE);
    }
  }
  return "<missing>";
}

std::string Field(const BreakageReport& report, std::string_view id) {
  for (const ReportField& field : report.fields) {
    if (field.id == id) {
      return field.value;
    }
  }
  return "<missing>";
}

TEST(BreakageReportTest, TheAddressHasNoQueryOrFragment) {
  EXPECT_EQ(ReportAddress(GURL("https://www.news.test/world/story?session=s3cr3t#top")),
            "https://www.news.test/world/story");
  EXPECT_EQ(ReportAddress(GURL("http://news.test:8080/")), "http://news.test:8080/");
}

TEST(BreakageReportTest, AnEditedAddressMustStayOnTheSite) {
  const GURL page("https://www.news.test/world/story?x=1");
  EXPECT_EQ(CheckedAddress("https://www.news.test/world", page), "https://www.news.test/world");
  EXPECT_EQ(CheckedAddress("https://m.news.test/a?token=1#f", page), "https://m.news.test/a");
  EXPECT_EQ(CheckedAddress("https://evil.test/", page), "https://www.news.test/");
  EXPECT_EQ(CheckedAddress("javascript:alert(1)", page), "https://www.news.test/");
  EXPECT_EQ(CheckedAddress("", page), "https://www.news.test/");
}

TEST(BreakageReportTest, TheFieldsSayWhatHappened) {
  const BreakageReport report = BuildReport(Inputs());
  EXPECT_EQ(report.site, "news.test");
  EXPECT_EQ(report.address, "https://www.news.test/world/story");
  EXPECT_EQ(Field(report, "level"), "Strict");
  EXPECT_EQ(Field(report, "outcome"), "Standard fixed it");
  EXPECT_EQ(Field(report, "version"), "152.0.7977.169-1");
  EXPECT_EQ(Field(report, "lists"),
            "EasyList 202610100609, EasyPrivacy 202610100609, Shade's fixes");
  EXPECT_EQ(Field(report, "blocked"),
            "metrics.news.test  ||metrics.news.test^\ntracker.test  ||tracker.test^$third-party");
}

TEST(BreakageReportTest, EachOutcomeHasItsWords) {
  ReportInputs inputs = Inputs();
  inputs.outcome = Outcome::kLowerDidntHelp;
  EXPECT_EQ(Field(BuildReport(inputs), "outcome"), "Standard didn't fix it; back to Strict");
  inputs.outcome = Outcome::kAlreadyOff;
  inputs.level = ProtectionLevel::kOff;
  EXPECT_EQ(Field(BuildReport(inputs), "outcome"), "The site was already Off");
  inputs.outcome = Outcome::kNotTried;
  EXPECT_EQ(Field(BuildReport(inputs), "outcome"), "Not tried");
}

TEST(BreakageReportTest, NothingBlockedIsSaid) {
  ReportInputs inputs = Inputs();
  inputs.blocked.clear();
  EXPECT_EQ(Field(BuildReport(inputs), "blocked"), "Nothing was blocked on this page.");
  inputs.blocked = {{"tracker.test", ""}};
  EXPECT_EQ(Field(BuildReport(inputs), "blocked"), "tracker.test  (rule not found)");
  inputs.more_hosts = true;
  EXPECT_EQ(Field(BuildReport(inputs), "blocked"),
            "tracker.test  (rule not found)\n(more hosts were blocked)");
}

TEST(BreakageReportTest, TheIssueUrlCarriesEveryField) {
  const BreakageReport report = BuildReport(Inputs());
  const GURL url =
      IssueUrl(report, Category::kMedia, "The video stays black.", "https://www.news.test/world");
  EXPECT_EQ(url.scheme(), "https");
  EXPECT_EQ(url.host(), "github.com");
  EXPECT_EQ(url.path(), "/robyroro/project-ghost/issues/new");
  EXPECT_EQ(Param(url, "template"), "breakage.yml");
  EXPECT_EQ(Param(url, "title"), "Breakage: news.test");
  EXPECT_EQ(Param(url, "site"), "https://www.news.test/world");
  EXPECT_EQ(Param(url, "level"), "Strict");
  EXPECT_EQ(Param(url, "outcome"), "Standard fixed it");
  EXPECT_EQ(Param(url, "version"), "152.0.7977.169-1");
  EXPECT_EQ(Param(url, "category"), "Video or audio");
  EXPECT_EQ(Param(url, "details"), "The video stays black.");
  EXPECT_EQ(Param(url, "blocked"),
            "metrics.news.test  ||metrics.news.test^\ntracker.test  ||tracker.test^$third-party");
  EXPECT_EQ(url.spec().find("s3cr3t"), std::string::npos);
}

TEST(BreakageReportTest, ALongListIsCutToFit) {
  ReportInputs inputs = Inputs();
  inputs.blocked.clear();
  for (int i = 0; i < 50; ++i) {
    inputs.blocked.push_back({base::StrCat({"host", base::NumberToString(i), ".test"}),
                              base::StrCat({"||host", base::NumberToString(i), ".test^",
                                            std::string(150, 'x')})});
  }
  const GURL url = IssueUrl(BuildReport(inputs), Category::kOther, "", "https://www.news.test/");
  EXPECT_LE(url.spec().size(), kMaxIssueUrlLength);
  const std::string blocked = Param(url, "blocked");
  EXPECT_TRUE(blocked.starts_with("host0.test")) << blocked;
  EXPECT_TRUE(blocked.ends_with(" more")) << blocked;
}

}  // namespace
}  // namespace ghost::protections
