// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The natives every scriptlet relies on, taken at the document's start,
// before any of the page's scripts can replace them. Scriptlets run in the
// page's own world (CosmeticFiltersAgent), all inside one function, so
// nothing here is left on window. Code that runs later, when the page
// touches a trapped property, calls only what was taken at the start.
//
// Only what most scriptlets share is taken here: touching an interface
// (Response, MutationObserver, ...) makes Blink install it, which costs
// time on every page. A scriptlet takes the other natives it needs itself,
// at its own start, which is also before the page's scripts.
function shadeSafe() {
  if (shadeSafe.natives) {
    return shadeSafe.natives;
  }
  const apply = Reflect.apply;
  const exec = RegExp.prototype.exec;
  const test = RegExp.prototype.test;
  const getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
  const getPrototypeOf = Object.getPrototypeOf;
  const addEventListener = EventTarget.prototype.addEventListener;
  const setTimeoutNative = setTimeout;
  const queueMicrotaskNative = queueMicrotask;
  // Set while an exception a scriptlet threw on purpose may be on its way to
  // the window's error event (abort()).
  let aborting = false;
  let quieting = false;
  const natives = {
    apply,
    defineProperty: Object.defineProperty,
    getOwnPropertyDescriptor,
    hasOwn: Object.hasOwn,
    Error,
    ReferenceError,
    addEventListener,
    exec(re, text) {
      return apply(exec, re, [String(text)]);
    },
    test(re, text) {
      return apply(test, re, [String(text)]);
    },
    // A native called as a method of this object would throw "Illegal
    // invocation": it needs window as `this`.
    setTimeout(callback, delay) {
      return apply(setTimeoutNative, window, [callback, delay]);
    },
    // Whether a list's pattern is a regular expression: /source/flags.
    isRegExp(text) {
      return apply(test, /^\/.+\/[imsu]*$/, [text]);
    },
    // A list's pattern: /source/flags is a regular expression, anything else
    // a literal substring. An invalid expression matches nothing.
    pattern(text) {
      const parts = apply(exec, /^\/(.+)\/([imsu]*)$/, [text]);
      if (parts) {
        try {
          return new RegExp(parts[1], parts[2]);
        } catch {
          return /(?!)/;
        }
      }
      return new RegExp(text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
    },
    // The descriptor `name` has on `owner` or its nearest prototype.
    descriptor(owner, name) {
      for (let object = owner; object; object = getPrototypeOf(object)) {
        const found = getOwnPropertyDescriptor(object, name);
        if (found) {
          return found;
        }
      }
      return undefined;
    },
    // The exception that stops a script (acs, aost), kept out of the console.
    // Scriptlets run as a script Blink sanitizes errors for, so the window's
    // error event reads "Script error." whatever was thrown: the flag tells
    // ours apart. It is dispatched before the next microtask, so the flag
    // lasts until then; a page that catches the exception gets no event.
    // The listener is added at the start of a scriptlet that may abort.
    quiet() {
      if (quieting) {
        return;
      }
      quieting = true;
      apply(addEventListener, window, ['error', event => {
        if (aborting) {
          aborting = false;
          event.preventDefault();
        }
      }]);
    },
    abort() {
      aborting = true;
      apply(queueMicrotaskNative, window, [() => {
        aborting = false;
      }]);
      return new natives.ReferenceError('Shade stopped this script');
    },
    // A request filter (no-xhr-if, no-fetch-if): `url:<pattern>`,
    // `method:<pattern>` or a bare pattern for the URL, separated by spaces;
    // all must match. With no condition, nothing matches.
    requestMatcher(props) {
      const conditions = [];
      for (const token of props.split(/\s+/)) {
        if (!token) {
          continue;
        }
        const colon = token.indexOf(':');
        const key = colon > 0 ? token.slice(0, colon) : '';
        if (key === 'url' || key === 'method') {
          conditions.push([key, natives.pattern(token.slice(colon + 1))]);
        } else {
          conditions.push(['url', natives.pattern(token)]);
        }
      }
      return request => {
        if (conditions.length === 0) {
          return false;
        }
        for (let i = 0; i < conditions.length; i++) {
          if (!natives.test(conditions[i][1], request[conditions[i][0]])) {
            return false;
          }
        }
        return true;
      };
    },
  };
  shadeSafe.natives = natives;
  return natives;
}
