// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
/// redirects under theirs. A scriptlet's names end in `.js`: the engine adds
/// it to the name in `+js(name, ...)`. The engine injects only
/// `application/javascript` resources; `fn/javascript` is for their
/// dependencies.
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
        mime: "application/javascript",
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
