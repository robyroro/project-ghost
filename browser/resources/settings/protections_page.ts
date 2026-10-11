// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {loadTimeData} from '//resources/js/load_time_data.js';
import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';

import {getHtml} from './protections_page.html.js';
import {searchTree} from './search.js';
import type {SearchResult} from './search.js';

// Shade's section of chrome://settings: a Settings "plugin" (an entry of the
// navigation menu), rendered by <settings-main> (patches/0038).
export class ShadeProtectionsPageElement extends CrLitElement {
  static get is() {
    return 'shade-protections-page';
  }

  override render() {
    return getHtml.bind(this)();
  }

  protected i18n_(name: string): string {
    return loadTimeData.getString(name);
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

customElements.define(ShadeProtectionsPageElement.is, ShadeProtectionsPageElement);
