// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Shade's procedural cosmetic rules (`:has-text()`, `:upward()`, ...) and
// action rules (`:style()`, `:remove()`, ...) that CSS can't express: finds
// the elements each rule selects and applies its action, at the document's
// start and again as the page changes. Runs in Shade's isolated world
// (CosmeticFiltersAgent) beside scanner.js, so the page can't see it or
// change what it uses. The rules are adblock-rust's JSON, checked by the
// browser.
(() => {
  const api = shadeCosmeticFilters;
  // After the first pass, at most one pass in this time.
  const INTERVAL_MS = 100;
  // The attributes this document's passes use: random, so a page can't
  // style or look for them in advance (components/cosmetic_filters/
  // procedural.h: IsMarkerAttribute).
  const token = Array.from(crypto.getRandomValues(new Uint8Array(8)),
                           b => b.toString(16).padStart(2, '0')).join('');
  const QUERY = `shade-q-${token}`;
  // The operators no browser's CSS knows. adblock-rust splits a rule at
  // them, except inside :has(), which it leaves to us.
  const PROCEDURAL = ['has-text', 'matches-attr', 'matches-css', 'matches-css-before',
                      'matches-css-after', 'matches-path', 'min-text-length', 'upward',
                      'xpath'];

  // A list's text argument: quotes around it are the list's; /source/flags
  // is a regular expression, anything else a literal substring (or, with
  // `exact`, the whole text).
  function pattern(text, exact = false) {
    let value = text.trim();
    if (value.length >= 2 && (value[0] === '"' || value[0] === "'") &&
        value.at(-1) === value[0]) {
      value = value.slice(1, -1);
    }
    const parts = /^\/(.+)\/([imsu]*)$/.exec(value);
    if (parts) {
      return new RegExp(parts[1], parts[2]);
    }
    return {test: s => (exact ? s === value : s.includes(value))};
  }

  function unique(elements) {
    return [...new Set(elements)];
  }

  function hasProcedural(selector) {
    return PROCEDURAL.some(name => selector.includes(`:${name}(`));
  }

  // `selector` relative to each element: `> .a`, `+ .a`, ` .a`, or `.a` for
  // the element itself.
  function relative(selector, elements) {
    const found = new Set();
    for (const element of elements) {
      element.setAttribute(QUERY, '');
      try {
        for (const match of document.querySelectorAll(`[${QUERY}]${selector}`)) {
          found.add(match);
        }
      } finally {
        element.removeAttribute(QUERY);
      }
    }
    return [...found];
  }

  // The index of the `)` closing the `(` at `open`, past quotes and escapes.
  function closing(text, open) {
    let depth = 0;
    for (let i = open; i < text.length; i++) {
      const c = text[i];
      if (c === '\\') {
        i++;
      } else if (c === '"' || c === "'") {
        const end = text.indexOf(c, i + 1);
        if (end < 0) {
          break;
        }
        i = end;
      } else if (c === '(') {
        depth++;
      } else if (c === ')' && --depth === 0) {
        return i;
      }
    }
    throw new Error(`unbalanced: ${text}`);
  }

  // A selector that holds procedural operators where adblock-rust didn't
  // split it (inside :has()), as operators: CSS between them, a procedural
  // operator as itself, and :has() around one as a `has` step.
  function parse(text) {
    const operators = [];
    let css = '';
    const flush = () => {
      // Leading space is a combinator when the selector is relative.
      if (css.trim() || !operators.length) {
        operators.push({type: 'css-selector', arg: css.trim() ? css : '*'});
      }
      css = '';
    };
    for (let i = 0; i < text.length;) {
      const c = text[i];
      const call = c === ':' ? /^:([a-z-]+)\(/.exec(text.slice(i)) : null;
      if (c === '\\') {
        css += text.slice(i, i + 2);
        i += 2;
      } else if (c === '"' || c === "'") {
        const end = text.indexOf(c, i + 1);
        if (end < 0) {
          throw new Error(`unbalanced: ${text}`);
        }
        css += text.slice(i, end + 1);
        i = end + 1;
      } else if (call) {
        const open = i + call[0].length - 1;
        const close = closing(text, open);
        const name = call[1];
        const arg = text.slice(open + 1, close);
        if (PROCEDURAL.includes(name)) {
          flush();
          operators.push({type: name, arg});
        } else if (name === 'has' && hasProcedural(arg)) {
          flush();
          operators.push({type: 'has', arg});
        } else {
          css += text.slice(i, close + 1);
        }
        i = close + 1;
      } else {
        css += c;
        i++;
      }
    }
    if (css.trim()) {
      operators.push({type: 'css-selector', arg: css});
    }
    return operators;
  }

  function cssMatcher(arg, pseudo) {
    const colon = arg.indexOf(':');
    if (colon < 0) {
      throw new Error(`matches-css: ${arg}`);
    }
    const property = arg.slice(0, colon).trim();
    const value = pattern(arg.slice(colon + 1), /*exact=*/true);
    return elements => elements.filter(
        e => value.test(getComputedStyle(e, pseudo).getPropertyValue(property)));
  }

  function attrMatcher(arg) {
    let rest = arg.trim();
    let name;
    if (rest[0] === '"') {
      const end = rest.indexOf('"', 1);
      if (end < 0) {
        throw new Error(`matches-attr: ${arg}`);
      }
      name = rest.slice(1, end);
      rest = rest.slice(end + 1);
    } else {
      const eq = rest.indexOf('=');
      name = eq < 0 ? rest : rest.slice(0, eq);
      rest = eq < 0 ? '' : rest.slice(eq);
    }
    if (rest && rest[0] !== '=') {
      throw new Error(`matches-attr: ${arg}`);
    }
    const names = pattern(name, /*exact=*/true);
    const values = rest ? pattern(rest.slice(1), /*exact=*/true) : null;
    return elements => elements.filter(e => Array.from(e.attributes).some(
        a => names.test(a.name) && (!values || values.test(a.value))));
  }

  // Each operator, from its argument, to a step: elements in, elements out.
  // The first step gets null and starts from the document.
  const OPERATORS = {
    'css-selector': arg => {
      if (hasProcedural(arg)) {
        return steps(parse(arg));
      }
      return elements =>
          elements === null ? [...document.querySelectorAll(arg)] : relative(arg, elements);
    },
    // The elements with a descendant (or, after a leading combinator, a
    // relative) the inner selector selects.
    'has': arg => {
      const inner = parse(arg);
      if (inner[0].type === 'css-selector' && !/^\s*[>+~]/.test(inner[0].arg)) {
        inner[0] = {type: 'css-selector', arg: ` ${inner[0].arg}`};
      }
      const select = steps(inner);
      return elements => elements.filter(e => select([e]).length > 0);
    },
    'has-text': arg => {
      const text = pattern(arg);
      return elements => elements.filter(e => text.test(e.textContent));
    },
    'matches-attr': attrMatcher,
    'matches-css': arg => cssMatcher(arg, null),
    'matches-css-before': arg => cssMatcher(arg, '::before'),
    'matches-css-after': arg => cssMatcher(arg, '::after'),
    'matches-path': arg => {
      const path = pattern(arg);
      return elements => (path.test(location.pathname + location.search) ? elements : []);
    },
    'min-text-length': arg => {
      const length = Number(arg);
      if (!Number.isInteger(length) || length < 0) {
        throw new Error(`min-text-length: ${arg}`);
      }
      return elements => elements.filter(e => e.textContent.length >= length);
    },
    'upward': arg => {
      const count = Number(arg);
      if (Number.isInteger(count)) {
        if (count < 1 || count > 256) {
          throw new Error(`upward: ${arg}`);
        }
        return elements => unique(elements.map(e => {
          let ancestor = e;
          for (let i = 0; i < count && ancestor; i++) {
            ancestor = ancestor.parentElement;
          }
          return ancestor;
        }).filter(Boolean));
      }
      return elements =>
          unique(elements.map(e => e.parentElement && e.parentElement.closest(arg))
                     .filter(Boolean));
    },
    'xpath': arg => elements => {
      const found = new Set();
      for (const context of elements === null ? [document] : elements) {
        const result = document.evaluate(arg, context, null,
                                         XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
        for (let i = 0; i < result.snapshotLength; i++) {
          const node = result.snapshotItem(i);
          if (node.nodeType === Node.ELEMENT_NODE) {
            found.add(node);
          }
        }
      }
      return [...found];
    },
  };

  // Operators to one step that runs them in order.
  function steps(operators) {
    const compiled = operators.map(operator => {
      const make = OPERATORS[operator.type];
      if (!make) {
        throw new Error(`unknown operator ${operator.type}`);
      }
      return make(operator.arg);
    });
    return elements => {
      for (const step of compiled) {
        elements = step(elements);
      }
      return elements;
    };
  }

  // A rule: its selection, and either a marker attribute (hiding and style,
  // undone when the element no longer matches) or an action applied once.
  function compile(filter, index) {
    const first = filter.selector[0];
    if (!first || (first.type !== 'css-selector' && first.type !== 'xpath')) {
      throw new Error('a rule starts with a selector');
    }
    const rule = {select: steps(filter.selector), marked: new Set()};
    const action = filter.action;
    if (!action) {
      rule.mark = `shade-${index}-${token}`;
      api.hide(rule.mark);
    } else if (action.type === 'style') {
      rule.mark = `shade-${index}-${token}`;
      api.style(rule.mark, action.arg);
    } else if (action.type === 'remove') {
      rule.apply = e => e.remove();
    } else if (action.type === 'remove-attr') {
      rule.apply = e => e.removeAttribute(action.arg);
    } else if (action.type === 'remove-class') {
      rule.apply = e => e.classList.remove(action.arg);
    } else {
      throw new Error(`unknown action ${action.type}`);
    }
    return rule;
  }

  const rules = [];
  api.filters().forEach((json, index) => {
    try {
      rules.push(compile(JSON.parse(json), index));
    } catch {
      // A rule this executor can't run is skipped; the others still apply.
    }
  });
  if (!rules.length) {
    return;
  }

  let timer = 0;

  function pass() {
    timer = 0;
    for (const rule of rules) {
      let elements;
      try {
        elements = rule.select(null);
      } catch {
        continue;
      }
      if (rule.mark) {
        const now = new Set(elements);
        for (const element of rule.marked) {
          if (!now.has(element)) {
            element.removeAttribute(rule.mark);
          }
        }
        for (const element of now) {
          if (!element.hasAttribute(rule.mark)) {
            element.setAttribute(rule.mark, '');
          }
        }
        rule.marked = now;
      } else {
        elements.forEach(rule.apply);
      }
    }
  }

  // Our own markers change attributes too: those records are ours.
  new MutationObserver(records => {
    if (records.every(r => r.type === 'attributes' && r.attributeName.startsWith('shade-'))) {
      return;
    }
    if (!timer) {
      timer = setTimeout(pass, INTERVAL_MS);
    }
  }).observe(document, {childList: true, subtree: true, characterData: true, attributes: true});

  pass();
})();
