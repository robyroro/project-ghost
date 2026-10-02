// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/components/update_client/request_scrubber.h"

#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

base::DictValue Golden(const char* name) {
  std::string text;
  CHECK(base::ReadFileToString(
      base::PathService::CheckedGet(base::DIR_SRC_TEST_DATA_ROOT)
          .AppendASCII("ghost/test/updater")
          .AppendASCII(name),
      &text));
  return std::move(*base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC));
}

TEST(RequestScrubberTest, KeepsExactlyTheAllowList) {
  base::DictValue request = Golden("request_all_fields.json");
  ScrubUpdateRequest(request);
  EXPECT_EQ(request, Golden("request_scrubbed.json"));
}

TEST(RequestScrubberTest, LeavesAnAllowedRequestUnchanged) {
  base::DictValue request = Golden("request_scrubbed.json");
  ScrubUpdateRequest(request);
  EXPECT_EQ(request, Golden("request_scrubbed.json"));
}

TEST(RequestScrubberTest, DropsAnythingButTheRequest) {
  base::DictValue root;
  root.Set("request", base::DictValue());
  root.Set("other", 1);
  ScrubUpdateRequest(root);
  EXPECT_FALSE(root.Find("other"));
}

}  // namespace
}  // namespace ghost
