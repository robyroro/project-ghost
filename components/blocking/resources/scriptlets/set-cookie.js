// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
  // Blink allows no navigation while the document starts: reload after.
  if (reload && read() === value) {
    safe.setTimeout(() => location.reload(), 0);
  }
}
