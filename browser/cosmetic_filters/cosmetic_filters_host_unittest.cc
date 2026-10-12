// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_host.h"

#include <string>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/test/test_future.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "ghost/components/cosmetic_filters/names.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/test_support/test_utils.h"
#include "url/gurl.h"

namespace ghost::cosmetic_filters {
namespace {

class CosmeticFiltersHostTest : public ChromeRenderViewHostTestHarness {
 protected:
  mojo::Remote<mojom::CosmeticFiltersHost> BindHost() {
    NavigateAndCommit(GURL("https://a.test/"));
    mojo::Remote<mojom::CosmeticFiltersHost> host;
    CosmeticFiltersHost::Create(main_rfh(), host.BindNewPipeAndPassReceiver());
    return host;
  }
};

// The renderer isn't trusted: names beyond the limits end it.
TEST_F(CosmeticFiltersHostTest, TooManyNamesAreABadMessage) {
  mojo::Remote<mojom::CosmeticFiltersHost> host = BindHost();
  mojo::test::BadMessageObserver bad_message;
  host->GetGenericStyleSheet(std::vector<std::string>(kMaxNamesPerCall + 1, "c"), {},
                             base::DoNothing());
  EXPECT_EQ(bad_message.WaitForBadMessage(), "CosmeticFiltersHost: names outside the limits");
}

TEST_F(CosmeticFiltersHostTest, ANameThatIsNotUtf8IsABadMessage) {
  mojo::Remote<mojom::CosmeticFiltersHost> host = BindHost();
  mojo::test::BadMessageObserver bad_message;
  host->GetGenericStyleSheet({"\xff"}, {}, base::DoNothing());
  EXPECT_EQ(bad_message.WaitForBadMessage(), "CosmeticFiltersHost: names outside the limits");
}

// Unit tests start no blocking service.
TEST_F(CosmeticFiltersHostTest, WithoutTheEngineNothingIsHidden) {
  mojo::Remote<mojom::CosmeticFiltersHost> host = BindHost();
  base::test::TestFuture<const std::string&> style_sheet;
  host->GetGenericStyleSheet({"generic-ad"}, {}, style_sheet.GetCallback());
  EXPECT_EQ(style_sheet.Get(), "");
}

}  // namespace
}  // namespace ghost::cosmetic_filters
