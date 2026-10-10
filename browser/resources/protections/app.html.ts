// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {ProtectionsAppElement} from './app.js';

export function getHtml(this: ProtectionsAppElement) {
  // clang-format off
  return this.applies_() ? html`<!--_html_template_start_-->
<div id="header"><div id="site">${this.site_()}</div></div>
${this.isOff_() ? html`
<div class="summary">
  <span class="big off">${this.i18n_('levelOff')}</span>
  <span class="label">${this.i18n_('offLabel')}</span>
</div>` : html`
<div class="summary">
  <span class="big" id="count">${this.blockedCount_()}</span>
  <span class="label">${this.countLabel_()}</span>
</div>`}
<div id="levels" role="radiogroup" aria-label="${this.i18n_('levelGroup')}">
  ${this.levels_().map(item => html`
  <label>
    <input type="radio" name="level" value="${item.level}"
        .checked="${this.isChosen_(item.level)}" @change="${this.onLevelChange_}">
    ${item.name}${this.isDefault_(item.level) ? html`<span class="default-mark">
      · ${this.i18n_('defaultMark')}</span>` : nothing}
  </label>`)}
</div>
<p id="about">${this.about_()}</p>
${this.offTheRecord_() ?
    html`<div id="note">${this.i18n_('incognitoNote')}</div>` : nothing}
<!--_html_template_end_-->` : nothing;
  // clang-format on
}
