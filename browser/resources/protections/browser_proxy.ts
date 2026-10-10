// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import type {PageHandlerInterface} from './protections.mojom-webui.js';
import {PageCallbackRouter, PageHandlerFactory, PageHandlerRemote} from './protections.mojom-webui.js';

export class ProtectionsBrowserProxy {
  handler: PageHandlerInterface;
  callbackRouter: PageCallbackRouter;

  constructor() {
    this.callbackRouter = new PageCallbackRouter();
    const handler = new PageHandlerRemote();
    PageHandlerFactory.getRemote().createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        handler.$.bindNewPipeAndPassReceiver());
    this.handler = handler;
  }

  static getInstance(): ProtectionsBrowserProxy {
    return instance || (instance = new ProtectionsBrowserProxy());
  }

  static setInstance(obj: ProtectionsBrowserProxy) {
    instance = obj;
  }
}

let instance: ProtectionsBrowserProxy|null = null;
