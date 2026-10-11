// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import '//resources/cr_elements/cr_button/cr_button.js';
import '//resources/cr_elements/cr_radio_button/cr_radio_button.js';
import '//resources/cr_elements/cr_radio_group/cr_radio_group.js';
import '//resources/cr_elements/cr_toggle/cr_toggle.js';
import './site_list.js';

import {loadTimeData} from '//resources/js/load_time_data.js';
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';
import {LifetimeBrowserProxyImpl} from '/shared/settings/lifetime_browser_proxy.js';
import {PrefService} from '/shared/settings/prefs2/pref_service.js';
import {PrefServiceObserverMixinLit} from '/shared/settings/prefs2/pref_service_observer_mixin_lit.js';

import {getCss} from './protections_page.css.js';
import {getHtml} from './protections_page.html.js';
import {searchTree} from './search.js';
import type {SearchResult} from './search.js';

const kDefaultLevelPref = 'ghost.privacy_policy.default_level';
const kCampaignPref = 'ghost.query_filter.strip_campaign_parameters';
const kGpcPref = 'ghost.gpc.enabled';

export interface LevelCard {
  level: string;    // the pref string
  name: string;     // the loadTimeData key prefix: shadeLevelOff, ...
  recommended: boolean;
}

const ShadeProtectionsPageElementBase =
    PrefServiceObserverMixinLit(CrLitElement);

// Shade's section of chrome://settings: a Settings "plugin" (an entry of the
// navigation menu), rendered by <settings-main> (patches/0038). Its controls
// read and write Shade's prefs through settingsPrivate (patches/0040).
export class ShadeProtectionsPageElement extends
    ShadeProtectionsPageElementBase {
  static get is() {
    return 'shade-protections-page';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      defaultLevel_: {type: String},
      campaign_: {type: Boolean},
      gpc_: {type: Boolean},
    };
  }

  protected accessor defaultLevel_: string = 'standard';
  protected accessor campaign_: boolean = false;
  protected accessor gpc_: boolean = true;

  protected readonly levels_: LevelCard[] = [
    {level: 'off', name: 'shadeLevelOff', recommended: false},
    {level: 'standard', name: 'shadeLevelStandard', recommended: true},
    {level: 'strict', name: 'shadeLevelStrict', recommended: false},
  ];

  override connectedCallback() {
    super.connectedCallback();
    this.addPrefObserver<string>(kDefaultLevelPref, pref => {
      this.defaultLevel_ = pref.value;
    });
    this.addPrefObserver<boolean>(kCampaignPref, pref => {
      this.campaign_ = pref.value;
    });
    this.addPrefObserver<boolean>(kGpcPref, pref => {
      this.gpc_ = pref.value;
    });
  }

  protected i18n_(name: string): string {
    return loadTimeData.getString(name);
  }

  protected isChosen_(card: LevelCard): boolean {
    return card.level === this.defaultLevel_;
  }

  protected isOffChosen_(card: LevelCard): boolean {
    return card.level === 'off' && this.isChosen_(card);
  }

  // The level's sentences, one per item.
  protected details_(card: LevelCard): string[] {
    return this.i18n_(`${card.name}Details`)
        .split(/(?<=\.)\s+/)
        .filter(sentence => sentence.length > 0);
  }

  protected gpcNeedsRelaunch_(): boolean {
    return this.gpc_ !== loadTimeData.getBoolean('shadeGpcEnabledAtStartup');
  }

  protected async onLevelsSelectedChanged_(e: CustomEvent<{value: string}>) {
    await PrefService.getInstance().whenInitialized();
    PrefService.getInstance().setPrefValue(kDefaultLevelPref, e.detail.value);
  }

  protected async onCampaignChange_(e: CustomEvent<boolean>) {
    await PrefService.getInstance().whenInitialized();
    PrefService.getInstance().setPrefValue(kCampaignPref, e.detail);
  }

  protected async onGpcChange_(e: CustomEvent<boolean>) {
    await PrefService.getInstance().whenInitialized();
    PrefService.getInstance().setPrefValue(kGpcPref, e.detail);
  }

  protected onRelaunchClick_() {
    LifetimeBrowserProxyImpl.getInstance().relaunch();
  }

  async searchContents(query: string): Promise<SearchResult> {
    await this.updateComplete;
    return searchTree(this.shadowRoot, query);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'shade-protections-page': ShadeProtectionsPageElement;
  }
}

customElements.define(
    ShadeProtectionsPageElement.is, ShadeProtectionsPageElement);
