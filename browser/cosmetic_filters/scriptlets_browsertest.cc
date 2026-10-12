// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Scriptlets through the browser, the renderer and the engine, with a test
// list (Phase 4B).

#include <optional>
#include <string>
#include <string_view>

#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"
#include "net/test/embedded_test_server/http_request.h"
#include "ui/base/page_transition_types.h"

namespace ghost::cosmetic_filters {
namespace {

class ScriptletsBrowserTest : public CosmeticFiltersTestBase {
 protected:
  std::string List() const override {
    return "a.test##+js(set, shadeValue, noopFunc)\n"
           "a.test##+js(set, shade.nested.value, true)\n"
           "off.test##+js(set, shadeValue, noopFunc)\n"
           "except.test##+js(set, shadeValue, noopFunc)\n"
           "except.test#@#+js()\n"
           "gpc.test##+js(set, navigator.globalPrivacyControl, false)\n"
           "acs.test##+js(acs, document.createElement, adNetwork)\n"
           "aost.test##+js(aost, shadeTarget, inlineScript)\n"
           "rmnt.test##+js(rmnt, script, adNetwork)\n"
           "xhr.test##+js(no-xhr-if, blocked.json)\n"
           "fetch.test##+js(no-fetch-if, blocked.json)\n"
           "storage.test##+js(set-local-storage-item, shadeKey, true)\n"
           "storage.test##+js(set-local-storage-item, oldKey, $remove$)\n"
           "cookie.test##+js(set-cookie, shadeConsent, accept)\n"
           "reload.test##+js(set-cookie, shadeReload, 1, , reload, 1)\n"
           "crumbs.test##+js(cookie-remover, tracking)\n";
  }

  // The value of `expression` once `ready` is true, or after 3 s.
  content::EvalJsResult EvalWhen(std::string_view ready, std::string_view expression) {
    return content::EvalJs(Tab(), base::StrCat({R"(new Promise(done => {
               const start = performance.now();
               (function poll() {
                 if (()",
                                                ready, R"() || performance.now() - start > 3000) {
                   done()",
                                                expression, R"();
                 } else {
                   setTimeout(poll, 20);
                 }
               })();
             }))"}));
  }

  void AddPages() override {
    // What the page's first script sees.
    AddPage("/first.html",
            "<script>window.seen = typeof shadeValue;"
            "window.shade = {}; shade.nested = {value: 5};"
            "window.nested = shade.nested.value;</script>");
    AddPage("/csp.html", "<script nonce=\"n\">window.seen = typeof shadeValue;</script>",
            {{"Content-Security-Policy", "script-src 'nonce-n'"}});
    AddPage("/written.html",
            "<iframe id=\"f\"></iframe><script>"
            "const d = document.getElementById('f').contentDocument;"
            "d.open(); d.write('<script>parent.childSeen = typeof shadeValue;<\\/script>');"
            "d.close();</script>");
    AddPage("/acs.html",
            "<script>/* adNetwork */ try { document.createElement('div');"
            " window.caught = 'no'; } catch (e) { window.caught = e.name; }</script>"
            "<script>/* adNetwork */ document.createElement('div'); window.after = 'ran';</script>"
            "<script>window.other = typeof document.createElement('div');</script>");
    AddPage("/aost.html",
            "<script>try { window.inline = String(window.shadeTarget); }"
            " catch (e) { window.inline = e.name; }</script>"
            "<script src=\"/stack.js\"></script>");
    AddResource("/stack.js", "application/javascript",
                "window.fromFile = String(window.shadeTarget);");
    AddPage("/rmnt.html",
            "<script>window.ran = 'adNetwork';</script><script>window.kept = true;</script>");
    AddResource("/blocked.json", "application/json", "{\"x\":1}");
    AddResource("/allowed.json", "application/json", "{\"x\":1}");
    AddPage("/xhr.html", R"(<script>
        window.results = {};
        for (const name of ['blocked', 'allowed']) {
          const xhr = new XMLHttpRequest();
          xhr.open('GET', `/${name}.json`);
          xhr.onload = () => { results[name] = `${xhr.status}:${xhr.responseText}`; };
          xhr.send();
        }
      </script>)");
    AddPage("/fetch.html", R"(<script>
        window.results = {};
        for (const name of ['blocked', 'allowed']) {
          fetch(`/${name}.json`).then(r => r.text()).then(t => { results[name] = t; });
        }
      </script>)");
    AddPage("/storage.html",
            "<script>window.key = localStorage.getItem('shadeKey');"
            "window.before = localStorage.getItem('oldKey');"
            "localStorage.setItem('oldKey', '1');</script>");
    AddPage("/cookie.html",
            "<script>window.cookies = document.cookie;"
            "sessionStorage.loads = Number(sessionStorage.loads || 0) + 1;</script>");
    AddPage("/crumbs.html", "<script>window.cookies = document.cookie;</script>",
            {{"Set-Cookie", "tracking_id=1; path=/"}, {"Set-Cookie", "keep=1; path=/"}});
  }
};

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, TheFirstScriptAlreadySeesTheScriptlet) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "function");
  // A chain the page builds later is caught as it does.
  EXPECT_EQ(content::EvalJs(Tab(), "window.nested"), true);
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, APageCspDoesntStopIt) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/csp.html")));
  // EvalJs isn't subject to the page's script-src either.
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "function");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, NothingIsLeftOnWindow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "[typeof shadeSafe, typeof shadeChain, typeof setConstant]"
                                   ".join()"),
            "undefined,undefined,undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, OffRunsNone) {
  privacy_policy::SetLevel(GetProfile(), Url("off.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("off.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, IncognitoRunsThem) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(incognito, HttpsUrl("a.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(incognito), "window.seen"), "function");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AnExceptionTurnsThemOff) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("except.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.seen"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AFrameItsParentWritesTakesTheParentsScriptlets) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/written.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.childSeen"), "function");
}

// EasyPrivacy's GPC rules (the user's choice, 2026-10-12): the page sees
// false, the server still gets the signal.
IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AGpcRuleLeavesTheHeader) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("gpc.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "navigator.globalPrivacyControl"), false);
  std::optional<net::test_server::HttpRequest::HeaderMap> headers =
      LastRequestHeaders("/first.html");
  ASSERT_TRUE(headers);
  ASSERT_TRUE(headers->contains("Sec-GPC"));
  EXPECT_EQ(headers->at("Sec-GPC"), "1");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AbortCurrentScript) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("acs.test", "/acs.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.caught"), "ReferenceError");
  EXPECT_EQ(content::EvalJs(Tab(), "String(window.after)"), "undefined");
  EXPECT_EQ(content::EvalJs(Tab(), "window.other"), "object");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AbortOnStackTrace) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("aost.test", "/aost.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.inline"), "ReferenceError");
  // The external script's stack doesn't match: its read goes through.
  EXPECT_EQ(content::EvalJs(Tab(), "window.fromFile"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, RemoveNodeText) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("rmnt.test", "/rmnt.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "String(window.ran)"), "undefined");
  EXPECT_EQ(content::EvalJs(Tab(), "window.kept"), true);
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, NoXhrIf) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("xhr.test", "/xhr.html")));
  EXPECT_EQ(EvalWhen("Object.keys(results).length === 2",
                     "[results.blocked, results.allowed].join('|')"),
            "200:|200:{\"x\":1}");
  EXPECT_FALSE(LastRequestHeaders("/blocked.json"));
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, NoFetchIf) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("fetch.test", "/fetch.html")));
  EXPECT_EQ(EvalWhen("Object.keys(results).length === 2",
                     "[results.blocked, results.allowed].join('|')"),
            "|{\"x\":1}");
  EXPECT_FALSE(LastRequestHeaders("/blocked.json"));
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, SetLocalStorageItem) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("storage.test", "/storage.html")));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("storage.test", "/storage.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "window.key"), "true");
  // The first load set it; the second's start removed it.
  EXPECT_EQ(content::EvalJs(Tab(), "String(window.before)"), "null");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, SetCookie) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("cookie.test", "/cookie.html")));
  EXPECT_NE(content::EvalJs(Tab(), "window.cookies").ExtractString().find("shadeConsent=accept"),
            std::string::npos);
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, SetCookieReloadsOnce) {
  // The scriptlet reloads as soon as the cookie took, often before the first
  // load's own script runs; the reloaded page finds the cookie and stays.
  content::TestNavigationObserver reloaded(Tab(), 2);
  // The first load never stops: the reload cuts it short.
  reloaded.set_wait_event(content::TestNavigationObserver::WaitEvent::kNavigationFinished);
  Tab()->GetController().LoadURL(Url("reload.test", "/cookie.html"), content::Referrer(),
                                 ui::PAGE_TRANSITION_TYPED, std::string());
  reloaded.Wait();
  EXPECT_EQ(content::EvalJs(Tab(), "performance.getEntriesByType('navigation')[0].type"),
            "reload");
  EXPECT_NE(content::EvalJs(Tab(), "document.cookie").ExtractString().find("shadeReload=1"),
            std::string::npos);
  // No second reload: what this document holds is still there a second later.
  ASSERT_TRUE(content::ExecJs(Tab(), "window.stayed = true"));
  EXPECT_EQ(EvalWhen("false", "String(window.stayed)"), "true");
}

IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, CookieRemover) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("crumbs.test", "/crumbs.html")));
  const std::string cookies = content::EvalJs(Tab(), "window.cookies").ExtractString();
  EXPECT_EQ(cookies.find("tracking_id"), std::string::npos) << cookies;
  EXPECT_NE(cookies.find("keep=1"), std::string::npos) << cookies;
}

}  // namespace
}  // namespace ghost::cosmetic_filters
