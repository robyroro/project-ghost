# 0005. Storage partitions for identities, off-the-record profiles for Ghost sessions

- Status: Accepted
- Date: 2026-09-28

## Context

Two features need isolated browser state.

- **Identities** (Personal, Work, Banking, …) are persistent. Tabs from several identities appear in the same window. The canonical use is being signed in to two accounts on the same site at once.
- **Ghost sessions** are disposable. Everything a session creates must be gone when it ends, and concurrent sessions must not see each other.

Chromium offers two isolation units.

- **`Profile` / `BrowserContext`**
  - Every profile-scoped service is separate: history, permissions, autofill, extensions, downloads, predictors.
  - Off-the-record (OTR) profiles are in-memory and are destroyed when their last browser window closes.
  - A regular profile can own several OTR profiles (`Profile::OTRProfileID`). DevTools uses this for isolated browser contexts.
  - A window belongs to exactly one profile.
- **`StoragePartition`** (selected by `StoragePartitionConfig`: domain, name, in-memory)
  - Owns its own `NetworkContext`, which holds cookies, HTTP cache, HSTS state, HTTP auth cache, socket pools and proxy configuration.
  - Also owns the site storage layers: localStorage, IndexedDB, Cache Storage, OPFS, service workers.
  - A tab can be bound to a partition, so tabs of different partitions can share a window.
  - Brave's Containers (1.92) are built this way.
  - Profile-scoped services are **not** partitioned. Content settings and permissions, history, autofill, favicons, predictors and extensions remain shared.

## Decision

**Identities are storage partitions in the regular profile.**
- Each identity maps to `StoragePartitionConfig(partition_domain="ghost-identity", partition_name=<uuid>, in_memory=false)`.
- Tabs are created with a site instance fixed to that partition.
- Per-identity proxy settings are applied when Chromium configures that partition's `NetworkContext`.
- The Default identity is the profile's default partition.
- Profile-scoped services are handled explicitly, service by service:
  - **Permissions** for powerful capabilities (camera, microphone, location, notifications, clipboard, MIDI, sensors and similar) are resolved per `(identity, origin)`, from identity-scoped stores owned by the identity service. If the Phase 6 spike shows the patch surface is too large for basic content settings (JavaScript, images), those stay profile-wide and are documented as shared.
  - **History** follows the identity's history policy: recorded, or off.
  - **Shared by design:** bookmarks, passwords, autofill, extensions, the downloads list, favicons, and the DNS-over-HTTPS configuration. Each is listed in the privacy model and covered by a test asserting that it *is* shared, so a change is noticed.

**Ghost sessions are unique off-the-record profiles.**
- Each session is an OTR profile created with a unique `OTRProfileID` and shown in its own windows.
- The session ends when its last window closes, and `ProfileDestroyer` tears it down.
- This inherits Chrome's existing off-the-record handling: no history, in-memory storage and permissions, no autofill saving, extensions disabled unless allowed in private windows.

**Private mode is the primary OTR profile** (Chromium's Incognito), with stricter protection defaults.

## Consequences

- Identities provide the per-tab user experience people expect from Firefox containers.
- Every profile-scoped service is a potential leak between identities. The isolation contract test suite enumerates each storage type against each boundary. New Chromium services must be classified when they appear in a milestone move.
- Ghost sessions are window-scoped. "Open link in Ghost session" opens a Ghost window, not a tab in the current window. We accept this: Chrome has hundreds of `IsOffTheRecord()` decisions, and re-implementing them per tab would be the largest source of leaks.
- Chrome UI assumes in places that off-the-record means Incognito: `IsIncognitoProfile()` is true only for the primary OTR profile. Phase 5 includes an audit of those call sites.
- Extensions can observe tabs of every identity, as in Firefox. The `chrome.cookies` API sees only the default partition. Both facts are documented.

## Alternatives considered

- **Identities as separate profiles.** Complete isolation with no patches. But identities become window-scoped and heavy (each profile loads its own services and extensions), and the one-click, same-window switching that motivates the feature is lost.
- **Ghost sessions as in-memory storage partitions.** Tab-level, so Ghost tabs could sit beside normal tabs. Rejected because history, downloads, permissions and autofill would record into the regular profile unless each service were taught about Ghost partitions. That is exactly the leak surface OTR profiles already solve.
- **One shared OTR profile for all Ghost windows** (Incognito's model). Sessions couldn't be isolated from each other, which is the defining property of a Ghost session.
