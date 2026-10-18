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
import type {Report, State} from './protections.mojom-webui.js';
import {Category, Level, Step} from './protections.mojom-webui.js';

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
      view_: {type: String},
      report_: {type: Object},
      category_: {type: Number},
      details_: {type: String},
      address_: {type: String},
    };
  }

  protected accessor state_: State|null = null;
  // The panel's own view: the levels, the offer, or the report. The browser's
  // step decides "asking" (BreakageCheck).
  protected accessor view_: 'main'|'offer'|'report' = 'main';
  protected accessor report_: Report|null = null;
  protected accessor category_: Category = Category.kContent;
  protected accessor details_: string = '';
  protected accessor address_: string = '';
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

  protected asking_(): boolean {
    return !!this.state_ && this.state_.step === Step.kAsking;
  }

  protected levelName_(level: Level): string {
    switch (level) {
      case Level.kOff:
        return this.i18n_('levelOff');
      case Level.kStrict:
        return this.i18n_('levelStrict');
      default:
        return this.i18n_('levelStandard');
    }
  }

  // The level one lower, which the offer proposes.
  protected lower_(): Level {
    return this.state_?.level === Level.kStrict ? Level.kStandard : Level.kOff;
  }

  protected checkFrom_(): Level {
    return this.state_ ? this.state_.checkFrom : Level.kStandard;
  }

  protected checkTo_(): Level {
    return this.state_ ? this.state_.checkTo : Level.kOff;
  }

  protected i18nF_(name: string, ...args: string[]): string {
    return loadTimeData.getStringF(name, ...args);
  }

  protected categories_(): Array<{value: Category, name: string}> {
    return [
      {value: Category.kContent, name: this.i18n_('categoryContent')},
      {value: Category.kMedia, name: this.i18n_('categoryMedia')},
      {value: Category.kSignIn, name: this.i18n_('categorySignIn')},
      {value: Category.kOther, name: this.i18n_('categoryOther')},
    ];
  }

  protected onNotWorkingClick_() {
    // Nothing lower to try at Off, nor once the user answered.
    if (this.isOff_() || this.state_?.step === Step.kAnswered) {
      this.showReport_();
    } else {
      this.view_ = 'offer';
    }
  }

  protected onOfferReloadClick_() {
    this.view_ = 'main';
    ProtectionsBrowserProxy.getInstance().handler.tryLowerLevel();
  }

  protected onCancelClick_() {
    this.view_ = 'main';
    this.report_ = null;
  }

  protected onAskYesClick_() {
    ProtectionsBrowserProxy.getInstance().handler.keepLevel();
    this.showReport_();
  }

  protected onAskNoClick_() {
    ProtectionsBrowserProxy.getInstance().handler.restoreLevel();
    this.showReport_();
  }

  private async showReport_() {
    this.view_ = 'report';
    this.report_ = null;
    const {report} = await ProtectionsBrowserProxy.getInstance().handler.getReport();
    this.report_ = report;
    this.address_ = report.address;
    this.category_ = Category.kContent;
    this.details_ = '';
  }

  protected onCategoryChange_(e: Event) {
    this.category_ = Number((e.target as HTMLSelectElement).value) as Category;
  }

  protected onDetailsInput_(e: Event) {
    this.details_ = (e.target as HTMLTextAreaElement).value;
  }

  protected onAddressInput_(e: Event) {
    this.address_ = (e.target as HTMLInputElement).value;
  }

  protected onReportOpenClick_() {
    ProtectionsBrowserProxy.getInstance().handler.openReport(
        this.category_, this.details_, this.address_);
  }

  protected onSettingsClick_(e: Event) {
    e.preventDefault();
    ProtectionsBrowserProxy.getInstance().handler.openSettings();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'protections-app': ProtectionsAppElement;
  }
}

customElements.define(ProtectionsAppElement.is, ProtectionsAppElement);
