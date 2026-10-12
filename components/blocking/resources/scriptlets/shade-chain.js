// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
