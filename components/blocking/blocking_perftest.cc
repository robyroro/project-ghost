// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The engine's cost with the shipped lists, over requests recorded from news
// front pages (test/request_corpus.tsv: type, page, URL; query values
// replaced with "x"). ADR 0006's budget: under 50 µs per check at p99. It
// fails over budget only in an official build. A development build's numbers
// are reported, not judged: dcheck_always_on compiles every Rust crate with
// -Cdebug-assertions, which makes adblock-rust's matching about 15 times
// slower (p50 150 µs instead of 9 µs), and it lacks ThinLTO.

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/process_metrics.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "ghost/components/blocking/filter_lists.h"
#include "ghost/components/blocking/rust/lib.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

constexpr base::TimeDelta kBudgetP99 = base::Microseconds(50);
// Phase 4A's budget for a document's rules and for a batch of names.
constexpr base::TimeDelta kCosmeticBudgetP99 = base::Milliseconds(1);
// Each request is checked this many times, for enough samples at p99.
constexpr int kPasses = 3;

struct Row {
  std::string type, page, url;
};

std::vector<Row> ReadCorpus() {
  base::FilePath path = base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
                            .AppendASCII("ghost/components/blocking/test/request_corpus.tsv");
  std::string text;
  CHECK(base::ReadFileToString(path, &text)) << path;
  std::vector<Row> rows;
  for (std::string_view line :
       base::SplitStringPiece(text, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::vector<std::string_view> f =
        base::SplitStringPiece(line, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    CHECK_EQ(f.size(), 3u) << line;
    rows.push_back({std::string(f[0]), std::string(f[1]), std::string(f[2])});
  }
  return rows;
}

std::string ShippedLists() {
  std::string lists;
  for (const std::string& list : ReadFilterLists(DefaultFilterListsDir())) {
    lists += list;
    lists += '\n';
  }
  return lists;
}

// p50 and p99 of the times.
std::pair<base::TimeDelta, base::TimeDelta> Percentiles(std::vector<base::TimeDelta> times) {
  std::sort(times.begin(), times.end());
  return {times[times.size() / 2], times[times.size() * 99 / 100]};
}

uint64_t PrivateBytes() {
  auto info = base::ProcessMetrics::CreateCurrentProcessMetrics()->GetMemoryInfo();
  return info.has_value() ? info->private_bytes : 0;
}

TEST(BlockingPerfTest, ChecksWithinBudget) {
  const std::string lists = ShippedLists();
  ASSERT_FALSE(lists.empty());
  const std::vector<Row> corpus = ReadCorpus();
  ASSERT_GT(corpus.size(), 1000u);

  const uint64_t memory_before = PrivateBytes();
  const base::TimeTicks compile_start = base::TimeTicks::Now();
  rust::Box<Engine> engine = new_engine(lists);
  const base::TimeDelta compile = base::TimeTicks::Now() - compile_start;
  const uint64_t memory_after = PrivateBytes();

  std::vector<base::TimeDelta> times;
  int blocked = 0;
  for (int pass = 0; pass < kPasses; ++pass) {
    for (const Row& row : corpus) {
      const base::TimeTicks start = base::TimeTicks::Now();
      const Verdict verdict = engine->check(row.url, row.page, row.type, "GET");
      times.push_back(base::TimeTicks::Now() - start);
      blocked += pass == 0 && verdict.blocked;
    }
  }
  const auto [p50, p99] = Percentiles(std::move(times));

  LOG(INFO) << "Blocking engine: " << lists.size() / 1024 << " KiB of lists compiled in "
            << compile.InMilliseconds() << " ms, about "
            << (memory_after - memory_before) / (1024 * 1024) << " MiB; "
            << corpus.size() << " requests, " << blocked << " blocked; check p50 "
            << p50.InMicrosecondsF() << " us, p99 " << p99.InMicrosecondsF()
            << " us (budget " << kBudgetP99.InMicroseconds() << " us).";
  // The lists are real: many recorded requests are ads or trackers.
  EXPECT_GT(blocked, 0);
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kBudgetP99);
#endif
}

// The rules a document gets at its commit.
TEST(BlockingPerfTest, CosmeticRulesWithinBudget) {
  rust::Box<Engine> engine = new_engine(ShippedLists());
  std::set<std::string> pages;
  for (const Row& row : ReadCorpus()) {
    pages.insert(row.page);
  }
  std::vector<base::TimeDelta> times;
  size_t largest = 0;
  for (int pass = 0; pass < 100; ++pass) {
    for (const std::string& page : pages) {
      const base::TimeTicks start = base::TimeTicks::Now();
      const CosmeticResources resources = engine->cosmetic_resources(page);
      times.push_back(base::TimeTicks::Now() - start);
      largest = std::max(largest, resources.hide_selectors.size());
    }
  }
  const auto [p50, p99] = Percentiles(std::move(times));
  LOG(INFO) << "Cosmetic rules: " << pages.size() << " pages, at most " << largest
            << " selectors; p50 " << p50.InMicrosecondsF() << " us, p99 "
            << p99.InMicrosecondsF() << " us (budget " << kCosmeticBudgetP99.InMicroseconds()
            << " us).";
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kCosmeticBudgetP99);
#endif
}

// A batch as a heavy page sends it: 1,000 names, 50 of them listed.
TEST(BlockingPerfTest, GenericSelectorsWithinBudget) {
  const std::string lists = ShippedLists();
  rust::Box<Engine> engine = new_engine(lists);
  rust::Vec<rust::String> classes;
  for (std::string_view line :
       base::SplitStringPiece(lists, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (classes.size() == 50) {
      break;
    }
    // A generic rule on one class, ##.name.
    if (line.starts_with("##.") && line.find_first_of(" :[>+~.#", 3) == std::string_view::npos) {
      const std::string_view name = line.substr(3);
      classes.push_back(rust::String(name.data(), name.size()));
    }
  }
  ASSERT_EQ(classes.size(), 50u);
  rust::Vec<rust::String> ids;
  for (int i = 0; i < 950; ++i) {
    ids.push_back(rust::String("element-" + base::NumberToString(i)));
  }
  const rust::Vec<rust::String> no_exceptions;
  std::vector<base::TimeDelta> times;
  size_t matched = 0;
  for (int pass = 0; pass < 1000; ++pass) {
    const base::TimeTicks start = base::TimeTicks::Now();
    matched = engine->generic_selectors(classes, ids, no_exceptions).size();
    times.push_back(base::TimeTicks::Now() - start);
  }
  const auto [p50, p99] = Percentiles(std::move(times));
  LOG(INFO) << "Generic selectors: 1000 names, " << matched << " matched; p50 "
            << p50.InMicrosecondsF() << " us, p99 " << p99.InMicrosecondsF() << " us (budget "
            << kCosmeticBudgetP99.InMicroseconds() << " us).";
  EXPECT_GT(matched, 0u);
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kCosmeticBudgetP99);
#endif
}

}  // namespace
}  // namespace ghost::blocking
