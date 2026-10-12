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
  const re = safe.pattern(needle);
  shadeTrap(chain, () => {
    const script = safe.apply(safe.currentScript, document, []);
    if (!(script instanceof safe.HTMLScriptElement)) {
      return;
    }
    const text = safe.apply(safe.scriptSrc, script, []) ||
                 safe.apply(safe.textContent.get, script, []);
    if (safe.test(re, text)) {
      throw safe.abort();
    }
  });
}
