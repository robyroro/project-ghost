// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/blocking/blocking_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "ghost/components/blocking/filter_lists.h"

namespace ghost::blocking {
namespace {

BlockingService* g_service = nullptr;

}  // namespace

BlockingService::BlockingService() = default;

// static
void BlockingService::Start() {
  if (g_service) {
    return;
  }
  static base::NoDestructor<BlockingService> service;
  g_service = service.get();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(&ReadFilterLists, DefaultFilterListsDir()),
      base::BindOnce([](std::vector<std::string> lists) {
        g_service->engine_.Load(std::move(lists));
      }));
}

// static
BlockingService* BlockingService::GetIfStarted() {
  return g_service;
}

void BlockingService::SetListsForTesting(std::vector<std::string> lists) {
  engine_.Load(std::move(lists));
}

}  // namespace ghost::blocking
