// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ShadeSiteListElement} from './site_list.js';

export function getHtml(this: ShadeSiteListElement) {
  // clang-format off
  return html`
<div class="group">
  ${this.isEmpty_() ? html`
    <div class="row empty">${this.i18n_('shadeSitesEmpty')}</div>` : ''}
  ${this.sites_.map(entry => html`
    <div class="row site">
      <div class="favicon"
          style="background-image: ${this.favicon_(entry.site)}"></div>
      <div class="name">${entry.site}</div>
      <select class="md-select" data-site="${entry.site}"
          aria-label="${this.i18n_('shadeSiteLevel', entry.site)}"
          @change="${this.onLevelChange_}">
        <option value="off" ?selected="${this.isLevel_(entry, 'off')}">
          ${this.i18n_('shadeLevelOff')}
        </option>
        <option value="standard"
            ?selected="${this.isLevel_(entry, 'standard')}">
          ${this.i18n_('shadeLevelStandard')}
        </option>
        <option value="strict" ?selected="${this.isLevel_(entry, 'strict')}">
          ${this.i18n_('shadeLevelStrict')}
        </option>
      </select>
      <cr-icon-button class="icon-clear remove" data-site="${entry.site}"
          title="${this.i18n_('shadeRemoveSite', entry.site)}"
          aria-label="${this.i18n_('shadeRemoveSite', entry.site)}"
          @click="${this.onRemoveClick_}">
      </cr-icon-button>
    </div>`)}
</div>`;
  // clang-format on
}
