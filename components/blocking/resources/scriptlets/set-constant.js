// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// set-constant (alias set): the property at the end of a chain reads as a
// fixed, harmless value, and the page's assignments to it are ignored.
// Values: undefined, null, true, false, '' ('' and "" too), noopFunc,
// trueFunc, falseFunc, throwFunc, {}, [], emptyObj, emptyArr, and integers
// from -32767 to 32767. Any other value leaves the page alone.
function setConstant(chain = '', raw = '') {
  const safe = shadeSafe();
  const named = {
    undefined: () => undefined,
    null: () => null,
    true: () => true,
    false: () => false,
    '': () => '',
    "''": () => '',
    '""': () => '',
    noopFunc: () => function () {},
    trueFunc: () => () => true,
    falseFunc: () => () => false,
    throwFunc: () => () => {
      throw new safe.Error();
    },
    '{}': () => ({}),
    '[]': () => [],
    emptyObj: () => ({}),
    emptyArr: () => [],
  };
  let value;
  if (safe.hasOwn(named, raw)) {
    value = named[raw]();
  } else if (safe.test(/^-?\d{1,5}$/, raw) && Math.abs(Number(raw)) <= 32767) {
    value = Number(raw);
  } else {
    return;
  }
  shadeChain(chain, (owner, name) => {
    const descriptor = safe.getOwnPropertyDescriptor(owner, name);
    if (descriptor && !descriptor.configurable) {
      return;
    }
    safe.defineProperty(owner, name, {
      configurable: true,
      enumerable: descriptor ? descriptor.enumerable : true,
      get() {
        return value;
      },
      set() {},
    });
  });
}
