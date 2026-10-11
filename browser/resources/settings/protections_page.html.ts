// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ShadeProtectionsPageElement} from './protections_page.js';

export function getHtml(this: ShadeProtectionsPageElement) {
  // clang-format off
  return html`
<h1>${this.i18n_('shadeProtectionsTitle')}</h1>`;
  // clang-format on
}
