// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/blocking/blocking_service.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_response.h"

namespace ghost::cosmetic_filters {

using net::test_server::BasicHttpResponse;
using net::test_server::HttpRequest;
using net::test_server::HttpResponse;

CosmeticFiltersTestBase::CosmeticFiltersTestBase() = default;
CosmeticFiltersTestBase::~CosmeticFiltersTestBase() = default;

void CosmeticFiltersTestBase::AddPage(std::string path,
                                      std::string html,
                                      base::StringPairs headers) {
  AddResource(std::move(path), "text/html",
              base::StrCat({"<!doctype html><html><body>", html, "</body></html>"}),
              std::move(headers));
}

void CosmeticFiltersTestBase::AddResource(std::string path,
                                          std::string content_type,
                                          std::string body,
                                          base::StringPairs headers) {
  resources_[std::move(path)] = {std::move(content_type), std::move(body), std::move(headers)};
}

void CosmeticFiltersTestBase::AddRedirect(std::string path,
                                          std::string to_host,
                                          std::string to_path) {
  Resource& resource = resources_[std::move(path)];
  resource.redirect_host = std::move(to_host);
  resource.redirect_path = std::move(to_path);
}

void CosmeticFiltersTestBase::SetUpOnMainThread() {
  AddPages();
  AddHostRules();
  host_resolver()->AddRule("*", "127.0.0.1");
  // The servers outlive no test: Unretained is safe.
  for (net::EmbeddedTestServer* server :
       {embedded_test_server(), &embedded_https_test_server()}) {
    server->RegisterRequestHandler(
        base::BindRepeating(&CosmeticFiltersTestBase::Serve, base::Unretained(this)));
    server->RegisterRequestMonitor(
        base::BindRepeating(&CosmeticFiltersTestBase::Record, base::Unretained(this)));
  }
  ASSERT_TRUE(embedded_test_server()->Start());
  // Incognito's strict HTTPS-First (3C) warns before any HTTP page.
  embedded_https_test_server().SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
  ASSERT_TRUE(embedded_https_test_server().Start());
  ASSERT_TRUE(blocking::BlockingService::GetIfStarted());
  blocking::BlockingService::GetIfStarted()->SetListsForTesting({List()});
}

GURL CosmeticFiltersTestBase::Url(std::string_view host, std::string_view path) {
  return embedded_test_server()->GetURL(host, path);
}

GURL CosmeticFiltersTestBase::HttpsUrl(std::string_view host, std::string_view path) {
  return embedded_https_test_server().GetURL(host, path);
}

content::WebContents* CosmeticFiltersTestBase::Tab(Browser* browser) {
  return (browser ? browser : this->browser())->tab_strip_model()->GetActiveWebContents();
}

std::string CosmeticFiltersTestBase::Display(const content::ToRenderFrameHost& frame,
                                             std::string_view id) {
  return content::EvalJs(frame, content::JsReplace(
                                    "getComputedStyle(document.getElementById($1)).display", id))
      .ExtractString();
}

std::string CosmeticFiltersTestBase::WaitForDisplayOf(const content::ToRenderFrameHost& frame,
                                                      std::string_view script,
                                                      std::string_view expected) {
  return content::EvalJs(frame, base::StrCat({R"(new Promise(done => {
             const start = performance.now();
             (function poll() {
               const element = )",
                                              script, R"(;
               const shown = element ? getComputedStyle(element).display : 'missing';
               if (shown === )",
                                              content::JsReplace("$1", expected), R"( ||
                   performance.now() - start > 2000) {
                 done(shown);
               } else {
                 requestAnimationFrame(poll);
               }
             })();
           }))"}))
      .ExtractString();
}

std::string CosmeticFiltersTestBase::WaitForDisplay(const content::ToRenderFrameHost& frame,
                                                    std::string_view id,
                                                    std::string_view expected) {
  return WaitForDisplayOf(frame, content::JsReplace("document.getElementById($1)", id),
                          expected);
}

void CosmeticFiltersTestBase::WaitForAScanner(content::WebContents* tab) {
  ASSERT_TRUE(content::ExecJs(tab, "new Promise(done => setTimeout(done, 2000))"));
}

std::optional<HttpRequest::HeaderMap> CosmeticFiltersTestBase::LastRequestHeaders(
    std::string_view path) {
  base::AutoLock lock(lock_);
  auto found = requests_.find(std::string(path));
  if (found == requests_.end()) {
    return std::nullopt;
  }
  return found->second;
}

std::unique_ptr<HttpResponse> CosmeticFiltersTestBase::Serve(const HttpRequest& request) {
  auto response = std::make_unique<BasicHttpResponse>();
  const std::string path(request.GetURL().path());
  auto found = resources_.find(path);
  if (found == resources_.end()) {
    response->set_code(net::HTTP_NOT_FOUND);
    return response;
  }
  if (!found->second.redirect_host.empty()) {
    // The servers have started: the target's port is known.
    response->set_code(net::HTTP_FOUND);
    response->AddCustomHeader(
        "Location",
        embedded_test_server()
            ->GetURL(found->second.redirect_host, found->second.redirect_path)
            .spec());
    return response;
  }
  for (const auto& [name, value] : found->second.headers) {
    response->AddCustomHeader(name, value);
  }
  response->set_content_type(found->second.content_type);
  response->set_content(found->second.body);
  return response;
}

void CosmeticFiltersTestBase::Record(const HttpRequest& request) {
  base::AutoLock lock(lock_);
  requests_[std::string(request.GetURL().path())] = request.headers;
}

}  // namespace ghost::cosmetic_filters
