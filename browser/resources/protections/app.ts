// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {CrLitElement} from '//resources/lit/v3_0/lit.rollup.js';

import {getHtml} from './app.html.js';
import {ProtectionsBrowserProxy} from './browser_proxy.js';

export class ProtectionsAppElement extends CrLitElement {
  static get is() {
    return 'protections-app';
  }

  override render() {
    return getHtml.bind(this)();
  }

  override firstUpdated() {
    ProtectionsBrowserProxy.getInstance().handler.showUI();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'protections-app': ProtectionsAppElement;
  }
}

customElements.define(ProtectionsAppElement.is, ProtectionsAppElement);
