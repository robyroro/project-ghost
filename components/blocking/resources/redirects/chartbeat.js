// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A stand-in for Chartbeat's tracker (redirect=chartbeat.js): pages that
// call its API keep working; nothing is measured or sent.
(function () {
  const noop = function () {};
  window.pSUPERFLY = window.pSUPERFLY || {activity: noop, virtualPage: noop};
})();
