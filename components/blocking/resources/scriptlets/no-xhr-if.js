// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// no-xhr-if (alias prevent-xhr): an XMLHttpRequest the conditions match
// isn't sent; it completes at once as an empty 200 response. Conditions:
// `url:<pattern>`, `method:<pattern>`, or a bare pattern for the URL.
function noXhrIf(props = '') {
  const safe = shadeSafe();
  const matches = safe.requestMatcher(props);
  const {Proxy, URL, Event} = window;
  const dispatchEvent = EventTarget.prototype.dispatchEvent;
  const weakMapGet = WeakMap.prototype.get;
  const weakMapSet = WeakMap.prototype.set;
  const requests = new WeakMap();
  const proto = XMLHttpRequest.prototype;
  proto.open = new Proxy(proto.open, {
    apply(open, xhr, args) {
      let url = '';
      try {
        url = new URL(String(args[1]), document.baseURI).href;
      } catch {}
      safe.apply(weakMapSet, requests, [xhr, {method: String(args[0]).toUpperCase(), url}]);
      return safe.apply(open, xhr, args);
    },
  });
  proto.send = new Proxy(proto.send, {
    apply(send, xhr, args) {
      const request = safe.apply(weakMapGet, requests, [xhr]);
      if (!request || !matches(request)) {
        return safe.apply(send, xhr, args);
      }
      const type = xhr.responseType;
      const text = type === '' || type === 'text';
      const fixed = {
        readyState: 4,
        status: 200,
        statusText: 'OK',
        responseURL: request.url,
        response: text ? '' : type === 'arraybuffer' ? new ArrayBuffer(0)
                : type === 'blob' ? new Blob([]) : null,
      };
      if (text) {
        fixed.responseText = '';
      }
      for (const name of Object.keys(fixed)) {
        safe.defineProperty(xhr, name, {configurable: true, value: fixed[name]});
      }
      safe.setTimeout(() => {
        for (const event of ['readystatechange', 'load', 'loadend']) {
          safe.apply(dispatchEvent, xhr, [new Event(event)]);
        }
      }, 0);
      return undefined;
    },
  });
}
