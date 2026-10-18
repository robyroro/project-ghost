// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/blocking/blocking_service.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/task/thread_pool.h"
#include "ghost/components/blocking/explain.h"
#include "ghost/components/blocking/filter_lists.h"

namespace ghost::blocking {
namespace {

BlockingService* g_service = nullptr;

}  // namespace

BlockingService::BlockingService() = default;

BlockingService::~BlockingService() = default;

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
  lists_for_testing_ = lists;
  engine_.Load(std::move(lists));
}

void BlockingService::DescribeBlocks(std::vector<CheckRequest> requests, DescribeCallback reply) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::optional<std::vector<std::string>> lists, std::vector<CheckRequest> requests) {
            if (!lists) {
              lists = ReadFilterLists(DefaultFilterListsDir());
            }
            std::vector<std::string> labels;
            for (const std::string& list : *lists) {
              labels.push_back(ListLabel(list));
            }
            return std::make_pair(ExplainBlocks(*lists, requests), std::move(labels));
          },
          lists_for_testing_, std::move(requests)),
      base::BindOnce(
          [](DescribeCallback reply,
             std::pair<std::vector<std::string>, std::vector<std::string>> result) {
            std::move(reply).Run(std::move(result.first), std::move(result.second));
          },
          std::move(reply)));
}

}  // namespace ghost::blocking
