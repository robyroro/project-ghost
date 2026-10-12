# Scriptlets and Procedural Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The scriptlets, procedural rules and redirect rules in EasyList and EasyPrivacy take effect: scriptlets in the page's world before its scripts, procedural rules from Shade's isolated world, redirects answered by the browser with resources of our own ([spec](../specs/2026-10-12-scriptlets-procedural-design.md)).

**Architecture:**
- **Engine.** adblock-rust gets CSS validation (procedural rules parse into operators) and our MPL-2.0 resources (`components/blocking/resources/`, built in with `include_bytes!`). The bridge returns a document's scriptlet, its pure-CSS style rules and its procedural rules (JSON), and a redirect's resource as a `data:` URL.
- **Browser.** 4A's throttle carries them in a `DocumentRules` struct, checked by `//ghost/components/cosmetic_filters` (`IsSafeStyle`, `SafeProceduralFilters`). `RequestFilter` answers a redirect itself.
- **Renderer.** The agent runs the scriptlet in the main world at document start, wrapped in one function, and runs `procedural.js` in the isolated world beside the scanner.
- **Patches.** None expected.

**Status:** done 2026-10-12 ([progress notes](../specs/2026-10-12-scriptlets-procedural-spike.md)). Beyond the plan: patch 0043; `rust_vendor.py` learned `inputs` and renamed dependencies; scriptlets are `application/javascript` (adblock-rust injects no other type); `:has()` around a procedural operator; the scanner's names read in C++ after the official build showed 4A's scanner over budget; the scriptlet budget amended with the user.

**Tech Stack:**
- adblock-rust 0.13.3 with `css-validation` (cssparser 0.35, selectors 0.32, vendored by `tools/rust_vendor.py`), `serde_json`, `base64`;
- Chromium 152.0.7977.158: `WebLocalFrame::ExecuteScript`, `ExecuteScriptInIsolatedWorld`, gin, mojo data pipes, `net::DataURL`;
- `InProcessBrowserTest`, libFuzzer.

**Settled while planning** (each checked in the source at the pin; the spec is amended to match):
- **CSS validation was off.** 3A built adblock-rust with `full-regex-handling` only. Without `css-validation`, `validate_css_selector` returns the whole selector as one `CssSelector`. So a procedural rule never reaches `procedural_actions`; it lands in `hide_selectors` as invalid CSS that Blink drops.
- **adblock-rust 0.13's operators:** `css-selector`, `has-text`, `matches-attr`, `matches-css`, `matches-css-before`, `matches-css-after`, `matches-path`, `min-text-length`, `upward`, `xpath`. The actions are `remove`, `style`, `remove-attr` and `remove-class`. It refuses `:-abp-properties`, and there is no `matches-media`.
- **`procedural_actions` are JSON** (`ProceduralOrActionFilter`: `{"selector":[{"type":…,"arg":…}],"action":{"type":…,"arg":…}}`), only for site-specific rules. The bridge moves the ones `as_css()` can express (a plain selector with `:style()`) into `style_rules`.
- **Function-style resources:** adblock-rust calls a resource as `name("arg", …)` when its text matches `^function\s+NAME\s*\(` (`extract_function_name`), and JSON-escapes each argument (`stringify_arg::<true>`). A template resource has `{{1}}` replaced by the raw argument. Our files carry the MPL header, so the bridge strips leading `//` lines before giving them to the engine.
- **Scriptlet and alias names end in `.js`:** the engine appends `.js` to the name in `+js(name, …)`. Redirect names don't (`redirect=noopjs`).
- **A redirect is a `data:` URL** (`BlockerResult::redirect`, base64), set only when a `redirect=` rule matched. It applies only when `should_block()`.
- **A procedural operator inside `:has()`** (*found in Task 1*): 63 EasyList rules nest `:has-text` in `:has()`, which adblock-rust passes through as CSS into `hide_selectors`. The bridge moves them to `procedural_filters` as one `css-selector` (Task 2); `procedural.js`'s `css-selector` step parses `:has()` with a procedural selector inside (Task 7).
- **The executor's API:** gin functions in the isolated world: `filters()` (the document's procedural rules), and `hide(attribute)` and `style(attribute, declarations)`, which insert user-origin rules for a marker attribute, checked again in the renderer.

**Conventions** (as in the earlier sub-projects):
- **Edit in webops, build in `chromium/src`.** `python <scratchpad>\sync.py` copies changed files into `src/ghost` with new mtimes. Commit in webops; then `git -C chromium/src/ghost pull` (after `git checkout -- .`), and compare `src/ghost` with webops.
- **Build and test commands:**
  - Build in `chromium\src` with depot_tools first on PATH: `autoninja -C out\vanilla <targets>`.
  - Unit tests: `out\vanilla\ghost_unittests.exe --gtest_filter=...`.
  - Browser tests run **only on the hidden desktop**: `powershell -File <scratchpad>\hidden.ps1 -CommandLine 'cmd.exe /c "out\vanilla\ghost_browsertests.exe --gtest_filter=... > <scratchpad>\bt.log 2>&1"'`, then read the log.
- **Every new file starts with the MPL header** (`tools/lint.py` checks `.js`, `.cc`, `.h`, `.rs`, `.mojom`, `.gn`, `.py`). Code below leaves it out, except where a file's first lines matter.
- **Never chain `git commit` after a test run** in one command. Stage before `python tools\lint.py`, since lint sees only tracked files.
- **Commit messages carry no AI trailer;** the user is the only author.
- **Scriptlets are written clean.** Write them from the behaviors this plan states, never from uBlock Origin's `scriptlets.js` (GPL-3.0). Don't open that file, and don't open AdGuard's scriptlets either.

---

### Task 0: Tools and state

- [ ] **Step 1:** Copy `hidden.ps1`, `sync.py`, `build.ps1` and `bt.ps1` from `C:\Users\robyv\AppData\Local\Temp\claude\C--Users-robyv-Desktop-DLU-webops\b353c0cb*\scratchpad\` into this session's scratchpad. If they're gone, recreate them as Task 0 of [4A's plan](2026-10-11-cosmetic-filtering.md) describes.
- [ ] **Step 2:** Check the state:
  - `git -C chromium/src branch --show-current` prints `ghost/152.0.7977.158`;
  - `python tools\patches.py check` passes;
  - `autoninja -C out\vanilla ghost_unittests ghost_browsertests` has no work, or only a little.

---

### Task 1: CSS validation in adblock-rust (spike points 4 and 5)

**Files:**
- Modify: `third_party/rust/Cargo.toml` (the feature and its comment)
- Create (generated): `third_party/rust/<crate>/v<epoch>/` for each new crate; `third_party/rust/Cargo.lock`, `notice_dirs.gni`, `//ghost/build/credits.gni` as the tool rewrites them
- Modify: `third_party/rust/adblock/v0_13/BUILD.gn` (regenerated: `features`, `deps`)
- Create: `docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md`

- [ ] **Step 1: The feature.** In `third_party/rust/Cargo.toml` replace the adblock line and its comment:

```toml
# The blocking engine (docs/adr/0006-blocking-engine-adblock-rust.md). No
# public-suffix crates (Chromium resolves registrable domains). CSS validation
# (Phase 4B) parses procedural rules into operators and refuses invalid
# selectors and :style() values.
adblock = { version = "=0.13.3", default-features = false, features = ["full-regex-handling", "css-validation"] }
```

- [ ] **Step 2: Pin and vendor.** Run `python tools\rust_vendor.py pin --src chromium\src`, then `python tools\rust_vendor.py vendor --src chromium\src`. Expected: new crate directories for cssparser, cssparser-macros, selectors, servo_arc, phf and its family, derive_more, fxhash, dtoa-short, new_debug_unreachable and whatever the graph needs. Crates Chromium already has (smallvec, bitflags, itoa, log, syn, quote, proc-macro2, …) are pinned to Chromium's versions, not copied.
  - If `vendor` stops naming a Chromium crate that lacks a feature, add the feature in a new patch (0043) in `chromium/src`. Don't amend 0028: rewriting an early patch makes siso rebuild everything after it.
  - If a crate's build script writes files (selectors' `build.rs` generates its attribute table with `phf_codegen`), add `build_script_outputs` for it in `vendor_config.toml` and run `vendor` again. The tool's error names the crate.
  - If a crate ships no license file, add `license_files` in `vendor_config.toml` with the text from its repository under `third_party/rust/licenses/<crate>/`, as for seahash.
- [ ] **Step 3: Licenses.** For each new crate, `README.chromium` names its license. Allowed: MPL-2.0, MIT, Apache-2.0, BSD ([licensing.md](../../licensing.md)). Any other license: stop and report to the user.
- [ ] **Step 4: Tooling tests.** `python -m unittest discover -s tools/tests -t tools`. Expected: pass (`test_rust_vendor` checks that the generated files agree).
- [ ] **Step 5: Build.** `python <scratchpad>\sync.py`, then `autoninja -C out\vanilla ghost_unittests ghost_blocking_perftests`. Expected: builds. A crate that fails to compile under Chromium's flags gets `rustflags` in `vendor_config.toml`, or its build script turned off as flatbuffers' is. Record which.
- [ ] **Step 6: The existing tests.** `out\vanilla\ghost_unittests.exe --gtest_filter=EngineBridge*:BlockingEngine*:CosmeticFilters*`. Expected: pass. A 4A test whose list holds a selector the validation now refuses is a finding: record it, then change the test's list to a valid selector.
- [ ] **Step 7: Count what parses (spike point 4).** Add a temporary test to `components/blocking/engine_bridge_unittest.cc`. Don't commit it. It builds the engine from the shipped lists, and for each host named by a `#?#` or `##+js` rule in the lists it prints `cosmetic_resources("https://<host>/").procedural_actions` (log the JSON from Rust with a temporary bridge function, or count from C++ once Task 2 adds the field). Record in the progress notes:
  - how many of the 294 procedural rules arrive, by operator;
  - what `deliveroo.*#?#a[role="button"]:has(p:has-text(Sponsored))` becomes (a procedural operator nested inside `:has()`);
  - the host with the most procedural rules and the host with the most scriptlets (Task 9 uses both);
  - how many selectors the shipped lists' `hide_selectors` lost compared with Step 6's build before the feature (count over the perftest corpus pages).

  Then remove the temporary code.
- [ ] **Step 8: The engine's cost (spike point 5).** `out\vanilla\ghost_blocking_perftests.exe`. Record its log lines (dev build: compared only with the dev build's 4A numbers in the 4A progress notes). The official numbers come in Task 9.
- [ ] **Step 9: Progress notes.** Create `docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md`: the date; the crates added and their licenses; anything set in `vendor_config.toml`; Steps 6–8's findings.
- [ ] **Step 10: Commit.**

```bash
git add third_party/rust build/credits.gni docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md
python tools/lint.py
git commit -m "blocking: CSS validation in adblock-rust, for procedural rules"
```

---

### Task 2: The resources and the bridge

**Files:**
- Create: `components/blocking/resources/scriptlets/shade-safe.js`, `shade-chain.js`, `shade-trap.js`, `set-constant.js` (the others come in Task 6)
- Create: `components/blocking/resources/redirects/noop.js` (the surrogates come in Task 8)
- Create: `components/blocking/resources/README.md`
- Create: `components/blocking/rust/resources.rs`
- Modify: `components/blocking/rust/lib.rs`, `components/blocking/rust/BUILD.gn`
- Modify: `components/blocking/blocking_engine.{h,cc}`
- Test: `components/blocking/engine_bridge_unittest.cc`, `components/blocking/blocking_engine_unittest.cc`

- [ ] **Step 1: Failing bridge tests.** Append to `engine_bridge_unittest.cc`:

```cpp
// Phase 4B: scriptlets, procedural and action rules, redirects.

std::string Script(const Engine& engine, const char* url) {
  return std::string(engine.cosmetic_resources(url).injected_script);
}

TEST(EngineBridgeTest, ScriptletsComeWithTheirSite) {
  rust::Box<Engine> engine = new_engine("a.test##+js(set, shadeValue, true)\n");
  const std::string script = Script(*engine, "https://a.test/");
  EXPECT_NE(script.find("function setConstant("), std::string::npos) << script;
  EXPECT_NE(script.find("function shadeSafe("), std::string::npos) << script;
  EXPECT_NE(script.find("setConstant(\"shadeValue\", \"true\")"), std::string::npos) << script;
  EXPECT_TRUE(Script(*engine, "https://b.test/").empty());
}

TEST(EngineBridgeTest, ArgumentsStayStrings) {
  // A list's argument is a JSON string literal in the call, never code.
  rust::Box<Engine> engine = new_engine("a.test##+js(set, a\"b, true)\n");
  EXPECT_NE(Script(*engine, "https://a.test/").find("setConstant(\"a\\\"b\", \"true\")"),
            std::string::npos);
}

TEST(EngineBridgeTest, TheLicenseHeaderIsntInjected) {
  rust::Box<Engine> engine = new_engine("a.test##+js(set, shadeValue, true)\n");
  EXPECT_EQ(Script(*engine, "https://a.test/").find("Mozilla Public License"),
            std::string::npos);
}

TEST(EngineBridgeTest, AScriptletWeDontHaveIsSkipped) {
  rust::Box<Engine> engine = new_engine("a.test##+js(json-edit, a)\n");
  EXPECT_TRUE(Script(*engine, "https://a.test/").empty());
}

TEST(EngineBridgeTest, AScriptletExceptionTurnsThemOff) {
  rust::Box<Engine> engine =
      new_engine("a.test##+js(set, shadeValue, true)\na.test#@#+js()\n");
  EXPECT_TRUE(Script(*engine, "https://a.test/").empty());
}

TEST(EngineBridgeTest, ProceduralRulesComeAsJson) {
  rust::Box<Engine> engine = new_engine("a.test#?#.box:has-text(Sponsored)\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://a.test/");
  ASSERT_EQ(resources.procedural_filters.size(), 1u);
  const std::string json(resources.procedural_filters[0]);
  EXPECT_NE(json.find("\"has-text\""), std::string::npos) << json;
  EXPECT_NE(json.find("Sponsored"), std::string::npos) << json;
}

TEST(EngineBridgeTest, ActionsInPlainCssComeAsStyleRules) {
  rust::Box<Engine> engine = new_engine("a.test##.box:style(color: red)\n");
  const CosmeticResources resources = engine->cosmetic_resources("https://a.test/");
  ASSERT_EQ(resources.style_rules.size(), 1u);
  EXPECT_EQ(std::string(resources.style_rules[0].selector), ".box");
  EXPECT_EQ(std::string(resources.style_rules[0].style), "color: red");
  EXPECT_TRUE(resources.procedural_filters.empty());
}

TEST(EngineBridgeTest, ARedirectComesAsADataUrl) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$script,redirect=noopjs\n");
  const Verdict verdict = engine->check("https://tracker.test/t.js", "https://a.test/",
                                        "script", "GET");
  EXPECT_TRUE(verdict.blocked);
  EXPECT_TRUE(std::string(verdict.redirect).starts_with("data:application/javascript;base64,"))
      << std::string(verdict.redirect);
}

TEST(EngineBridgeTest, ARedirectToAResourceWeDontHaveJustBlocks) {
  rust::Box<Engine> engine = new_engine("||tracker.test^$script,redirect=nothing.js\n");
  const Verdict verdict = engine->check("https://tracker.test/t.js", "https://a.test/",
                                        "script", "GET");
  EXPECT_TRUE(verdict.blocked);
  EXPECT_TRUE(verdict.redirect.empty());
}

TEST(EngineBridgeTest, ShippedResourcesHaveNoProblem) {
  for (const rust::String& problem : resource_problems()) {
    ADD_FAILURE() << std::string(problem);
  }
}

TEST(EngineBridgeTest, OnlyFunctionStyleScriptletsAreAccepted) {
  // A template pastes a list's argument into code: refused.
  EXPECT_FALSE(resource_problem_for_testing("x.js", "application/javascript",
                                            "alert({{1}})", false)
                   .empty());
  // A scriptlet adblock-rust can't call by name: refused.
  EXPECT_FALSE(resource_problem_for_testing("x.js", "fn/javascript", "(function () {})();", true)
                   .empty());
  EXPECT_TRUE(resource_problem_for_testing("x.js", "fn/javascript",
                                           "// header\n\nfunction shadeX(a) {}\n", true)
                  .empty());
  // A redirect needn't be a function.
  EXPECT_TRUE(resource_problem_for_testing("x.js", "application/javascript",
                                           "(function () {})();", false)
                  .empty());
}
```

- [ ] **Step 2: The engine's fields, failing tests.** Append to `blocking_engine_unittest.cc`:

```cpp
TEST_F(BlockingEngineTest, CosmeticRulesCarryScriptletsAndProceduralRules) {
  BlockingEngine engine;
  engine.Load({"a.test##+js(set, shadeValue, true)\n"
               "a.test#?#.box:has-text(Ad)\n"
               "a.test##.styled:style(color: red)\n"});
  base::test::TestFuture<CosmeticRules> rules;
  engine.GetCosmeticRules(GURL("https://a.test/"), rules.GetCallback());
  EXPECT_NE(rules.Get().scriptlet.find("setConstant("), std::string::npos);
  EXPECT_EQ(rules.Get().procedural_filters.size(), 1u);
  ASSERT_EQ(rules.Get().style_rules.size(), 1u);
  EXPECT_EQ(rules.Get().style_rules[0].selector, ".styled");
}

TEST_F(BlockingEngineTest, ADecisionCarriesItsRedirect) {
  BlockingEngine engine;
  engine.Load({"||tracker.test^$script,redirect=noopjs\n"});
  const Decision decision = Check(engine, TrackerScript());
  EXPECT_TRUE(decision.blocked);
  EXPECT_TRUE(decision.redirect.starts_with("data:application/javascript;base64,"));
}
```

- [ ] **Step 3: Run them to see them fail.** `autoninja -C out\vanilla ghost_unittests` fails to compile: `injected_script`, `style_rules`, `procedural_filters`, `redirect`, `resource_problems`, `CosmeticRules::scriptlet` and so on don't exist yet. That is the expected failure.
- [ ] **Step 4: The first resources.** `components/blocking/resources/scriptlets/shade-safe.js` (MPL header, then):

```js
// The natives Shade's scriptlets rely on, taken at the document's start,
// before any of the page's scripts can replace them. Scriptlets run in the
// page's own world (CosmeticFiltersAgent), all inside one function, so
// nothing here is left on window. Code that runs later, when the page
// touches a trapped property, calls only what is taken here.
function shadeSafe() {
  if (shadeSafe.natives) {
    return shadeSafe.natives;
  }
  const apply = Reflect.apply;
  const exec = RegExp.prototype.exec;
  const test = RegExp.prototype.test;
  const getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
  const getPrototypeOf = Object.getPrototypeOf;
  const weakMapGet = WeakMap.prototype.get;
  const weakMapSet = WeakMap.prototype.set;
  const addEventListener = EventTarget.prototype.addEventListener;
  const promiseResolve = Promise.resolve;
  const natives = {
    apply,
    defineProperty: Object.defineProperty,
    getOwnPropertyDescriptor,
    hasOwn: Object.hasOwn,
    Error,
    ReferenceError,
    Proxy,
    Promise,
    Response,
    Request,
    URL,
    Event,
    MutationObserver,
    HTMLScriptElement,
    setTimeout,
    addEventListener,
    dispatchEvent: EventTarget.prototype.dispatchEvent,
    currentScript: getOwnPropertyDescriptor(Document.prototype, 'currentScript').get,
    scriptSrc: getOwnPropertyDescriptor(HTMLScriptElement.prototype, 'src').get,
    textContent: getOwnPropertyDescriptor(Node.prototype, 'textContent'),
    cookie: getOwnPropertyDescriptor(Document.prototype, 'cookie'),
    documentUrl: document.URL,
    exec(re, text) {
      return apply(exec, re, [String(text)]);
    },
    test(re, text) {
      return apply(test, re, [String(text)]);
    },
    weakMapGet(map, key) {
      return apply(weakMapGet, map, [key]);
    },
    weakMapSet(map, key, value) {
      apply(weakMapSet, map, [key, value]);
    },
    resolved(value) {
      return apply(promiseResolve, Promise, [value]);
    },
    // Whether a list's pattern is a regular expression: /source/flags.
    isRegExp(text) {
      return apply(test, /^\/.+\/[imsu]*$/, [text]);
    },
    // A list's pattern: /source/flags is a regular expression, anything else
    // a literal substring. An invalid expression matches nothing.
    pattern(text) {
      const parts = apply(exec, /^\/(.+)\/([imsu]*)$/, [text]);
      if (parts) {
        try {
          return new RegExp(parts[1], parts[2]);
        } catch {
          return /(?!)/;
        }
      }
      return new RegExp(text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
    },
    // The descriptor `name` has on `owner` or its nearest prototype.
    descriptor(owner, name) {
      for (let object = owner; object; object = getPrototypeOf(object)) {
        const found = getOwnPropertyDescriptor(object, name);
        if (found) {
          return found;
        }
      }
      return undefined;
    },
    // Keeps the exceptions a scriptlet throws on purpose (they carry
    // `marker`) out of the console.
    quiet(marker) {
      apply(addEventListener, window, ['error', event => {
        if (typeof event.message === 'string' && event.message.includes(marker)) {
          event.preventDefault();
        }
      }]);
    },
    // A request filter (no-xhr-if, no-fetch-if): `url:<pattern>`,
    // `method:<pattern>` or a bare pattern for the URL, separated by spaces;
    // all must match. With no condition, nothing matches.
    requestMatcher(props) {
      const conditions = [];
      for (const token of props.split(/\s+/)) {
        if (!token) {
          continue;
        }
        const colon = token.indexOf(':');
        const key = colon > 0 ? token.slice(0, colon) : '';
        if (key === 'url' || key === 'method') {
          conditions.push([key, natives.pattern(token.slice(colon + 1))]);
        } else {
          conditions.push(['url', natives.pattern(token)]);
        }
      }
      return request => {
        if (conditions.length === 0) {
          return false;
        }
        for (let i = 0; i < conditions.length; i++) {
          if (!natives.test(conditions[i][1], request[conditions[i][0]])) {
            return false;
          }
        }
        return true;
      };
    },
  };
  shadeSafe.natives = natives;
  return natives;
}
```

`shade-chain.js`:

```js
// Calls found(owner, name) once a property chain (`a.b.c`, from window)
// leads to the object holding its last name: at once when `a.b` exists, or
// when the page assigns the missing link, as often as it does.
function shadeChain(chain, found) {
  const safe = shadeSafe();
  const isObject = value =>
    (typeof value === 'object' && value !== null) || typeof value === 'function';
  const walk = (owner, path) => {
    const dot = path.indexOf('.');
    if (dot === -1) {
      found(owner, path);
      return;
    }
    const name = path.slice(0, dot);
    const rest = path.slice(dot + 1);
    const value = owner[name];
    if (isObject(value)) {
      walk(value, rest);
      return;
    }
    const descriptor = safe.getOwnPropertyDescriptor(owner, name);
    if (descriptor && !descriptor.configurable) {
      return;
    }
    let stored = value;
    safe.defineProperty(owner, name, {
      configurable: true,
      enumerable: true,
      get() {
        return stored;
      },
      set(next) {
        stored = next;
        if (isObject(next)) {
          walk(next, rest);
        }
      },
    });
  };
  if (chain) {
    walk(window, chain);
  }
}
```

`shade-trap.js`:

```js
// Calls check() whenever the page reads or writes the property at the end of
// a chain; check() throws to stop the script that touched it. Otherwise the
// property behaves as before, inherited accessors included.
function shadeTrap(chain, check) {
  const safe = shadeSafe();
  shadeChain(chain, (owner, name) => {
    const own = safe.getOwnPropertyDescriptor(owner, name);
    if (own && !own.configurable) {
      return;
    }
    const descriptor = safe.descriptor(owner, name);
    const accessor = descriptor && (descriptor.get || descriptor.set);
    let value = descriptor && !accessor ? descriptor.value : undefined;
    safe.defineProperty(owner, name, {
      configurable: true,
      enumerable: own ? own.enumerable : true,
      get() {
        check();
        return accessor ? (descriptor.get ? safe.apply(descriptor.get, this, []) : undefined)
                        : value;
      },
      set(next) {
        check();
        if (!accessor) {
          value = next;
        } else if (descriptor.set) {
          safe.apply(descriptor.set, this, [next]);
        }
      },
    });
  });
}
```

`set-constant.js`:

```js
// set-constant (alias set): the property at the end of a chain reads as a
// fixed, harmless value, and the page's assignments to it are ignored.
// Values: undefined, null, true, false, '' ('' and "" too), noopFunc,
// trueFunc, falseFunc, throwFunc, {}, [], emptyObj, emptyArr, and integers
// from -32767 to 32767. Any other value leaves the page alone.
function setConstant(chain = '', raw = '') {
  const safe = shadeSafe();
  const named = {
    undefined: () => undefined,
    null: () => null,
    true: () => true,
    false: () => false,
    '': () => '',
    "''": () => '',
    '""': () => '',
    noopFunc: () => function () {},
    trueFunc: () => () => true,
    falseFunc: () => () => false,
    throwFunc: () => () => {
      throw new safe.Error();
    },
    '{}': () => ({}),
    '[]': () => [],
    emptyObj: () => ({}),
    emptyArr: () => [],
  };
  let value;
  if (safe.hasOwn(named, raw)) {
    value = named[raw]();
  } else if (safe.test(/^-?\d{1,5}$/, raw) && Math.abs(Number(raw)) <= 32767) {
    value = Number(raw);
  } else {
    return;
  }
  shadeChain(chain, (owner, name) => {
    const descriptor = safe.getOwnPropertyDescriptor(owner, name);
    if (descriptor && !descriptor.configurable) {
      return;
    }
    safe.defineProperty(owner, name, {
      configurable: true,
      enumerable: descriptor ? descriptor.enumerable : true,
      get() {
        return value;
      },
      set() {},
    });
  });
}
```

`components/blocking/resources/redirects/noop.js`:

```js
// A script that does nothing: the answer to a blocked script a page needs to
// load without an error (redirect=noopjs).
(function () {})();
```

- [ ] **Step 5: `resources.rs`.**

```rust
//! Shade's scriptlet and redirect resources (components/blocking/resources/,
//! MPL-2.0, written for Shade: docs/licensing.md), built into the binary.
//! The engine gets only those without a problem; a test checks there is none.

use adblock::resources::{MimeType, PermissionMask, Resource, ResourceType};
use base64::{engine::Engine as _, prelude::BASE64_STANDARD};

pub struct Entry<'a> {
    pub name: &'a str,
    pub aliases: &'a [&'a str],
    pub mime: &'a str,
    pub dependencies: &'a [&'a str],
    /// Injected by `+js(...)` rules, or a dependency of one: must be
    /// function-style.
    pub scriptlet: bool,
    pub body: &'a [u8],
}

/// The smallest transparent GIF.
const GIF_1X1: &[u8] = &[
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x01, 0x44, 0x00, 0x3b,
];

const SAFE: &str = "shade-safe.fn";
const CHAIN: &str = "shade-chain.fn";
const TRAP: &str = "shade-trap.fn";

/// Scriptlets under uBlock Origin's names and aliases, which the lists use;
/// redirects under theirs.
const SHIPPED: &[Entry<'static>] = &[
    Entry {
        name: SAFE,
        aliases: &[],
        mime: "fn/javascript",
        dependencies: &[],
        scriptlet: true,
        body: include_bytes!("../resources/scriptlets/shade-safe.js"),
    },
    Entry {
        name: CHAIN,
        aliases: &[],
        mime: "fn/javascript",
        dependencies: &[SAFE],
        scriptlet: true,
        body: include_bytes!("../resources/scriptlets/shade-chain.js"),
    },
    Entry {
        name: TRAP,
        aliases: &[],
        mime: "fn/javascript",
        dependencies: &[SAFE, CHAIN],
        scriptlet: true,
        body: include_bytes!("../resources/scriptlets/shade-trap.js"),
    },
    Entry {
        name: "set-constant.js",
        aliases: &["set.js"],
        mime: "fn/javascript",
        dependencies: &[SAFE, CHAIN],
        scriptlet: true,
        body: include_bytes!("../resources/scriptlets/set-constant.js"),
    },
    Entry {
        name: "noop.js",
        aliases: &["noopjs"],
        mime: "application/javascript",
        dependencies: &[],
        scriptlet: false,
        body: include_bytes!("../resources/redirects/noop.js"),
    },
    Entry {
        name: "noop.txt",
        aliases: &["nooptext"],
        mime: "text/plain",
        dependencies: &[],
        scriptlet: false,
        body: b"",
    },
    Entry {
        name: "noop.json",
        aliases: &["noopjson"],
        mime: "application/json",
        dependencies: &[],
        scriptlet: false,
        body: b"{}",
    },
    Entry {
        name: "1x1.gif",
        aliases: &["1x1-transparent.gif"],
        mime: "image/gif",
        dependencies: &[],
        scriptlet: false,
        body: GIF_1X1,
    },
];

fn is_javascript(mime: &str) -> bool {
    mime == "application/javascript" || mime == "fn/javascript"
}

/// The code without the license header: adblock-rust calls a scriptlet by
/// name only when its text starts `function <name>(`.
fn code(text: &str) -> &str {
    let mut rest = text;
    while let Some(line) = rest.lines().next() {
        let trimmed = line.trim();
        if !(trimmed.is_empty() || trimmed.starts_with("//")) {
            break;
        }
        rest = rest[line.len()..].trim_start_matches(['\r', '\n']);
    }
    rest
}

/// adblock-rust's own test for a function-style resource
/// (resource_storage.rs, extract_function_name).
fn is_function_style(code: &str) -> bool {
    let Some(rest) = code.strip_prefix("function") else { return false };
    if !rest.starts_with(char::is_whitespace) {
        return false;
    }
    let rest = rest.trim_start();
    let Some(end) = rest.find(|c: char| "(){}".contains(c) || c.is_whitespace()) else {
        return false;
    };
    end > 0 && rest[end..].trim_start().starts_with('(')
}

/// What's wrong with a resource, if anything.
pub fn problem(entry: &Entry, all: &[Entry]) -> Option<String> {
    let name = entry.name;
    if is_javascript(entry.mime) {
        let Ok(text) = std::str::from_utf8(entry.body) else {
            return Some(format!("{name}: not UTF-8"));
        };
        let code = code(text);
        // adblock-rust pastes a template's arguments into its code.
        if (1..=9).any(|i| code.contains(&format!("{{{{{i}}}}}"))) {
            return Some(format!("{name}: a template placeholder"));
        }
        if entry.scriptlet && !is_function_style(code) {
            return Some(format!("{name}: not function-style"));
        }
    } else if entry.scriptlet {
        return Some(format!("{name}: a scriptlet must be JavaScript"));
    }
    entry
        .dependencies
        .iter()
        .find(|dependency| !all.iter().any(|other| other.name == **dependency))
        .map(|missing| format!("{name}: no resource {missing}"))
}

pub fn problems() -> Vec<String> {
    SHIPPED.iter().filter_map(|entry| problem(entry, SHIPPED)).collect()
}

fn to_resource(entry: &Entry) -> Resource {
    let body = if is_javascript(entry.mime) {
        // problem() has checked it is UTF-8.
        code(std::str::from_utf8(entry.body).unwrap_or_default()).as_bytes()
    } else {
        entry.body
    };
    Resource {
        name: entry.name.to_string(),
        aliases: entry.aliases.iter().map(|alias| alias.to_string()).collect(),
        kind: ResourceType::Mime(MimeType::from(entry.mime)),
        content: BASE64_STANDARD.encode(body),
        dependencies: entry.dependencies.iter().map(|d| d.to_string()).collect(),
        permission: PermissionMask::default(),
    }
}

/// The resources the engine gets.
pub fn usable() -> Vec<Resource> {
    SHIPPED.iter().filter(|entry| problem(entry, SHIPPED).is_none()).map(to_resource).collect()
}
```

Check at the pin that `MimeType` implements `From<&str>` (`grep -n "impl From<&str> for MimeType" third_party/rust/adblock/v0_13/crate/src/resources/mod.rs`) and that `PermissionMask` is exported from `adblock::resources`. If either isn't, match the crate's actual API.

- [ ] **Step 6: The bridge.** In `lib.rs`:
  - add `mod resources;` and `use adblock::cosmetic_filter_cache::ProceduralOrActionFilter;`;
  - in `mod ffi`, extend `Verdict` and `CosmeticResources`, add `StyleRule`, and declare the two new functions:

```rust
    /// Whether a request is blocked, and by which rule (for the console).
    struct Verdict {
        blocked: bool,
        filter: String,
        /// When a redirect rule blocked the request: the resource to answer
        /// with instead, as a data: URL. Empty otherwise.
        redirect: String,
    }

    /// An action rule CSS can express: a plain selector and its :style().
    /// (Not StyleRule: //ghost/components/blocking has one, in the same
    /// namespace.)
    struct CosmeticStyleRule {
        selector: String,
        style: String,
    }

    /// A document's cosmetic rules: what to hide on its site, what the
    /// generic rules mustn't hide there, and (4B) what to run in it.
    struct CosmeticResources {
        hide_selectors: Vec<String>,
        exceptions: Vec<String>,
        /// A `$generichide` exception: no generic rule on this page.
        generichide: bool,
        /// The site's scriptlets as one script, for the page's own world.
        injected_script: String,
        style_rules: Vec<CosmeticStyleRule>,
        /// Procedural and action rules CSS can't express: adblock-rust's
        /// JSON, one per rule.
        procedural_filters: Vec<String>,
    }
```

and in `extern "Rust"`:

```rust
        /// Problems in the resources built into Shade; none is expected.
        fn resource_problems() -> Vec<String>;
        /// The problem a resource would have, or an empty string.
        fn resource_problem_for_testing(
            name: &str,
            mime: &str,
            body: &str,
            scriptlet: bool,
        ) -> String;
```

  - in `new_engine`, give the engine the resources and update the comment:

```rust
    // Every rule type: network rules for requests, cosmetic rules, scriptlets
    // and procedural rules for pages (Phases 4A, 4B).
    filters.add_filter_list(lists.to_string(), ParseOptions::default());
    let mut engine = adblock::Engine::new_with_filter_set(filters);
    engine.use_resources(resources::usable());
    Box::new(Engine(engine))
```

  - `check` sets the redirect only when the request is blocked:

```rust
            Ok(request) => {
                let result = self.0.check_network_request(&request);
                let blocked = result.should_block();
                ffi::Verdict {
                    blocked,
                    filter: result.filter.map(|f| format!("{f:?}")).unwrap_or_default(),
                    redirect: if blocked { result.redirect.unwrap_or_default() } else { String::new() },
                }
            }
            Err(_) => ffi::Verdict { blocked: false, filter: String::new(), redirect: String::new() },
```

  - `cosmetic_resources` splits the procedural actions:

```rust
    fn cosmetic_resources(&self, url: &str) -> ffi::CosmeticResources {
        let resources = self.0.url_cosmetic_resources(url);
        let mut style_rules = Vec::new();
        let mut procedural_filters = Vec::new();
        for json in resources.procedural_actions {
            match serde_json::from_str::<ProceduralOrActionFilter>(&json)
                .ok()
                .and_then(|filter| filter.as_css())
            {
                Some((selector, style)) => style_rules.push(ffi::CosmeticStyleRule { selector, style }),
                None => procedural_filters.push(json),
            }
        }
        style_rules.sort_by(|a, b| (&a.selector, &a.style).cmp(&(&b.selector, &b.style)));
        procedural_filters.sort();
        ffi::CosmeticResources {
            hide_selectors: sorted(resources.hide_selectors),
            exceptions: sorted(resources.exceptions),
            generichide: resources.generichide,
            injected_script: resources.injected_script,
            style_rules,
            procedural_filters,
        }
    }
```

  - the free functions:

```rust
fn resource_problems() -> Vec<String> {
    resources::problems()
}

fn resource_problem_for_testing(name: &str, mime: &str, body: &str, scriptlet: bool) -> String {
    let entry = resources::Entry {
        name,
        aliases: &[],
        mime,
        dependencies: &[],
        scriptlet,
        body: body.as_bytes(),
    };
    resources::problem(&entry, &[]).unwrap_or_default()
}
```

Also update the module comment's first lines: "ask it about one request, or about a document's cosmetic rules, scriptlets and procedural rules".

- [ ] **Step 7: The Rust target.** In `components/blocking/rust/BUILD.gn`:

```gn
rust_static_library("engine") {
  allow_unsafe = true  # The FFI that cxx generates.
  crate_root = "lib.rs"
  sources = [
    "lib.rs",
    "resources.rs",
  ]
  # Built in with include_bytes! (resources.rs).
  inputs = [
    "../resources/redirects/noop.js",
    "../resources/scriptlets/set-constant.js",
    "../resources/scriptlets/shade-chain.js",
    "../resources/scriptlets/shade-safe.js",
    "../resources/scriptlets/shade-trap.js",
  ]
  cxx_bindings = [ "lib.rs" ]
  deps = [
    "//ghost/components/blocking:registrable_domain",
    "//ghost/third_party/rust/adblock/v0_13:lib",
    "//third_party/rust/base64/v0_22:lib",
    "//third_party/rust/serde_json/v1:lib",
  ]
  configs += [ "//ghost/build/config:rust_fuzz_coverage" ]
  visibility = [ "//ghost/*" ]
}
```

- [ ] **Step 8: `BlockingEngine`.** In `blocking_engine.h`, extend `Decision` and `CosmeticRules`, and add `StyleRule`:

```cpp
struct Decision {
  bool blocked = false;
  // The rule that matched, for the console; may be empty.
  std::string filter;
  // When a redirect rule blocked the request: what to answer with instead,
  // as a data: URL (Phase 4B). Empty otherwise.
  std::string redirect;
};

// An action rule CSS can express: a selector and its :style() declarations.
struct StyleRule {
  std::string selector;
  std::string style;
};
```

and in `CosmeticRules`, after `generichide`:

```cpp
  // The site's scriptlets as one script, for the page's own world (4B).
  std::string scriptlet;
  // Action rules CSS can express, and the procedural and action rules it
  // can't (adblock-rust's JSON, one per rule).
  std::vector<StyleRule> style_rules;
  std::vector<std::string> procedural_filters;
```

Update the struct's comment to `// A document's cosmetic rules (Phases 4A, 4B).` In `blocking_engine.cc`'s `Answer`, after `decision.filter = …`, add `decision.redirect = std::string(verdict.redirect);`. In `AnswerCosmeticRules`, after `rules.generichide = …`, add:

```cpp
      rules.scriptlet = std::string(resources.injected_script);
      for (const CosmeticStyleRule& rule : resources.style_rules) {
        rules.style_rules.push_back({std::string(rule.selector), std::string(rule.style)});
      }
      for (const rust::String& filter : resources.procedural_filters) {
        rules.procedural_filters.emplace_back(filter);
      }
```

- [ ] **Step 9: README.** `components/blocking/resources/README.md`:

```markdown
# Scriptlets and redirect resources

Shade's own resources for filter lists' `+js(...)` scriptlets and
`$redirect=` rules, under the MPL-2.0 like the rest of `//ghost`. They are
written for Shade from the behavior uBlock Origin documents publicly
(its wiki), under the names and argument orders the lists use, and never
from uBlock Origin's or AdGuard's code, which is GPL-3.0
([licensing.md](../../../docs/licensing.md#filter-lists-and-scriptlets)).

- `scriptlets/`: each scriptlet is one function (adblock-rust calls it by
  name, with each argument a JSON string). `shade-safe.js` takes the natives
  the others use before the page's scripts run; `shade-chain.js` and
  `shade-trap.js` follow property chains. The browser runs a document's
  scriptlets in its own world at its start, all inside one function.
- `redirects/`: what a blocked script gets instead of an error. The
  non-JavaScript resources (`noop.txt`, `noop.json`, `1x1.gif`) are bytes in
  `../rust/resources.rs`.

`../rust/resources.rs` lists every resource with its aliases; the bridge
strips each file's license header before the engine sees it. A unit test
(`EngineBridgeTest.ShippedResourcesHaveNoProblem`) refuses a scriptlet that
isn't function-style and any `{{N}}` template placeholder.
```

- [ ] **Step 10: Build and run.** `python <scratchpad>\sync.py`, then `autoninja -C out\vanilla ghost_unittests`. Then `out\vanilla\ghost_unittests.exe --gtest_filter=EngineBridge*:BlockingEngine*`. Expected: all pass.
- [ ] **Step 11: Commit.**

```bash
git add components/blocking
python tools/lint.py
git commit -m "blocking: scriptlet and redirect resources, and the bridge's 4B fields"
```

---

### Task 3: Checking style and procedural rules in the browser

**Files:**
- Modify: `components/cosmetic_filters/style_sheet.{h,cc}`, `style_sheet_unittest.cc`
- Create: `components/cosmetic_filters/procedural.{h,cc}`, `procedural_unittest.cc`
- Create: `components/cosmetic_filters/fuzz/procedural_fuzzer.cc`
- Modify: `components/cosmetic_filters/BUILD.gn`, `BUILD.gn` (the fuzzer in the top-level group)

- [ ] **Step 1: Failing tests.** Append to `style_sheet_unittest.cc`:

```cpp
TEST(StyleSheetTest, SafeStylesStayInTheirRule) {
  EXPECT_TRUE(IsSafeStyle("color: red"));
  EXPECT_TRUE(IsSafeStyle("position: absolute !important; top: -9999px !important"));
  EXPECT_FALSE(IsSafeStyle(""));
  for (const char* unsafe :
       {"color: red}body{display:none", "a{", "@import x", "color: red /* x */",
        "background: url(https://t.test/)", "background: URL(x)",
        "background: image-set(x 1x)", "content: \"\\7d\"", "color: red\n}"}) {
    EXPECT_FALSE(IsSafeStyle(unsafe)) << unsafe;
  }
}

TEST(StyleSheetTest, StyleRulesOnePerSelector) {
  EXPECT_EQ(StyleSheetFor(std::vector<StyledSelector>{{".a", "color: red"},
                                                      {".b{", "color: red"},
                                                      {".c", "x: y}"},
                                                      {".d", "margin: 0"}}),
            ".a{color: red}\n.d{margin: 0}\n");
}
```

`procedural_unittest.cc`:

```cpp
#include "ghost/components/cosmetic_filters/procedural.h"

#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace ghost::cosmetic_filters {
namespace {

TEST(ProceduralTest, KeepsWellFormedRules) {
  const std::vector<std::string> rules = {
      R"({"selector":[{"type":"css-selector","arg":".a"},{"type":"has-text","arg":"Ad"}]})",
      R"({"selector":[{"type":"css-selector","arg":".a"}],"action":{"type":"remove"}})",
      R"({"selector":[{"type":"css-selector","arg":".a"},{"type":"upward","arg":"2"}],)"
      R"("action":{"type":"style","arg":"color: red"}})",
  };
  EXPECT_EQ(SafeProceduralFilters(rules), rules);
}

TEST(ProceduralTest, DropsWhatCouldEscape) {
  EXPECT_TRUE(SafeProceduralFilters({
                  "not json",
                  "[]",
                  R"({"selector":[]})",
                  R"({"action":{"type":"remove"}})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],"action":{}})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],)"
                  R"("action":{"type":"style","arg":"x}body{display:none"}})",
                  R"({"selector":[{"type":"css-selector","arg":".a"}],)"
                  R"("action":{"type":"style"}})",
              })
                  .empty());
}

TEST(ProceduralTest, MarkerAttributes) {
  EXPECT_TRUE(IsMarkerAttribute("shade-0-0123456789abcdef"));
  for (const char* name : {"", "shade-", "class", "shade-A", "shade-a b", "shade-a]",
                           "shade-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}) {
    EXPECT_FALSE(IsMarkerAttribute(name)) << name;
  }
}

}  // namespace
}  // namespace ghost::cosmetic_filters
```

- [ ] **Step 2: Run them to see them fail.** `autoninja -C out\vanilla ghost_unittests` fails to compile: `IsSafeStyle`, `StyledSelector`, `procedural.h` are missing.
- [ ] **Step 3: `style_sheet.h`.** Add, after `StyleSheetFor`:

```cpp
// Whether a :style() action's declarations stay inside their rule: not
// empty; no {, }, @, comments, backslashes or newlines; no url( or
// image-set(, which would load. `;` separates declarations and is allowed.
bool IsSafeStyle(std::string_view declarations);

// An action rule CSS can express: a selector and its declarations.
struct StyledSelector {
  std::string selector;
  std::string style;
};

// One rule per pair, `selector{style}`; a pair whose selector or style isn't
// safe is dropped.
std::string StyleSheetFor(const std::vector<StyledSelector>& rules);
```

(add `#include <string_view>`). In `style_sheet.cc`:

```cpp
bool IsSafeStyle(std::string_view declarations) {
  if (declarations.empty()) {
    return false;
  }
  for (std::string_view unsafe : {"{", "}", "@", "/*", "*/", "\\", "\n", "\r", "\f"}) {
    if (declarations.find(unsafe) != std::string_view::npos) {
      return false;
    }
  }
  const std::string lower = base::ToLowerASCII(declarations);
  return lower.find("url(") == std::string::npos && lower.find("image-set(") == std::string::npos;
}

std::string StyleSheetFor(const std::vector<StyledSelector>& rules) {
  std::string sheet;
  for (const StyledSelector& rule : rules) {
    if (IsSafe(rule.selector) && IsSafeStyle(rule.style)) {
      base::StrAppend(&sheet, {rule.selector, "{", rule.style, "}\n"});
    }
  }
  return sheet;
}
```

(includes `base/strings/string_util.h`, `base/strings/strcat.h`).

- [ ] **Step 4: `procedural.h`.**

```cpp
#ifndef GHOST_COMPONENTS_COSMETIC_FILTERS_PROCEDURAL_H_
#define GHOST_COMPONENTS_COSMETIC_FILTERS_PROCEDURAL_H_

#include <string>
#include <string_view>
#include <vector>

namespace ghost::cosmetic_filters {

// The procedural and action rules (adblock-rust's JSON, one per rule) the
// renderer may run: each an object with a non-empty `selector` list, whose
// `action`, if any, has a `type`, and whose `style` action passes
// IsSafeStyle. The others are dropped.
std::vector<std::string> SafeProceduralFilters(const std::vector<std::string>& filters);

// The attributes procedural.js marks elements with: `shade-` then 1 to 64 of
// [a-z0-9-].
bool IsMarkerAttribute(std::string_view name);

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_COMPONENTS_COSMETIC_FILTERS_PROCEDURAL_H_
```

`procedural.cc`:

```cpp
#include "ghost/components/cosmetic_filters/procedural.h"

#include <optional>

#include "base/json/json_reader.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"

namespace ghost::cosmetic_filters {
namespace {

bool IsSafe(const std::string& filter) {
  std::optional<base::Value::Dict> rule = base::JSONReader::ReadDict(filter);
  if (!rule) {
    return false;
  }
  const base::Value::List* selector = rule->FindList("selector");
  if (!selector || selector->empty()) {
    return false;
  }
  const base::Value* action = rule->Find("action");
  if (!action) {
    return true;
  }
  if (!action->is_dict()) {
    return false;
  }
  const std::string* type = action->GetDict().FindString("type");
  if (!type) {
    return false;
  }
  if (*type != "style") {
    return true;
  }
  const std::string* style = action->GetDict().FindString("arg");
  return style && IsSafeStyle(*style);
}

}  // namespace

std::vector<std::string> SafeProceduralFilters(const std::vector<std::string>& filters) {
  std::vector<std::string> safe;
  for (const std::string& filter : filters) {
    if (IsSafe(filter)) {
      safe.push_back(filter);
    }
  }
  return safe;
}

bool IsMarkerAttribute(std::string_view name) {
  constexpr std::string_view kPrefix = "shade-";
  if (!name.starts_with(kPrefix) || name.size() <= kPrefix.size() ||
      name.size() > kPrefix.size() + 64) {
    return false;
  }
  for (char c : name.substr(kPrefix.size())) {
    if (!base::IsAsciiLower(c) && !base::IsAsciiDigit(c) && c != '-') {
      return false;
    }
  }
  return true;
}

}  // namespace ghost::cosmetic_filters
```

Check the pin's `JSONReader::ReadDict` signature (`grep -n "ReadDict" base/json/json_reader.h`) and match it.

- [ ] **Step 5: The fuzzer.** `components/cosmetic_filters/fuzz/procedural_fuzzer.cc`:

```cpp
// The browser's checks on procedural and style rules, with arbitrary input:
// from 3E, lists arrive between releases.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "ghost/components/cosmetic_filters/procedural.h"
#include "ghost/components/cosmetic_filters/style_sheet.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string input(reinterpret_cast<const char*>(data), size);
  ghost::cosmetic_filters::IsSafeStyle(input);
  ghost::cosmetic_filters::IsMarkerAttribute(input);
  ghost::cosmetic_filters::SafeProceduralFilters({input});
  return 0;
}
```

- [ ] **Step 6: BUILD.** `components/cosmetic_filters/BUILD.gn`: add `procedural.cc` and `procedural.h` to `cosmetic_filters`, and `procedural_unittest.cc` to `unit_tests`. Add `import("//testing/libfuzzer/fuzzer_test.gni")` and:

```gn
fuzzer_test("ghost_cosmetic_procedural_fuzzer") {
  sources = [ "fuzz/procedural_fuzzer.cc" ]
  deps = [ ":cosmetic_filters" ]
}
```

In the top-level `BUILD.gn`, add `"//ghost/components/cosmetic_filters:ghost_cosmetic_procedural_fuzzer"` beside the blocking fuzzers.

- [ ] **Step 7: Run.** `autoninja -C out\vanilla ghost_unittests`, then `out\vanilla\ghost_unittests.exe --gtest_filter=StyleSheet*:Procedural*`. Expected: pass.
- [ ] **Step 8: Commit.**

```bash
git add components/cosmetic_filters BUILD.gn
python tools/lint.py
git commit -m "cosmetic filters: checks for style and procedural rules"
```

---

### Task 4: The push and the main-world scriptlet (spike points 1 and 2)

**Files:**
- Modify: `common/cosmetic_filters.mojom`
- Modify: `browser/cosmetic_filters/prepared_rules.{h,cc}`, `cosmetic_filters_throttle.cc`, `cosmetic_filters_tab_helper.cc`, `BUILD.gn`
- Modify: `renderer/cosmetic_filters/cosmetic_filters_agent.{h,cc}`
- Create: `browser/cosmetic_filters/cosmetic_filters_test_base.{h,cc}` (4A's harness, shared)
- Modify: `browser/cosmetic_filters/cosmetic_filters_browsertest.cc` (uses the base)
- Create: `browser/cosmetic_filters/scriptlets_browsertest.cc`
- Modify: `BUILD.gn` (browser test sources)

- [ ] **Step 1: The shared harness.** Move 4A's fixture into `cosmetic_filters_test_base.h`:

```cpp
#ifndef GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TEST_BASE_H_
#define GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TEST_BASE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "net/test/embedded_test_server/http_request.h"
#include "url/gurl.h"

namespace content {
class ToRenderFrameHost;
class WebContents;
}  // namespace content

namespace net::test_server {
class HttpResponse;
}

namespace ghost::cosmetic_filters {

// Pages on every host name (host-resolver rules send them all to the test
// servers), a test list, and helpers to read what the page shows.
class CosmeticFiltersTestBase : public InProcessBrowserTest {
 protected:
  CosmeticFiltersTestBase();
  ~CosmeticFiltersTestBase() override;

  // The test's list.
  virtual std::string List() const = 0;
  // Called before the servers start: add the pages here.
  virtual void AddPages() = 0;

  // `html` inside <!doctype html><html><body>…</body></html> at `path`.
  void AddPage(std::string path, std::string html, base::StringPairs headers = {});
  // Any other resource, as is.
  void AddResource(std::string path, std::string content_type, std::string body,
                   base::StringPairs headers = {});

  void SetUpOnMainThread() override;

  GURL Url(std::string_view host, std::string_view path);
  GURL HttpsUrl(std::string_view host, std::string_view path);
  content::WebContents* Tab(Browser* browser = nullptr);
  // The element's computed display, at once.
  std::string Display(const content::ToRenderFrameHost& frame, std::string_view id);
  // The display of what `script` finds, once it is `expected`, or after 2 s.
  std::string WaitForDisplayOf(const content::ToRenderFrameHost& frame,
                               std::string_view script,
                               std::string_view expected);
  // The headers of the last request the servers got for `path`, if any.
  std::optional<net::test_server::HttpRequest::HeaderMap> LastRequestHeaders(
      std::string_view path);

 private:
  struct Resource {
    std::string content_type;
    std::string body;
    base::StringPairs headers;
  };

  std::unique_ptr<net::test_server::HttpResponse> Serve(
      const net::test_server::HttpRequest& request);
  void Record(const net::test_server::HttpRequest& request);

  // Filled before the servers start, read-only after.
  std::map<std::string, Resource> resources_;
  base::Lock lock_;
  std::map<std::string, net::test_server::HttpRequest::HeaderMap> requests_ GUARDED_BY(lock_);
};

}  // namespace ghost::cosmetic_filters

#endif  // GHOST_BROWSER_COSMETIC_FILTERS_COSMETIC_FILTERS_TEST_BASE_H_
```

  - **`cosmetic_filters_test_base.cc`:** move 4A's `Serve`, `Url`, `Tab`, `Display` and `WaitForDisplayOf` into the class unchanged, with these changes:
    - `Serve` looks the path up in `resources_` and adds each resource's headers;
    - `SetUpOnMainThread` calls `AddPages()` and registers `Serve`, plus `Record` as a request monitor (`RegisterRequestMonitor`), on both servers before starting them. Then it calls `SetListsForTesting({List()})`;
    - `HttpsUrl` uses `embedded_https_test_server()`;
    - `LastRequestHeaders` reads `requests_` under the lock.
  - **4A's test file:** `cosmetic_filters_browsertest.cc` becomes `class CosmeticFiltersBrowserTest : public CosmeticFiltersTestBase`. `List()` returns `kTestList`; `AddPages()` adds each page of 4A's map. The `/csp.html` case becomes `AddPage("/csp.html", <generic.html's html>, {{"Content-Security-Policy", "script-src 'none'"}})`.
- [ ] **Step 2: 4A's tests still pass.** Add the base to `BUILD.gn`'s browser test sources (`browser/cosmetic_filters/cosmetic_filters_test_base.cc`, `.h`), then build `ghost_browsertests` and run `--gtest_filter=CosmeticFiltersBrowserTest.*` on the hidden desktop. Expected: 12 pass. Commit:

```bash
git add browser/cosmetic_filters BUILD.gn
python tools/lint.py
git commit -m "cosmetic filters: a shared browser-test harness"
```

- [ ] **Step 3: Failing browser tests.** `scriptlets_browsertest.cc`:

```cpp
// Scriptlets through the browser, the renderer and the engine, with a test
// list (Phase 4B).

#include <string>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h"
#include "ghost/browser/privacy_policy/site_levels.h"

namespace ghost::cosmetic_filters {
namespace {

class ScriptletsBrowserTest : public CosmeticFiltersTestBase {
 protected:
  std::string List() const override {
    return "a.test##+js(set, shadeValue, noopFunc)\n"
           "a.test##+js(set, shade.nested.value, true)\n"
           "off.test##+js(set, shadeValue, noopFunc)\n"
           "except.test##+js(set, shadeValue, noopFunc)\n"
           "except.test#@#+js()\n";
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
  privacy_policy::SetLevel(browser()->profile(), Url("off.test", "/"),
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

}  // namespace
}  // namespace ghost::cosmetic_filters
```

  - **`APageCspDoesntStopIt`:** if `EvalJs` fails on this page, have the nonce'd script set `document.title` and read it with `Tab()->GetTitle()` instead.
  - **`SetLevel`:** use the setter 4A's `OffHidesNothing` uses, with its exact signature. Add the file to `BUILD.gn`'s browser test sources.
- [ ] **Step 4: Run them to see them fail.** Build, then run `--gtest_filter=ScriptletsBrowserTest.*` on the hidden desktop. Expected: `TheFirstScriptAlreadySeesTheScriptlet`, `APageCspDoesntStopIt`, `IncognitoRunsThem` and `AFrameItsParentWritesTakesTheParentsScriptlets` fail, with `seen` equal to "undefined". The others pass, since nothing runs yet.
- [ ] **Step 5: The mojom.**

```
module ghost.mojom;

// A document's cosmetic rules, pushed before it is parsed.
struct DocumentRules {
  // A user-origin style sheet (possibly empty): site rules, and action rules
  // CSS can express.
  string style_sheet;
  // Whether to scan the document for the generic rules.
  bool scan;
  // The site's scriptlets as one script, for the page's own world; may be
  // empty (Phase 4B).
  string scriptlet;
  // Procedural and action rules CSS can't express, adblock-rust's JSON, one
  // per rule, checked by the browser (SafeProceduralFilters).
  array<string> procedural_filters;
};

// Browser to renderer, per frame. Channel-associated and sent at
// ReadyToCommitNavigation, so it arrives before the document it is for, as
// subresource_filter's ActivateForNextCommittedLoad.
interface CosmeticFiltersAgent {
  // The rules of the document the frame commits next.
  SetRulesForNextDocument(DocumentRules rules);
};
```

(`CosmeticFiltersHost` unchanged.)

- [ ] **Step 6: The browser side.** `prepared_rules.h`:

```cpp
#include "ghost/common/cosmetic_filters.mojom.h"

// A navigation's document's rules, from the throttle to the tab helper.
class PreparedRules : public content::NavigationHandleUserData<PreparedRules> {
 public:
  ~PreparedRules() override;

  const mojom::DocumentRules& rules() const { return *rules_; }

 private:
  friend NavigationHandleUserData;
  PreparedRules(content::NavigationHandle& navigation, mojom::DocumentRulesPtr rules);

  const mojom::DocumentRulesPtr rules_;

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};
```

`prepared_rules.cc`: the constructor stores `std::move(rules)`. The throttle's Off branch becomes `PreparedRules::CreateForNavigationHandle(navigation, mojom::DocumentRules::New());`. `OnRules` becomes:

```cpp
void CosmeticFiltersThrottle::OnRules(blocking::CosmeticRules rules) {
  std::vector<StyledSelector> styled;
  for (blocking::StyleRule& rule : rules.style_rules) {
    styled.push_back({std::move(rule.selector), std::move(rule.style)});
  }
  auto document = mojom::DocumentRules::New();
  document->style_sheet = base::StrCat({StyleSheetFor(rules.hide_selectors), StyleSheetFor(styled)});
  document->scan = !rules.generichide;
  document->scriptlet = std::move(rules.scriptlet);
  document->procedural_filters = SafeProceduralFilters(rules.procedural_filters);
  PreparedRules::CreateForNavigationHandle(*navigation_handle(), std::move(document));
  Resume();
}
```

(includes `base/strings/strcat.h`, `ghost/components/cosmetic_filters/procedural.h`). The tab helper sends `agent->SetRulesForNextDocument(rules->rules().Clone());`.

- [ ] **Step 7: The agent.** In `cosmetic_filters_agent.h`:
  - `SetRulesForNextDocument(mojom::DocumentRulesPtr rules) override;`
  - `Rules` becomes `{ std::string style_sheet; bool scan = false; std::string scriptlet; std::vector<std::string> procedural_filters; }`;
  - add a private `void StartScripts();`, which `RunScriptsAtDocumentStart` and the constructor's posted task call instead of `StartScanner()`: it runs `RunScriptlet()`, then `StartScanner()` (Task 7 adds `StartProcedural()`);
  - add `void RunScriptlet();` and `bool scriptlet_ran_ = false;`.

In `cosmetic_filters_agent.cc`:

```cpp
// The scriptlets' name in DevTools and stack traces; abort-on-stack-trace
// leaves out frames under shade://.
constexpr char kScriptletsUrl[] = "shade://scriptlets/";
```

```cpp
// static
void CosmeticFiltersAgent::RunScriptsAtDocumentStart(content::RenderFrame* render_frame) {
  if (CosmeticFiltersAgent* agent = Get(render_frame)) {
    agent->StartScripts();
  }
}

void CosmeticFiltersAgent::SetRulesForNextDocument(mojom::DocumentRulesPtr rules) {
  next_ = Rules{std::move(rules->style_sheet), rules->scan, std::move(rules->scriptlet),
                std::move(rules->procedural_filters)};
}

void CosmeticFiltersAgent::StartScripts() {
  // The scriptlets first: they must run before anything else in the page.
  RunScriptlet();
  StartScanner();
}

void CosmeticFiltersAgent::RunScriptlet() {
  if (!current_ || current_->scriptlet.empty() || scriptlet_ran_) {
    return;
  }
  scriptlet_ran_ = true;
  // One function around them all, so their helpers leave no name on window.
  render_frame()->GetWebFrame()->ExecuteScript(blink::WebScriptSource(
      blink::WebString::FromUtf8(base::StrCat({"(function () {\n", current_->scriptlet, "\n})();"})),
      GURL(kScriptletsUrl)));
}
```

`DidCreateNewDocument` also resets `scriptlet_ran_ = false;`. The constructor's posted task binds `&CosmeticFiltersAgent::StartScripts` instead of `StartScanner`. Check the pin's API: `grep -n "ExecuteScript(" third_party/blink/public/web/web_local_frame.h`. If there is no `ExecuteScript(const WebScriptSource&)`, use the main-world variant the header offers (`RequestExecuteScript` with `blink::mojom::EvaluationTiming::kSynchronous`, world 0) and record it in the progress notes.

- [ ] **Step 8: Run.** `python <scratchpad>\sync.py`, build `ghost_unittests ghost_browsertests chrome`, run `ScriptletsBrowserTest.*` and `CosmeticFiltersBrowserTest.*` on the hidden desktop. Expected: all pass.
  - **Spike point 1:** if `TheFirstScriptAlreadySeesTheScriptlet` fails, the scriptlet runs after the parser's first script. Record it. Then find the hook that runs first: `DidCreateScriptContext` for world 0 runs before any script. Run the scriptlet there when `current_` is set, and record the choice.
  - **Spike point 2:** if `AFrameItsParentWritesTakesTheParentsScriptlets` fails, record which hooks fire for that document. If none can run the scriptlet before the parent's write, change the test to expect "undefined", name it `AFrameItsParentWritesDoesntGetScriptlets`, and list it under limitations in the progress notes and Task 10's docs.
- [ ] **Step 9: Progress notes and commit.** Add spike points 1 and 2 to the progress notes.

```bash
git add common browser/cosmetic_filters renderer/cosmetic_filters BUILD.gn docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md
python tools/lint.py
git commit -m "cosmetic filters: scriptlets run in the page's world at its start"
```

---

### Task 5: GPC rules keep the header

**Files:**
- Modify: `browser/cosmetic_filters/scriptlets_browsertest.cc`

- [ ] **Step 1: The test.** Add `gpc.test##+js(set, navigator.globalPrivacyControl, false)\n` to `List()`, then:

```cpp
// EasyPrivacy's GPC rules (the user's choice, 2026-10-12): the page sees
// false, the server still gets the signal.
IN_PROC_BROWSER_TEST_F(ScriptletsBrowserTest, AGpcRuleLeavesTheHeader) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("gpc.test", "/first.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "navigator.globalPrivacyControl"), false);
  std::optional<net::test_server::HttpRequest::HeaderMap> headers =
      LastRequestHeaders("/first.html");
  ASSERT_TRUE(headers);
  EXPECT_EQ(headers->at("Sec-GPC"), "1");
}
```

- [ ] **Step 2: Run.** Expected: pass. If it fails, `set-constant` didn't reach `navigator`, which is a bug to fix in `set-constant.js` before committing. If `Sec-GPC` is missing, that is a 3C regression: stop and report it.
- [ ] **Step 3: Commit.**

```bash
git add browser/cosmetic_filters/scriptlets_browsertest.cc
python tools/lint.py
git commit -m "cosmetic filters: EasyPrivacy's GPC rules leave the Sec-GPC header"
```

---

### Task 6: The other scriptlets

One scriptlet per cycle: add its test (all in `scriptlets_browsertest.cc`, list lines and pages in `List()` and `AddPages()`), see it fail, write the file, add it to `resources.rs`'s `SHIPPED` and to `inputs` in `components/blocking/rust/BUILD.gn`, see it pass, then commit with `cosmetic filters: the <name> scriptlet`. Each entry has `mime: "fn/javascript"`, `scriptlet: true` and the `dependencies` given.

**Files:**
- Create: `components/blocking/resources/scriptlets/abort-current-script.js`, `abort-on-stack-trace.js`, `remove-node-text.js`, `no-xhr-if.js`, `no-fetch-if.js`, `set-local-storage-item.js`, `set-cookie.js`, `cookie-remover.js`
- Modify: `components/blocking/rust/resources.rs`, `components/blocking/rust/BUILD.gn`, `browser/cosmetic_filters/scriptlets_browsertest.cc`

- [ ] **Step 1: abort-current-script** (`abort-current-script.js`, aliases `acs.js`, `abort-current-inline-script.js`, `acis.js`; dependencies `SAFE, CHAIN, TRAP`).

List: `a.test##+js(acs, document.createElement, adNetwork)`. Page `/acs.html`:

```html
<script>/* adNetwork */ try { document.createElement('div'); window.caught = 'no'; } catch (e) { window.caught = e.name; }</script>
<script>/* adNetwork */ document.createElement('div'); window.after = 'ran';</script>
<script>window.other = typeof document.createElement('div');</script>
```

Test: `caught` is "ReferenceError", `after` is undefined, `other` is "object".

```js
// abort-current-script (aliases acs, abort-current-inline-script, acis): a
// script that reads or writes the property at the end of a chain is
// stopped with an exception when its text (inline) or URL (external)
// matches the needle; an empty needle matches every script. Other scripts
// see the property as before.
function abortCurrentScript(chain = '', needle = '') {
  const safe = shadeSafe();
  const re = safe.pattern(needle);
  const marker = `shade-abort-${Math.random().toString(36).slice(2)}`;
  shadeTrap(chain, () => {
    const script = safe.apply(safe.currentScript, document, []);
    if (!(script instanceof safe.HTMLScriptElement)) {
      return;
    }
    const text = safe.apply(safe.scriptSrc, script, []) ||
                 safe.apply(safe.textContent.get, script, []);
    if (safe.test(re, text)) {
      throw new safe.ReferenceError(marker);
    }
  });
  safe.quiet(marker);
}
```

- [ ] **Step 2: abort-on-stack-trace** (`abort-on-stack-trace.js`, alias `aost.js`; dependencies `SAFE, CHAIN, TRAP`).

List: `a.test##+js(aost, shadeTarget, inlineScript)`. Page `/aost.html`, plus a resource `/stack.js` (`application/javascript`, body `window.fromFile = String(window.shadeTarget);`):

```html
<script>try { window.inline = String(window.shadeTarget); } catch (e) { window.inline = e.name; }</script>
<script src="/stack.js"></script>
```

Test: `inline` is "ReferenceError"; `fromFile` is "undefined" (the external script's read wasn't stopped).

```js
// abort-on-stack-trace (alias aost): reading or writing the property at the
// end of a chain throws when the call stack matches the needle. The stack
// is one line, its frames joined by tabs, each `<function> <url>` without
// line and column; a frame in the document's inline scripts reads
// `inlineScript`, one without a URL `injectedScript`. Shade's own frames
// (shade://) are left out.
function abortOnStackTrace(chain = '', needle = '') {
  const safe = shadeSafe();
  const re = safe.pattern(needle);
  const marker = `shade-abort-${Math.random().toString(36).slice(2)}`;
  const page = safe.documentUrl.split('#')[0];
  const frame = /^\s*at (?:(.*?) \()?(.*?)(?::\d+:\d+)?\)?$/;
  const stackOf = () => {
    // The page may have changed how stacks are made.
    const limit = safe.Error.stackTraceLimit;
    const prepare = safe.Error.prepareStackTrace;
    safe.Error.stackTraceLimit = 50;
    safe.Error.prepareStackTrace = undefined;
    const stack = String(new safe.Error().stack);
    safe.Error.stackTraceLimit = limit;
    safe.Error.prepareStackTrace = prepare;
    return stack;
  };
  shadeTrap(chain, () => {
    const lines = stackOf().split('\n');
    let stack = '';
    for (let i = 1; i < lines.length; i++) {
      const parts = safe.exec(frame, lines[i]);
      if (!parts || parts[2].startsWith('shade://')) {
        continue;
      }
      const url = parts[2];
      const where = url === page ? 'inlineScript'
                  : url === '' || url === '<anonymous>' ? 'injectedScript'
                  : url;
      stack += (stack ? '\t' : '') + (parts[1] ? `${parts[1]} ${where}` : where);
    }
    if (safe.test(re, stack)) {
      throw new safe.ReferenceError(marker);
    }
  });
  safe.quiet(marker);
}
```

- [ ] **Step 3: remove-node-text** (`remove-node-text.js`, alias `rmnt.js`; dependencies `SAFE`).

List: `a.test##+js(rmnt, script, adNetwork)`. Page `/rmnt.html`: `<script>window.ran = 'adNetwork';</script><script>window.kept = true;</script>`. Test: `ran` is undefined, `kept` is true.

```js
// remove-node-text (alias rmnt): empties the elements of a name (`script`,
// `style`, …, or `#text` for text nodes) whose text matches the needle, as
// the parser adds them: the observer runs before an added script does.
function removeNodeText(nodeName = '', needle = '') {
  const safe = shadeSafe();
  if (!nodeName || !needle) {
    return;
  }
  const wanted = nodeName === '#text' ? '#text' : nodeName.toUpperCase();
  const re = safe.pattern(needle);
  const empty = node => {
    if (safe.test(re, safe.apply(safe.textContent.get, node, []))) {
      safe.apply(safe.textContent.set, node, ['']);
    }
  };
  new safe.MutationObserver(records => {
    for (let i = 0; i < records.length; i++) {
      const added = records[i].addedNodes;
      for (let j = 0; j < added.length; j++) {
        const node = added[j];
        if (node.nodeName === wanted) {
          empty(node);
        } else if (node.nodeType === 3 && node.parentNode && node.parentNode.nodeName === wanted) {
          empty(node.parentNode);
        }
      }
    }
  }).observe(document, {childList: true, subtree: true});
}
```

- [ ] **Step 4: no-xhr-if** (`no-xhr-if.js`, alias `prevent-xhr.js`; dependencies `SAFE`).

List: `a.test##+js(no-xhr-if, blocked.json)`. Resources `/blocked.json` and `/allowed.json` (`application/json`, `{"x":1}`). Page `/xhr.html`:

```html
<script>
window.results = {};
for (const name of ['blocked', 'allowed']) {
  const xhr = new XMLHttpRequest();
  xhr.open('GET', `/${name}.json`);
  xhr.onload = () => { results[name] = `${xhr.status}:${xhr.responseText}`; };
  xhr.send();
}
</script>
```

Test: poll until `Object.keys(results).length === 2`, then expect `results.blocked == "200:"` and `results.allowed == "200:{\"x\":1}"`. Also `LastRequestHeaders("/blocked.json")` is empty: the server never saw the request.

```js
// no-xhr-if (alias prevent-xhr): an XMLHttpRequest the conditions match
// isn't sent; it completes at once as an empty 200 response. Conditions:
// `url:<pattern>`, `method:<pattern>`, or a bare pattern for the URL.
function noXhrIf(props = '') {
  const safe = shadeSafe();
  const matches = safe.requestMatcher(props);
  const requests = new WeakMap();
  const proto = XMLHttpRequest.prototype;
  proto.open = new safe.Proxy(proto.open, {
    apply(open, xhr, args) {
      let url = '';
      try {
        url = new safe.URL(String(args[1]), document.baseURI).href;
      } catch {}
      safe.weakMapSet(requests, xhr, {method: String(args[0]).toUpperCase(), url});
      return safe.apply(open, xhr, args);
    },
  });
  proto.send = new safe.Proxy(proto.send, {
    apply(send, xhr, args) {
      const request = safe.weakMapGet(requests, xhr);
      if (!request || !matches(request)) {
        return safe.apply(send, xhr, args);
      }
      const type = xhr.responseType;
      const text = type === '' || type === 'text';
      const fixed = {
        readyState: 4,
        status: 200,
        statusText: 'OK',
        responseURL: request.url,
        response: text ? '' : type === 'arraybuffer' ? new ArrayBuffer(0)
                : type === 'blob' ? new Blob([]) : null,
      };
      if (text) {
        fixed.responseText = '';
      }
      for (const name of Object.keys(fixed)) {
        safe.defineProperty(xhr, name, {configurable: true, value: fixed[name]});
      }
      safe.setTimeout(() => {
        for (const event of ['readystatechange', 'load', 'loadend']) {
          safe.apply(safe.dispatchEvent, xhr, [new safe.Event(event)]);
        }
      }, 0);
      return undefined;
    },
  });
}
```

- [ ] **Step 5: no-fetch-if** (`no-fetch-if.js`, alias `prevent-fetch.js`; dependencies `SAFE`).

List: `a.test##+js(no-fetch-if, blocked.json)`. Page `/fetch.html`:

```html
<script>
window.results = {};
for (const name of ['blocked', 'allowed']) {
  fetch(`/${name}.json`).then(r => r.text()).then(t => { results[name] = t; });
}
</script>
```

Test: as Step 4, with `results.blocked == ""` and `results.allowed == "{\"x\":1}"`. Give it its own host (`fetch.test`) in the list and the URL, so Step 4's rule doesn't also apply.

```js
// no-fetch-if (alias prevent-fetch): a fetch() the conditions match isn't
// sent; it resolves at once to an empty 200 response. Conditions as
// no-xhr-if's.
function noFetchIf(props = '') {
  const safe = shadeSafe();
  const matches = safe.requestMatcher(props);
  window.fetch = new safe.Proxy(window.fetch, {
    apply(fetch, self, args) {
      const input = args[0];
      const init = args[1];
      let request;
      try {
        const isRequest = input instanceof safe.Request;
        request = {
          url: new safe.URL(isRequest ? input.url : String(input), document.baseURI).href,
          method: String((init && init.method) || (isRequest ? input.method : 'GET')).toUpperCase(),
        };
      } catch {
        return safe.apply(fetch, self, args);
      }
      if (!matches(request)) {
        return safe.apply(fetch, self, args);
      }
      return safe.resolved(new safe.Response('', {status: 200, statusText: 'OK'}));
    },
  });
}
```

- [ ] **Step 6: set-local-storage-item** (`set-local-storage-item.js`; dependencies `SAFE`).

List (host `storage.test`): `storage.test##+js(set-local-storage-item, shadeKey, true)` and `storage.test##+js(set-local-storage-item, oldKey, $remove$)`. Page `/storage.html`: `<script>window.key = localStorage.getItem('shadeKey'); window.before = localStorage.getItem('oldKey'); localStorage.setItem('oldKey', '1');</script>`. Test: load twice. The second load has `key == "true"` and `before == null`.

```js
// set-local-storage-item: at the document's start, sets a key of the site's
// localStorage to a fixed, harmless value, or removes it (`$remove$`; then
// the key may be a /pattern/ for several). Values: undefined, null, true,
// false, '', yes, no, on, off, accept, reject, allow, deny, emptyObj ({}),
// emptyArr ([]) and integers from 0 to 32767. Any other value leaves the
// storage alone.
function setLocalStorageItem(key = '', value = '') {
  const safe = shadeSafe();
  if (!key) {
    return;
  }
  const words = ['undefined', 'null', 'true', 'false', '', 'yes', 'no', 'on', 'off',
                 'accept', 'reject', 'allow', 'deny'];
  try {
    const storage = window.localStorage;
    if (value === '$remove$') {
      if (!safe.isRegExp(key)) {
        storage.removeItem(key);
        return;
      }
      const re = safe.pattern(key);
      for (let i = storage.length - 1; i >= 0; i--) {
        const name = storage.key(i);
        if (name !== null && safe.test(re, name)) {
          storage.removeItem(name);
        }
      }
      return;
    }
    let stored;
    if (value === 'emptyObj') {
      stored = '{}';
    } else if (value === 'emptyArr') {
      stored = '[]';
    } else if (words.includes(value) ||
               (safe.test(/^\d{1,5}$/, value) && Number(value) <= 32767)) {
      stored = value;
    } else {
      return;
    }
    if (storage.getItem(key) !== stored) {
      storage.setItem(key, stored);
    }
  } catch {
    // Storage disabled or full.
  }
}
```

- [ ] **Step 7: set-cookie** (`set-cookie.js`; dependencies `SAFE`).

List: `cookie.test##+js(set-cookie, shadeConsent, accept)` and `reload.test##+js(set-cookie, shadeReload, 1, , reload, 1)`. Page `/cookie.html`: `<script>window.cookies = document.cookie; sessionStorage.loads = Number(sessionStorage.loads || 0) + 1;</script>`. Tests:
  - on `cookie.test`, `cookies` contains `shadeConsent=accept`;
  - on `reload.test`, navigate with `ui_test_utils::NavigateToURLBlockUntilNavigationsComplete(browser(), url, 2)` (the load and the scriptlet's reload). Then expect `sessionStorage.loads` to be "2" after 500 ms more (no second reload), and `document.cookie` to contain `shadeReload=1`.

```js
// set-cookie: sets a first-party cookie to a fixed, harmless value when it
// differs, with path `/`, or the path given (`none`: no path). After the
// path, options as pairs: `reload, 1` reloads the page once the cookie
// took. Values: those of set-local-storage-item, plus ok, accepted,
// rejected, allowed, denied, essential, nonessential, necessary, required,
// hide, hidden.
function setCookie(name = '', value = '', path = '', ...options) {
  const safe = shadeSafe();
  const words = ['undefined', 'null', 'true', 'false', '', 'yes', 'no', 'on', 'off',
                 'accept', 'reject', 'allow', 'deny', 'ok', 'accepted', 'rejected', 'allowed',
                 'denied', 'essential', 'nonessential', 'necessary', 'required', 'hide',
                 'hidden'];
  if (!safe.test(/^[\w.-]+$/, name) ||
      !(words.includes(value) || (safe.test(/^\d{1,5}$/, value) && Number(value) <= 32767)) ||
      !(path === '' || path === 'none' || safe.test(/^\/[^;\s]*$/, path))) {
    return;
  }
  const read = () => {
    const cookies = safe.apply(safe.cookie.get, document, []).split(';');
    for (let i = 0; i < cookies.length; i++) {
      const pair = cookies[i].trim();
      const eq = pair.indexOf('=');
      if ((eq < 0 ? pair : pair.slice(0, eq)) === name) {
        return eq < 0 ? '' : pair.slice(eq + 1);
      }
    }
    return null;
  };
  if (read() === value) {
    return;
  }
  const where = path === 'none' ? '' : `; path=${path || '/'}`;
  safe.apply(safe.cookie.set, document, [`${name}=${value}${where}`]);
  let reload = false;
  for (let i = 0; i + 1 < options.length; i += 2) {
    if (options[i] === 'reload' && options[i + 1] === '1') {
      reload = true;
    }
  }
  if (reload && read() === value) {
    location.reload();
  }
}
```

- [ ] **Step 8: cookie-remover** (`cookie-remover.js`, alias `remove-cookie.js`; dependencies `SAFE`).

List: `crumbs.test##+js(cookie-remover, tracking)`. Page `/crumbs.html`, served with `Set-Cookie: tracking_id=1; path=/` and a second `Set-Cookie: keep=1; path=/` (`AddPage` headers), body `<script>window.cookies = document.cookie;</script>`. Test: `cookies` doesn't contain `tracking_id`, and does contain `keep=1`.

```js
// cookie-remover (alias remove-cookie): removes the cookies whose names
// match the needle at the document's start, and again when the page is
// hidden or unloads, when sites tend to write them. HttpOnly cookies are
// out of reach.
function cookieRemover(needle = '') {
  const safe = shadeSafe();
  if (!needle) {
    return;
  }
  const re = safe.pattern(needle);
  const remove = () => {
    const labels = location.hostname.split('.');
    const domains = [''];
    for (let i = 0; i < labels.length - 1; i++) {
      domains.push(`; domain=${labels.slice(i).join('.')}`);
    }
    const paths = ['/', location.pathname];
    const cookies = safe.apply(safe.cookie.get, document, []).split(';');
    for (let i = 0; i < cookies.length; i++) {
      const pair = cookies[i].trim();
      const eq = pair.indexOf('=');
      const name = eq < 0 ? pair : pair.slice(0, eq);
      if (!name || !safe.test(re, name)) {
        continue;
      }
      for (const domain of domains) {
        for (const path of paths) {
          safe.apply(safe.cookie.set, document,
                     [`${name}=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=${path}${domain}`]);
        }
      }
    }
  };
  remove();
  safe.apply(safe.addEventListener, window, ['beforeunload', remove]);
  safe.apply(safe.addEventListener, document, ['visibilitychange', () => {
    if (document.visibilityState === 'hidden') {
      remove();
    }
  }]);
}
```

- [ ] **Step 9: Every list scriptlet resolves.** Add to `engine_bridge_unittest.cc`:

```cpp
TEST(EngineBridgeTest, EveryScriptletTheListsUseResolvesExceptJsonEdit) {
  for (const char* name : {"set", "acs", "aost", "rmnt", "no-xhr-if", "no-fetch-if",
                           "set-local-storage-item", "set-cookie", "cookie-remover"}) {
    rust::Box<Engine> engine =
        new_engine(std::string("a.test##+js(") + name + ", x, true)\n");
    EXPECT_FALSE(Script(*engine, "https://a.test/").empty()) << name;
  }
}
```

Run it; expected: pass. Commit:

```bash
git add components/blocking browser/cosmetic_filters
python tools/lint.py
git commit -m "blocking: every scriptlet the shipped lists use resolves"
```

---

### Task 7: The procedural executor

**Files:**
- Create: `renderer/cosmetic_filters/procedural.js`
- Modify: `renderer/cosmetic_filters/BUILD.gn`, `cosmetic_filters_agent.{h,cc}`
- Create: `browser/cosmetic_filters/procedural_browsertest.cc`
- Modify: `BUILD.gn`

- [ ] **Step 1: Failing browser tests.** `procedural_browsertest.cc`, a `CosmeticFiltersTestBase`:

```cpp
class ProceduralBrowserTest : public CosmeticFiltersTestBase {
 protected:
  std::string List() const override {
    return "p.test#?#.box:has-text(Sponsored)\n"
           "p.test#?#.box:has-text(/promo\\d+/)\n"
           "p.test#?#.card:matches-attr(data-kind=\"ad\")\n"
           "p.test#?#.placed:matches-css(position: absolute)\n"
           "p.test#?#.marked:matches-css-before(content: /AD/)\n"
           "p.test#?#.onpath:matches-path(/procedural)\n"
           "p.test#?#.long:min-text-length(40)\n"
           "p.test#?#.label:has-text(Ad):upward(.wrapper)\n"
           "p.test#?#.deep:upward(2)\n"
           "p.test#?#:xpath(//section[@data-x=\"1\"])\n"
           "p.test#?#.parent:has-text(Promoted) > .child\n"
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
                         "wrapper", "grand", "section", "child"}) {
    EXPECT_EQ(WaitForDisplayOf(Tab(), base::StrCat({"document.getElementById('", id, "')"}),
                               "none"),
              "none")
        << id;
  }
  for (const char* id : {"plain", "card-ok", "short"}) {
    EXPECT_EQ(Display(Tab(), id), "block") << id;
  }
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, MatchesPathLooksAtThePath) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/other.html")));
  EXPECT_EQ(WaitForDisplayOf(Tab(), "document.getElementById('onpath')", "none"), "block");
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
  EXPECT_EQ(WaitForDisplayOf(Tab(), "document.getElementById('late')", "none"), "none");
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, ThePageSeesNoExecutor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("p.test", "/procedural.html")));
  EXPECT_EQ(content::EvalJs(Tab(), "typeof shadeCosmeticFilters"), "undefined");
}

IN_PROC_BROWSER_TEST_F(ProceduralBrowserTest, OffAppliesNone) {
  privacy_policy::SetLevel(browser()->profile(), Url("off.test", "/"),
                           privacy_policy::ProtectionLevel::kOff);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), Url("off.test", "/procedural.html")));
  EXPECT_EQ(WaitForDisplayOf(Tab(), "document.getElementById('sponsored')", "none"), "block");
}
```

`WaitForDisplayOf` returns the display it ends on, so "block" after 2 s means the element was never hidden. Add the file to `BUILD.gn`. Build and run `ProceduralBrowserTest.*` on the hidden desktop. Expected: `EveryOperatorSelects`, `EveryActionApplies` and `AnElementAddedLaterIsCaught` fail; only `.restyle`'s colour already applies, as a style rule since Task 4. `MatchesPathLooksAtThePath`, `ThePageSeesNoExecutor` and `OffAppliesNone` pass.

- [ ] **Step 2: `procedural.js`.**

```js
// Shade's procedural cosmetic rules (`:has-text()`, `:upward()`, …) and
// action rules (`:style()`, `:remove()`, …) that CSS can't express: finds
// the elements each rule selects and applies its action, at the document's
// start and again as the page changes. Runs in Shade's isolated world
// (CosmeticFiltersAgent) beside scanner.js, so the page can't see it or
// change what it uses. The rules are adblock-rust's JSON, checked by the
// browser.
(() => {
  const api = shadeCosmeticFilters;
  // After the first pass, at most one pass in this time.
  const INTERVAL_MS = 100;
  // The attributes this document's passes use: random, so a page can't
  // style or look for them in advance (components/cosmetic_filters/
  // procedural.h: IsMarkerAttribute).
  const token = Array.from(crypto.getRandomValues(new Uint8Array(8)),
                           b => b.toString(16).padStart(2, '0')).join('');
  const QUERY = `shade-q-${token}`;

  // A list's text argument: quotes around it are the list's; /source/flags
  // is a regular expression, anything else a literal substring (or, with
  // `exact`, the whole text).
  function pattern(text, exact = false) {
    let value = text.trim();
    if (value.length >= 2 && (value[0] === '"' || value[0] === "'") &&
        value.at(-1) === value[0]) {
      value = value.slice(1, -1);
    }
    const parts = /^\/(.+)\/([imsu]*)$/.exec(value);
    if (parts) {
      return new RegExp(parts[1], parts[2]);
    }
    return {test: s => (exact ? s === value : s.includes(value))};
  }

  function unique(elements) {
    return [...new Set(elements)];
  }

  // `selector` relative to each element: `> .a`, `+ .a`, ` .a`, or `.a` for
  // the element itself.
  function relative(selector, elements) {
    const found = new Set();
    for (const element of elements) {
      element.setAttribute(QUERY, '');
      try {
        for (const match of document.querySelectorAll(`[${QUERY}]${selector}`)) {
          found.add(match);
        }
      } finally {
        element.removeAttribute(QUERY);
      }
    }
    return [...found];
  }

  function cssMatcher(arg, pseudo) {
    const colon = arg.indexOf(':');
    if (colon < 0) {
      throw new Error(`matches-css: ${arg}`);
    }
    const property = arg.slice(0, colon).trim();
    const value = pattern(arg.slice(colon + 1), /*exact=*/true);
    return elements => elements.filter(
        e => value.test(getComputedStyle(e, pseudo).getPropertyValue(property)));
  }

  function attrMatcher(arg) {
    let rest = arg.trim();
    let name;
    if (rest[0] === '"') {
      const end = rest.indexOf('"', 1);
      if (end < 0) {
        throw new Error(`matches-attr: ${arg}`);
      }
      name = rest.slice(1, end);
      rest = rest.slice(end + 1);
    } else {
      const eq = rest.indexOf('=');
      name = eq < 0 ? rest : rest.slice(0, eq);
      rest = eq < 0 ? '' : rest.slice(eq);
    }
    if (rest && rest[0] !== '=') {
      throw new Error(`matches-attr: ${arg}`);
    }
    const names = pattern(name, /*exact=*/true);
    const values = rest ? pattern(rest.slice(1), /*exact=*/true) : null;
    return elements => elements.filter(e => Array.from(e.attributes).some(
        a => names.test(a.name) && (!values || values.test(a.value))));
  }

  // Each operator, from its argument, to a step: elements in, elements out.
  // The first step gets null and starts from the document.
  const OPERATORS = {
    'css-selector': arg => elements =>
        elements === null ? [...document.querySelectorAll(arg)] : relative(arg, elements),
    'has-text': arg => {
      const text = pattern(arg);
      return elements => elements.filter(e => text.test(e.textContent));
    },
    'matches-attr': attrMatcher,
    'matches-css': arg => cssMatcher(arg, null),
    'matches-css-before': arg => cssMatcher(arg, '::before'),
    'matches-css-after': arg => cssMatcher(arg, '::after'),
    'matches-path': arg => {
      const path = pattern(arg);
      return elements => (path.test(location.pathname + location.search) ? elements : []);
    },
    'min-text-length': arg => {
      const length = Number(arg);
      if (!Number.isInteger(length) || length < 0) {
        throw new Error(`min-text-length: ${arg}`);
      }
      return elements => elements.filter(e => e.textContent.length >= length);
    },
    'upward': arg => {
      const count = Number(arg);
      if (Number.isInteger(count)) {
        if (count < 1 || count > 256) {
          throw new Error(`upward: ${arg}`);
        }
        return elements => unique(elements.map(e => {
          let ancestor = e;
          for (let i = 0; i < count && ancestor; i++) {
            ancestor = ancestor.parentElement;
          }
          return ancestor;
        }).filter(Boolean));
      }
      return elements =>
          unique(elements.map(e => e.parentElement && e.parentElement.closest(arg))
                     .filter(Boolean));
    },
    'xpath': arg => elements => {
      const found = new Set();
      for (const context of elements === null ? [document] : elements) {
        const result = document.evaluate(arg, context, null,
                                         XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
        for (let i = 0; i < result.snapshotLength; i++) {
          const node = result.snapshotItem(i);
          if (node.nodeType === Node.ELEMENT_NODE) {
            found.add(node);
          }
        }
      }
      return [...found];
    },
  };

  // A rule: its steps, and either a marker attribute (hiding and style,
  // undone when the element no longer matches) or an action applied once.
  function compile(filter, index) {
    const first = filter.selector[0];
    if (!first || (first.type !== 'css-selector' && first.type !== 'xpath')) {
      throw new Error('a rule starts with a selector');
    }
    const steps = filter.selector.map(operator => {
      const make = OPERATORS[operator.type];
      if (!make) {
        throw new Error(`unknown operator ${operator.type}`);
      }
      return make(operator.arg);
    });
    const action = filter.action;
    const rule = {steps, marked: new Set()};
    if (!action) {
      rule.mark = `shade-${index}-${token}`;
      api.hide(rule.mark);
    } else if (action.type === 'style') {
      rule.mark = `shade-${index}-${token}`;
      api.style(rule.mark, action.arg);
    } else if (action.type === 'remove') {
      rule.apply = e => e.remove();
    } else if (action.type === 'remove-attr') {
      rule.apply = e => e.removeAttribute(action.arg);
    } else if (action.type === 'remove-class') {
      rule.apply = e => e.classList.remove(action.arg);
    } else {
      throw new Error(`unknown action ${action.type}`);
    }
    return rule;
  }

  const rules = [];
  api.filters().forEach((json, index) => {
    try {
      rules.push(compile(JSON.parse(json), index));
    } catch {
      // A rule this executor can't run is skipped; the others still apply.
    }
  });
  if (!rules.length) {
    return;
  }

  let timer = 0;

  function pass() {
    timer = 0;
    for (const rule of rules) {
      let elements = null;
      try {
        for (const step of rule.steps) {
          elements = step(elements);
        }
      } catch {
        continue;
      }
      if (rule.mark) {
        const now = new Set(elements);
        for (const element of rule.marked) {
          if (!now.has(element)) {
            element.removeAttribute(rule.mark);
          }
        }
        for (const element of now) {
          if (!element.hasAttribute(rule.mark)) {
            element.setAttribute(rule.mark, '');
          }
        }
        rule.marked = now;
      } else {
        elements.forEach(rule.apply);
      }
    }
  }

  // Our own markers change attributes too: those records are ours.
  new MutationObserver(records => {
    if (records.every(r => r.type === 'attributes' && r.attributeName.startsWith('shade-'))) {
      return;
    }
    if (!timer) {
      timer = setTimeout(pass, INTERVAL_MS);
    }
  }).observe(document, {childList: true, subtree: true, characterData: true, attributes: true});

  pass();
})();
```

- [ ] **Step 3: The agent.**
  - **`BUILD.gn`:** add `action("procedural_script")` like `scanner_script`, with `--name kProceduralScript`. Add it to `deps`, and add `//base` if it isn't there already.
  - **`cosmetic_filters_agent.h`:** add `void StartProcedural();`, `void Filters(gin::Arguments* arguments);`, `void Hide(const std::string& attribute);`, `void Style(const std::string& attribute, const std::string& declarations);` and `bool procedural_started_ = false;`.
  - **`cosmetic_filters_agent.cc`:** include `ghost/components/cosmetic_filters/procedural.h`, `ghost/components/cosmetic_filters/style_sheet.h` and `ghost/renderer/cosmetic_filters/procedural_script.h`. Add `constexpr char kProceduralUrl[] = "shade://cosmetic-filters/procedural.js";`. In `DidCreateScriptContext`, set three more functions on `api` the way `report` is set: `filters` (`&CosmeticFiltersAgent::Filters`), `hide` (`&CosmeticFiltersAgent::Hide`) and `style` (`&CosmeticFiltersAgent::Style`). Then:

```cpp
void CosmeticFiltersAgent::StartScripts() {
  // The scriptlets first: they must run before anything else in the page.
  RunScriptlet();
  StartScanner();
  StartProcedural();
}

void CosmeticFiltersAgent::StartProcedural() {
  if (!current_ || current_->procedural_filters.empty() || procedural_started_) {
    return;
  }
  procedural_started_ = true;
  NameTheWorld();
  render_frame()->GetWebFrame()->ExecuteScriptInIsolatedWorld(
      kWorldId,
      blink::WebScriptSource(blink::WebString::FromUtf8(kProceduralScript), GURL(kProceduralUrl)),
      blink::BackForwardCacheAware::kAllow);
}

void CosmeticFiltersAgent::Filters(gin::Arguments* arguments) {
  arguments->Return(current_ ? current_->procedural_filters : std::vector<std::string>());
}

// procedural.js's markers, checked again here: the isolated world is ours,
// but what it inserts is a user-origin style sheet.
void CosmeticFiltersAgent::Hide(const std::string& attribute) {
  if (IsMarkerAttribute(attribute)) {
    Insert(base::StrCat({"[", attribute, "]{display:none !important}\n"}));
  }
}

void CosmeticFiltersAgent::Style(const std::string& attribute, const std::string& declarations) {
  if (IsMarkerAttribute(attribute) && IsSafeStyle(declarations)) {
    Insert(base::StrCat({"[", attribute, "]{", declarations, "}\n"}));
  }
}
```

  - **`DidCreateNewDocument`:** also reset `procedural_started_ = false;`.
- [ ] **Step 4: Run.** Sync, build, run `ProceduralBrowserTest.*`, `ScriptletsBrowserTest.*` and `CosmeticFiltersBrowserTest.*` on the hidden desktop. Expected: all pass. If `EveryOperatorSelects` fails for one operator, read Task 1's JSON for it: the executor must take adblock-rust's argument as it comes (for example `matches-attr`'s quoting). Fix `procedural.js`, not the test.
- [ ] **Step 5: Commit.**

```bash
git add renderer/cosmetic_filters browser/cosmetic_filters BUILD.gn
python tools/lint.py
git commit -m "cosmetic filters: the procedural executor in Shade's world"
```

---

### Task 8: Redirects (spike point 3)

**Files:**
- Modify: `browser/blocking/request_filter.cc`, `browser/blocking/BUILD.gn`
- Create: `components/blocking/resources/redirects/chartbeat.js`, `googletagmanager_gtm.js`, `fingerprint2.js`
- Modify: `components/blocking/rust/resources.rs`, `components/blocking/rust/BUILD.gn`
- Create: `browser/blocking/redirect_browsertest.cc`
- Modify: `BUILD.gn`

- [ ] **Step 1: Failing browser tests.** `redirect_browsertest.cc`:

```cpp
// Redirect rules: the request filter answers with Shade's resource instead
// of the network (Phase 4B).

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
                  if (Object.keys(results).length === 4 || performance.now() - start > 3000) {
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
```

Includes as in the scriptlet tests, plus `ghost/browser/cosmetic_filters/cosmetic_filters_test_base.h` (a test source of the same binary). Add the file to `BUILD.gn`. Expected now: fails, with `script`, `image`, `fetch` and `gtm` each blocked ('error', 'error', 'TypeError', 'error'). `fetch()` is checked as `xmlhttprequest` (3A's type mapping): if the `$xhr` rule doesn't match it, record that and use `$xhr,other` in the list.

- [ ] **Step 2: The surrogates.** `redirects/chartbeat.js`:

```js
// A stand-in for Chartbeat's tracker (redirect=chartbeat.js): pages that
// call its API keep working; nothing is measured or sent.
(function () {
  const noop = function () {};
  window.pSUPERFLY = window.pSUPERFLY || {activity: noop, virtualPage: noop};
})();
```

`redirects/googletagmanager_gtm.js`:

```js
// A stand-in for Google Tag Manager (redirect=googletagmanager_gtm.js):
// nothing loads or is sent, but the callbacks pages wait for
// (eventCallback, hitCallback) still run, so links and forms that wait for
// them work.
(function () {
  const run = callback => {
    if (typeof callback === 'function') {
      setTimeout(callback, 0);
    }
  };
  const runAll = items => {
    for (const item of items) {
      if (item && typeof item === 'object') {
        run(item.eventCallback);
      }
    }
  };
  const dataLayer = (window.dataLayer = window.dataLayer || []);
  runAll(dataLayer);
  dataLayer.push = function (...items) {
    runAll(items);
    return Array.prototype.push.apply(dataLayer, items);
  };
  window.google_tag_manager = window.google_tag_manager || {};
  window.ga = window.ga || function (...args) {
    for (const arg of args) {
      if (arg && typeof arg === 'object') {
        run(arg.hitCallback);
      }
    }
  };
})();
```

`redirects/fingerprint2.js`:

```js
// A stand-in for FingerprintJS 2 (redirect=fingerprint2.js): its API answers
// with an empty fingerprint, so pages that wait for one carry on.
(function () {
  const empty = [];
  const later = (callback, ...args) => {
    if (typeof callback === 'function') {
      setTimeout(() => callback(...args), 1);
    }
  };
  function Fingerprint2() {}
  Fingerprint2.get = (options, callback) =>
      later(typeof options === 'function' ? options : callback, empty);
  Fingerprint2.getV18 = (options, callback) =>
      later(typeof options === 'function' ? options : callback, '', empty);
  Fingerprint2.getPromise = () => Promise.resolve(empty);
  Fingerprint2.x64hash128 = () => '';
  Fingerprint2.prototype.get = function (callback) {
    later(callback, '', empty);
  };
  window.Fingerprint2 = Fingerprint2;
})();
```

Add the three to `SHIPPED` (`mime: "application/javascript"`, `scriptlet: false`, no aliases, `name` = the file name) and to `inputs`.

- [ ] **Step 3: The request filter.** In `request_filter.cc`, includes:

```cpp
#include <algorithm>

#include "mojo/public/cpp/system/data_pipe.h"
#include "net/base/data_url.h"
#include "net/http/http_response_headers.h"
#include "services/network/public/mojom/fetch_api.mojom-shared.h"
```

In the anonymous namespace:

```cpp
// The response type a page would see for this request had the network
// answered (Fetch: basic for its own origin, opaque for a no-cors request
// elsewhere, cors otherwise).
network::mojom::FetchResponseType ResponseTypeFor(const network::ResourceRequest& request) {
  if (request.request_initiator && request.request_initiator->IsSameOriginWith(request.url)) {
    return network::mojom::FetchResponseType::kBasic;
  }
  return request.mode == network::mojom::RequestMode::kNoCors
             ? network::mojom::FetchResponseType::kOpaque
             : network::mojom::FetchResponseType::kCors;
}
```

`InFlight::Block` becomes:

```cpp
  void Block(const Decision& decision) {
    VLOG(1) << "Blocked " << request_.url << " by " << decision.filter;
    protections::PageProtections::RecordBlocked(frame_);
    if (decision.redirect.empty() || !Answer(decision.redirect)) {
      client_->OnComplete(network::URLLoaderCompletionStatus(net::ERR_BLOCKED_BY_CLIENT));
    }
    delete this;
  }

  // Answers with one of Shade's resources (a redirect rule), without the
  // network. False if the data: URL doesn't parse or the pipe can't be made.
  bool Answer(const std::string& data_url) {
    std::string mime_type;
    std::string charset;
    std::string body;
    if (!net::DataURL::Parse(GURL(data_url), &mime_type, &charset, &body)) {
      return false;
    }
    mojo::ScopedDataPipeProducerHandle producer;
    mojo::ScopedDataPipeConsumerHandle consumer;
    if (mojo::CreateDataPipe(std::max<size_t>(body.size(), 1), producer, consumer) !=
        MOJO_RESULT_OK) {
      return false;
    }
    if (!body.empty() && producer->WriteAllData(base::as_byte_span(body)) != MOJO_RESULT_OK) {
      return false;
    }
    auto head = network::mojom::URLResponseHead::New();
    head->mime_type = mime_type;
    head->charset = charset;
    head->headers = net::HttpResponseHeaders::Builder({1, 1}, "200 OK")
                        .AddHeader("Content-Type", mime_type)
                        .AddHeader("Access-Control-Allow-Origin", "*")
                        .Build();
    head->response_type = ResponseTypeFor(request_);
    head->content_length = body.size();
    client_->OnReceiveResponse(std::move(head), std::move(consumer), std::nullopt);
    network::URLLoaderCompletionStatus status(net::OK);
    status.decoded_body_length = body.size();
    client_->OnComplete(status);
    return true;
  }
```

Check at the pin:
  - `grep -n "WriteAllData\|WriteData" mojo/public/cpp/system/data_pipe.h`: if there is no `WriteAllData`, use `WriteData(span, MOJO_WRITE_DATA_FLAG_ALL_OR_NONE, bytes_written)`;
  - `grep -n "class Builder" net/http/http_response_headers.h`;
  - `grep -n "bool Parse" net/base/data_url.h`.

  Match each one's signature. Add `//net` and `//mojo/public/cpp/system` to `browser/blocking/BUILD.gn`'s deps if they aren't there.

- [ ] **Step 4: Run (spike point 3).** Sync, build, run `RedirectBrowserTest.*` and `RequestFilterBrowserTest.*` on the hidden desktop. Expected: pass.
  - **If `fetch` fails** (`TypeError`), the CORS response type is wrong for a response the filter makes itself. Try `kCors` with the `Access-Control-Allow-Origin` header the request's origin needs (`request_.request_initiator->Serialize()`), and record what worked.
  - **If `script` fails with an opaque response,** try `kCors` for it too, and record that.
  - Record the result in the progress notes either way.
- [ ] **Step 5: Commit.**

```bash
git add browser/blocking components/blocking BUILD.gn docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md
python tools/lint.py
git commit -m "blocking: redirect rules answered with Shade's resources"
```

---

### Task 9: Budgets

**Files:**
- Modify: `components/blocking/blocking_perftest.cc`
- Modify: `tools/scanner_cost.py`, `tools/tests/test_scanner_cost.py`
- Create: `test/cosmetic/light.html`

- [ ] **Step 1: The perftest.** In `CosmeticRulesWithinBudget`, also log the largest `injected_script.size()` and `procedural_filters.size()` beside the selectors. Then add `#include <vector>` and a test over sites with scriptlets in EasyPrivacy:

```cpp
// Sites EasyPrivacy's scriptlet rules name (2026-10-10 list).
constexpr const char* kScriptletPages[] = {
    "https://www.madewell.com/",     "https://www.visible.com/",      "https://www.usaa.com/",
    "https://bolighub.dk/",          "https://www.ranker.com/",       "https://www.oregonlive.com/",
    "https://www.reuters.com/",      "https://www.mensjournal.com/",  "https://www.cbsnews.com/",
    "https://www.usatoday.com/",     "https://www.fedex.com/",        "https://mwed.jp/",
    "https://www.nicovideo.jp/",     "https://abema.tv/",             "https://coomer.st/",
    "https://www.ncbi.nlm.nih.gov/", "https://x.com/",                "https://www.aliexpress.com/",
    "https://open.spotify.com/",     "https://techcrunch.com/",       "https://www.al.com/",
    "https://nypost.com/",
};

// The rules a document gets on the sites with scriptlets: their scripts are
// assembled per document.
TEST(BlockingPerfTest, ScriptletSitesWithinBudget) {
  rust::Box<Engine> engine = new_engine(ShippedLists());
  std::vector<base::TimeDelta> times;
  size_t largest = 0;
  for (int pass = 0; pass < 100; ++pass) {
    for (const char* page : kScriptletPages) {
      const base::TimeTicks start = base::TimeTicks::Now();
      const CosmeticResources resources = engine->cosmetic_resources(page);
      times.push_back(base::TimeTicks::Now() - start);
      largest = std::max(largest, resources.injected_script.size());
    }
  }
  const auto [p50, p99] = Percentiles(std::move(times));
  LOG(INFO) << "Scriptlet sites: " << std::size(kScriptletPages) << " pages, scripts up to "
            << largest << " bytes; p50 " << p50.InMicrosecondsF() << " us, p99 "
            << p99.InMicrosecondsF() << " us (budget " << kCosmeticBudgetP99.InMicroseconds()
            << " us).";
  EXPECT_GT(largest, 0u);
#if defined(OFFICIAL_BUILD)
  EXPECT_LT(p99, kCosmeticBudgetP99);
#endif
}
```

- [ ] **Step 2: `scanner_cost.py --site`.**
  - Add `--site` (default `cosmetic.test`) and `--page` (default `heavy.html`). Pass the site to `off_preferences(site)`, to the host-resolver rule and to the page URL, in place of the `SITE` constant. `SITE` stays as the default's name.
  - Update the docstring: it now measures cosmetic filtering on any site the lists name. Phase 4B uses the site with the most scriptlets with `light.html`, and the site with the most procedural rules with `heavy.html`.
  - In `tools/tests/test_scanner_cost.py`, the `off_preferences` test passes a site and expects it in the result.
  - `test/cosmetic/light.html`: the MPL comment, then a small page (a heading and three paragraphs).

  Run `python -m unittest discover -s tools/tests -t tools`; expected: pass.
- [ ] **Step 3: The official build.** Ask the user whether to build the official `chrome` (about 1 h of linking after a 4B change, from `out/release`, as in 4A's Task 10) or to measure in the dev build only. Wait for the answer.
  - **If official:** `autoninja -C out\release chrome ghost_blocking_perftests`. Run `out\release\ghost_blocking_perftests.exe`, then:
    - `python tools\scanner_cost.py --chrome out\release\chrome.exe --launcher <scratchpad>\hidden.ps1 --site <scriptlet host> --page light.html`;
    - the same with `--site <procedural host> --page heavy.html`;
    - the same with `--site cosmetic.test --page heavy.html`.
  - **If dev:** the same runs with `out\vanilla`, reported as the dev build's.
- [ ] **Step 4: Judge and record.** Against the spec's budgets:
  - the perftests: p99 under 1 ms;
  - the scriptlet site: under 1 ms;
  - procedural: the procedural host minus `cosmetic.test`, under 5 ms.

  Record every number in the progress notes. A budget missed in an official build is reported to the user, not changed.
- [ ] **Step 5: Commit.**

```bash
git add components/blocking/blocking_perftest.cc tools test/cosmetic docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md
python tools/lint.py
git commit -m "blocking: 4B's budgets, measured"
```

---

### Task 10: Mutation checks, audit, fuzzing

- [ ] **Step 1: The mutation script.** Copy 4A's `mutate4a.py` from the old scratchpad to `<scratchpad>\mutate4b.py`. It applies one mutant in `src/ghost`, builds, runs the given filter on the hidden desktop, and restores. **Check that each mutant's build succeeds before reading its tests:** a mutant that doesn't compile leaves the old binary, which passes.
- [ ] **Step 2: The six mutants.** Each must make the named tests fail:

| Mutant | Change | Must fail |
|---|---|---|
| M1 | `StartScripts` runs `RunScriptlet()` from a posted task instead of directly | `ScriptletsBrowserTest.TheFirstScriptAlreadySeesTheScriptlet` |
| M2 | `problem()` skips the placeholder and function-style checks (prefix both `if` conditions with `false && `) | `EngineBridgeTest.OnlyFunctionStyleScriptletsAreAccepted` |
| M3 | the throttle's Off branch sends the full rules (computes them as for Standard) | `ScriptletsBrowserTest.OffRunsNone`, `ProceduralBrowserTest.OffAppliesNone` |
| M4 | `IsSafeStyle` returns `!declarations.empty()` | `StyleSheetTest.SafeStylesStayInTheirRule`, `ProceduralTest.DropsWhatCouldEscape` |
| M5 | `Block` ignores `decision.redirect` | `RedirectBrowserTest.ResourcesAnswerWithoutTheNetwork` |
| M6 | `StartProcedural` runs `procedural.js` with `ExecuteScript` in the main world (and `DidCreateScriptContext` installs `api` for world 0) | `ProceduralBrowserTest.ThePageSeesNoExecutor` |

  Record each result in the progress notes. After the last one, compare `src/ghost` with webops (`cmp` over `git ls-files`). Any difference is a mutant left behind: restore it.
- [ ] **Step 3: The whole suites.** `ghost_unittests` and `ghost_browsertests` in full, on the hidden desktop. Expected: all pass with no retry. Record the counts.
- [ ] **Step 4: The egress audit.** Run it as in 4A (`python tools\egress_audit.py …` with the scenarios of `docs/testing.md`). Expected: no unexpected host. The scriptlets and redirects make no request of their own.
- [ ] **Step 5: Fuzzing.** In `out/fuzz`: `autoninja -C out\fuzz ghost_cosmetic_procedural_fuzzer`, then run it for 30 minutes (`-max_total_time=1800`). Expected: no crash. Record the run.
- [ ] **Step 6: Commit the notes.**

```bash
git add docs/superpowers/specs/2026-10-12-scriptlets-procedural-spike.md
python tools/lint.py
git commit -m "docs: 4B mutation checks, audit and fuzzing"
```

---

### Task 11: Documentation and push

**Files:**
- Modify: `docs/privacy-model.md`, `docs/architecture.md`, `docs/threat-model.md`, `docs/licensing.md`, `docs/testing.md`, `docs/roadmap.md`
- Modify: `docs/superpowers/specs/2026-10-12-scriptlets-procedural-design.md` (status, done-when boxes)
- Modify: `docs/superpowers/plans/2026-10-12-scriptlets-procedural.md` (status)

- [ ] **Step 1: privacy-model.md.** In the blocking section:
  - what Standard and Strict apply now: scriptlets, procedural rules, redirects; Off applies none;
  - the GPC rules: on the sites EasyPrivacy names, the page reads `navigator.globalPrivacyControl` as `false` so it doesn't break, while every request still carries `Sec-GPC: 1`.
- [ ] **Step 2: architecture.md.**
  - the main-world scriptlet at document start, wrapped in one function, and why there and not in the isolated world;
  - the procedural executor beside the scanner;
  - redirects answered by the request filter;
  - CSS validation on in adblock-rust, and the new crates.
- [ ] **Step 3: threat-model.md.**
  - code running in the page's world, which comes only from the browser and passes list arguments only as JSON strings;
  - list regular expressions run against page text (a slow expression is the list's risk; the list is the trust boundary, signed from 3E);
  - the renderer re-checks markers and styles;
  - limitations: an element added by script can show until the next pass (100 ms); `json-edit` is not supported; Task 4's frame finding, if it is a limitation.
- [ ] **Step 4: licensing.md.**
  - "Scriptlets are code": ours now exist (`components/blocking/resources/`), written clean from public behavior descriptions, and never from uBlock Origin's or AdGuard's code;
  - the new crates and their licenses.
- [ ] **Step 5: testing.md.** The new suites (`ScriptletsBrowserTest`, `ProceduralBrowserTest`, `RedirectBrowserTest`), the new fuzzer, `scanner_cost.py --site`, and the mutation checks M1–M6.
- [ ] **Step 6: roadmap.md.** Mark 4B **Done** with its date, as 4A's entry does:
  - the dirs and files, the design and progress-notes links;
  - the budgets' numbers, and the checks: mutations, test counts, fuzzing, egress;
  - the limitations.
- [ ] **Step 7: Spec and plan status.** The spec's status line gets "done <date>" and its boxes are checked. The plan's header gets a **Status** line, as 4A's has.
- [ ] **Step 8: Commit and push.**

```bash
git add docs
python tools/lint.py
git commit -m "docs: scriptlets and procedural rules (4B) done"
```

Ask the user before pushing. After `git push`, check both tooling CI jobs (Ubuntu and Windows) to completion, then pull `src/ghost`.
