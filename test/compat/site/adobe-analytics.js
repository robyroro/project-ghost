// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// tools/compat.py --self-test: what the page needs from its script.
const ready = document.createElement('p');
ready.id = 'ready';
ready.textContent = 'The script ran.';
document.body.appendChild(ready);
