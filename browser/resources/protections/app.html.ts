// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import type {ProtectionsAppElement} from './app.js';

export function getHtml(this: ProtectionsAppElement) {
  // clang-format off
  return this.applies_() ? html`<!--_html_template_start_-->
<div id="header"><div id="site">${this.site_()}</div></div>
${this.asking_() ? html`
<div class="step">
  <div class="title" id="ask-title">${this.i18n_('askTitle')}</div>
  <p>${this.i18nF_('askText', this.site_(), this.levelName_(this.checkTo_()))}</p>
  <div class="buttons">
    <button id="ask-no" @click="${this.onAskNoClick_}">${this.i18nF_('askNo',
        this.levelName_(this.checkFrom_()))}</button>
    <button id="ask-yes" class="action" @click="${this.onAskYesClick_}">${
        this.i18n_('askYes')}</button>
  </div>
  <p class="note">${this.i18n_('askNote')}</p>
</div>` : this.view_ === 'offer' ? html`
<div class="step">
  <div class="title">${this.i18n_('notWorking')}</div>
  <p>${this.i18nF_('offerText', this.levelName_(this.lower_()), this.site_())}</p>
  <div class="buttons">
    <button id="offer-cancel" @click="${this.onCancelClick_}">${this.i18n_('cancel')}</button>
    <button id="offer-reload" class="action" @click="${this.onOfferReloadClick_}">${
        this.i18nF_('offerReload', this.levelName_(this.lower_()))}</button>
  </div>
</div>` : this.view_ === 'report' ? html`
<div class="step">
  <div class="title" id="report-site">${this.i18nF_('reportTitle',
      this.report_ ? this.report_.site : this.site_())}</div>
  ${this.report_ ? html`
  <label class="field">${this.i18n_('reportCategory')}
    <select id="report-category" @change="${this.onCategoryChange_}">
      ${this.categories_().map(item => html`
      <option value="${item.value}" ?selected="${item.value === this.category_}">${
          item.name}</option>`)}
    </select>
  </label>
  <textarea id="report-details" rows="2" maxlength="2000"
      placeholder="${this.i18n_('reportDetails')}" .value="${this.details_}"
      @input="${this.onDetailsInput_}"></textarea>
  <label class="field">${this.i18n_('reportAddress')}
    <input id="report-address" type="url" maxlength="2048" .value="${this.address_}"
        @input="${this.onAddressInput_}">
  </label>
  <div id="report-fields">${this.report_.fields.map(field => html`
    <div class="report-field"><span class="label">${field.label}</span>
<span class="value">${field.value}</span></div>`)}</div>
  <p class="note">${this.i18n_('reportNote')}</p>
  <div class="buttons">
    <button id="report-cancel" @click="${this.onCancelClick_}">${this.i18n_('cancel')}</button>
    <button id="report-open" class="action" @click="${this.onReportOpenClick_}">${
        this.i18n_('reportOpen')}</button>
  </div>` : html`<p class="note">${this.i18n_('reportLoading')}</p>`}
</div>` : html`
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
<button id="not-working" @click="${this.onNotWorkingClick_}">${
    this.i18n_('notWorking')}</button>`}
${this.offTheRecord_() ?
    html`<div id="note">${this.i18n_('incognitoNote')}</div>` : nothing}
<div id="footer">
  <a id="settings" href="#" @click="${this.onSettingsClick_}">
    ${this.i18n_('settingsLink')}
  </a>
</div>
<!--_html_template_end_-->` : nothing;
  // clang-format on
}
