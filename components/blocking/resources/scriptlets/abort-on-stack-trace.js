// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// abort-on-stack-trace (alias aost): reading or writing the property at the
// end of a chain throws when the call stack matches the needle. The stack
// is one line, its frames joined by tabs, each `<function> <url>` without
// line and column; a frame in the document's inline scripts reads
// `inlineScript`, one without a URL `injectedScript`. Shade's own frames
// (shade://) are left out.
function abortOnStackTrace(chain = '', needle = '') {
  const safe = shadeSafe();
  const re = safe.pattern(needle);
  const page = document.URL.split('#')[0];
  safe.quiet();
  const frame = /^\s*at (?:(.*?) \()?(.*?)(?::\d+:\d+)?\)?$/;
  const stackOf = () => {
    // The page may have changed how stacks are made.
    const limit = safe.Error.stackTraceLimit;
    const prepare = safe.Error.prepareStackTrace;
    safe.Error.stackTraceLimit = 50;
    safe.Error.prepareStackTrace = undefined;
    const stack = String(new safe.Error().stack);
    safe.Error.stackTraceLimit = limit;
    safe.Error.prepareStackTrace = prepare;
    return stack;
  };
  shadeTrap(chain, () => {
    const lines = stackOf().split('\n');
    let stack = '';
    for (let i = 1; i < lines.length; i++) {
      const parts = safe.exec(frame, lines[i]);
      if (!parts || parts[2].startsWith('shade://')) {
        continue;
      }
      const url = parts[2];
      const where = url === page ? 'inlineScript'
                  : url === '' || url === '<anonymous>' ? 'injectedScript'
                  : url;
      stack += (stack ? '\t' : '') + (parts[1] ? `${parts[1]} ${where}` : where);
    }
    if (safe.test(re, stack)) {
      throw safe.abort();
    }
  });
}
