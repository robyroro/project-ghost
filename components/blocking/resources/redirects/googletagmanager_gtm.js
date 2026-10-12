// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A stand-in for Google Tag Manager (redirect=googletagmanager_gtm.js):
// nothing loads or is sent, but the callbacks pages wait for
// (eventCallback, hitCallback) still run, so links and forms that wait for
// them work.
(function () {
  const run = callback => {
    if (typeof callback === 'function') {
      setTimeout(callback, 0);
    }
  };
  const runAll = items => {
    for (const item of items) {
      if (item && typeof item === 'object') {
        run(item.eventCallback);
      }
    }
  };
  const dataLayer = (window.dataLayer = window.dataLayer || []);
  runAll(dataLayer);
  dataLayer.push = function (...items) {
    runAll(items);
    return Array.prototype.push.apply(dataLayer, items);
  };
  window.google_tag_manager = window.google_tag_manager || {};
  window.ga = window.ga || function (...args) {
    for (const arg of args) {
      if (arg && typeof arg === 'object') {
        run(arg.hitCallback);
      }
    }
  };
})();
