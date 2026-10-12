// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! adblock-rust behind a small cxx bridge: build an engine from list text,
//! ask it about one request, or about a document's cosmetic rules.
//! Everything else (threads, lifetimes, policy) is C++'s, in
//! //ghost/components/blocking.

use std::collections::HashSet;

use adblock::lists::{FilterSet, ParseOptions};
use adblock::request::Request;
use adblock::url_parser::{set_domain_resolver, ResolvesDomain};

#[cxx::bridge(namespace = "ghost::blocking")]
mod ffi {
    /// Whether a request is blocked, and by which rule (for the console).
    struct Verdict {
        blocked: bool,
        filter: String,
    }

    /// A document's cosmetic rules: what to hide on its site, and what the
    /// generic rules mustn't hide there.
    struct CosmeticResources {
        hide_selectors: Vec<String>,
        exceptions: Vec<String>,
        /// A `$generichide` exception: no generic rule on this page.
        generichide: bool,
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
        /// The site-specific selectors (and the generic ones too complex to
        /// index) for a document's URL, its exceptions and generichide. A URL
        /// the engine can't parse has none. Sorted, so answers are stable.
        fn cosmetic_resources(self: &Engine, url: &str) -> CosmeticResources;
        /// The generic selectors the class and id names match, less the
        /// document's exceptions. Sorted.
        fn generic_selectors(
            self: &Engine,
            classes: &Vec<String>,
            ids: &Vec<String>,
            exceptions: &Vec<String>,
        ) -> Vec<String>;
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
    // Every rule type: network rules for requests, cosmetic rules for pages
    // (Phase 4A); scriptlets and procedural rules are parsed but not used
    // until 4B.
    filters.add_filter_list(lists.to_string(), ParseOptions::default());
    Box::new(Engine(adblock::Engine::new_with_filter_set(filters)))
}

impl Engine {
    fn check(&self, url: &str, source_url: &str, request_type: &str, method: &str) -> ffi::Verdict {
        // adblock-rust parses methods in lowercase; HTTP sends them uppercase.
        match Request::new(url, source_url, request_type, &method.to_ascii_lowercase()) {
            Ok(request) => {
                let result = self.0.check_network_request(&request);
                ffi::Verdict {
                    blocked: result.should_block(),
                    filter: result.filter.map(|f| format!("{f:?}")).unwrap_or_default(),
                }
            }
            Err(_) => ffi::Verdict { blocked: false, filter: String::new() },
        }
    }

    fn cosmetic_resources(&self, url: &str) -> ffi::CosmeticResources {
        let resources = self.0.url_cosmetic_resources(url);
        ffi::CosmeticResources {
            hide_selectors: sorted(resources.hide_selectors),
            exceptions: sorted(resources.exceptions),
            generichide: resources.generichide,
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

fn sorted(set: HashSet<String>) -> Vec<String> {
    let mut strings: Vec<String> = set.into_iter().collect();
    strings.sort();
    strings
}
