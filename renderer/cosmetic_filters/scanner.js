// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Shade's cosmetic filtering: reports the class and id names of this
// document's elements to the browser, which answers with the generic hiding
// rules they match. Runs in an isolated world of its own
// (CosmeticFiltersAgent), so the page's scripts can't see or change it.
(() => {
  // //ghost/components/cosmetic_filters/names.h: the browser ends a renderer
  // that sends more.
  const MAX_NAMES = 1000;
  // After the first batch, at most one batch in this time.
  const INTERVAL_MS = 100;

  const report = shadeCosmeticFilters.report;
  const seenClasses = new Set();
  const seenIds = new Set();
  let classes = [];
  let ids = [];
  let timer = 0;
  let sentFirst = false;

  // Attributes, not element.id and classList: no DOMTokenList wrapper per
  // element in this world, and SVG elements answer the same way.
  function add(element) {
    const id = element.getAttribute('id');
    if (id && !seenIds.has(id)) {
      seenIds.add(id);
      ids.push(id);
    }
    const names = element.getAttribute('class');
    if (names) {
      for (const name of names.split(/\s+/)) {
        if (name && !seenClasses.has(name)) {
          seenClasses.add(name);
          classes.push(name);
        }
      }
    }
  }

  function visit(node) {
    if (node.nodeType !== Node.ELEMENT_NODE) {
      return;
    }
    add(node);
    if (node.firstElementChild) {
      for (const element of node.querySelectorAll('[id],[class]')) {
        add(element);
      }
    }
  }

  function flush() {
    timer = 0;
    while (classes.length || ids.length) {
      const someClasses = classes.splice(0, MAX_NAMES);
      const someIds = ids.splice(0, MAX_NAMES - someClasses.length);
      report(someClasses, someIds);
    }
  }

  function schedule() {
    if (!classes.length && !ids.length) {
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
        add(record.target);
      } else {
        record.addedNodes.forEach(visit);
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
    visit(document.documentElement);
    schedule();
  }
})();
