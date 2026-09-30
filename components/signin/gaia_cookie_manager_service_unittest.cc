// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// With ghost::kGoogleAccountsInCookieJar at its default, the account list is
// never requested from Google (patches/0009). The request would be issued
// synchronously in this environment, so an empty loader factory proves that
// nothing was sent.

#include "base/test/task_environment.h"
#include "components/signin/public/identity_manager/accounts_cookie_mutator.h"
#include "components/signin/public/identity_manager/accounts_in_cookie_jar_info.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ghost {
namespace {

class GaiaCookieManagerServiceTest : public testing::Test {
 protected:
  signin::IdentityManager* identity_manager() {
    return identity_env_.identity_manager();
  }

  base::test::TaskEnvironment task_environment_;
  network::TestURLLoaderFactory url_loader_factory_;
  signin::IdentityTestEnvironment identity_env_{&url_loader_factory_};
};

// Reading the list while it is stale is what made a fresh profile ask Google
// at startup.
TEST_F(GaiaCookieManagerServiceTest, ReadingTheAccountListDoesNotAskGoogle) {
  signin::AccountsInCookieJarInfo accounts =
      identity_manager()->GetAccountsInCookieJar();

  EXPECT_EQ(url_loader_factory_.NumPending(), 0);
  EXPECT_TRUE(accounts.GetPotentiallyInvalidSignedInAccounts().empty());
}

// The path taken when Google's sign-in cookies change, for example after the
// user signs in to Google on the web.
TEST_F(GaiaCookieManagerServiceTest, CookieJarUpdatesDoNotAskGoogle) {
  identity_manager()->GetAccountsCookieMutator()->TriggerCookieJarUpdate();

  EXPECT_EQ(url_loader_factory_.NumPending(), 0);
}

}  // namespace
}  // namespace ghost
