// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// no-fetch-if (alias prevent-fetch): a fetch() the conditions match isn't
// sent; it resolves at once to an empty 200 response. Conditions as
// no-xhr-if's.
function noFetchIf(props = '') {
  const safe = shadeSafe();
  const matches = safe.requestMatcher(props);
  window.fetch = new safe.Proxy(window.fetch, {
    apply(fetch, self, args) {
      const input = args[0];
      const init = args[1];
      let request;
      try {
        const isRequest = input instanceof safe.Request;
        request = {
          url: new safe.URL(isRequest ? input.url : String(input), document.baseURI).href,
          method: String((init && init.method) || (isRequest ? input.method : 'GET'))
                      .toUpperCase(),
        };
      } catch {
        return safe.apply(fetch, self, args);
      }
      if (!matches(request)) {
        return safe.apply(fetch, self, args);
      }
      return safe.resolved(new safe.Response('', {status: 200, statusText: 'OK'}));
    },
  });
}
