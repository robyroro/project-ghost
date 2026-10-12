// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Calls check() whenever the page reads or writes the property at the end of
// a chain; check() throws to stop the script that touched it. Otherwise the
// property behaves as before, inherited accessors included.
function shadeTrap(chain, check) {
  const safe = shadeSafe();
  shadeChain(chain, (owner, name) => {
    const own = safe.getOwnPropertyDescriptor(owner, name);
    if (own && !own.configurable) {
      return;
    }
    const descriptor = safe.descriptor(owner, name);
    const accessor = descriptor && (descriptor.get || descriptor.set);
    let value = descriptor && !accessor ? descriptor.value : undefined;
    safe.defineProperty(owner, name, {
      configurable: true,
      enumerable: own ? own.enumerable : true,
      get() {
        check();
        if (!accessor) {
          return value;
        }
        return descriptor.get ? safe.apply(descriptor.get, this, []) : undefined;
      },
      set(next) {
        check();
        if (!accessor) {
          value = next;
        } else if (descriptor.set) {
          safe.apply(descriptor.set, this, [next]);
        }
      },
    });
  });
}
