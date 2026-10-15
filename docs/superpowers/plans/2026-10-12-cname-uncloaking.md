# CNAME Uncloaking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tracker behind a first-party subdomain is blocked before the request leaves. The request filter resolves the host through the network service, and the lists judge each DNS alias as they judge a URL ([spec](../specs/2026-10-12-cname-uncloaking-design.md)).

**Architecture:**
- **Engine.** The bridge gains `check_aliases`. It first asks whether an exception covers the request's own URL. If none does, the first alias URL a rule blocks decides. `BlockingEngine::CheckAliases` runs it on the engine's sequence, and `Decision` names the alias that was blocked.
- **Aliases.** `//ghost/components/blocking/dns_aliases` decides which aliases are worth checking and builds each alias's URL. These are pure functions.
- **Browser.** `AliasResolver` wraps one `NetworkContext::ResolveHost` call with the request's key, its secure DNS policy and `direct_only`. `RequestFilter::InFlight` adds the uncloaking step between the URL's verdict and sending, and again before passing a redirect on.
- **Patches.** None expected.

**Tech Stack:**
- adblock-rust 0.13.3 (`Engine::check_network_request_subset`), cxx;
- Chromium 152.0.7977.158: `network::mojom::NetworkContext::ResolveHost`, `network::ResolveHostClientBase`, `net::AddressList::dns_aliases()`, `GURL::Replacements`;
- `InProcessBrowserTest` with `RuleBasedHostResolverProc::AddIPLiteralRuleWithDnsAliases`, libFuzzer.

**Status:** done 2026-10-15 ([progress notes](../specs/2026-10-12-cname-uncloaking-spike.md)). Beyond the plan: `AliasResolver` takes `network::mojom::SecureDnsPolicy` (the mojom field has no typemap); the test URLs are built in C++ (`JsReplace` drops a `$` not followed by a digit); `uncloak_cost.py` pairs its lines with the NetLog's resolutions; `AliasMemo` and M7, after the official build showed the first request to each host over the 1 ms budget; the budget amended to the memo's p90.

**Settled while planning** (each checked in the source at the pin; the spec is amended to match):
- **Exceptions show only after a match.** `Blocker::check_parameterised` looks for exceptions only when a blocking rule matched, so `@@||metrics.a.test^` alone leaves `BlockerResult::exception` empty. The bridge asks `check_network_request_subset(request, previously_matched_rule = true, …)`, which skips the blocking rules and reports an exception. That happens inside `check_aliases`, so `Decision` gains no `exception` field. A same-site request at Standard has no URL check, so its host is resolved before the exception is known. That's harmless: the request was going to resolve it anyway.
- **net gives the aliases as a set.** `HostResolverManager::RequestImpl` keeps `std::set<std::string>` (`FixUpDnsAliases`: valid names, canonicalized, no IP literals) and `EndpointResultToAddressList` copies it into the vector in sorted order. So there is no chain order. `AliasesToCheck` sorts, and the order decides only which rule is logged when two aliases are listed.
- **`direct_only`** is honored in `NetworkContext::ResolveHost` by `ProxyCheckingHostResolverRequest`, which resolves the proxy first and answers `ERR_DNS_DIRECT_ONLY` when the URL isn't direct. With WPAD or a PAC script, this means a proxy resolution for each request. The spike measures it.
- **Destroying the client cancels.** `ResolveHostRequest` cancels on the client pipe's disconnect. `AliasResolver` also calls `ResolveHostHandle::Cancel` when destroyed before an answer.
- **The budget's clock.** `InFlight` logs `Uncloak <host>: <µs> us` at `VLOG(2)` (`--vmodule=request_filter=2`): the time from the resolution's start to the aliases' verdict. `tools/uncloak_cost.py` reads those lines from `chrome_debug.log`. The spec's trace event is replaced by this log line, which is simpler and works the same in an official build.

**Conventions** (as in the earlier sub-projects):
- **Edit in webops, build in `chromium/src`.** `python <scratchpad>\sync.py` copies changed files into `src/ghost` with new mtimes. Commit in webops; then `git -C chromium/src/ghost checkout -- .` and `git -C chromium/src/ghost pull`, and compare `src/ghost` with webops.
- **Build and test commands:**
  - Build: `powershell -File <scratchpad>\build.ps1 -Targets "<targets>" [-Out vanilla] [-Log build.log]`.
  - Unit tests: `chromium\src\out\vanilla\ghost_unittests.exe --gtest_filter=...`.
  - Browser tests run **only on the hidden desktop**: `powershell -File <scratchpad>\bt.ps1 -Filter "<filter>" [-Log bt.log]`. It prints the results.
- **Every new file starts with the MPL header** (`tools/lint.py` checks `.cc`, `.h`, `.rs`, `.gn`, `.py`). Code below leaves it out.
- **Stage before `python tools\lint.py`,** because lint sees only tracked files. Never chain `git commit` after a test run in one command. Commit messages go through a file (`git commit -F`), never piped from PowerShell.
- **Commit messages carry no AI trailer;** the user is the only author.

---

### Task 0: Tools and state

- [ ] **Step 1:** Check the scratchpad has `hidden.ps1`, `sync.py`, `build.ps1` and `bt.ps1`. They were copied from `C:\Users\robyv\AppData\Local\Temp\claude\C--Users-robyv-Desktop-DLU-webops\28b6f2a8-6de8-4b64-9c7f-992c45957cbd\scratchpad\` while planning. If they're missing, copy them again from there.
- [ ] **Step 2:** Check the state:
  - `git -C chromium/src branch --show-current` prints `ghost/152.0.7977.158`;
  - `python tools\patches.py check` passes;
  - `git -C chromium/src/ghost log --oneline -1` matches webops' `main`;
  - `build.ps1 -Targets "ghost_unittests ghost_browsertests"` has no work, or only a little.

---

### Task 1: The engine checks aliases

**Files:**
- Modify: `components/blocking/rust/lib.rs`
- Modify: `components/blocking/blocking_engine.h`, `components/blocking/blocking_engine.cc`
- Test: `components/blocking/engine_bridge_unittest.cc`, `components/blocking/blocking_engine_unittest.cc`

- [ ] **Step 1: The bridge's failing tests.** Append to `engine_bridge_unittest.cc`, before the closing namespaces:

```cpp
// Phase 4C: a request's DNS aliases, as URLs with the alias as host.

rust::Vec<rust::String> Urls(std::initializer_list<const char*> urls) {
  rust::Vec<rust::String> result;
  for (const char* url : urls) {
    result.push_back(rust::String(url));
  }
  return result;
}

AliasVerdict CheckAliases(const Engine& engine,
                          std::initializer_list<const char*> alias_urls,
                          const char* source = "https://a.test/") {
  return engine.check_aliases("https://metrics.a.test/m.js", source, "script", "GET",
                              Urls(alias_urls));
}

TEST(EngineBridgeTest, AListedAliasBlocksTheRequest) {
  rust::Box<Engine> engine = new_engine("||tracker.test^\n");
  const AliasVerdict verdict =
      CheckAliases(*engine, {"https://cdn.test/m.js", "https://a.tracker.test/m.js"});
  EXPECT_TRUE(verdict.blocked);
  EXPECT_EQ(std::string(verdict.alias_url), "https://a.tracker.test/m.js");
  EXPECT_FALSE(verdict.filter.empty());
}

TEST(EngineBridgeTest, NoListedAliasBlocksNothing) {
  rust::Box<Engine> engine = new_engine("||tracker.test^\n");
  const AliasVerdict verdict = CheckAliases(*engine, {"https://cdn.test/m.js"});
  EXPECT_FALSE(verdict.blocked);
  EXPECT_TRUE(verdict.alias_url.empty());
  EXPECT_FALSE(CheckAliases(*engine, {}).blocked);
}

TEST(EngineBridgeTest, AnExceptionForTheRequestLeavesItsAliasesAlone) {
  // No blocking rule names metrics.a.test: the exception must still count.
  rust::Box<Engine> engine = new_engine("||tracker.test^\n@@||metrics.a.test^\n");
  EXPECT_FALSE(CheckAliases(*engine, {"https://a.tracker.test/m.js"}).blocked);
}

TEST(EngineBridgeTest, AnAliasIsJudgedAgainstThePage) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$third-party\n");
  EXPECT_TRUE(CheckAliases(*engine, {"https://a.tracker.test/m.js"}).blocked);
  EXPECT_FALSE(CheckAliases(*engine, {"https://a.tracker.test/m.js"},
                            "https://www.tracker.test/")
                   .blocked);
}

TEST(EngineBridgeTest, AnAliasCanBeRedirected) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$script,redirect=noopjs\n");
  const AliasVerdict verdict = CheckAliases(*engine, {"https://a.tracker.test/m.js"});
  EXPECT_TRUE(verdict.blocked);
  EXPECT_TRUE(std::string(verdict.redirect).starts_with("data:application/javascript;base64,"));
}
```

- [ ] **Step 2: Build and watch it fail.** `build.ps1 -Targets ghost_unittests`. Expected: compile errors because `AliasVerdict` and `check_aliases` don't exist.

- [ ] **Step 3: The bridge.** In `lib.rs`, inside `mod ffi`, after `struct Verdict { … }`:

```rust
    /// The verdict on a request's DNS aliases (Phase 4C).
    struct AliasVerdict {
        blocked: bool,
        filter: String,
        /// As in Verdict.
        redirect: String,
        /// The alias URL that was blocked; empty otherwise.
        alias_url: String,
    }
```

After the `fn check(…) -> Verdict;` declaration in `extern "Rust"`:

```rust
        /// The verdict on a request's DNS aliases: `alias_urls` are its URL
        /// with each alias as host. Nothing is blocked when an exception
        /// matches `url` itself; otherwise the first alias URL a rule blocks
        /// decides. Party is judged against `source_url`, as for any request.
        fn check_aliases(
            self: &Engine,
            url: &str,
            source_url: &str,
            request_type: &str,
            method: &str,
            alias_urls: &Vec<String>,
        ) -> AliasVerdict;
```

In `impl Engine`, after `fn check`:

```rust
    fn check_aliases(
        &self,
        url: &str,
        source_url: &str,
        request_type: &str,
        method: &str,
        alias_urls: &Vec<String>,
    ) -> ffi::AliasVerdict {
        let allowed = ffi::AliasVerdict {
            blocked: false,
            filter: String::new(),
            redirect: String::new(),
            alias_url: String::new(),
        };
        let Ok(request) =
            Request::new(url, source_url, request_type, &method.to_ascii_lowercase())
        else {
            return allowed;
        };
        // adblock-rust looks for exceptions only once a blocking rule has
        // matched: say one has, so an exception for the request's own URL
        // shows even when no rule names it.
        if self.0.check_network_request_subset(&request, true, false).exception.is_some() {
            return allowed;
        }
        for alias_url in alias_urls {
            let verdict = self.check(alias_url, source_url, request_type, method);
            if verdict.blocked {
                return ffi::AliasVerdict {
                    blocked: true,
                    filter: verdict.filter,
                    redirect: verdict.redirect,
                    alias_url: alias_url.clone(),
                };
            }
        }
        allowed
    }
```

- [ ] **Step 4: Run the bridge tests.** Build `ghost_unittests`, then `ghost_unittests.exe --gtest_filter=EngineBridgeTest.*`. Expected: all pass, the five new tests included.

- [ ] **Step 5: The engine's failing tests.** Append to `blocking_engine_unittest.cc`, before the closing namespaces:

```cpp
// Phase 4C.
Decision CheckAliases(BlockingEngine& engine, std::vector<GURL> alias_urls) {
  base::test::TestFuture<Decision> decision;
  engine.CheckAliases({GURL("https://metrics.a.test/m.js"), GURL("https://a.test/"),
                       network::mojom::RequestDestination::kScript, "GET"},
                      std::move(alias_urls), decision.GetCallback());
  return decision.Take();
}

TEST_F(BlockingEngineTest, AListedAliasBlocksAndIsNamed) {
  BlockingEngine engine;
  engine.Load({"||tracker.test^\n"});
  const Decision decision = CheckAliases(engine, {GURL("https://a.tracker.test/m.js")});
  EXPECT_TRUE(decision.blocked);
  EXPECT_EQ(decision.alias, "https://a.tracker.test/m.js");
}

TEST_F(BlockingEngineTest, AliasChecksBeforeLoadingAreAnsweredAfterIt) {
  BlockingEngine engine;
  base::test::TestFuture<Decision> decision;
  engine.CheckAliases({GURL("https://metrics.a.test/m.js"), GURL("https://a.test/"),
                       network::mojom::RequestDestination::kScript, "GET"},
                      {GURL("https://a.tracker.test/m.js")}, decision.GetCallback());
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(decision.IsReady()) << "answered before the lists loaded";
  engine.Load({"||tracker.test^\n"});
  EXPECT_TRUE(decision.Get().blocked);
}

TEST_F(BlockingEngineTest, AFailedEngineBlocksNoAlias) {
  BlockingEngine engine;
  engine.Load({});
  EXPECT_FALSE(CheckAliases(engine, {GURL("https://a.tracker.test/m.js")}).blocked);
}

TEST_F(BlockingEngineTest, AnInvalidAliasUrlIsLeftOut) {
  BlockingEngine engine;
  engine.Load({"||tracker.test^\n"});
  EXPECT_FALSE(CheckAliases(engine, {GURL("not a url")}).blocked);
}
```

- [ ] **Step 6: Build and watch it fail.** Expected: `CheckAliases` and `Decision::alias` don't exist.

- [ ] **Step 7: `BlockingEngine`.** In `blocking_engine.h`, add to `Decision`, after `redirect`:

```cpp
  // When one of the request's DNS aliases was blocked (Phase 4C): its URL,
  // for the console. Empty otherwise.
  std::string alias;
```

In `class BlockingEngine`, after `Check`:

```cpp
  // The verdict on a request's DNS aliases (Phase 4C). `alias_urls` are its
  // URL with each alias as host (dns_aliases.h). Not blocked when an
  // exception matches the request's own URL; otherwise the first alias URL a
  // rule blocks decides. Waits while the engine loads, as Check does.
  void CheckAliases(CheckRequest request,
                    std::vector<GURL> alias_urls,
                    base::OnceCallback<void(Decision)> reply);
```

In `blocking_engine.cc`, in `class BlockingEngine::Core`'s public section, after `Check`:

```cpp
  void CheckAliases(CheckRequest request,
                    std::vector<GURL> alias_urls,
                    base::OnceCallback<void(Decision)> reply) {
    WhenLoaded(base::BindOnce(&Core::AnswerAliases, base::Unretained(this), std::move(request),
                              std::move(alias_urls), std::move(reply)));
  }
```

After `Answer`:

```cpp
  void AnswerAliases(const CheckRequest& request,
                     const std::vector<GURL>& alias_urls,
                     base::OnceCallback<void(Decision)> reply) {
    Decision decision;
    // As in Answer: a method that isn't UTF-8 mustn't reach the bridge.
    if (!base::IsStringUTF8AllowingNoncharacters(request.method)) {
      decision.blocked = true;
    } else if (state_ == State::kReady && request.url.is_valid()) {
      rust::Vec<rust::String> urls;
      for (const GURL& url : alias_urls) {
        if (url.is_valid()) {
          urls.push_back(rust::String(url.spec()));
        }
      }
      const std::string& page =
          request.source_url.is_valid() ? request.source_url.spec() : base::EmptyString();
      AliasVerdict verdict = (*engine_)->check_aliases(
          request.url.spec(), page, ToAdblockType(request.destination), request.method, urls);
      decision.blocked = verdict.blocked;
      decision.filter = std::string(verdict.filter);
      decision.redirect = std::string(verdict.redirect);
      decision.alias = std::string(verdict.alias_url);
    }
    std::move(reply).Run(std::move(decision));
  }
```

After `BlockingEngine::Check`:

```cpp
void BlockingEngine::CheckAliases(CheckRequest request,
                                  std::vector<GURL> alias_urls,
                                  base::OnceCallback<void(Decision)> reply) {
  core_.AsyncCall(&Core::CheckAliases)
      .WithArgs(std::move(request), std::move(alias_urls),
                base::BindPostTaskToCurrentDefault(std::move(reply)));
}
```

- [ ] **Step 8: Run.** `ghost_unittests.exe --gtest_filter=EngineBridgeTest.*:BlockingEngineTest.*`. Expected: all pass.

- [ ] **Step 9: Commit.**

```bash
git add components/blocking
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "blocking: the engine judges a request's DNS aliases"
```

---

### Task 2: Which aliases, and their URLs

**Files:**
- Create: `components/blocking/dns_aliases.h`, `components/blocking/dns_aliases.cc`, `components/blocking/dns_aliases_unittest.cc`
- Modify: `components/blocking/BUILD.gn`

- [ ] **Step 1: The failing tests.** `dns_aliases_unittest.cc`:

```cpp
#include "ghost/components/blocking/dns_aliases.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace ghost::blocking {
namespace {

using testing::ElementsAre;
using testing::IsEmpty;

TEST(DnsAliasesTest, NoAliasesNothingToCheck) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {}), IsEmpty());
}

TEST(DnsAliasesTest, TheHostIsNotItsOwnAlias) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"metrics.a.test"}), IsEmpty());
}

TEST(DnsAliasesTest, AnAliasOnTheSameSiteIsNotCloaking) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"edge.a.test", "a.test"}), IsEmpty());
}

TEST(DnsAliasesTest, AnAliasOnAnotherSiteIsChecked) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"metrics.a.test", "a.tracker.test"}),
              ElementsAre("a.tracker.test"));
}

TEST(DnsAliasesTest, AddressesAndNamesThatArentHostsAreSkipped) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test",
                             {"192.0.2.1", "[2001:db8::1]", "2001:db8::1", "", "bad host.test",
                              "UPPER.tracker.test"}),
              IsEmpty());
}

TEST(DnsAliasesTest, EachOnceAndSorted) {
  EXPECT_THAT(AliasesToCheck("metrics.a.test",
                             {"b.tracker.test", "a.tracker.test", "b.tracker.test"}),
              ElementsAre("a.tracker.test", "b.tracker.test"));
}

TEST(DnsAliasesTest, ATrailingDotIsDropped) {
  // A rule's `^` doesn't match a dot: "a.tracker.test." would escape ||tracker.test^.
  EXPECT_THAT(AliasesToCheck("metrics.a.test", {"a.tracker.test."}),
              ElementsAre("a.tracker.test"));
  EXPECT_THAT(AliasesToCheck("metrics.a.test.", {"metrics.a.test"}), IsEmpty());
}

TEST(DnsAliasesTest, WithAliasKeepsEverythingButTheHost) {
  EXPECT_EQ(WithAlias(GURL("https://metrics.a.test:8443/p/m.js?q=1#f"), "a.tracker.test"),
            GURL("https://a.tracker.test:8443/p/m.js?q=1#f"));
}

TEST(DnsAliasesTest, WithAnAliasThatIsntAHostIsInvalid) {
  EXPECT_FALSE(WithAlias(GURL("https://metrics.a.test/m.js"), "bad host").is_valid());
  EXPECT_FALSE(WithAlias(GURL("https://metrics.a.test/m.js"), "").is_valid());
}

}  // namespace
}  // namespace ghost::blocking
```

In `components/blocking/BUILD.gn`, add `"dns_aliases_unittest.cc"` to `unit_tests`' sources.

- [ ] **Step 2: Build and watch it fail.** Expected: `dns_aliases.h` not found.

- [ ] **Step 3: The functions.** `dns_aliases.h`:

```cpp
#ifndef GHOST_COMPONENTS_BLOCKING_DNS_ALIASES_H_
#define GHOST_COMPONENTS_BLOCKING_DNS_ALIASES_H_

#include <string>
#include <string_view>
#include <vector>

#include "url/gurl.h"

namespace ghost::blocking {

// CNAME uncloaking (Phase 4C): a request's host may be an alias of a
// tracker's. Of the host's DNS aliases (net's: canonical names, in no
// particular order), the ones worth checking with the lists: not the host
// itself, not an IP address or a name that isn't a canonical host, not one
// on the host's own site (a.site.com to b.site.com isn't cloaking), without
// a trailing dot, each once, sorted.
std::vector<std::string> AliasesToCheck(std::string_view host,
                                        const std::vector<std::string>& dns_aliases);

// `url` with `alias` as its host, the rest kept; an invalid GURL when
// `alias` isn't a canonical host.
GURL WithAlias(const GURL& url, std::string_view alias);

}  // namespace ghost::blocking

#endif  // GHOST_COMPONENTS_BLOCKING_DNS_ALIASES_H_
```

`dns_aliases.cc`:

```cpp
#include "ghost/components/blocking/dns_aliases.h"

#include <set>

#include "base/strings/strcat.h"
#include "ghost/components/site/registrable_domain.h"

namespace ghost::blocking {

namespace {

std::string_view WithoutTrailingDot(std::string_view host) {
  return host.ends_with('.') ? host.substr(0, host.size() - 1) : host;
}

// A canonical host name: what a URL makes of it is itself, and it isn't an
// address.
bool IsCanonicalName(std::string_view alias) {
  if (alias.empty()) {
    return false;
  }
  const GURL probe(base::StrCat({"https://", alias, "/"}));
  return probe.is_valid() && probe.host() == alias && !probe.HostIsIPAddress();
}

}  // namespace

std::vector<std::string> AliasesToCheck(std::string_view host,
                                        const std::vector<std::string>& dns_aliases) {
  host = WithoutTrailingDot(host);
  const std::string_view site = RegistrableDomain(host);
  std::set<std::string> kept;
  for (const std::string& dns_alias : dns_aliases) {
    const std::string_view alias = WithoutTrailingDot(dns_alias);
    if (alias == host || !IsCanonicalName(alias) || RegistrableDomain(alias) == site) {
      continue;
    }
    kept.emplace(alias);
  }
  return std::vector<std::string>(kept.begin(), kept.end());
}

GURL WithAlias(const GURL& url, std::string_view alias) {
  if (!IsCanonicalName(alias)) {
    return GURL();
  }
  GURL::Replacements replacements;
  replacements.SetHostStr(alias);
  GURL result = url.ReplaceComponents(replacements);
  return result.is_valid() && result.host() == alias ? result : GURL();
}

}  // namespace ghost::blocking
```

In `components/blocking/BUILD.gn`, add `"dns_aliases.cc"` and `"dns_aliases.h"` to `source_set("blocking")`'s sources, and add `deps += [ "//ghost/components/site" ]` (the source set already has `deps = [ … ]`, so add the item there).

- [ ] **Step 4: Run.** `ghost_unittests.exe --gtest_filter=DnsAliasesTest.*`. Expected: 9 pass.

- [ ] **Step 5: Commit.**

```bash
git add components/blocking
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "blocking: which DNS aliases to check, and their URLs"
```

---

### Task 3: The alias resolver

**Files:**
- Create: `browser/blocking/alias_resolver.h`, `browser/blocking/alias_resolver.cc`
- Modify: `browser/blocking/BUILD.gn`

It is tested through the browser tests of Task 4; it has no logic of its own to unit-test beyond the mojo call.

- [ ] **Step 1: The header.** `alias_resolver.h`:

```cpp
#ifndef GHOST_BROWSER_BLOCKING_ALIAS_RESOLVER_H_
#define GHOST_BROWSER_BLOCKING_ALIAS_RESOLVER_H_

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/request_priority.h"
#include "net/dns/public/secure_dns_policy.h"
#include "services/network/public/cpp/resolve_host_client_base.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "url/gurl.h"

namespace network::mojom {
class NetworkContext;
}

namespace ghost::blocking {

// One host's DNS aliases, from the network service's host resolver (CNAME
// uncloaking, Phase 4C). It resolves with the request's key and secure DNS
// policy, so the request's own resolution finds the answer in the cache. It
// asks only for a direct connection: when a proxy would resolve the host, the
// answer is ERR_DNS_DIRECT_ONLY, with no aliases and no local query.
// Destroying it cancels the resolution.
class AliasResolver : public network::ResolveHostClientBase {
 public:
  // Runs once, with no aliases on any error; never after destruction.
  using Callback = base::OnceCallback<void(std::vector<std::string> aliases)>;
  // Called with each resolution's host when it starts (net::ERR_IO_PENDING),
  // and again with its result unless it was cancelled first.
  using Observer = base::RepeatingCallback<void(const std::string& host, int result)>;

  // Starts at once.
  AliasResolver(network::mojom::NetworkContext* network_context,
                const GURL& url,
                const net::NetworkAnonymizationKey& key,
                net::SecureDnsPolicy secure_dns_policy,
                net::RequestPriority priority,
                Callback callback);
  AliasResolver(const AliasResolver&) = delete;
  AliasResolver& operator=(const AliasResolver&) = delete;
  ~AliasResolver() override;

  static void SetObserverForTesting(Observer observer);

  // network::mojom::ResolveHostClient:
  void OnComplete(int result,
                  const net::ResolveErrorInfo& resolve_error_info,
                  const net::AddressList& resolved_addresses,
                  const net::HostResolverEndpointResults& alternative_endpoints) override;

 private:
  void Finish(int result, std::vector<std::string> aliases);

  const std::string host_;
  Callback callback_;
  mojo::Receiver<network::mojom::ResolveHostClient> receiver_{this};
  mojo::Remote<network::mojom::ResolveHostHandle> handle_;
};

}  // namespace ghost::blocking

#endif  // GHOST_BROWSER_BLOCKING_ALIAS_RESOLVER_H_
```

- [ ] **Step 2: The implementation.** `alias_resolver.cc`:

```cpp
#include "ghost/browser/blocking/alias_resolver.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "url/scheme_host_port.h"

namespace ghost::blocking {

namespace {

AliasResolver::Observer& TestObserver() {
  static base::NoDestructor<AliasResolver::Observer> observer;
  return *observer;
}

}  // namespace

AliasResolver::AliasResolver(network::mojom::NetworkContext* network_context,
                             const GURL& url,
                             const net::NetworkAnonymizationKey& key,
                             net::SecureDnsPolicy secure_dns_policy,
                             net::RequestPriority priority,
                             Callback callback)
    : host_(url.host()), callback_(std::move(callback)) {
  auto parameters = network::mojom::ResolveHostParameters::New();
  parameters->initial_priority = priority;
  parameters->secure_dns_policy = secure_dns_policy;
  // Never a local query for a host a proxy resolves.
  parameters->direct_only = true;
  parameters->control_handle = handle_.BindNewPipeAndPassReceiver();
  // The request's scheme, host and port, as its own resolution asks.
  network_context->ResolveHost(
      network::mojom::HostResolverHost::NewSchemeHostPort(url::SchemeHostPort(url)), key,
      std::move(parameters), receiver_.BindNewPipeAndPassRemote());
  // At the start: a query asked and then cancelled was still asked.
  if (!TestObserver().is_null()) {
    TestObserver().Run(host_, net::ERR_IO_PENDING);
  }
  // The network service went away: no answer.
  receiver_.set_disconnect_handler(base::BindOnce(&AliasResolver::Finish, base::Unretained(this),
                                                  net::ERR_FAILED, std::vector<std::string>()));
}

AliasResolver::~AliasResolver() {
  if (callback_ && handle_.is_bound()) {
    handle_->Cancel(net::ERR_ABORTED);
  }
}

// static
void AliasResolver::SetObserverForTesting(Observer observer) {
  TestObserver() = std::move(observer);
}

void AliasResolver::OnComplete(int result,
                               const net::ResolveErrorInfo& resolve_error_info,
                               const net::AddressList& resolved_addresses,
                               const net::HostResolverEndpointResults& alternative_endpoints) {
  Finish(result,
         result == net::OK ? resolved_addresses.dns_aliases() : std::vector<std::string>());
}

void AliasResolver::Finish(int result, std::vector<std::string> aliases) {
  if (!callback_) {
    return;
  }
  receiver_.reset();
  handle_.reset();
  if (!TestObserver().is_null()) {
    TestObserver().Run(host_, result);
  }
  // Last: the callback may destroy this.
  std::move(callback_).Run(std::move(aliases));
}

}  // namespace ghost::blocking
```

- [ ] **Step 3: Build files.** In `browser/blocking/BUILD.gn`, add `"alias_resolver.cc"` and `"alias_resolver.h"` to the sources. `//net`, `//url`, `//services/network/public/cpp` and `//services/network/public/mojom` are already among the deps.

- [ ] **Step 4: Build.** `build.ps1 -Targets ghost_browsertests`. Expected: builds, since nothing calls it yet.

- [ ] **Step 5: Commit.**

```bash
git add browser/blocking
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "blocking: resolve a request's host for its DNS aliases"
```

---

### Task 4: The request filter uncloaks (spike point 2)

**Files:**
- Modify: `browser/cosmetic_filters/cosmetic_filters_test_base.h`, `.cc` (a host-rule hook, redirects)
- Create: `browser/blocking/cname_uncloaking_browsertest.cc`
- Modify: `BUILD.gn` (the browser test's source)
- Modify: `browser/blocking/request_filter.h`, `browser/blocking/request_filter.cc`

- [ ] **Step 1: The test base.** In `cosmetic_filters_test_base.h`, after `AddPages()`:

```cpp
  // Called before the catch-all rule that sends every host to the servers:
  // host-resolver rules of the test's own (DNS aliases, failures) go here.
  virtual void AddHostRules() {}
```

After `AddResource(…)`:

```cpp
  // A 302 from `path` to `to_path` on `to_host`, on the HTTP server.
  void AddRedirect(std::string path, std::string to_host, std::string to_path);
```

Add to `struct Resource`:

```cpp
    // A redirect's target, when set (AddRedirect).
    std::string redirect_host;
    std::string redirect_path;
```

In `cosmetic_filters_test_base.cc`:

```cpp
void CosmeticFiltersTestBase::AddRedirect(std::string path,
                                          std::string to_host,
                                          std::string to_path) {
  Resource& resource = resources_[std::move(path)];
  resource.redirect_host = std::move(to_host);
  resource.redirect_path = std::move(to_path);
}
```

In `SetUpOnMainThread`, call `AddHostRules();` between `AddPages();` and `host_resolver()->AddRule("*", "127.0.0.1");`. In `Serve`, right after `found` is checked:

```cpp
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
```

- [ ] **Step 2: The failing browser tests.** `browser/blocking/cname_uncloaking_browsertest.cc`:

```cpp
// CNAME uncloaking (Phase 4C): the request filter resolves a request's host
// and blocks it when one of its DNS aliases is listed, before it leaves.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/blocking/alias_resolver.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "ghost/browser/protections/page_protections.h"
#include "ghost/components/privacy_policy/protection_level.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"

namespace ghost::blocking {
namespace {

class CnameUncloakingBrowserTest : public cosmetic_filters::CosmeticFiltersTestBase {
 protected:
  // `^` matches the port the test server's URLs carry.
  std::string List() const override {
    return "||tracker.test^\n"
           "@@||allowed.a.test^\n"
           "||edge.a.test^\n"
           "||blocked.test^\n"
           "||widgets.test^$script,redirect=noopjs\n";
  }

  void AddHostRules() override {
    // The rules match by host name; each host has one alias unless the test
    // says otherwise.
    host_resolver()->AddIPLiteralRuleWithDnsAliases("metrics.a.test", "127.0.0.1",
                                                    {"a.tracker.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("stats.b.test", "127.0.0.1",
                                                    {"b.tracker.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("allowed.a.test", "127.0.0.1",
                                                    {"c.tracker.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("cdn.a.test", "127.0.0.1",
                                                    {"edge.a.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("widget.a.test", "127.0.0.1",
                                                    {"cdn.widgets.test"});
    host_resolver()->AddIPLiteralRuleWithDnsAliases("chain.a.test", "127.0.0.1",
                                                    {"edge.cdn.test", "d.tracker.test"});
    host_resolver()->AddSimulatedFailure("gone.a.test");
  }

  void AddPages() override {
    AddPage("/page.html", "<p>page</p>");
    for (const char* path : {"/metrics.js", "/stats.js", "/allowed.js", "/edge.js", "/hop-target.js",
                             "/blocked.js", "/widget.js", "/chain.js", "/gone.js", "/proxy.js"}) {
      AddResource(path, "application/javascript", "window.loaded = true;");
    }
    AddRedirect("/hop.js", "metrics.a.test", "/hop-target.js");
  }

  void SetUpOnMainThread() override {
    AliasResolver::SetObserverForTesting(base::BindRepeating(
        &CnameUncloakingBrowserTest::Resolved, base::Unretained(this)));
    cosmetic_filters::CosmeticFiltersTestBase::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    AliasResolver::SetObserverForTesting({});
    cosmetic_filters::CosmeticFiltersTestBase::TearDownOnMainThread();
  }

  // Loads a script from `host` on the page's port: "loaded" or "error".
  std::string LoadScript(std::string_view host, std::string_view path) {
    return content::EvalJs(Tab(), content::JsReplace(R"(new Promise(done => {
               const script = document.createElement('script');
               script.src = `http://${$1}:${location.port}${$2}`;
               script.onload = () => done('loaded');
               script.onerror = () => done('error');
               document.head.append(script);
             }))",
                                                     host, path))
        .ExtractString();
  }

  void OpenPage() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/page.html")));
  }

  int Blocked() { return protections::PageProtections::FromWebContents(Tab())->blocked_count(); }

  // The hosts the filter resolved, with each result.
  std::vector<std::pair<std::string, int>> Resolutions() {
    base::AutoLock lock(lock_);
    return resolutions_;
  }

  bool WasResolved(std::string_view host) {
    for (const auto& [resolved, result] : Resolutions()) {
      if (resolved == host) {
        return true;
      }
    }
    return false;
  }

 private:
  void Resolved(const std::string& host, int result) {
    base::AutoLock lock(lock_);
    resolutions_.emplace_back(host, result);
  }

  base::Lock lock_;
  std::vector<std::pair<std::string, int>> resolutions_ GUARDED_BY(lock_);
};

// 1. Standard checks no same-site URL, but checks its host's aliases.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ASameSiteCloakedRequestNeverLeaves) {
  OpenPage();
  EXPECT_EQ(LoadScript("metrics.a.test", "/metrics.js"), "error");
  EXPECT_FALSE(LastRequestHeaders("/metrics.js")) << "the request reached the server";
  EXPECT_EQ(Blocked(), 1);
}

// 2.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ACrossSiteCloakedRequestIsBlocked) {
  OpenPage();
  EXPECT_EQ(LoadScript("stats.b.test", "/stats.js"), "error");
  EXPECT_FALSE(LastRequestHeaders("/stats.js"));
}

// 3.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, AnExceptionForTheHostKeepsItsAliasesUnchecked) {
  OpenPage();
  EXPECT_EQ(LoadScript("allowed.a.test", "/allowed.js"), "loaded");
}

// 4.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, AnAliasOnTheSameSiteIsNotChecked) {
  OpenPage();
  EXPECT_EQ(LoadScript("cdn.a.test", "/edge.js"), "loaded");
}

// 5.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, OffResolvesNothing) {
  privacy_policy::SetLevel(browser()->profile(), Url("a.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  OpenPage();
  EXPECT_EQ(LoadScript("metrics.a.test", "/metrics.js"), "loaded");
  EXPECT_TRUE(Resolutions().empty());
}

// 6.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ARedirectToACloakedHostIsBlocked) {
  OpenPage();
  EXPECT_EQ(LoadScript("hop.a.test", "/hop.js"), "error");
  EXPECT_TRUE(LastRequestHeaders("/hop.js"));
  EXPECT_FALSE(LastRequestHeaders("/hop-target.js")) << "the redirect was followed";
}

// 7. A blocked tracker's name server hears nothing from Shade.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ABlockedUrlsHostIsNeverResolved) {
  OpenPage();
  EXPECT_EQ(LoadScript("blocked.test", "/blocked.js"), "error");
  EXPECT_EQ(LoadScript("metrics.a.test", "/metrics.js"), "error");
  EXPECT_FALSE(WasResolved("blocked.test"));
  EXPECT_TRUE(WasResolved("metrics.a.test")) << "the observer saw nothing at all";
}

// 9.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, ARedirectRuleOnTheAliasAnswers) {
  OpenPage();
  EXPECT_EQ(LoadScript("widget.a.test", "/widget.js"), "loaded");
  EXPECT_FALSE(LastRequestHeaders("/widget.js")) << "the request reached the server";
  EXPECT_EQ(content::EvalJs(Tab(), "String(window.loaded)"), "undefined");
  EXPECT_EQ(Blocked(), 1);
}

// 10.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, AFailedResolutionLetsTheRequestGo) {
  OpenPage();
  EXPECT_EQ(LoadScript("gone.a.test", "/gone.js"), "error");
  EXPECT_TRUE(WasResolved("gone.a.test"));
  EXPECT_EQ(Blocked(), 0);
}

// Spike point 2: a chain of more than one alias reaches the filter.
IN_PROC_BROWSER_TEST_F(CnameUncloakingBrowserTest, EveryAliasIsChecked) {
  OpenPage();
  EXPECT_EQ(LoadScript("chain.a.test", "/chain.js"), "error");
  EXPECT_FALSE(LastRequestHeaders("/chain.js"));
}

// 8. Through a proxy, the filter makes no local DNS query: the page itself is
// direct (bypassed), its subresource host isn't.
class CnameUncloakingProxyBrowserTest : public CnameUncloakingBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    CnameUncloakingBrowserTest::SetUpCommandLine(command_line);
    // Nothing listens on port 1: a proxied request fails to connect.
    command_line->AppendSwitchASCII("proxy-server", "127.0.0.1:1");
    command_line->AppendSwitchASCII("proxy-bypass-list", "a.test");
  }
};

IN_PROC_BROWSER_TEST_F(CnameUncloakingProxyBrowserTest, NothingIsResolvedBesideAProxy) {
  OpenPage();
  EXPECT_EQ(LoadScript("metrics.a.test", "/proxy.js"), "error");
  // Started, then answered without a query.
  const std::vector<std::pair<std::string, int>> resolutions = Resolutions();
  ASSERT_EQ(resolutions.size(), 2u);
  EXPECT_EQ(resolutions[1], std::make_pair(std::string("metrics.a.test"),
                                            static_cast<int>(net::ERR_DNS_DIRECT_ONLY)));
  EXPECT_EQ(Blocked(), 0);
}

}  // namespace
}  // namespace ghost::blocking
```

In the root `BUILD.gn`, add `"browser/blocking/cname_uncloaking_browsertest.cc",` to `ghost_browsertests`' sources, after `connection_filter_browsertest.cc`.

- [ ] **Step 3: Build and run; watch them fail.** `build.ps1 -Targets ghost_browsertests`, then `bt.ps1 -Filter "CnameUncloaking*"`. Expected:
  - fail: 1, 2, 6, 7 (no resolution), 9, 10 (`WasResolved`), `EveryAliasIsChecked`, and the proxy test;
  - pass already: 3, 4, 5 (nothing uncloaks yet).
  - If `EveryAliasIsChecked` crashes on `CHECK_EQ(address_list.dns_aliases().size(), 1u)` in `host_resolver_system_task.cc`, the system path can't carry two aliases in a test. Then remove that test and record it in the progress notes (spike point 2): the built-in resolver still gives every alias in practice.

- [ ] **Step 4: The filter's context.** In `request_filter.h`, add includes `"net/base/network_anonymization_key.h"` and `"services/network/public/mojom/network_context.mojom-forward.h"`. Before `class RequestFilter`:

```cpp
// What uncloaking a request's host needs from its factory (Phase 4C).
struct UncloakContext {
  UncloakContext();
  UncloakContext(const UncloakContext&);
  UncloakContext& operator=(const UncloakContext&);
  ~UncloakContext();

  // The network context the factory's requests go through; null once the
  // profile is gone.
  base::RepeatingCallback<network::mojom::NetworkContext*()> network_context;
  // The factory's key, for requests that don't carry their own.
  net::NetworkAnonymizationKey network_anonymization_key;
};
```

The `RequestFilter` constructor takes `UncloakContext uncloak` after `PolicyCallback policy`. Add the member `const UncloakContext uncloak_;` after `policy_`, and update the class comment: "… asks the engine about each request that needs a verdict, and about its host's DNS aliases (Phase 4C), holds it until the answers, …".

- [ ] **Step 5: The factory.** In `request_filter.cc`, add includes `"content/public/browser/storage_partition.h"`, `"ghost/browser/blocking/alias_resolver.h"`, `"ghost/components/blocking/dns_aliases.h"`, `"net/base/url_util.h"` and `"services/network/public/mojom/network_context.mojom.h"`. In the anonymous namespace:

```cpp
// The network context of the frame's storage partition; without a frame (a
// shared or service worker), the profile's default one. Null once the
// profile is gone.
network::mojom::NetworkContext* NetworkContextOf(base::WeakPtr<content::BrowserContext> context,
                                                 content::GlobalRenderFrameHostId frame) {
  if (content::RenderFrameHost* host = content::RenderFrameHost::FromID(frame)) {
    return host->GetStoragePartition()->GetNetworkContext();
  }
  return context ? context->GetDefaultStoragePartition()->GetNetworkContext() : nullptr;
}
```

After `RequestFilter::~RequestFilter() = default;`, define the struct's members:

```cpp
UncloakContext::UncloakContext() = default;
UncloakContext::UncloakContext(const UncloakContext&) = default;
UncloakContext& UncloakContext::operator=(const UncloakContext&) = default;
UncloakContext::~UncloakContext() = default;
```

In `MaybeProxyURLLoaderFactory`, build it and pass it:

```cpp
  const content::GlobalRenderFrameHostId frame_id =
      frame ? frame->GetGlobalId() : content::GlobalRenderFrameHostId();
  UncloakContext uncloak;
  uncloak.network_context =
      base::BindRepeating(&NetworkContextOf, context->GetWeakPtr(), frame_id);
  uncloak.network_anonymization_key = isolation_info.network_anonymization_key();
  auto [receiver, target] = factory_builder.Append();
  base::MakeSelfDeleting<RequestFilter>(
      std::move(receiver), std::move(target), isolation_info.top_frame_origin(), frame_id,
      &service->engine(), base::BindRepeating(&PolicyIfAlive, context->GetWeakPtr()),
      std::move(uncloak));
```

The constructor stores `uncloak_(std::move(uncloak))`. `CreateLoaderAndStart` passes `uncloak_` to `InFlight` as its last argument.

- [ ] **Step 6: `InFlight` uncloaks.** `InFlight`'s constructor takes `UncloakContext uncloak` last and stores `uncloak_(std::move(uncloak))`. Members, after `check_same_site_`:

```cpp
  const UncloakContext uncloak_;
  // The resolution in progress, if any.
  std::unique_ptr<AliasResolver> resolver_;
```

Replace `Start()`. It now binds the page's end of the client at once, so a page that drops the request while its host resolves ends it, and the resolution with it. The loader stays unbound until the verdict: a `SetPriority` or `FollowRedirect` before then would reach an unbound `target_loader_`.

```cpp
  void Start() {
    client_.Bind(std::move(pending_client_));
    client_.set_disconnect_handler(base::BindOnce(&InFlight::OnPageGone, base::Unretained(this)));
    auto done = base::BindOnce(&InFlight::OnStartVerdict, weak_factory_.GetWeakPtr());
    if (!NeedsVerdict(request_, request_.url, source_, check_same_site_)) {
      // A same-site request isn't checked by its URL (Standard), but its
      // host's aliases are: an alias on another site is a third party. It
      // stays here after: it may redirect to a tracker.
      Uncloak(request_.url, std::move(done));
      return;
    }
    engine_->Check(Check(request_.url),
                   base::BindOnce(&InFlight::OnUrlVerdict, weak_factory_.GetWeakPtr(),
                                  request_.url, std::move(done)));
  }
```

Replace `OnReceiveRedirect`:

```cpp
  void OnReceiveRedirect(const net::RedirectInfo& redirect_info,
                         network::mojom::URLResponseHeadPtr head) override {
    // A redirect to a tracker is checked like a request to it, aliases
    // included.
    auto done = base::BindOnce(&InFlight::OnRedirectVerdict, weak_factory_.GetWeakPtr(),
                               redirect_info, std::move(head));
    if (!NeedsVerdict(request_, redirect_info.new_url, source_, check_same_site_)) {
      Uncloak(redirect_info.new_url, std::move(done));
      return;
    }
    engine_->Check(Check(redirect_info.new_url),
                   base::BindOnce(&InFlight::OnUrlVerdict, weak_factory_.GetWeakPtr(),
                                  redirect_info.new_url, std::move(done)));
  }
```

In the private section, after `Check()`:

```cpp
  // The URL's verdict: blocked ends it, before any DNS query, so a blocked
  // tracker's name server hears nothing; otherwise the host's aliases decide.
  void OnUrlVerdict(GURL url, base::OnceCallback<void(Decision)> done, Decision decision) {
    if (decision.blocked) {
      std::move(done).Run(std::move(decision));
      return;
    }
    Uncloak(url, std::move(done));
  }

  // Resolves the URL's host, then has the engine judge its aliases (CNAME
  // uncloaking). No aliases, or no answer: not blocked.
  void Uncloak(const GURL& url, base::OnceCallback<void(Decision)> done) {
    network::mojom::NetworkContext* network_context =
        uncloak_.network_context ? uncloak_.network_context.Run() : nullptr;
    if (!network_context || !url.SchemeIsHTTPOrHTTPS() || url.HostIsIPAddress() ||
        net::IsLocalhost(url)) {
      std::move(done).Run(Decision());
      return;
    }
    const net::NetworkAnonymizationKey& key =
        request_.trusted_params
            ? request_.trusted_params->isolation_info.network_anonymization_key()
            : uncloak_.network_anonymization_key;
    const net::SecureDnsPolicy secure_dns =
        request_.trusted_params && request_.trusted_params->disable_secure_dns
            ? net::SecureDnsPolicy::kDisable
            : net::SecureDnsPolicy::kAllow;
    resolver_ = std::make_unique<AliasResolver>(
        network_context, url, key, secure_dns, request_.priority,
        base::BindOnce(&InFlight::OnAliases, weak_factory_.GetWeakPtr(), url,
                       base::TimeTicks::Now(), std::move(done)));
  }

  void OnAliases(GURL url,
                 base::TimeTicks start,
                 base::OnceCallback<void(Decision)> done,
                 std::vector<std::string> aliases) {
    // The resolver ran this as its last act: it can go.
    resolver_.reset();
    std::vector<GURL> alias_urls;
    for (const std::string& alias : AliasesToCheck(url.host(), aliases)) {
      if (GURL alias_url = WithAlias(url, alias); alias_url.is_valid()) {
        alias_urls.push_back(std::move(alias_url));
      }
    }
    auto timed = base::BindOnce(
        [](std::string host, base::TimeTicks start, base::OnceCallback<void(Decision)> done,
           Decision decision) {
          // Read by tools/uncloak_cost.py (the budget).
          VLOG(2) << "Uncloak " << host << ": "
                  << (base::TimeTicks::Now() - start).InMicroseconds() << " us";
          std::move(done).Run(std::move(decision));
        },
        url.host(), start, std::move(done));
    if (alias_urls.empty()) {
      std::move(timed).Run(Decision());
      return;
    }
    engine_->CheckAliases(Check(url), std::move(alias_urls), std::move(timed));
  }
```

In `OnStartVerdict`, delete its first line, `client_.Bind(std::move(pending_client_));` (Start binds it now), and the later `client_.set_disconnect_handler(…)` line, which Start has set. `OnRedirectVerdict` stays as it is. In `Block`, name the alias in the log:

```cpp
    VLOG(1) << "Blocked " << request_.url
            << (decision.alias.empty() ? "" : " (alias " + decision.alias + ")") << " by "
            << decision.filter;
```

Add includes as needed: `<memory>`, `<string>`, `<vector>`, `"base/time/time.h"`, `"net/dns/public/secure_dns_policy.h"`.

- [ ] **Step 7: Run the new tests.** Sync, build, `bt.ps1 -Filter "CnameUncloaking*"`. Expected: all pass, `EveryAliasIsChecked` included unless Step 3 removed it.
  - If test 1 fails because the request reached the server, check `NeedsVerdict` is false for it (same-site at Standard), and that `Uncloak` got a network context.
  - If the proxy test sees no resolution, check that the page loaded directly: the bypass list must name the page's host.

- [ ] **Step 8: Run the earlier blocking suites.** `bt.ps1 -Filter "RequestFilter*:ConnectionFilter*:Redirect*:BlockedCount*:ProtectionLevels*:CosmeticFilters*:Scriptlets*:Procedural*:QueryFilter*"`. Expected: all pass. A same-site request now waits for a resolution: a test with a timing assumption that breaks is a finding. Record it, then fix the test, not the filter.

- [ ] **Step 9: Commit.**

```bash
git add BUILD.gn browser
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "blocking: uncloak a request's host before it leaves (4C)"
```

---

### Task 5: Spike (points 1, 3, 4) and progress notes

**Files:**
- Create: `docs/superpowers/specs/2026-10-12-cname-uncloaking-spike.md`

Run every browser on the hidden desktop: `powershell -File <scratchpad>\hidden.ps1 -CommandLine '<command>'`.

- [ ] **Step 1: One DNS transaction per host (spike point 1).** Write `<scratchpad>\dns_jobs.py`:

```python
"""Counts the host resolver's jobs per host in a NetLog: one job is one
real resolution (a cache hit makes none)."""
import collections
import json
import sys

log = json.load(open(sys.argv[1], encoding="utf-8"))
types = {v: k for k, v in log["constants"]["logEventTypes"].items()}
begin = log["constants"]["logEventPhase"]["PHASE_BEGIN"]
jobs = collections.Counter()
for event in log["events"]:
    if types.get(event["type"]) == "HOST_RESOLVER_MANAGER_JOB" and event.get("phase") == begin:
        jobs[event.get("params", {}).get("host", "?")] += 1
for host, count in jobs.most_common():
    print(count, host)
```

Then for each of two fresh profiles, DoH off (default) and DoH on (the profile's `Local State` written before launch: `{"dns_over_https": {"mode": "secure", "templates": "https://cloudflare-dns.com/dns-query"}}`), run:

```
out\vanilla\chrome.exe --user-data-dir=<tmp> --no-first-run --log-net-log=<scratchpad>\dns-<mode>.json https://www.lemonde.fr/
```

Wait 20 s, close it through `--remote-debugging-port` (`Browser.close`) or kill it. Run `python <scratchpad>\dns_jobs.py <scratchpad>\dns-<mode>.json`. Expected: every host at 1 (a host at 2 is our resolution and the request's missing each other's cache entry: compare the two events' `network_anonymization_key` params, and fix the key in `Uncloak` before going on). Record the counts' summary, not the NetLog (it holds the user's IPs; never commit it).
- [ ] **Step 2: WPAD's cost.** Run the same in the default profile with Windows' "Automatically detect settings" as the user has it (read `HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings\Connections`, or ask). In the NetLog, check whether `PROXY_RESOLUTION_SERVICE` events double per request. Record it; the budget task measures its time.
- [ ] **Step 3: Workers in other partitions (spike point 3).** Look for storage partitions other than the default in Shade at the pin: `git -C chromium/src grep -n "GetStoragePartitionConfig\b\|StoragePartitionConfig::Create(" -- chrome/browser/chrome_content_browser_client.cc chrome/browser/web_applications/isolated_web_apps chrome/browser/guest_view`. Decide whether a shared or service worker can run in a non-default partition in Shade's configuration (Chrome Apps, `<webview>` and Isolated Web Apps: each enabled or not in Shade). Record the finding:
  - **none reachable:** the default-partition fallback is right; document it as an assumption in architecture.md (Task 9);
  - **reachable:** stop and report to the user. The fix (0037 passing `render_process_id` to `MaybeProxyURLLoaderFactory`, whose `RenderProcessHost` gives the partition) is a patch change to approve.
- [ ] **Step 4: A real cloaked tracker (spike point 4).** From `components/blocking/data/easyprivacy.txt`'s `easyprivacy_specific_cname_*` sections, pick hosts that still have a CNAME today (`nslookup -type=CNAME <host>` names a tracker domain the lists block), and the site that uses each (often named in the section's comment). Load that site in the dev build with `--enable-logging --vmodule=request_filter=1`, and look in `<user-data-dir>\chrome_debug.log` for `Blocked … (alias …)`. Record two sites, their hosts and aliases, and the rule. If none can be found live, record that, and the browser tests stand.
- [ ] **Step 5: The progress notes.** Create `docs/superpowers/specs/2026-10-12-cname-uncloaking-spike.md` with a heading per spike point, Task 4's findings (Steps 3 and 8), and what Steps 1–4 found. Use the format of `2026-10-12-scriptlets-procedural-spike.md`.
- [ ] **Step 6: Commit.**

```bash
git add docs/superpowers/specs/2026-10-12-cname-uncloaking-spike.md
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "docs: 4C spike notes"
```

---

### Task 6: The fuzzer feeds aliases

**Files:**
- Modify: `components/blocking/fuzz/blocking_request_fuzzer.cc`

- [ ] **Step 1: The fifth field.** Update the file's comment: "…optionally followed by a method and by DNS aliases (comma-separated), so the corpus seeds this fuzzer. With aliases, the request's alias URLs go through `check_aliases` too (Phase 4C)". Add includes `"ghost/components/blocking/dns_aliases.h"` and `"url/gurl.h"`. At the end of `LLVMFuzzerTestOneInput`, replace the single `check` call with:

```cpp
  const std::string_view method = fields.size() > 3 ? fields[3] : "GET";
  ShippedEngine().check(Str(fields[2]), Str(fields[1]), Str(fields[0]), Str(method));
  if (fields.size() > 4) {
    const GURL url(fields[2]);
    rust::Vec<rust::String> alias_urls;
    if (url.is_valid()) {
      const std::vector<std::string> aliases =
          base::SplitString(fields[4], ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
      for (const std::string& alias : ghost::blocking::AliasesToCheck(url.host(), aliases)) {
        const GURL alias_url = ghost::blocking::WithAlias(url, alias);
        CHECK(alias_url.is_valid());
        CHECK_EQ(alias_url.host(), alias);
        alias_urls.push_back(rust::String(alias_url.spec()));
      }
    }
    ShippedEngine().check_aliases(Str(fields[2]), Str(fields[1]), Str(fields[0]), Str(method),
                                  alias_urls);
  }
```

The `CHECK`s make an alias that `AliasesToCheck` keeps but `WithAlias` can't place a finding.
- [ ] **Step 2: Build it in the fuzzing directory.** `build.ps1 -Out fuzz -Targets ghost_blocking_request_fuzzer`. Expected: builds. Run it for 60 s (`out\fuzz\ghost_blocking_request_fuzzer.exe -max_total_time=60`) as a smoke test. The 30-minute run is Task 8's.
- [ ] **Step 3: Commit.**

```bash
git add components/blocking/fuzz
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "blocking: the request fuzzer checks DNS aliases"
```

---

### Task 7: Budgets

**Files:**
- Modify: `components/blocking/blocking_perftest.cc`
- Create: `tools/uncloak_cost.py`, `tools/tests/test_uncloak_cost.py`

- [ ] **Step 1: The perftest.** In `blocking_perftest.cc`, add a test after the request-check test. It reuses `ReadCorpus()`, `ShippedLists()`, `Percentiles()`, `kBudgetP99` and `kPasses`, which are already in the file. Also add `#include "ghost/components/blocking/dns_aliases.h"` and `#include "url/gurl.h"`:

```cpp
// An alias's check (Phase 4C): the request's exception lookup and one alias
// URL, for every corpus request as if its host had a tracker's alias.
TEST(BlockingPerfTest, AliasChecksWithinBudget) {
  const std::vector<Row> rows = ReadCorpus();
  rust::Box<Engine> engine = new_engine(ShippedLists());
  std::vector<base::TimeDelta> times;
  for (int pass = 0; pass < kPasses; ++pass) {
    for (const Row& row : rows) {
      const GURL alias_url = WithAlias(GURL(row.url), "cname.tracker-alias.test");
      if (!alias_url.is_valid()) {
        continue;
      }
      rust::Vec<rust::String> alias_urls;
      alias_urls.push_back(rust::String(alias_url.spec()));
      const base::TimeTicks start = base::TimeTicks::Now();
      engine->check_aliases(row.url, row.page, row.type, "GET", alias_urls);
      times.push_back(base::TimeTicks::Now() - start);
    }
  }
  const auto [p50, p99] = Percentiles(std::move(times));
  LOG(INFO) << "Alias checks: p50 " << p50.InMicrosecondsF() << " us, p99 "
            << p99.InMicrosecondsF() << " us (budget " << kBudgetP99.InMicroseconds() << " us).";
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kBudgetP99);
#endif
}
```

Build `ghost_blocking_perftests` in `out/vanilla` and run it. Expected: passes, with the line logged.
- [ ] **Step 2: The tool's failing tests.** `tools/tests/test_uncloak_cost.py`:

```python
import unittest

import uncloak_cost


class ParseTest(unittest.TestCase):
    def test_reads_the_filters_lines_only(self):
        log = "\n".join([
            "[1:2:1012/101010.123:VERBOSE2:request_filter.cc(321)] Uncloak cdn.example.com: 412 us",
            "[1:2:1012/101010.124:VERBOSE1:request_filter.cc(300)] Blocked https://x/ by ||x^",
            "[1:2:1012/101010.125:VERBOSE2:request_filter.cc(321)] Uncloak a.example.com: 1203 us",
            "garbage",
        ])
        self.assertEqual(uncloak_cost.parse(log), [412, 1203])


class PercentileTest(unittest.TestCase):
    def test_nearest_rank(self):
        values = list(range(1, 101))
        self.assertEqual(uncloak_cost.percentile(values, 50), 50)
        self.assertEqual(uncloak_cost.percentile(values, 99), 99)
        self.assertEqual(uncloak_cost.percentile([7], 99), 7)

    def test_empty_is_none(self):
        self.assertIsNone(uncloak_cost.percentile([], 99))


class PagesTest(unittest.TestCase):
    def test_the_corpus_front_pages(self):
        pages = uncloak_cost.corpus_pages()
        self.assertEqual(len(pages), 20)
        self.assertTrue(all(page.startswith("https://") for page in pages))


if __name__ == "__main__":
    unittest.main()
```

Run `python -m unittest discover -s tools/tests -t tools`. Expected: `ModuleNotFoundError: uncloak_cost`.
- [ ] **Step 3: The tool.** `tools/uncloak_cost.py`:

```python
#!/usr/bin/env python3
"""Measures what CNAME uncloaking adds to requests (Phase 4C).

    uncloak_cost.py --chrome out/release/chrome.exe [--launcher hidden.ps1] [--wait 15]

Loads the perftest corpus' 20 front pages twice in one fresh profile, with the
request filter's timing lines on (--vmodule=request_filter=2): each line is
the time from a request's resolution to its aliases' verdict. The first pass
fills the host cache; the second is the budget's (p99 under 1 ms in an
official build). Live sites, so the numbers vary with the network: run it
twice and report both.

--launcher runs the browser through a script that takes -CommandLine (on the
development PC, the hidden-desktop launcher, so no window shows).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import cdp

REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "components" / "blocking" / "test" / "request_corpus.tsv"
LINE = re.compile(r"request_filter\.cc\(\d+\)\] Uncloak \S+: (\d+) us$")
BUDGET_US = 1000


def parse(log: str) -> list[int]:
    """The microseconds of each uncloaking in a chrome_debug.log text."""
    return [int(m.group(1)) for m in map(LINE.search, log.splitlines()) if m]


def percentile(values: list[int], p: float) -> int | None:
    """The nearest-rank percentile; None for no values."""
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(p / 100 * len(ordered)) - 1)]


def corpus_pages() -> list[str]:
    pages = {line.split("\t")[1] for line in CORPUS.read_text(encoding="utf-8").splitlines()
             if line.strip()}
    return sorted(pages)


def _wait_for(url: str, timeout: float = 60) -> object:
    deadline = time.monotonic() + timeout
    while True:
        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                return json.load(response)
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)


def measure(chrome: Path, launcher: Path | None, port: int, wait: float) -> tuple[list[int], list[int]]:
    with tempfile.TemporaryDirectory() as profile:
        log = Path(profile, "chrome_debug.log")
        args = [str(chrome), f"--user-data-dir={profile}", "--no-first-run",
                "--no-default-browser-check", f"--remote-debugging-port={port}",
                "--enable-logging", "--vmodule=request_filter=2", "about:blank"]
        if launcher:
            process = subprocess.Popen(["powershell", "-NoProfile", "-File", str(launcher),
                                        "-CommandLine", subprocess.list2cmdline(args)])
        else:
            process = subprocess.Popen(args)
        try:
            version = _wait_for(f"http://127.0.0.1:{port}/json/version")
            target = next(t for t in _wait_for(f"http://127.0.0.1:{port}/json/list")
                          if t["type"] == "page")
            session = cdp.Session.connect(target["webSocketDebuggerUrl"])
            passes = []
            for _ in range(2):
                offset = log.stat().st_size if log.exists() else 0
                for page in corpus_pages():
                    session.call("Page.navigate", url=page)
                    time.sleep(wait)
                with log.open(encoding="utf-8", errors="replace") as f:
                    f.seek(offset)
                    passes.append(parse(f.read()))
            session.close()
            browser = cdp.Session.connect(version["webSocketDebuggerUrl"])
            try:
                browser.call("Browser.close")
            except (cdp.ProtocolError, OSError):
                pass
            process.wait(timeout=60)
            return passes[0], passes[1]
        except BaseException:
            process.kill()
            raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--chrome", type=Path, required=True)
    parser.add_argument("--launcher", type=Path)
    parser.add_argument("--port", type=int, default=9335)
    parser.add_argument("--wait", type=float, default=15)
    args = parser.parse_args(argv)
    first, second = measure(args.chrome, args.launcher, args.port, args.wait)
    for name, values in (("first", first), ("second", second)):
        print(f"{name}: {len(values)} requests, p50 {percentile(values, 50)} us, "
              f"p99 {percentile(values, 99)} us")
    p99 = percentile(second, 99)
    print(f"budget: second pass p99 under {BUDGET_US} us: "
          + ("no data" if p99 is None else "met" if p99 < BUDGET_US else "MISSED"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Run the tooling tests again. Expected: pass.
- [ ] **Step 4: The official build.** Ask the user whether to build the official `chrome` from `out/release` (about 1 h of linking, as in 4B) or to measure in the dev build only. Wait for the answer.
  - **If official:** `build.ps1 -Out release -Targets "chrome ghost_blocking_perftests"`. Run `out\release\ghost_blocking_perftests.exe --gtest_filter=*AliasChecks*:*WithinBudget*`, then run `python tools\uncloak_cost.py --chrome chromium\src\out\release\chrome.exe --launcher <scratchpad>\hidden.ps1` twice.
  - **If dev:** the same with `out\vanilla`, reported as the dev build's.
- [ ] **Step 5: Judge and record.** Against the spec:
  - the alias check, p99 under 50 µs;
  - the second pass, p99 under 1 ms;
  - the first pass reported.

  Also check that 4B's request budget still holds (the perftest's request test is unchanged, so it should). Record every number in the progress notes. A budget missed in an official build is reported to the user, not changed.
- [ ] **Step 6: Commit.**

```bash
git add components/blocking/blocking_perftest.cc tools docs/superpowers/specs/2026-10-12-cname-uncloaking-spike.md
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "blocking: 4C's budgets, measured"
```

---

### Task 8: Mutation checks, audit, fuzzing

- [ ] **Step 1: The mutation script.** Copy `mutate4b.py` from the planning session's source scratchpad (`…\28b6f2a8-6de8-4b64-9c7f-992c45957cbd\scratchpad\`) to `<scratchpad>\mutate4c.py`. It applies one mutant in `src/ghost`, builds, runs the given filter on the hidden desktop, and restores. **Check that each mutant's build succeeds before reading its tests:** a mutant that doesn't compile leaves the old binary, which passes.
- [ ] **Step 2: The six mutants.** Each must make the named tests fail:

| Mutant | Change (in `request_filter.cc` unless named) | Must fail |
|---|---|---|
| M1 | `OnAliases` runs `std::move(timed).Run(Decision())` before the `CheckAliases` call (aliases never judged) | `CnameUncloakingBrowserTest.ASameSiteCloakedRequestNeverLeaves` |
| M2 | `lib.rs` `check_aliases` skips the exception lookup (`if false && …`) | `EngineBridgeTest.AnExceptionForTheRequestLeavesItsAliasesAlone`, `CnameUncloakingBrowserTest.AnExceptionForTheHostKeepsItsAliasesUnchecked` |
| M3 | `dns_aliases.cc` drops `\|\| RegistrableDomain(alias) == site` | `DnsAliasesTest.AnAliasOnTheSameSiteIsNotCloaking`, `CnameUncloakingBrowserTest.AnAliasOnTheSameSiteIsNotChecked` |
| M4 | `OnUrlVerdict` starts a resolution before looking at the verdict: its first line becomes `Uncloak(url, base::DoNothing());` (a blocked request then deletes the `InFlight`, which cancels it, but the query was asked) | `CnameUncloakingBrowserTest.ABlockedUrlsHostIsNeverResolved` |
| M5 | `OnReceiveRedirect`'s not-`NeedsVerdict` branch passes the redirect on at once (`client_->OnReceiveRedirect(…)`) instead of `Uncloak` | `CnameUncloakingBrowserTest.ARedirectToACloakedHostIsBlocked` |
| M6 | `alias_resolver.cc` sets `parameters->direct_only = false` | `CnameUncloakingProxyBrowserTest.NothingIsResolvedBesideAProxy` |

  Record each result in the progress notes. After the last one, compare `src/ghost` with webops (`cmp` over `git ls-files`). Any difference is a mutant left behind: restore it.
- [ ] **Step 3: The whole suites.** `ghost_unittests` and `ghost_browsertests` in full, on the hidden desktop. Expected: all pass with no retry. Record the counts.
- [ ] **Step 4: The egress audit.** Run it as in 4B (`python tools\egress_audit.py run …` with the scenarios of `docs/testing.md`). Expected: no unexpected host. Uncloaking resolves only hosts that requests were going to contact.
- [ ] **Step 5: Fuzzing.** `out\fuzz\ghost_blocking_request_fuzzer.exe -max_total_time=1800` with a corpus directory seeded from `request_corpus.tsv` lines plus a few with a fifth field (`script\thttps://a.test/\thttps://metrics.a.test/m.js\tGET\ta.tracker.test,edge.a.test`). Expected: no crash. Record the run.
- [ ] **Step 6: Commit the notes.**

```bash
git add docs/superpowers/specs/2026-10-12-cname-uncloaking-spike.md
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "docs: 4C mutation checks, audit and fuzzing"
```

---

### Task 9: Documentation and push

**Files:**
- Modify: `docs/privacy-model.md`, `docs/architecture.md`, `docs/threat-model.md`, `docs/testing.md`, `docs/roadmap.md`
- Modify: `docs/superpowers/specs/2026-10-12-cname-uncloaking-design.md` (status, done-when boxes)
- Modify: `docs/superpowers/plans/2026-10-12-cname-uncloaking.md` (status)

- [ ] **Step 1: privacy-model.md.** In the blocking section:
  - Standard and Strict uncloak every request they hold, same-site ones included, before it leaves. Off resolves nothing.
  - A host blocked by its URL is never resolved.
  - An exception for a host leaves its aliases unchecked.
  - The limits: through a proxy nothing is uncloaked; the system resolver (when Chromium's own can't be used) gives no aliases; WebSocket and WebTransport aren't uncloaked.
- [ ] **Step 2: architecture.md.**
  - `AliasResolver` and `InFlight`'s step: URL verdict, then resolution, then aliases;
  - the cache key and secure DNS policy that make it one DNS transaction;
  - the storage-partition assumption from Task 5 Step 3.
- [ ] **Step 3: threat-model.md.**
  - The resolver's trust: a hostile resolver can make a site's resources look cloaked.
  - Evasion: a CNAME to an unlisted host, or proxying through the site.
  - A request served from the HTTP cache may now make a DNS query.
  - WPAD's added proxy resolutions, from Task 5 Step 2.
- [ ] **Step 4: testing.md.**
  - `CnameUncloakingBrowserTest` and the proxy fixture;
  - `DnsAliasesTest`, the bridge's and engine's alias tests;
  - the fuzzer's fifth field;
  - `AliasChecksWithinBudget` and `tools/uncloak_cost.py`;
  - the mutation checks M1–M6.
- [ ] **Step 5: roadmap.md.** Mark 4C **Done** with its date, as 4B's entry does:
  - where it lives, and the design and progress-notes links;
  - the budgets' numbers, and the checks: mutations, test counts, fuzzing, egress;
  - the limitations.
- [ ] **Step 6: Spec and plan status.** The spec's status line gets "done <date>" and its boxes are checked. The plan's header gets a **Status** line, as 4B's has.
- [ ] **Step 7: Commit and push.**

```bash
git add docs
python tools/lint.py
git commit -F <scratchpad>/msg.txt   # "docs: CNAME uncloaking (4C) done"
```

Ask the user before pushing. After `git push`, check both tooling CI jobs (Ubuntu and Windows) to completion, then pull `src/ghost`.
