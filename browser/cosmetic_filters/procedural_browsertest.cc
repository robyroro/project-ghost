// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Procedural and action rules through the browser, the renderer and the
// engine, with a test list (Phase 4B).

#include <string>

#include "base/strings/strcat.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"

namespace ghost::cosmetic_filters {
namespace {

class ProceduralBrowserTest : public CosmeticFiltersTestBase {
 protected:
  std::string List() const override {
    return "p.test#?#.box:has-text(Sponsored)\n"
           // Not `\d`: adblock-rust's CSS validation rewrites escapes in an
           // operator's argument (progress notes, Task 7).
           "p.test#?#.box:has-text(/promo[0-9]+/)\n"
           "p.test#?#.card:matches-attr(data-kind=\"ad\")\n"
           "p.test#?#.placed:matches-css(position: absolute)\n"
           "p.test#?#.marked:matches-css-before(content: /AD/)\n"
           "p.test#?#.onpath:matches-path(/procedural)\n"
           "p.test#?#.long:min-text-length(40)\n"
           "p.test#?#.label:has-text(Ad):upward(.wrapper)\n"
           "p.test#?#.deep:upward(2)\n"
           "p.test#?#:xpath(//section[@data-x=\"1\"])\n"
           "p.test#?#.parent:has-text(Promoted) > .child\n"
           "p.test#?#.feed:has(p:has-text(Sponsored))\n"
           "p.test##.restyle:style(color: rgb(255, 0, 0) !important)\n"
           "p.test#?#.restyle2:has-text(Paint):style(color: rgb(0, 0, 255) !important)\n"
           "p.test##.gone:remove()\n"
           "p.test##.attr:remove-attr(onclick)\n"
           "p.test##.cls:remove-class(promo)\n"
           "off.test#?#.box:has-text(Sponsored)\n";
  }

  void AddPages() override {
    const std::string body = R"(
      <div class="box" id="sponsored">Sponsored</div>
      <div class="box" id="promo">promo42</div>
      <div class="box" id="plain">News</div>
      <div class="card" id="card" data-kind="ad">x</div>
      <div class="card" id="card-ok" data-kind="news">x</div>
      <style>.placed{position:absolute}.marked::before{content:"AD"}</style>
      <div class="placed" id="placed">x</div>
      <div class="marked" id="marked">x</div>
      <div class="onpath" id="onpath">x</div>
      <div class="long" id="long">this text is longer than forty characters, surely</div>
      <div class="long" id="short">short</div>
      <div class="wrapper" id="wrapper"><span class="label">Ad</span></div>
      <div id="grand"><div><span class="deep">x</span></div></div>
      <section id="section" data-x="1">x</section>
      <div class="parent">Promoted <div class="child" id="child">x</div></div>
      <div class="feed" id="feed"><p>Sponsored</p></div>
      <div class="feed" id="feed-ok"><p>News</p></div>
      <div class="restyle" id="restyle">x</div>
      <div class="restyle2" id="restyle2">Paint</div>
      <div class="gone" id="gone">x</div>
      <div class="attr" id="attr" onclick="1">x</div>
      <div class="cls promo" id="cls">x</div>)";
    AddPage("/procedural.html", body);
    AddPage("/other.html", "<div class=\"onpath\" id=\"onpath\">x</div>");
    AddPage("/late.html",
            "<script>setTimeout(() => { const d = document.createElement('div');"
            "d.className = 'box'; d.id = 'late'; d.textContent = 'Sponsored';"
            "document.body.append(d); }, 300);</script>");
  }
};

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, EveryOperatorSelects) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/procedural.html")));
  for (const char* id : {"sponsored", "promo", "card", "placed", "marked", "onpath", "long",
                         "wrapper", "grand", "section", "child", "feed"}) {
    EXPECT_EQ(WaitForDisplay(Tab(), id, "none"), "none") << id;
  }
  for (const char* id : {"plain", "card-ok", "short", "feed-ok"}) {
    EXPECT_EQ(Display(Tab(), id), "block") << id;
  }
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, MatchesPathLooksAtThePath) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/other.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "onpath", "none"), "block");
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, EveryActionApplies) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/procedural.html")));
  EXPECT_EQ(content::EvalJs(Tab(), R"(new Promise(done => setTimeout(() => done([
                getComputedStyle(document.getElementById('restyle')).color,
                getComputedStyle(document.getElementById('restyle2')).color,
                String(document.getElementById('gone')),
                String(document.getElementById('attr').hasAttribute('onclick')),
                document.getElementById('cls').className,
              ].join('|')), 500)))"),
            "rgb(255, 0, 0)|rgb(0, 0, 255)|null|false|cls");
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, AnElementAddedLaterIsCaught) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/late.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "late", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, ThePageSeesNoExecutor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/procedural.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "typeof shadeCosmeticFilters"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, OffAppliesNone) {
  privacy_policy::SetLevel(GetProfile(), Url("off.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("off.test", "/procedural.html")));
  EXPECT_EQ(WaitForDisplay(Tab(), "sponsored", "none"), "block");
}

}  // namespace
}  // namespace ghost::cosmetic_filters
