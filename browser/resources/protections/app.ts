// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import '/strings.m.js';

import {loadTimeData} from '//resources/js/load_time_data.js';
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';
import type {PropertyValues} from '//resources/lit/v3_0/lit.rollup.js';

import {getCss} from './app.css.js';
import {getHtml} from './app.html.js';
import {ProtectionsBrowserProxy} from './browser_proxy.js';
import type {State} from './protections.mojom-webui.js';
import {Level} from './protections.mojom-webui.js';

export class ProtectionsAppElement extends CrLitElement {
  static get is() {
    return 'protections-app';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      state_: {type: Object},
    };
  }

  protected accessor state_: State|null = null;
  private shown_: boolean = false;

  override connectedCallback() {
    super.connectedCallback();
    const proxy = ProtectionsBrowserProxy.getInstance();
    proxy.callbackRouter.onStateChanged.addListener((state: State) => {
      this.state_ = state;
    });
    // A preloaded page is shown again later, for whichever tab is active then.
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') {
        this.refresh_();
      }
    });
    this.refresh_();
  }

  override updated(changedProperties: PropertyValues<this>) {
    super.updated(changedProperties);
    if (this.state_ && !this.shown_) {
      this.shown_ = true;
      ProtectionsBrowserProxy.getInstance().handler.showUI();
    }
  }

  private async refresh_() {
    const {state} = await ProtectionsBrowserProxy.getInstance().handler.getState();
    this.state_ = state;
  }

  protected i18n_(name: string): string {
    return loadTimeData.getString(name);
  }

  protected applies_(): boolean {
    return !!this.state_ && this.state_.applies;
  }

  protected site_(): string {
    return this.state_ ? this.state_.site : '';
  }

  protected isOff_(): boolean {
    return this.isChosen_(Level.kOff);
  }

  protected blockedCount_(): number {
    return this.state_ ? this.state_.blockedCount : 0;
  }

  protected countLabel_(): string {
    return this.i18n_(this.blockedCount_() === 1 ? 'blockedOne' : 'blockedMany');
  }

  protected offTheRecord_(): boolean {
    return !!this.state_ && this.state_.offTheRecord;
  }

  protected isChosen_(level: Level): boolean {
    return !!this.state_ && this.state_.level === level;
  }

  protected isDefault_(level: Level): boolean {
    return !!this.state_ && this.state_.modeDefault === level;
  }

  protected levels_(): Array<{level: Level, name: string}> {
    return [
      {level: Level.kOff, name: this.i18n_('levelOff')},
      {level: Level.kStandard, name: this.i18n_('levelStandard')},
      {level: Level.kStrict, name: this.i18n_('levelStrict')},
    ];
  }

  protected about_(): string {
    switch (this.state_?.level) {
      case Level.kOff:
        return this.i18n_('aboutOff');
      case Level.kStrict:
        return this.i18n_('aboutStrict');
      default:
        return this.i18n_('aboutStandard');
    }
  }

  protected onLevelChange_(e: Event) {
    const level = Number((e.target as HTMLInputElement).value) as Level;
    ProtectionsBrowserProxy.getInstance().handler.setLevel(level);
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'protections-app': ProtectionsAppElement;
  }
}

customElements.define(ProtectionsAppElement.is, ProtectionsAppElement);
