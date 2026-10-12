// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// abort-current-script (aliases acs, abort-current-inline-script, acis): a
// script that reads or writes the property at the end of a chain is
// stopped with an exception when its text (inline) or URL (external)
// matches the needle; an empty needle matches every script. Other scripts
// see the property as before.
function abortCurrentScript(chain = '', needle = '') {
  const safe = shadeSafe();
  const currentScript = safe.getOwnPropertyDescriptor(Document.prototype, 'currentScript').get;
  const ScriptElement = HTMLScriptElement;
  const src = safe.getOwnPropertyDescriptor(ScriptElement.prototype, 'src').get;
  const text = safe.getOwnPropertyDescriptor(Node.prototype, 'textContent').get;
  const re = safe.pattern(needle);
  safe.quiet();
  shadeTrap(chain, () => {
    const script = safe.apply(currentScript, document, []);
    if (!(script instanceof ScriptElement)) {
      return;
    }
    if (safe.test(re, safe.apply(src, script, []) || safe.apply(text, script, []))) {
      throw safe.abort();
    }
  });
}
