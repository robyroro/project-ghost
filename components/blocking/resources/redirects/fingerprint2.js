// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A stand-in for FingerprintJS 2 (redirect=fingerprint2.js): its API answers
// with an empty fingerprint, so pages that wait for one carry on.
(function () {
  const empty = [];
  const later = (callback, ...args) => {
    if (typeof callback === 'function') {
      setTimeout(() => callback(...args), 1);
    }
  };
  function Fingerprint2() {}
  Fingerprint2.get = (options, callback) =>
      later(typeof options === 'function' ? options : callback, empty);
  Fingerprint2.getV18 = (options, callback) =>
      later(typeof options === 'function' ? options : callback, '', empty);
  Fingerprint2.getPromise = () => Promise.resolve(empty);
  Fingerprint2.x64hash128 = () => '';
  Fingerprint2.prototype.get = function (callback) {
    later(callback, '', empty);
  };
  window.Fingerprint2 = Fingerprint2;
})();
