# Tracking-Parameter Stripping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A top-level navigation that brings its URL from another site or from the user loses its tracking parameters before it is sent, and commits at the clean URL ([spec](../specs/2026-10-10-query-filter-design.md)).

**Architecture:** `//ghost/components/query_filter` is pure logic: the list (`data/parameters.txt`, compiled in by a GN action), its parser, `Strip()` and the decisions (`ComesFromElsewhere`, `CrossesSites`). `//ghost/browser/query_filter` holds a `blink::URLLoaderThrottle` for outermost-main-frame navigations, which rewrites the URL at the start (an internal redirect) and at cross-site redirects, and the profile pref for campaign parameters. Patch 0032 adds the throttle in `ChromeContentBrowserClient::CreateURLLoaderThrottles`; patch 0002 grows by the call that registers Ghost's profile prefs.

**Tech Stack:** C++ (Chromium 152.0.7977.158: `//ghost/components/site` (3A's registrable domains), `//url`, `blink::URLLoaderThrottle`, `user_prefs`), GN, Python 3 (build-time embedding, stdlib only), gtest unit and browser tests, libFuzzer.

**Conventions (from 3A):** edit in webops, copy into `chromium/src/ghost` with the scratchpad's `sync.sh` (bash `cp`, so mtimes change), build in `out/vanilla`; never edit `src` while a build runs; webops commits carry no trailers; Chromium commits use `git commit -s` with `Why:`/`Upstream:` trailers and a message file; `python tools/patches.py export --src chromium/src` then `check`. Run: `autoninja -C out\vanilla ghost_unittests ghost_browsertests` from `chromium\src` with `C:\src\depot_tools` on `PATH`.

---

## File structure

| File | Responsibility |
|---|---|
| `tools/embed_text.py`, `tools/tests/test_embed_text.py` | Build-time: a text file as a C++ raw string constant in a header |
| `components/query_filter/data/parameters.txt` | The list (MPL-2.0) |
| `components/query_filter/parameter_list.{h,cc}` | `Parameter`, `ParameterList`, `ParseParameterList()`, `ShippedParameterList()` |
| `components/query_filter/query_filter.{h,cc}` | `Scope`, `Strip()`, `IsFilteredNavigation()`, `ComesFromElsewhere()`, `CrossesSites()` |
| `components/query_filter/*_unittest.cc` | Unit tests |
| `components/query_filter/fuzz/query_filter_fuzzer.cc` | libFuzzer target |
| `components/query_filter/BUILD.gn` | `query_filter`, the embedding action, `unit_tests`, the fuzzer |
| `browser/query_filter/prefs.{h,cc}` | The pref name and its registration |
| `browser/query_filter/query_filter_throttle.{h,cc}` | The throttle and `MaybeCreateQueryFilterThrottle()` |
| `browser/query_filter/query_filter_browsertest.cc` | Browser tests |
| `browser/query_filter/BUILD.gn` | `query_filter` source set (no `//chrome/browser` dependency: it's linked into `//chrome/browser:core`) |
| `browser/prefs/profile_prefs.{h,cc}` | `ghost::RegisterProfilePrefs()`, called by patch 0002 |
| `BUILD.gn` | test sources and deps; the fuzzer in `gn_all` |
| `patches/0002-*.patch`, `patches/0032-*.patch` | Exported from the Chromium checkout |

### Task 0: One definition of a site

`net::registry_controlled_domains::SameDomainOrHost` ignores unknown registries, so `shop.b.test` and `www.b.test` (and intranet names) would be two sites; 3A's `RegistrableDomain()` counts them and treats IP addresses as their own site. Stripping must agree with blocking, so the function moves where both can use it.

**Files:** Create `components/site/registrable_domain.{h,cc}`, `components/site/registrable_domain_unittest.cc`, `components/site/BUILD.gn`; modify `components/blocking/registrable_domain.{h,cc}` (keeps only `RegistrableDomainRange`, the bridge's callback), `components/blocking/registrable_domain_unittest.cc` (keeps the range test), `components/blocking/BUILD.gn`, `browser/blocking/request_filter.cc`, root `BUILD.gn` (`ghost_unittests` gains `//ghost/components/site:unit_tests`).

- [ ] **Step 1:** Move `RegistrableDomain()` and its comment to `ghost::RegistrableDomain()` in `components/site/registrable_domain.h` ("The one definition of the same site in Shade: blocking, through adblock-rust's bridge, and the query filter."), with `//net` and `//url` deps and no cxx; move `KnownRegistries`, `UnknownRegistriesCountTheirLastLabel`, `HostsWithoutADomainAreThemselves` to `components/site/registrable_domain_unittest.cc` unchanged.
- [ ] **Step 2:** `components/blocking/registrable_domain.cc`'s `RegistrableDomainRange` calls `ghost::RegistrableDomain`; `request_filter.cc` includes `ghost/components/site/registrable_domain.h`. `//ghost/components/blocking:registrable_domain` depends on `//ghost/components/site`.
- [ ] **Step 3:** Sync, build `ghost_unittests ghost_browsertests`, run `RegistrableDomain*:Blocking*:EngineBridge*:RequestFilter*`: all pass as before.
- [ ] **Step 4:** Commit: `site: one definition of a site, shared by blocking and the query filter`.

### Task 1: Build-time text embedding

**Files:** Create `tools/embed_text.py`, `tools/tests/test_embed_text.py`.

- [ ] **Step 1: Tests first.**

```python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import tempfile
import unittest
from pathlib import Path

import embed_text


class RenderTest(unittest.TestCase):
    def test_a_header_with_the_text_as_a_raw_string(self):
        header = embed_text.render("a\nb \"c\"\n", "ghost::query_filter", "kList",
                                   "gen/ghost/list.h", "data/list.txt")
        self.assertIn("namespace ghost::query_filter {\n", header)
        self.assertIn('inline constexpr char kList[] = R"embed(a\nb "c"\n)embed";\n', header)
        self.assertIn("#ifndef GEN_GHOST_LIST_H_\n", header)
        self.assertIn("data/list.txt", header)

    def test_refuses_text_that_would_end_the_raw_string(self):
        with self.assertRaises(ValueError):
            embed_text.render('x)embed"y', "n", "k", "o.h", "i.txt")

    def test_main_writes_lf_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, out = Path(tmp, "in.txt"), Path(tmp, "out.h")
            src.write_bytes(b"one\r\ntwo\n")
            embed_text.main(["--input", str(src), "--output", str(out), "--namespace", "n",
                             "--name", "kText"])
            data = out.read_bytes()
            self.assertNotIn(b"\r", data)
            self.assertIn(b'R"embed(one\ntwo\n)embed"', data)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2:** `python -m unittest discover -s tools/tests -t tools -p test_embed_text.py` → fails (no module).
- [ ] **Step 3: Implement.**

```python
#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Writes a text file into a C++ header as a raw string constant (a GN action).

    embed_text.py --input data/list.txt --output gen/list.h --namespace ns --name kList

Line endings become LF, so a checkout's autocrlf can't change the binary.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DELIMITER = "embed"


def render(text: str, namespace: str, name: str, output: str, source: str) -> str:
    if f'){DELIMITER}"' in text:
        raise ValueError(f'{source} contains ){DELIMITER}", which would end the raw string')
    guard = re.sub(r"[^A-Za-z0-9]", "_", output).upper() + "_"
    return (f"// Generated by //ghost/tools/embed_text.py from {source}. Don't edit.\n\n"
            f"#ifndef {guard}\n#define {guard}\n\n"
            f"namespace {namespace} {{\n\n"
            f'inline constexpr char {name}[] = R"{DELIMITER}({text}){DELIMITER}";\n\n'
            f"}}  // namespace {namespace}\n\n#endif  // {guard}\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for flag in ("--input", "--output", "--namespace", "--name"):
        parser.add_argument(flag, required=True)
    args = parser.parse_args(argv)
    text = Path(args.input).read_bytes().decode("utf-8").replace("\r\n", "\n")
    Path(args.output).write_text(
        render(text, args.namespace, args.name, Path(args.output).as_posix(),
               Path(args.input).as_posix()), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4:** The tests pass; `python tools/lint.py` clean.
- [ ] **Step 5:** Commit: `tools: embed_text writes a text file into a C++ header`.

### Task 2: The list and its parser

**Files:** Create `components/query_filter/data/parameters.txt`, `parameter_list.{h,cc}`, `parameter_list_unittest.cc`, `BUILD.gn`. Modify `BUILD.gn` (root: `ghost_unittests` deps gain `//ghost/components/query_filter:unit_tests`).

- [ ] **Step 1: The list.** Global click identifiers from privacy-model.md and brave-core's default rule set (`components/query_filter/browser/test_support/query_filter_test_helper.cc`, MPL-2.0); `gbraid` and a global `igshid` from the privacy model; site-tied entries from brave-core plus Spotify's share `si`; Brave's conditional entries (`mkt_tok`, `h_sid`, `h_slt`, `ck_subscriber_id`, kept on unsubscribe links) are left out, since the format has no conditions.

```
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Tracking parameters Shade strips from top-level navigations that come from
# another site or from the user (docs/privacy-model.md#tracking-parameters).
#
# One entry a line: a parameter name, then optionally the registrable domains
# it is limited to (subdomains included). A final * matches every name with
# that prefix. # starts a comment. [click] is stripped in every profile;
# [campaign] in Incognito, and in Normal when the user asks.
#
# Sources: P = docs/privacy-model.md; B = brave-core's query filter rules
# (MPL-2.0, The Brave Authors); O = observed, as noted.

[click]
__hsfp              # B, HubSpot
__hssc              # B, HubSpot
__hstc              # B, HubSpot
__s                 # B, Drip
_bhlid              # B, beehiiv
_branch_match_id    # B, Branch
_branch_referrer    # B, Branch
_gl                 # B, Google's cross-domain linker
_hsenc              # P B, HubSpot
_openstat           # B, Openstat
at_recipient_id     # B, Adobe Campaign
at_recipient_list   # B, Adobe Campaign
bbeml               # B, Bluecore
bsft_clkid          # B, Blueshift
bsft_uid            # B, Blueshift
dclid               # P B, Google Display
et_rid              # B, Emarsys
fb_action_ids       # B, Meta
fb_comment_id       # B, Meta
fbclid              # P B, Meta
gbraid              # P, Google Ads
gclid               # P B, Google Ads
guce_referrer       # B, Yahoo
guce_referrer_sig   # B, Yahoo
hsCtaTracking       # B, HubSpot
igshid              # P B, Instagram (B limits it to instagram.com)
irclickid           # B, Impact
mc_eid              # P B, Mailchimp
ml_subscriber       # B, MailerLite
ml_subscriber_hash  # B, MailerLite
msclkid             # P B, Microsoft Ads
mtm_cid             # B, Matomo
oft_c               # B, Ontraport
oft_ck              # B, Ontraport
oft_d               # B, Ontraport
oft_id              # B, Ontraport
oft_ids             # B, Ontraport
oft_k               # B, Ontraport
oft_lk              # B, Ontraport
oft_sk              # B, Ontraport
oly_anon_id         # B, Omeda
oly_enc_id          # B, Omeda
pk_cid              # B, Matomo
rb_clickid          # B, Rakuten
s_cid               # B, Adobe Analytics
sc_customer         # B, Salesforce
sc_eh               # B, Salesforce
sc_uid              # B, Salesforce
sfmc_activityid     # B, Salesforce Marketing Cloud
sfmc_id             # B, Salesforce Marketing Cloud
sms_click           # B, SMS campaigns
sms_source          # B, SMS campaigns
sms_uph             # B, SMS campaigns
srsltid             # B, Google Merchant Center
ss_email_id         # B, Squarespace
syclid              # B, Yandex
ttclid              # P B, TikTok
twclid              # P B, X
unicorn_click_id    # B, Unicorn
vero_conv           # B, Vero
vero_id             # B, Vero
vgo_ee              # B, ActiveCampaign
wbraid              # P B, Google Ads
wickedid            # B, Wicked Reports
yclid               # P B, Yandex
ymclid              # B, Yandex
ysclid              # B, Yandex

igsh      instagram.com         # B, Instagram share
ref_src   twitter.com x.com     # B, X
ref_url   twitter.com x.com     # B, X
si        youtube.com youtu.be  # B, YouTube share
si        spotify.com           # O, Spotify share links (open.spotify.com/...?si=)

[campaign]
utm_*               # P, Google Analytics campaign parameters
```

- [ ] **Step 2: Tests first** (`parameter_list_unittest.cc`):

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/parameter_list.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::query_filter {
namespace {

TEST(ParameterListTest, ReadsGroupsEntriesPrefixesAndSites) {
  const ParameterList list = ParseParameterList(
      "# a comment\n"
      "[click]\n"
      "fbclid   # Meta\n"
      "si youtube.com youtu.be\n"
      "\n"
      "[campaign]\n"
      "utm_*\n");
  ASSERT_EQ(list.click.size(), 2u);
  EXPECT_EQ(list.click[0].name, "fbclid");
  EXPECT_FALSE(list.click[0].prefix);
  EXPECT_TRUE(list.click[0].sites.empty());
  EXPECT_EQ(list.click[1].sites, (std::vector<std::string>{"youtube.com", "youtu.be"}));
  ASSERT_EQ(list.campaign.size(), 1u);
  EXPECT_EQ(list.campaign[0].name, "utm_");
  EXPECT_TRUE(list.campaign[0].prefix);
}

TEST(ParameterListTest, SkipsMalformedLinesAndKeepsTheRest) {
  std::vector<std::string> skipped;
  const ParameterList list = ParseParameterList(
      "orphan\n"           // before any group
      "[click]\n"
      "*\n"                // an empty prefix would match everything
      "a*b\n"              // * only at the end
      "x Youtube.com\n"    // domains are lowercase
      "y *.example.com\n"  // and plain
      "z example\n"        // with a dot
      "[unknown]\n"
      "inside_unknown\n"
      "[click]\n"
      "gclid\n",
      &skipped);
  ASSERT_EQ(list.click.size(), 1u);
  EXPECT_EQ(list.click[0].name, "gclid");
  EXPECT_EQ(skipped.size(), 8u);
}

TEST(ParameterListTest, TheShippedListParsesWithoutASkippedLine) {
  std::vector<std::string> skipped;
  const ParameterList list = ParseParameterList(ShippedParameterListText(), &skipped);
  EXPECT_TRUE(skipped.empty()) << skipped.front();
  EXPECT_GT(list.click.size(), 60u);
  EXPECT_EQ(list.campaign.size(), 1u);
}

}  // namespace
}  // namespace ghost::query_filter
```

- [ ] **Step 3: Implement** `parameter_list.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_QUERY_FILTER_PARAMETER_LIST_H_
#define GHOST_COMPONENTS_QUERY_FILTER_PARAMETER_LIST_H_

#include <string>
#include <string_view>
#include <vector>

namespace ghost::query_filter {

// One entry of the list (data/parameters.txt).
struct Parameter {
  std::string name;  // Without the final '*' of a prefix.
  bool prefix = false;
  // Registrable domains the entry is limited to; empty: every site.
  std::vector<std::string> sites;
};

struct ParameterList {
  std::vector<Parameter> click;
  std::vector<Parameter> campaign;
};

// Parses data/parameters.txt's format. A malformed line is left out and, when
// |skipped| is given, appended to it.
ParameterList ParseParameterList(std::string_view text,
                                 std::vector<std::string>* skipped = nullptr);

// The list compiled into the browser, and its text.
const ParameterList& ShippedParameterList();
std::string_view ShippedParameterListText();

}  // namespace ghost::query_filter

#endif  // GHOST_COMPONENTS_QUERY_FILTER_PARAMETER_LIST_H_
```

and `parameter_list.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/parameter_list.h"

#include <optional>

#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/query_filter/parameter_list_data.h"

namespace ghost::query_filter {
namespace {

bool IsNameChar(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '_' || c == '-' || c == '.';
}

bool IsValidName(std::string_view name) {
  return !name.empty() && std::ranges::all_of(name, IsNameChar);
}

bool IsValidSite(std::string_view site) {
  return site.find('.') != std::string_view::npos &&
         std::ranges::all_of(site, [](char c) {
           return base::IsAsciiLower(c) || base::IsAsciiDigit(c) || c == '-' || c == '.';
         });
}

std::optional<Parameter> ParseEntry(std::string_view line) {
  std::vector<std::string_view> words = base::SplitStringPiece(
      line, " \t", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  Parameter parameter;
  std::string_view name = words[0];
  if (name.ends_with('*')) {
    parameter.prefix = true;
    name.remove_suffix(1);
  }
  if (!IsValidName(name)) {
    return std::nullopt;
  }
  parameter.name = std::string(name);
  for (size_t i = 1; i < words.size(); ++i) {
    if (!IsValidSite(words[i])) {
      return std::nullopt;
    }
    parameter.sites.emplace_back(words[i]);
  }
  return parameter;
}

}  // namespace

ParameterList ParseParameterList(std::string_view text, std::vector<std::string>* skipped) {
  ParameterList list;
  std::vector<Parameter>* group = nullptr;
  for (std::string_view line :
       base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const std::string_view entry =
        base::TrimWhitespaceASCII(line.substr(0, line.find('#')), base::TRIM_ALL);
    if (entry.empty()) {
      continue;
    }
    if (entry == "[click]" || entry == "[campaign]") {
      group = entry == "[click]" ? &list.click : &list.campaign;
      continue;
    }
    std::optional<Parameter> parameter;
    if (group && !entry.starts_with('[')) {
      parameter = ParseEntry(entry);
    }
    if (!parameter) {
      if (entry.starts_with('[')) {
        group = nullptr;  // An unknown group: its entries are skipped too.
      }
      if (skipped) {
        skipped->emplace_back(line);
      }
      continue;
    }
    group->push_back(std::move(*parameter));
  }
  return list;
}

std::string_view ShippedParameterListText() {
  return kShippedParameterList;
}

const ParameterList& ShippedParameterList() {
  static const base::NoDestructor<ParameterList> list([] {
    std::vector<std::string> skipped;
    ParameterList parsed = ParseParameterList(kShippedParameterList, &skipped);
    for (const std::string& line : skipped) {
      LOG(ERROR) << "Query filter: a line of the shipped list is malformed: " << line;
    }
    return parsed;
  }());
  return *list;
}

}  // namespace ghost::query_filter
```

`components/query_filter/BUILD.gn`:

```gn
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Tracking-parameter stripping's logic (docs/superpowers/specs/
# 2026-10-10-query-filter-design.md): no //content, tested without a browser.

# data/parameters.txt in the binary: the first navigation after startup is
# covered, with no file to read. 3E delivers updates as a component.
action("parameter_list_data") {
  script = "//ghost/tools/embed_text.py"
  inputs = [ "data/parameters.txt" ]
  outputs = [ "$target_gen_dir/parameter_list_data.h" ]
  args = [
    "--input",
    rebase_path(inputs[0], root_build_dir),
    "--output",
    rebase_path(outputs[0], root_build_dir),
    "--namespace",
    "ghost::query_filter",
    "--name",
    "kShippedParameterList",
  ]
}

source_set("query_filter") {
  sources = [
    "parameter_list.cc",
    "parameter_list.h",
  ]
  deps = [
    ":parameter_list_data",
    "//base",
  ]
}

source_set("unit_tests") {
  testonly = true
  sources = [ "parameter_list_unittest.cc" ]
  deps = [
    ":query_filter",
    "//testing/gtest",
  ]
}
```

The generated header's include is `ghost/components/query_filter/parameter_list_data.h` (`$target_gen_dir` is `gen/ghost/components/query_filter`, on the include path as `gen/`).

- [ ] **Step 4:** Sync, build `ghost_unittests`, run `--gtest_filter=ParameterListTest.*`: 3 pass.
- [ ] **Step 5:** Commit: `query_filter: the parameter list and its parser`.

### Task 3: Stripping and the decisions

**Files:** Create `components/query_filter/query_filter.{h,cc}`, `query_filter_unittest.cc`; modify `components/query_filter/BUILD.gn` (sources; deps `//net`, `//url`).

- [ ] **Step 1: Tests first** (`query_filter_unittest.cc`):

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/query_filter.h"

#include <optional>
#include <string>

#include "ghost/components/query_filter/parameter_list.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::query_filter {
namespace {

const ParameterList& TestList() {
  static const ParameterList list = ParseParameterList(
      "[click]\nfbclid\ngclid\nsi youtube.com youtu.be\n[campaign]\nutm_*\n");
  return list;
}

// The URL Strip() makes, or "unchanged".
std::string Stripped(std::string_view url, Scope scope = Scope::kClick) {
  std::optional<GURL> result = Strip(GURL(url), TestList(), scope);
  return result ? result->spec() : "unchanged";
}

TEST(QueryFilterTest, RemovesAParameterFirstInTheMiddleAndLast) {
  EXPECT_EQ(Stripped("https://b.test/?fbclid=1&x=2"), "https://b.test/?x=2");
  EXPECT_EQ(Stripped("https://b.test/?x=1&fbclid=2&y=3"), "https://b.test/?x=1&y=3");
  EXPECT_EQ(Stripped("https://b.test/p?x=1&gclid=2"), "https://b.test/p?x=1");
}

TEST(QueryFilterTest, KeepsTheRestByteForByte) {
  EXPECT_EQ(Stripped("https://b.test/?b=%2F%20+&fbclid=1&a=&a=2&&c"),
            "https://b.test/?b=%2F%20+&a=&a=2&&c");
}

TEST(QueryFilterTest, KeepsTheFragmentAndDropsAnEmptiedQuery) {
  EXPECT_EQ(Stripped("https://b.test/p?fbclid=1#top"), "https://b.test/p#top");
  EXPECT_EQ(Stripped("https://b.test/p?fbclid=1&gclid=2"), "https://b.test/p");
}

TEST(QueryFilterTest, MatchesEncodedNamesExactlyAndEveryRepeat) {
  EXPECT_EQ(Stripped("https://b.test/?f%62clid=1&x=2"), "https://b.test/?x=2");
  EXPECT_EQ(Stripped("https://b.test/?fbclid=1&fbclid=2&fbclid"), "https://b.test/");
  EXPECT_EQ(Stripped("https://b.test/?fbclid2=1&xfbclid=2&FBCLID=3"), "unchanged");
  EXPECT_EQ(Stripped("https://b.test/?x=fbclid"), "unchanged");
}

TEST(QueryFilterTest, ASiteTiedEntryOnlyOnItsSiteAndSubdomains) {
  EXPECT_EQ(Stripped("https://www.youtube.com/watch?v=1&si=2"),
            "https://www.youtube.com/watch?v=1");
  EXPECT_EQ(Stripped("https://youtu.be/1?si=2"), "https://youtu.be/1");
  EXPECT_EQ(Stripped("https://b.test/?si=2"), "unchanged");
  EXPECT_EQ(Stripped("https://notyoutube.com/?si=2"), "unchanged");
}

TEST(QueryFilterTest, CampaignParametersOnlyInTheirScope) {
  EXPECT_EQ(Stripped("https://b.test/?utm_source=a&x=1"), "unchanged");
  EXPECT_EQ(Stripped("https://b.test/?utm_source=a&utm_medium=b&x=1", Scope::kClickAndCampaign),
            "https://b.test/?x=1");
}

TEST(QueryFilterTest, NothingToDo) {
  EXPECT_EQ(Stripped("https://b.test/"), "unchanged");
  EXPECT_EQ(Stripped("https://b.test/?"), "unchanged");
  EXPECT_EQ(Stripped("not a url"), "unchanged");
}

TEST(QueryFilterTest, StrippingTwiceChangesNothing) {
  const GURL once = *Strip(GURL("https://b.test/?fbclid=1&x=2"), TestList(), Scope::kClick);
  EXPECT_FALSE(Strip(once, TestList(), Scope::kClick));
}

TEST(QueryFilterTest, OnlyHttpGetNavigationsAreFiltered) {
  EXPECT_TRUE(IsFilteredNavigation(GURL("https://b.test/"), "GET"));
  EXPECT_TRUE(IsFilteredNavigation(GURL("http://b.test/"), "GET"));
  EXPECT_FALSE(IsFilteredNavigation(GURL("https://b.test/"), "POST"));
  EXPECT_FALSE(IsFilteredNavigation(GURL("file:///c:/a?fbclid=1"), "GET"));
  EXPECT_FALSE(IsFilteredNavigation(GURL("chrome://settings/?fbclid=1"), "GET"));
}

TEST(QueryFilterTest, AURLComesFromElsewhere) {
  const GURL url("https://shop.b.test/?fbclid=1");
  EXPECT_TRUE(ComesFromElsewhere(url, std::nullopt));  // The user.
  EXPECT_TRUE(ComesFromElsewhere(url, url::Origin::Create(GURL("https://a.test/"))));
  EXPECT_TRUE(ComesFromElsewhere(url, url::Origin()));  // Opaque.
  EXPECT_FALSE(ComesFromElsewhere(url, url::Origin::Create(GURL("https://www.b.test/"))));
  EXPECT_FALSE(ComesFromElsewhere(url, url::Origin::Create(GURL("http://b.test/"))));
}

TEST(QueryFilterTest, ARedirectCrossesSites) {
  EXPECT_TRUE(CrossesSites(GURL("https://r.test/"), GURL("https://b.test/")));
  EXPECT_FALSE(CrossesSites(GURL("https://a.b.test/"), GURL("https://c.b.test/")));
  EXPECT_TRUE(CrossesSites(GURL("http://127.0.0.1/"), GURL("http://127.0.0.2/")));
}

}  // namespace
}  // namespace ghost::query_filter
```

- [ ] **Step 2:** Build and run: compile errors (no `query_filter.h`), the expected failure.
- [ ] **Step 3: Implement** `query_filter.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_COMPONENTS_QUERY_FILTER_QUERY_FILTER_H_
#define GHOST_COMPONENTS_QUERY_FILTER_QUERY_FILTER_H_

#include <optional>
#include <string_view>

#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::query_filter {

struct ParameterList;

// What is stripped: click identifiers only, or campaign parameters as well.
enum class Scope { kClick, kClickAndCampaign };

// |url| without the parameters |list| names for |scope|, or std::nullopt when
// it has none. The rest of the query is kept byte for byte, in order; the
// fragment is kept; an emptied query loses its '?'. Names are compared after
// percent-decoding, case-sensitively.
std::optional<GURL> Strip(const GURL& url, const ParameterList& list, Scope scope);

// An http(s) navigation with method GET: the only kind changed. A POST's body
// could be lost in an internal redirect.
bool IsFilteredNavigation(const GURL& url, std::string_view method);

// Whether a navigation to |url| brings it from elsewhere: started by the user
// (no initiator), by another site, or by an opaque origin.
bool ComesFromElsewhere(const GURL& url, const std::optional<url::Origin>& initiator);

// Whether a redirect from |from| to |to| crosses sites (ghost::RegistrableDomain:
// unknown registries count, an IP address is its own site).
bool CrossesSites(const GURL& from, const GURL& to);

}  // namespace ghost::query_filter

#endif  // GHOST_COMPONENTS_QUERY_FILTER_QUERY_FILTER_H_
```

`query_filter.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/query_filter/query_filter.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "ghost/components/site/registrable_domain.h"

namespace ghost::query_filter {
namespace {

bool Matches(const std::vector<Parameter>& parameters, std::string_view name,
             std::string_view site) {
  return std::ranges::any_of(parameters, [&](const Parameter& parameter) {
    const bool name_matches =
        parameter.prefix ? name.starts_with(parameter.name) : name == parameter.name;
    return name_matches &&
           (parameter.sites.empty() || std::ranges::contains(parameter.sites, site));
  });
}

}  // namespace

std::optional<GURL> Strip(const GURL& url, const ParameterList& list, Scope scope) {
  if (!url.is_valid() || !url.has_query()) {
    return std::nullopt;
  }
  const std::string_view site = RegistrableDomain(url.host());
  std::vector<std::string_view> kept;
  bool stripped = false;
  for (std::string_view token :
       base::SplitStringPiece(url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const std::string name = base::UnescapeBinaryURLComponent(token.substr(0, token.find('=')));
    if (Matches(list.click, name, site) ||
        (scope == Scope::kClickAndCampaign && Matches(list.campaign, name, site))) {
      stripped = true;
    } else {
      kept.push_back(token);
    }
  }
  if (!stripped) {
    return std::nullopt;
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

bool IsFilteredNavigation(const GURL& url, std::string_view method) {
  return url.SchemeIsHTTPOrHTTPS() && method == "GET";
}

bool ComesFromElsewhere(const GURL& url, const std::optional<url::Origin>& initiator) {
  if (!initiator || initiator->opaque()) {
    return true;
  }
  return RegistrableDomain(url.host()) != RegistrableDomain(initiator->host());
}

bool CrossesSites(const GURL& from, const GURL& to) {
  return RegistrableDomain(from.host()) != RegistrableDomain(to.host());
}

}  // namespace ghost::query_filter
```

BUILD: `query_filter` gains `query_filter.cc`, `query_filter.h`, deps `//ghost/components/site`, `//url` (public_deps `//url`); `unit_tests` gains `query_filter_unittest.cc` and `//url`.

- [ ] **Step 4:** Build and run `--gtest_filter=QueryFilterTest.*:ParameterListTest.*`: all pass. A failing expectation is fixed in the code, not the test, unless the test contradicts the spec.
- [ ] **Step 5:** Commit: `query_filter: Strip and the decisions on where a URL comes from`.

### Task 4: The fuzzer

**Files:** Create `components/query_filter/fuzz/query_filter_fuzzer.cc`; modify `components/query_filter/BUILD.gn`, root `BUILD.gn` (`gn_all` gains `//ghost/components/query_filter:ghost_query_filter_fuzzer`).

- [ ] **Step 1:**

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The list parser and Strip() over arbitrary input: the first line is a URL,
// the rest a list. Stripping keeps a valid URL of the same origin, and a
// second pass changes nothing (a navigation's throttles can run twice).

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string_view>

#include "base/check.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "ghost/components/query_filter/query_filter.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace ghost::query_filter {

void CheckStrip(const GURL& url, const ParameterList& list, Scope scope) {
  const std::optional<GURL> stripped = Strip(url, list, scope);
  if (!stripped) {
    return;
  }
  CHECK(stripped->is_valid());
  CHECK(url::Origin::Create(*stripped).IsSameOriginWith(url::Origin::Create(url)));
  CHECK(!Strip(*stripped, list, scope));
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const size_t newline = input.find('\n');
  const GURL url(input.substr(0, newline));
  const ParameterList list =
      ParseParameterList(newline == std::string_view::npos ? "" : input.substr(newline + 1));
  for (Scope scope : {Scope::kClick, Scope::kClickAndCampaign}) {
    CheckStrip(url, list, scope);
    CheckStrip(url, ShippedParameterList(), scope);
  }
  return 0;
}

}  // namespace ghost::query_filter
```

BUILD: `import("//testing/libfuzzer/fuzzer_test.gni")` and

```gn
fuzzer_test("ghost_query_filter_fuzzer") {
  sources = [ "fuzz/query_filter_fuzzer.cc" ]
  deps = [
    ":query_filter",
    "//base",
    "//url",
  ]
}
```

- [ ] **Step 2:** `gn gen out\vanilla` succeeds (a no-op group there); commit with Task 3's or alone: `query_filter: a fuzzer over the parser and Strip`.

### Task 5: The campaign pref

**Files:** Create `browser/query_filter/prefs.{h,cc}`, `browser/query_filter/BUILD.gn`, `browser/prefs/profile_prefs.{h,cc}`; modify `browser/prefs/BUILD.gn`; Chromium `chrome/browser/prefs/browser_prefs.cc` (patch 0002).

- [ ] **Step 1:** `browser/query_filter/prefs.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_QUERY_FILTER_PREFS_H_
#define GHOST_BROWSER_QUERY_FILTER_PREFS_H_

namespace user_prefs {
class PrefRegistrySyncable;
}

namespace ghost::query_filter {

// Whether a regular profile strips campaign parameters (utm_*) too; Incognito
// always does. Off by default (docs/privacy-model.md#tracking-parameters);
// its switch comes with 3D. Not synced.
inline constexpr char kStripCampaignParametersPref[] =
    "ghost.query_filter.strip_campaign_parameters";

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

}  // namespace ghost::query_filter

#endif  // GHOST_BROWSER_QUERY_FILTER_PREFS_H_
```

`prefs.cc`: `registry->RegisterBooleanPref(kStripCampaignParametersPref, false);` (include `components/pref_registry/pref_registry_syncable.h`).

`browser/prefs/profile_prefs.h` declares `void ghost::RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);` ("Registers Ghost's own profile prefs; chrome's RegisterProfilePrefs() calls it (patches/0002)."), and `profile_prefs.cc` calls `query_filter::RegisterProfilePrefs(registry)`. `browser/prefs/BUILD.gn` adds the two files and `//ghost/browser/query_filter:prefs`.

`browser/query_filter/BUILD.gn` (first part):

```gn
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Tracking-parameter stripping on navigations. Linked into
# //chrome/browser:core (patches/0032) and //chrome/browser/prefs:impl
# (patches/0002), so it must not depend on //chrome/browser.

source_set("prefs") {
  sources = [
    "prefs.cc",
    "prefs.h",
  ]
  deps = [ "//components/pref_registry" ]
}
```

- [ ] **Step 2:** In the Chromium checkout, amend the commit of patch 0002 (`git rebase -i` is not available: commit a fixup and `git rebase --autosquash` with `GIT_SEQUENCE_EDITOR=:`): `browser_prefs.cc` includes `ghost/browser/prefs/profile_prefs.h` and calls `ghost::RegisterProfilePrefs(registry);` right before the `OverrideProfilePrefDefaults` comment. Export; `patches.py check`. Only 0002 changes.
- [ ] **Step 3:** Build `chrome`; a unit test in `browser/prefs/pref_defaults_unittest.cc`'s style: a `TestingProfile`'s prefs have `kStripCampaignParametersPref` registered and false. Add it as `browser/query_filter/prefs_unittest.cc` to `ghost_unittests`.
- [ ] **Step 4:** Commit webops (`query_filter: the campaign-parameters pref`) and the patch.

### Task 6: The throttle, the hook, the browser tests

**Files:** Create `browser/query_filter/query_filter_throttle.{h,cc}`, `query_filter_browsertest.cc`; modify `browser/query_filter/BUILD.gn`, root `BUILD.gn` (`ghost_browsertests`); Chromium `chrome/browser/chrome_content_browser_client.cc`, `chrome/browser/BUILD.gn` (patch 0032).

- [ ] **Step 1: Browser tests first** (`query_filter_browsertest.cc`). The fixture maps every host to the embedded server and records `host + relative_url` of every request; `Serve` answers `/redirect?<url>` with 302 and anything else with a page. Tests (names are the spec's cases):
  - `ALinkFromAnotherSiteArrivesClean`: on `a.test/page.html`, `content::NavigateToURLFromRenderer(tab, b.test/land?fbclid=1&x=2)` with expected commit URL `b.test/land?x=2`; the server saw `b.test/land?x=2` and never `b.test/land?fbclid=1&x=2`; the last committed entry's URL is clean and the entry count grew by one.
  - `ALinkWithinASiteKeepsItsParameters`: `a.test` → `www.a.test/land?fbclid=1`: committed with it.
  - `ACrossSiteRedirectArrivesClean`: browser-initiated to `r.test/redirect?<b.test/land?gclid=1&x=2>`: commits `b.test/land?x=2`; the server never saw `gclid`.
  - `ASameSiteRedirectKeepsItsParameters`: from `a.test` (renderer) to `a.test/redirect?<www.a.test/land?gclid=1>`: kept.
  - `TheAddressBarArrivesClean`: `ui_test_utils::NavigateToURL(browser(), b.test/land?fbclid=1)` (browser-initiated, no initiator): clean.
  - `CampaignParametersStayInNormal`, `...AreStrippedInIncognito`, `...AreStrippedWithThePref`.
  - `AnIframeKeepsItsParameters`: `a.test` adds an iframe to `b.test/frame?fbclid=1`; the server saw it with `fbclid`.
  - `AFormPostKeepsItsParameters`: a form on `a.test` POSTs to `b.test/form?fbclid=1`; the server saw it as is.
  - `BackAndReloadUseTheCleanURL`: after the clean cross-site navigation, go back, forward and reload: every request the server saw for `b.test/land` is clean.
- [ ] **Step 2:** Build `ghost_browsertests` without the throttle: they fail to link or fail (no stripping).
- [ ] **Step 3: Implement** `query_filter_throttle.h`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef GHOST_BROWSER_QUERY_FILTER_QUERY_FILTER_THROTTLE_H_
#define GHOST_BROWSER_QUERY_FILTER_QUERY_FILTER_THROTTLE_H_

#include <memory>

#include "ghost/components/query_filter/query_filter.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace network {
struct ResourceRequest;
}

namespace ghost::query_filter {

// Strips tracking parameters from a top-level navigation whose URL comes from
// elsewhere: at the start (an internal redirect: the original URL is never
// sent) and at each redirect that crosses sites. The page commits at the
// clean URL.
class QueryFilterThrottle : public blink::URLLoaderThrottle {
 public:
  explicit QueryFilterThrottle(Scope scope);
  ~QueryFilterThrottle() override;

  // blink::URLLoaderThrottle:
  void WillStartRequest(network::ResourceRequest* request, bool* defer) override;
  void WillRedirectRequest(net::RedirectInfo* redirect_info,
                           const network::mojom::URLResponseHead& response_head,
                           bool* defer,
                           network::HttpRequestHeadersUpdateParams* headers_update_params) override;

 private:
  const Scope scope_;
  GURL url_;  // The URL being loaded, to judge the next redirect.
};

// A throttle for a navigation of the outermost main frame, in the scope the
// profile asks for; nullptr for anything else (iframes, subresources).
// ChromeContentBrowserClient::CreateURLLoaderThrottles calls it
// (patches/0032).
std::unique_ptr<blink::URLLoaderThrottle> MaybeCreateQueryFilterThrottle(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context);

}  // namespace ghost::query_filter

#endif  // GHOST_BROWSER_QUERY_FILTER_QUERY_FILTER_THROTTLE_H_
```

`query_filter_throttle.cc`:

```cpp
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/query_filter/query_filter_throttle.h"

#include <optional>

#include "components/prefs/pref_service.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "ghost/browser/query_filter/prefs.h"
#include "ghost/components/query_filter/parameter_list.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"

namespace ghost::query_filter {

QueryFilterThrottle::QueryFilterThrottle(Scope scope) : scope_(scope) {}

QueryFilterThrottle::~QueryFilterThrottle() = default;

void QueryFilterThrottle::WillStartRequest(network::ResourceRequest* request, bool* defer) {
  url_ = request->url;
  if (!IsFilteredNavigation(request->url, request->method) ||
      !ComesFromElsewhere(request->url, request->request_initiator)) {
    return;
  }
  if (std::optional<GURL> clean = Strip(request->url, ShippedParameterList(), scope_)) {
    request->url = *clean;
    url_ = *clean;
  }
}

void QueryFilterThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    network::HttpRequestHeadersUpdateParams* headers_update_params) {
  const GURL from = url_;
  url_ = redirect_info->new_url;
  if (!IsFilteredNavigation(redirect_info->new_url, redirect_info->new_method) ||
      !CrossesSites(from, redirect_info->new_url)) {
    return;
  }
  if (std::optional<GURL> clean = Strip(redirect_info->new_url, ShippedParameterList(), scope_)) {
    redirect_info->new_url = *clean;
    url_ = *clean;
  }
}

std::unique_ptr<blink::URLLoaderThrottle> MaybeCreateQueryFilterThrottle(
    const network::ResourceRequest& request,
    content::BrowserContext* browser_context) {
  if (!request.is_outermost_main_frame ||
      request.destination != network::mojom::RequestDestination::kDocument) {
    return nullptr;
  }
  const bool campaign =
      browser_context->IsOffTheRecord() ||
      user_prefs::UserPrefs::Get(browser_context)->GetBoolean(kStripCampaignParametersPref);
  return std::make_unique<QueryFilterThrottle>(campaign ? Scope::kClickAndCampaign
                                                        : Scope::kClick);
}

}  // namespace ghost::query_filter
```

`browser/query_filter/BUILD.gn` adds:

```gn
source_set("query_filter") {
  sources = [
    "query_filter_throttle.cc",
    "query_filter_throttle.h",
  ]
  public_deps = [
    "//ghost/components/query_filter",
    "//third_party/blink/public/common:headers",
    "//url",
  ]
  deps = [
    ":prefs",
    "//components/prefs",
    "//components/user_prefs",
    "//content/public/browser",
    "//net",
    "//services/network/public/cpp",
    "//services/network/public/mojom",
  ]
}
```

- [ ] **Step 4: Patch 0032.** In the Chromium checkout: `chrome/browser/BUILD.gn` `core` deps gain `"//ghost/browser/query_filter"` (sorted after `//ghost/browser/blocking`); `chrome_content_browser_client.cc` includes `ghost/browser/query_filter/query_filter_throttle.h` and, right after `DCHECK(profile);` in `CreateURLLoaderThrottles`:

```cpp
  // First, so that the other throttles (Safe Browsing among them) see the
  // URL without tracking parameters.
  if (auto query_filter =
          ghost::query_filter::MaybeCreateQueryFilterThrottle(request, browser_context)) {
    result.push_back(std::move(query_filter));
  }
```

Commit with `git commit -s -F <file>`: subject `chrome_content_browser_client: strip tracking parameters from navigations`, `Why: no other embedder hook can change a navigation's URL before it is sent`, `Upstream: not upstreamable: product-specific privacy policy`. Export; `check`.
- [ ] **Step 5:** Sync, build `chrome ghost_unittests ghost_browsertests`; run `QueryFilter*`: all pass. Any failure is a finding for the progress notes before it is fixed.
- [ ] **Step 6:** Commit webops: `query_filter: navigations lose their tracking parameters on the way in`.

### Task 7: The spike's questions

- [ ] **Step 1:** Record in the progress notes (`docs/superpowers/specs/2026-10-10-query-filter-spike.md`) what Task 6's tests showed for questions 1–3 (initiator empty for browser-initiated; the original URL never sent, one history entry; redirects rewritten).
- [ ] **Step 2:** Question 4 by reading `content/browser/renderer_host/navigation_request.cc` (where `is_outermost_main_frame` is set for prerender and fenced frames) and from `BackAndReloadUseTheCleanURL`; record.
- [ ] **Step 3:** Question 5: `StrippingTwiceChangesNothing` covers the idempotence a second throttle run needs; record. Also note whether speculation-rules prefetch passes through `CreateURLLoaderThrottles` with `is_outermost_main_frame` (read `content/browser/preloading/prefetch/`); prefetch is turned off in 3C either way.

### Task 8: Fuzzing, mutation checks, egress audit

- [ ] **Step 1:** In `out/fuzz`: `autoninja -C out\fuzz ghost_query_filter_fuzzer`; seeds: one file per URL of `components/blocking/test/request_corpus.tsv`, each followed by `\n` and the shipped list; run `-max_total_time=1800`; no crash. Record runs, coverage.
- [ ] **Step 2:** M1–M4 of the spec, each made in `src/ghost`, seen failing, undone; then compare `src/ghost` with webops (every tracked file byte for byte).
- [ ] **Step 3:** Egress audit on `out/vanilla`: no unexpected host.

### Task 9: Docs, push

- [ ] **Step 1:** privacy-model (what is stripped and when, the list published, `utm_*` per profile), architecture (units, patch 0032, the open question closed), testing, licensing (brave-core entries), roadmap (3B done), progress notes, spec and plan status.
- [ ] **Step 2:** Tooling tests, lint, `patches.py check`; push (approved: "write the plan and do everything"); wait for both tooling jobs.
- [ ] **Step 3:** Memory updated.
