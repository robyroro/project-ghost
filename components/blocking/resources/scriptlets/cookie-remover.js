// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
