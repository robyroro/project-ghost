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
  // //ghost/components/blocking:filter_lists copies them beside the binaries;
  // patches/0030 packs them in the installer.
  EXPECT_EQ(ReadFilterLists(DefaultFilterListsDir()).size(), std::size(kFilterListFiles));
}

TEST(FilterListsTest, ShadesOwnListIsReadLast) {
  const std::vector<std::string> lists = ReadFilterLists(DefaultFilterListsDir());
  ASSERT_FALSE(lists.empty());
  EXPECT_TRUE(lists.back().starts_with("! Title: Shade's fixes")) << lists.back().substr(0, 80);
}

TEST(FilterListsTest, AListIsNamedByItsTitleAndVersion) {
  EXPECT_EQ(ListLabel("[Adblock Plus 2.0]\n! Version: 202610100609\n! Title: EasyList\n||a^\n"),
            "EasyList 202610100609");
  EXPECT_EQ(ListLabel("! Title: Shade's fixes\n! a comment\n"), "Shade's fixes");
  EXPECT_EQ(ListLabel("||a.test^\n! Title: Too late\n"), "Untitled list");
}

TEST(FilterListsTest, TheShippedListsHaveNames) {
  std::vector<std::string> labels;
  for (const std::string& list : ReadFilterLists(DefaultFilterListsDir())) {
    labels.push_back(ListLabel(list));
  }
  ASSERT_EQ(labels.size(), 3u);
  EXPECT_TRUE(labels[0].starts_with("EasyList 2")) << labels[0];
  EXPECT_TRUE(labels[1].starts_with("EasyPrivacy 2")) << labels[1];
  EXPECT_EQ(labels[2], "Shade's fixes");
}

}  // namespace
}  // namespace ghost::blocking
