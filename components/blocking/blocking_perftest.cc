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
#include <string>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/process_metrics.h"
#include "base/strings/string_split.h"
#include "base/time/time.h"
#include "ghost/components/blocking/filter_lists.h"
#include "ghost/components/blocking/rust/lib.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

constexpr base::TimeDelta kBudgetP99 = base::Microseconds(50);
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

uint64_t PrivateBytes() {
  auto info = base::ProcessMetrics::CreateCurrentProcessMetrics()->GetMemoryInfo();
  return info.has_value() ? info->private_bytes : 0;
}

TEST(BlockingPerfTest, ChecksWithinBudget) {
  std::string lists;
  for (const std::string& list : ReadFilterLists(DefaultFilterListsDir())) {
    lists += list;
    lists += '\n';
  }
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
  std::sort(times.begin(), times.end());
  const base::TimeDelta p50 = times[times.size() / 2];
  const base::TimeDelta p99 = times[times.size() * 99 / 100];

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

}  // namespace
}  // namespace ghost::blocking
