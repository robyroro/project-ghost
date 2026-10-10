// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ProtectionsAppElement} from './app.js';

export function getHtml(this: ProtectionsAppElement) {
  // clang-format off
  return html`<!--_html_template_start_-->
<div id="container" style="padding: 16px">Protections</div>
<!--_html_template_end_-->`;
  // clang-format on
}
