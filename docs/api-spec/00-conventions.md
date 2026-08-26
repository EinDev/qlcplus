# QLC+ Control API — shared conventions

This document is the shared contract every domain fragment must follow. Read
this in full before writing your fragment. Everything here was decided
up front (with the repo owner) so that seven parallel work-streams merge into
one coherent spec instead of seven incompatible ones.

## 0. Context: what this API is for

QLC+ 5's real UI today (`qmlui/`) is a Qt/QML application that links directly
against the C++ engine (`engine/src/`). This API spec is the contract for a
**future Electron app that fully replaces qmlui** — so it must expose 100% of
what an operator can do in the current UI: patch fixtures, author fixture
definitions, build every function type, build and drive the Virtual Console,
configure I/O plugins and universes, and observe live DMX/engine state.

There is an existing legacy protocol in `webaccess/src/webaccess.cpp`
(pipe-delimited strings like `QLC+API|...`, `CH|addr|val`, `POLL`, all
addressed by Virtual Console widget ID). **Ignore its message shapes** — this
is a clean-slate JSON design — but it's worth skimming for *what operations
exist* (grep `cmdList\[` in that file) as a sanity check that you haven't
missed a capability, and its comment blocks / the functions it calls into
(`VCButton::`, `VCSlider::`, etc.) are a legitimate map of what the engine
supports.

## 1. Deliverable format: AsyncAPI 3.0

The final spec is one AsyncAPI 3.0 YAML document. AsyncAPI models a
message-based API as: `channels` (addressable message streams) → `operations`
(a `send` or `receive` action on a channel, from the client's point of view)
→ `messages` (named payload envelopes) → `schemas` (the JSON Schema payload
shapes), all living under `components` and cross-referenced with `$ref`.

**Every fragment uses exactly one channel**, `qlcplus`, already declared in
the skeleton (`_skeleton.yaml` in this directory — read it). You are not
declaring new channels. You are contributing entries to:

- `components.messages` — one entry per request, response, and event your
  domain defines
- `components.schemas` — the payload shape for each message
- `operations` — one `send` operation per client→server request, one
  `receive` operation per server→client response/event

## 2. Message envelope

Every WebSocket frame is one JSON object, one of three kinds, distinguished
by `type`:

```jsonc
// Client -> Server request
{
  "type": "request",
  "id": "c-8f2e1a",           // client-generated, unique per connection; echoed in the response
  "method": "fixtures.patch", // "<domain>.<resource>.<action>" or "<domain>.<action>"
  "params": { /* method-specific */ }
}

// Server -> Client response (always exactly one, correlated by id)
{
  "type": "response",
  "id": "c-8f2e1a",            // matches the request id
  "ok": true,
  "result": { /* method-specific, present when ok=true */ }
}
// or, on failure:
{
  "type": "response",
  "id": "c-8f2e1a",
  "ok": false,
  "error": { "code": "CONFLICT", "message": "human-readable", "details": { /* optional, e.g. current state */ } }
}

// Server -> Client event (unsolicited, no id/correlation)
{
  "type": "event",
  "topic": "fixtures.patched",   // dot-namespaced, matches the domain
  "data": { /* topic-specific */ },
  "originClientId": "cl-9a3f"    // the client whose request caused this, or null for engine-internal/other causes
}
```

**Naming rule** (avoids collisions when we merge 7 fragments into one file):
prefix every `components.messages` / `components.schemas` key with your
domain short-name in PascalCase, e.g. `FixturesPatchRequest`,
`FixturesPatchResponse`, `FixturesPatchedEvent`, `FixturesPatchParams`. Use
your domain's `method`/`topic` dot-prefix consistently too (e.g. everything
you own starts `fixtures.`).

## 3. Request/response methods vs. events

- **Methods** (`request`/`response`) are for anything the client asks the
  engine to do or fetch: CRUD on any resource, "get me the current state of
  X", explicit commands like "start this function".
- **Events** are for anything the server needs to push unprompted: state
  changed (by another client, or by engine-internal logic like a function
  finishing), live values (DMX, running-function progress), errors/log
  lines.
- A mutation is always: client sends `request` → server applies it → server
  sends the `response` to the requester **and** broadcasts the corresponding
  `event` to all subscribed clients (including the requester — clients
  should apply state from the event, not from the response's `result`, to
  keep a single code path for "state changed". The response's `result` for a
  mutation is just `{ "revision": <docRevision> }` or similar — don't
  duplicate the full new state there).

## 4. Two tiers of state, two concurrency rules

This is the answer to "full multi-client editing" without building a real
collaborative-editing system (which would be wild overkill for a lighting
console — nobody needs operational transforms to patch a moving head).

### 4a. Document state (structural — needs conflict handling)

Fixtures, patches, function definitions, Virtual Console layout, I/O
patches, fixture definitions themselves — anything that's part of the show
file / project and gets saved. This is **all one project**, so there is a
single global monotonic counter, `docRevision`, incremented by the engine on
every successful structural mutation, from *any* domain.

- Every structural mutation request **must** include `"baseRevision": <int>`
  — the `docRevision` the client last observed.
- The server accepts only if `baseRevision === currentDocRevision`. Server
  applies it, increments `docRevision`, and both the `response.result` and
  the broadcast `event.data` carry the new `docRevision`.
- On mismatch, respond `ok:false`, `error.code: "CONFLICT"`, and put the
  *current* authoritative resource state in `error.details` so the client
  can rebase and retry. Don't try to auto-merge.
- Structural change events should carry enough to apply without a refetch:
  either the full new resource (fine for most things — fixtures, a VC
  widget's config, an I/O patch) or, for large nested resources like a
  Scene's channel-value list or a Show's timeline, prefer a JSON Patch
  (RFC 6902) array in `event.data.patch` plus the resulting `docRevision`,
  so an event isn't megabytes for a one-channel tweak. Use your judgement
  per-domain and say which you picked in your notes file.

### 4b. Live/runtime state (ephemeral — no conflict handling, ever)

DMX output values, a fader's live position while being dragged, a function's
running/elapsed-time, blackout on/off, Grand Master level, "which VC page is
showing" — anything that is not saved to the show file, or that real
operators expect last-write-wins semantics for (exactly like a real lighting
console: two operators can both grab the Grand Master, and whoever moved it
last wins — there is no "conflict").

- No `baseRevision`, no conflict possible by construction.
- Client sends the request, server applies it immediately, response is a
  bare ack (`{"ok": true, "result": {}}`), event broadcasts the new value.
- High-frequency ones (raw DMX universe frames, live fader drag position)
  must be **opt-in via subscription** (see §5) — don't blast 44Hz DMX frames
  to every connected client whether they asked or not.

If you're not sure which tier something in your domain belongs to: does it
get saved into the `.qxw` show file? → 4a. Does it only exist while the
engine is running? → 4b.

## 5. Subscriptions

Structural (4a) events are always delivered to every connected client — low
volume, and everyone editing the show needs them. Live (4b) events,
especially DMX frame data and any per-tick value stream, are opt-in:

```jsonc
{ "type": "request", "id": "...", "method": "subscribe",
  "params": { "topics": ["dmx.universe.1", "functions.status"] } }
{ "type": "request", "id": "...", "method": "unsubscribe",
  "params": { "topics": ["dmx.universe.1"] } }
```

`subscribe`/`unsubscribe` themselves are generic, already in the skeleton —
you don't need to define them. In your fragment's notes file, just list the
topic names you emit that should be subscribe-gated (typically: anything
that updates faster than ~2 Hz per resource, or anything per-universe/
per-channel).

## 6. Advisory locking (soft, UX-only — not a correctness mechanism)

For a nicer multi-editor UX, any client may claim a soft lock on a
document-state resource it's about to edit in a modal/detail view:

```jsonc
{ "type": "request", "id": "...", "method": "locks.acquire",
  "params": { "resourceType": "function", "resourceId": "42" } }
{ "type": "request", "id": "...", "method": "locks.release",
  "params": { "resourceType": "function", "resourceId": "42" } }
```
broadcasts `event.topic: "locks.changed"` with who holds what. This is
**advisory only** — the server does not reject writes from a non-holder; the
real safety net is always §4a's `baseRevision` check. `locks.*` is already in
the skeleton; domain fragments don't redefine it, just note in your fragment
if a resource type of yours is lockable (most structural resource types
should be).

## 7. Errors

Standard `error.code` values (extend with domain-specific codes as needed,
prefixed by domain, e.g. `FIXTURES_ADDRESS_OVERLAP`):

- `CONFLICT` — `baseRevision` mismatch (§4a)
- `NOT_FOUND` — resourceId doesn't exist
- `INVALID_PARAMS` — schema/semantic validation failure
- `UNAUTHORIZED` — session not authenticated / lacks permission
- `UNSUPPORTED` — e.g. plugin/hardware feature not available on this engine build

## 8. Session handshake, identity, auth

Already defined in the skeleton (don't redefine): client connects, sends
`hello` request (`{ apiVersion, clientName, authToken? }`), server responds
`welcome` (`{ clientId, docRevision, serverVersion }`) or rejects with
`UNAUTHORIZED`. `originClientId` on events lets every client tell its own
echoed changes apart from others' (useful for e.g. not re-animating a fader
the local user is actively dragging). Auth token validation reuses whatever
credential model `webaccess/src/webaccessauth.cpp` already implements
server-side — note in your fragment if your domain has its own
permission/role wrinkles (e.g. "who's allowed to reconfigure I/O plugins"),
otherwise assume "authenticated = full access", consistent with today's
single-operator-app model.

## 9. What to actually go look at

Don't design from memory of what a lighting console "probably" does — read
the actual engine headers for your domain (listed in your task prompt) for
the real properties, enums, and operations, including things a UI wouldn't
obviously expose (e.g. `Function::Speed` attributes beyond just Intensity,
`Fixture::Head`, blend modes, HTP/LTP channel groups). Cross-check against
what `qmlui/qml/` actually renders/edits for your domain (the QML files call
into C++ models under `qmlui/` which wrap the engine) — that tells you what
a real operator actually touches day to day, vs. obscure engine internals
that may not need first-class API coverage.

## 10. Deliverables per domain fragment

Two files in `docs/api-spec/fragments/`:

1. `<domain>.yaml` — a YAML document containing exactly three top-level
   keys: `messages`, `schemas`, `operations` (these get spliced into
   `components.messages`, `components.schemas`, and the top-level
   `operations` of the final spec, respectively). See
   `fragments/_example.yaml` for the exact shape expected, with one
   worked request/response/event triplet.
2. `<domain>-notes.md` — free-form: open questions, things you deliberately
   left out of scope and why, which resource types are lockable, which
   events are subscribe-gated, anything you think the merge pass needs to
   know or the repo owner needs to decide.

Keep your final chat report to the coordinator SHORT — a few sentences plus
the file paths. Don't paste the YAML into your final report; it's already on
disk.
