// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import {highlight, removeHighlights, stripDiacritics} from '//resources/js/search_highlight_utils.js';
import type {Range} from '//resources/js/search_highlight_utils.js';

// What chrome://settings' <settings-main> expects from a plugin's
// searchContents (settings/search_settings.ts, typed here to avoid importing
// Settings, which depends on this library).
export interface SearchResult {
  canceled: boolean;
  matchCount: number;
  wasClearSearch: boolean;
}

function escape(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// The highlights of the last search in each root, removed by the next one.
const highlights: WeakMap<ShadowRoot, HTMLElement[]> = new WeakMap();

// Highlights |query| in the text of |root| (visible text only: closed cards
// render no list) and counts the matches.
export function searchTree(root: ShadowRoot, query: string): SearchResult {
  removeHighlights(highlights.get(root) || []);
  highlights.delete(root);
  if (query === '') {
    return {canceled: false, matchCount: 0, wasClearSearch: true};
  }
  const pattern = new RegExp(escape(stripDiacritics(query)), 'gi');
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const nodes: Text[] = [];
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    if (node.parentElement?.closest('style, [no-search]') === null) {
      nodes.push(node as Text);
    }
  }
  let matchCount = 0;
  const wrappers: HTMLElement[] = [];
  for (const node of nodes) {
    const ranges: Range[] = [];
    for (const match of stripDiacritics(node.data).matchAll(pattern)) {
      ranges.push({start: match.index, length: match[0].length});
    }
    if (ranges.length > 0) {
      wrappers.push(highlight(node, ranges));
      matchCount += ranges.length;
    }
  }
  highlights.set(root, wrappers);
  return {canceled: false, matchCount, wasClearSearch: false};
}
