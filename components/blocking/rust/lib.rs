// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! adblock-rust behind a small cxx bridge: build an engine from list text,
//! ask it about one request, or about a document's cosmetic rules,
//! scriptlets and procedural rules. Everything else (threads, lifetimes,
//! policy) is C++'s, in //ghost/components/blocking.

mod resources;

use std::collections::HashSet;

use adblock::cosmetic_filter_cache::ProceduralOrActionFilter;
use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::url_parser::{set_domain_resolver, ResolvesDomain};

#[cxx::bridge(namespace = "ghost::blocking")]
mod ffi {
    /// Whether a request is blocked, and by which rule (for the console).
    struct Verdict {
        blocked: bool,
        filter: String,
        /// When a redirect rule blocked the request: the resource to answer
        /// with instead, as a data: URL. Empty otherwise.
        redirect: String,
    }

    /// The verdict on a request's DNS aliases (Phase 4C).
    struct AliasVerdict {
        blocked: bool,
        filter: String,
        /// As in Verdict.
        redirect: String,
        /// The alias URL that was blocked; empty otherwise.
        alias_url: String,
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

    unsafe extern "C++" {
        include!("ghost/components/blocking/registrable_domain.h");
        fn RegistrableDomainRange(canonical_host: &str, start: &mut usize, end: &mut usize);
    }

    extern "Rust" {
        type Engine;
        /// An engine for the network rules in `lists` (filter-list text, one
        /// rule per line). Lines it can't parse are skipped.
        fn new_engine(lists: &str) -> Box<Engine>;
        /// The verdict for one request. A URL the engine can't parse is
        /// allowed.
        fn check(
            self: &Engine,
            url: &str,
            source_url: &str,
            request_type: &str,
            method: &str,
        ) -> Verdict;
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
        /// The site-specific selectors (and the generic ones too complex to
        /// index) for a document's URL, its exceptions and generichide, its
        /// scriptlets, style and procedural rules. A URL the engine can't
        /// parse has none. Sorted, so answers are stable.
        fn cosmetic_resources(self: &Engine, url: &str) -> CosmeticResources;
        /// The generic selectors the class and id names match, less the
        /// document's exceptions. Sorted.
        fn generic_selectors(
            self: &Engine,
            classes: &Vec<String>,
            ids: &Vec<String>,
            exceptions: &Vec<String>,
        ) -> Vec<String>;
        /// Problems in the resources built into Shade; none is expected.
        fn resource_problems() -> Vec<String>;
        /// The problem a resource would have, or an empty string.
        fn resource_problem_for_testing(
            name: &str,
            mime: &str,
            body: &str,
            scriptlet: bool,
        ) -> String;
        /// Each line of `list` the parser refuses, as "line N: <rule>:
        /// <error>"; comments, headers and blank lines are fine. For Shade's
        /// own list, which is checked rule by rule (Phase 4D-1).
        fn list_problems(list: &str) -> Vec<String>;
    }
}

pub struct Engine(adblock::Engine);

/// Chromium's registry decides what a site is, so blocking and the browser
/// agree (see registrable_domain.h).
struct ChromiumDomains;

impl ResolvesDomain for ChromiumDomains {
    fn get_host_domain(&self, host: &str) -> (usize, usize) {
        let (mut start, mut end) = (0usize, host.len());
        ffi::RegistrableDomainRange(host, &mut start, &mut end);
        (start, end)
    }
}

fn new_engine(lists: &str) -> Box<Engine> {
    // The resolver is process-wide and set once; a second engine finds it set.
    let _ = set_domain_resolver(Box::new(ChromiumDomains));
    let mut filters = FilterSet::new(false);
    // Every rule type: network rules for requests, cosmetic rules, scriptlets
    // and procedural rules for pages (Phases 4A, 4B).
    filters.add_filter_list(lists.to_string(), ParseOptions::default());
    let mut engine = adblock::Engine::new_with_filter_set(filters);
    engine.use_resources(resources::usable());
    Box::new(Engine(engine))
}

impl Engine {
    fn check(&self, url: &str, source_url: &str, request_type: &str, method: &str) -> ffi::Verdict {
        // adblock-rust parses methods in lowercase; HTTP sends them uppercase.
        match Request::new(url, source_url, request_type, &method.to_ascii_lowercase()) {
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
        }
    }

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

    fn cosmetic_resources(&self, url: &str) -> ffi::CosmeticResources {
        let resources = self.0.url_cosmetic_resources(url);
        let mut style_rules = Vec::new();
        let mut procedural_filters = Vec::new();
        for json in resources.procedural_actions {
            match serde_json::from_str::<ProceduralOrActionFilter>(&json)
                .ok()
                .and_then(|filter| filter.as_css())
            {
                Some((selector, style)) => {
                    style_rules.push(ffi::CosmeticStyleRule { selector, style })
                }
                None => procedural_filters.push(json),
            }
        }
        // adblock-rust passes :has() through as CSS, a procedural operator
        // inside it too (`a:has(p:has-text(Ad))`), which Blink would drop:
        // such a selector goes to the executor, which parses it.
        let (procedural, hide_selectors): (HashSet<String>, HashSet<String>) = resources
            .hide_selectors
            .into_iter()
            .partition(|selector| has_procedural_operator(selector));
        for selector in procedural {
            procedural_filters.push(
                serde_json::json!({"selector": [{"type": "css-selector", "arg": selector}]})
                    .to_string(),
            );
        }
        style_rules.sort_by(|a, b| (&a.selector, &a.style).cmp(&(&b.selector, &b.style)));
        procedural_filters.sort();
        ffi::CosmeticResources {
            hide_selectors: sorted(hide_selectors),
            exceptions: sorted(resources.exceptions),
            generichide: resources.generichide,
            injected_script: resources.injected_script,
            style_rules,
            procedural_filters,
        }
    }

    fn generic_selectors(
        &self,
        classes: &Vec<String>,
        ids: &Vec<String>,
        exceptions: &Vec<String>,
    ) -> Vec<String> {
        let exceptions: HashSet<String> = exceptions.iter().cloned().collect();
        let mut selectors = self.0.hidden_class_id_selectors(classes, ids, &exceptions);
        selectors.sort();
        selectors
    }
}

/// Whether a selector holds one of adblock-rust's procedural operators, which
/// no browser's CSS knows.
fn has_procedural_operator(selector: &str) -> bool {
    const OPERATORS: &[&str] = &[
        ":has-text(",
        ":matches-attr(",
        ":matches-css(",
        ":matches-css-before(",
        ":matches-css-after(",
        ":matches-path(",
        ":min-text-length(",
        ":upward(",
        ":xpath(",
    ];
    OPERATORS.iter().any(|operator| selector.contains(operator))
}

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

fn list_problems(list: &str) -> Vec<String> {
    list.lines()
        .enumerate()
        .filter(|(_, line)| {
            let line = line.trim();
            !line.is_empty() && !line.starts_with('!') && !line.starts_with('[')
        })
        .filter_map(|(i, line)| {
            adblock::lists::parse_filter(line, false, ParseOptions::default())
                .err()
                .map(|e| format!("line {}: {}: {}", i + 1, line.trim(), e))
        })
        .collect()
}

fn sorted(set: HashSet<String>) -> Vec<String> {
    let mut strings: Vec<String> = set.into_iter().collect();
    strings.sort();
    strings
}
