// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Shade's cosmetic filtering: tells the agent which parts of this document
// changed. The agent reads their class and id names in C++, without a
// script touching each element (in an isolated world every element touched
// costs a wrapper of its own), and asks the browser for the generic hiding
// rules they match. Runs in an isolated world of its own
// (CosmeticFiltersAgent), so the page's scripts can't see or change it.
(() => {
  // After the first batch, at most one batch in this time.
  const INTERVAL_MS = 100;
  // A record that adds more nodes than this is scanned from its target (one
  // node for the agent) rather than node by node.
  const MAX_ADDED = 8;

  const scan = shadeCosmeticFilters.scan;
  // Subtrees to scan, and elements whose own class or id changed.
  let subtrees = new Set();
  let elements = new Set();
  let timer = 0;
  let sentFirst = false;

  function flush() {
    timer = 0;
    const someSubtrees = [...subtrees];
    const someElements = [...elements];
    subtrees = new Set();
    elements = new Set();
    scan(someSubtrees, someElements);
  }

  function schedule() {
    if (!subtrees.size && !elements.size) {
      return;
    }
    if (!sentFirst) {
      sentFirst = true;
      flush();
    } else if (!timer) {
      timer = setTimeout(flush, INTERVAL_MS);
    }
  }

  new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes') {
        elements.add(record.target);
        continue;
      }
      const added = record.addedNodes;
      if (added.length > MAX_ADDED) {
        subtrees.add(record.target);
        continue;
      }
      for (let i = 0; i < added.length; i++) {
        if (added[i].nodeType === Node.ELEMENT_NODE) {
          subtrees.add(added[i]);
        }
      }
    }
    schedule();
  }).observe(document, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ['class', 'id'],
  });

  if (document.documentElement) {
    subtrees.add(document.documentElement);
    schedule();
  }
})();
