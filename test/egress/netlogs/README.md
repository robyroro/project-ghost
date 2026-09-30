# NetLog fixtures

NetLogs captured from our own builds, so that the egress audit's parser is tested against the event shapes Chromium really writes (`tools/tests/test_egress_audit.py`, `RealNetLogTest`).

Each one is reduced with `tools/egress_audit.py scrub`:
- it keeps only the parameters the audit reads;
- it replaces the DNS resolvers' addresses with documentation addresses;
- it refuses to write a log in which any other non-public address survives.

A raw NetLog holds the capturing machine's addresses, network adapters and DNS configuration. Raw logs are never committed. Read a scrubbed log through before committing it.

| File | Build | Run |
|---|---|---|
| `scenarios-152.0.7977.140.json` | Ghost dev build at 152.0.7977.140, before the password leak check was turned off | `run --idle-seconds 60` with the `address` and `login` scenarios, 2026-09-30. It holds one finding: `passwordsleakcheck-pa.googleapis.com`, first seen during `login`. |
