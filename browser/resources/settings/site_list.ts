// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import '//resources/cr_elements/cr_icon_button/cr_icon_button.js';

import {getFaviconForPageURL} from '//resources/js/icon.js';
import {loadTimeData} from '//resources/js/load_time_data.js';
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';
import {PrefService} from '/shared/settings/prefs2/pref_service.js';
import {PrefServiceObserverMixinLit} from '/shared/settings/prefs2/pref_service_observer_mixin_lit.js';

import {getCss} from './site_list.css.js';
import {getHtml} from './site_list.html.js';

const kSiteLevelsPref = 'ghost.privacy_policy.site_levels';

export interface SiteEntry {
  site: string;
  level: string;
}

const ShadeSiteListElementBase = PrefServiceObserverMixinLit(CrLitElement);

// The sites with their own level, from the site levels pref, written back
// through settingsPrivate like every other control of the section. The keys
// are the panel's (registrable domains); the list only changes or removes
// them. It redraws when the pref changes and keeps no state of its own, so a
// choice made in the panel shows here at once.
export class ShadeSiteListElement extends ShadeSiteListElementBase {
  static get is() {
    return 'shade-site-list';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      sites_: {type: Array},
    };
  }

  protected accessor sites_: SiteEntry[] = [];

  override connectedCallback() {
    super.connectedCallback();
    this.addPrefObserver<Record<string, string>>(kSiteLevelsPref, pref => {
      this.sites_ = Object.entries(pref.value)
                        .sort(([a], [b]) => a.localeCompare(b))
                        .map(([site, level]) => ({site, level}));
    });
  }

  protected i18n_(name: string, arg?: string): string {
    return arg === undefined ? loadTimeData.getString(name) :
                               loadTimeData.getStringF(name, arg);
  }

  protected isEmpty_(): boolean {
    return this.sites_.length === 0;
  }

  protected favicon_(site: string): string {
    return getFaviconForPageURL(`https://${site}/`, false);
  }

  protected isLevel_(entry: SiteEntry, level: string): boolean {
    return entry.level === level;
  }

  protected async onLevelChange_(e: Event) {
    const select = e.target as HTMLSelectElement;
    const prefs = PrefService.getInstance();
    await prefs.whenInitialized();
    prefs.setPrefDictEntry(kSiteLevelsPref, select.dataset['site']!, select.value);
  }

  protected async onRemoveClick_(e: Event) {
    const site = (e.currentTarget as HTMLElement).dataset['site']!;
    const prefs = PrefService.getInstance();
    await prefs.whenInitialized();
    const levels = {...prefs.getPref<Record<string, string>>(kSiteLevelsPref).value};
    delete levels[site];
    prefs.setPrefValue(kSiteLevelsPref, levels);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'shade-site-list': ShadeSiteListElement;
  }
}

customElements.define(ShadeSiteListElement.is, ShadeSiteListElement);
