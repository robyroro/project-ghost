// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {html} from '//resources/lit/v3_0/lit.rollup.js';

import type {ShadeProtectionsPageElement} from './protections_page.js';

export function getHtml(this: ShadeProtectionsPageElement) {
  // clang-format off
  return html`
<h1 id="title">${this.i18n_('shadeProtectionsTitle')}</h1>
<div class="lead">${this.i18n_('shadeProtectionsLead')}</div>

<h2>${this.i18n_('shadeDefaultLevel')}</h2>
<cr-radio-group id="levels" .selected="${this.defaultLevel_}"
    aria-label="${this.i18n_('shadeDefaultLevel')}"
    @selected-changed="${this.onLevelsSelectedChanged_}">
  ${this.levels_.map(card => html`
    <cr-radio-button class="card" name="${card.level}"
        label="${this.i18n_(card.name)}">
      ${card.recommended ? html`
        <span class="chip">${this.i18n_('shadeRecommended')}</span>` : ''}
      <div class="summary">${this.i18n_(`${card.name}Summary`)}</div>
      ${this.isChosen_(card) ? html`
        <ul class="details">
          ${this.details_(card).map(sentence => html`<li>${sentence}</li>`)}
        </ul>` : ''}
      ${this.isOffChosen_(card) ? html`
        <div class="warning">${this.i18n_('shadeLevelOffWarning')}</div>` : ''}
    </cr-radio-button>`)}
</cr-radio-group>
<div class="note">${this.i18n_('shadeIncognitoNote')}</div>

<h2>${this.i18n_('shadeMoreProtections')}</h2>
<div class="group">
  <div class="row">
    <div class="text">
      <div>${this.i18n_('shadeGpc')}</div>
      <div class="sub">${this.i18n_('shadeGpcSub')}</div>
    </div>
    ${this.gpcNeedsRelaunch_() ? html`
      <cr-button id="relaunch" @click="${this.onRelaunchClick_}">
        ${this.i18n_('shadeRelaunch')}
      </cr-button>` : ''}
    <cr-toggle id="gpc" ?checked="${this.gpc_}"
        aria-label="${this.i18n_('shadeGpc')}"
        @change="${this.onGpcChange_}">
    </cr-toggle>
  </div>
  <div class="row">
    <div class="text">
      <div>${this.i18n_('shadeCampaign')}</div>
      <div class="sub">${this.i18n_('shadeCampaignSub')}</div>
    </div>
    <cr-toggle id="campaign" ?checked="${this.campaign_}"
        aria-label="${this.i18n_('shadeCampaign')}"
        @change="${this.onCampaignChange_}">
    </cr-toggle>
  </div>
</div>

<h2>${this.i18n_('shadeSites')}</h2>
<shade-site-list></shade-site-list>`;
  // clang-format on
}
