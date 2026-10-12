// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Redirect rules: the request filter answers with Shade's resource instead
// of the network (Phase 4B).

#include <string>

#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"

namespace ghost::blocking {
namespace {

class RedirectBrowserTest : public cosmetic_filters::CosmeticFiltersTestBase {
 protected:
  // `^` matches the port the test server's URLs carry.
  std::string List() const override {
    return "||tracker.test^*/noop.js$script,redirect=noopjs\n"
           "||tracker.test^*/data.json$xhr,redirect=noopjson\n"
           "||tracker.test^*/pixel.gif$image,redirect=1x1.gif\n"
           "||tracker.test^*/gtm.js$script,redirect=googletagmanager_gtm.js\n";
  }

  void AddPages() override {
    // The real files differ from Shade's resources, so a request that
    // reached the server would show.
    AddResource("/noop.js", "application/javascript", "results.real = 'script';",
                {{"Access-Control-Allow-Origin", "*"}});
    AddResource("/data.json", "application/json", "{\"real\":1}",
                {{"Access-Control-Allow-Origin", "*"}});
    AddResource("/gtm.js", "application/javascript", "results.real = 'gtm';");
    // The URLs carry the server's port, known only once the page loads.
    AddPage("/redirects.html", R"(<script>
      window.results = {};
      const base = `http://tracker.test:${location.port}`;
      const script = document.createElement('script');
      script.src = `${base}/noop.js`;
      script.onload = () => { results.script = 'loaded'; };
      script.onerror = () => { results.script = 'error'; };
      document.head.append(script);
      const image = new Image();
      image.onload = () => { results.image = image.naturalWidth; };
      image.onerror = () => { results.image = 'error'; };
      image.src = `${base}/pixel.gif`;
      fetch(`${base}/data.json`, {mode: 'cors'})
          .then(r => r.json()).then(j => { results.fetch = JSON.stringify(j); })
          .catch(e => { results.fetch = e.name; });
      const gtm = document.createElement('script');
      gtm.src = `${base}/gtm.js`;
      gtm.onload = () => {
        dataLayer.push({event: 'x', eventCallback: () => { results.gtm = 'called'; }});
      };
      gtm.onerror = () => { results.gtm = 'error'; };
      document.head.append(gtm);
    </script>)");
  }
};

IN_PROC_BROWSER_TEST_F(RedirectBrowserTest, ResourcesAnswerWithoutTheNetwork) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("a.test", "/redirects.html")));
  EXPECT_EQ(content::EvalJs(Tab(), R"(new Promise(done => {
                const start = performance.now();
                (function poll() {
                  if (['script', 'image', 'fetch', 'gtm'].every(k => k in results) ||
                      performance.now() - start > 3000) {
                    done([results.script, results.image, results.fetch, results.gtm].join('|'));
                  } else {
                    setTimeout(poll, 50);
                  }
                })();
              }))"),
            "loaded|1|{}|called");
  EXPECT_EQ(content::EvalJs(Tab(), "String(results.real)"), "undefined");
  for (const char* path : {"/noop.js", "/pixel.gif", "/data.json", "/gtm.js"}) {
    EXPECT_FALSE(LastRequestHeaders(path)) << path << " reached the server";
  }
}

}  // namespace
}  // namespace ghost::blocking
