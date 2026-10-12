// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
