// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type {PageHandlerInterface} from './protections.mojom-webui.js';
import {PageHandler} from './protections.mojom-webui.js';

export class ProtectionsBrowserProxy {
  handler: PageHandlerInterface;

  constructor() {
    this.handler = PageHandler.getRemote();
  }

  static getInstance(): ProtectionsBrowserProxy {
    return instance || (instance = new ProtectionsBrowserProxy());
  }

  static setInstance(obj: ProtectionsBrowserProxy) {
    instance = obj;
  }
}

let instance: ProtectionsBrowserProxy|null = null;
