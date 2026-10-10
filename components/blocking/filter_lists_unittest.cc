// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/blocking/filter_lists.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::blocking {
namespace {

TEST(FilterListsTest, ReadsTheListsInOrder) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("easyprivacy.txt"), "||t.test^\n"));
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("easylist.txt"), "||a.test^\n"));
  EXPECT_EQ(ReadFilterLists(dir.GetPath()),
            (std::vector<std::string>{"||a.test^\n", "||t.test^\n"}));
}

TEST(FilterListsTest, AMissingListIsLeftOut) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(dir.GetPath().AppendASCII("easylist.txt"), "||a.test^\n"));
  EXPECT_EQ(ReadFilterLists(dir.GetPath()), (std::vector<std::string>{"||a.test^\n"}));
  EXPECT_TRUE(ReadFilterLists(dir.GetPath().AppendASCII("absent")).empty());
}

TEST(FilterListsTest, TheBuildCopiesTheShippedLists) {
  // patches/0029 copies components/blocking/data/*.txt beside the binaries.
  EXPECT_EQ(ReadFilterLists(DefaultFilterListsDir()).size(), std::size(kFilterListFiles));
}

}  // namespace
}  // namespace ghost::blocking
