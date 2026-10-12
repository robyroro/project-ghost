// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// remove-node-text (alias rmnt): empties the elements of a name (`script`,
// `style`, ..., or `#text` for text nodes) whose text matches the needle, as
// the parser adds them: the observer runs before an added script does.
function removeNodeText(nodeName = '', needle = '') {
  const safe = shadeSafe();
  if (!nodeName || !needle) {
    return;
  }
  const wanted = nodeName === '#text' ? '#text' : nodeName.toUpperCase();
  const re = safe.pattern(needle);
  const empty = node => {
    if (safe.test(re, safe.apply(safe.textContent.get, node, []))) {
      safe.apply(safe.textContent.set, node, ['']);
    }
  };
  new safe.MutationObserver(records => {
    for (let i = 0; i < records.length; i++) {
      const added = records[i].addedNodes;
      for (let j = 0; j < added.length; j++) {
        const node = added[j];
        if (node.nodeName === wanted) {
          empty(node);
        } else if (node.nodeType === 3 && node.parentNode &&
                   node.parentNode.nodeName === wanted) {
          empty(node.parentNode);
        }
      }
    }
  }).observe(document, {childList: true, subtree: true});
}
