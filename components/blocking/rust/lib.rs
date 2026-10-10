// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! adblock-rust behind a small cxx bridge: build an engine from list text,
//! ask it about one request. Everything else (threads, lifetimes, policy)
//! is C++'s, in //ghost/components/blocking.

use adblock::lists::{FilterSet, ParseOptions, RuleTypes};
use adblock::request::Request;
use adblock::url_parser::{set_domain_resolver, ResolvesDomain};

#[cxx::bridge(namespace = "ghost::blocking")]
mod ffi {
    /// Whether a request is blocked, and by which rule (for the console).
    struct Verdict {
        blocked: bool,
        filter: String,
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
    // Network rules only: cosmetic filtering is Phase 4, and leaving its rules
    // out keeps the engine smaller.
    filters.add_filter_list(
        lists.to_string(),
        ParseOptions { rule_types: RuleTypes::NetworkOnly, ..Default::default() },
    );
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
}
