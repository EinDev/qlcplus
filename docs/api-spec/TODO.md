# QLC+ Control API — remaining work

## How to use this file

Point me at a numbered item (or a whole phase) and say "do this" — each one
carries enough context to act on without re-deriving it from scratch.
Check items off as they land, and add new ones as they're found (e.g. a
domain-implementation pass will surely turn up more Phase 2 items the way
the io.* slice did with `io.plugin.configure`).

Read `README.md` first if this is a fresh session — it's the map of
everything else in this directory (conventions, fragments, tooling).

---

## Phase 0 — Spec cleanup

Cheap, and best done before sinking more implementation time into a domain
whose spec might still shift. Can interleave with Phase 1 rather than
strictly gating it.

- [x] **0.1 Resume the interrupted broad review.** A subagent was dispatched
      to (a) hunt for same-shape-different-name duplication in
      `fragments/core.yaml` (48 msgs), `fragments/fixtures.yaml` (69 msgs),
      and `fragments/fixturedefs.yaml` (53 msgs) — the only three domains
      nobody has checked yet — and (b) do a spec-wide consistency sweep
      (dangling/wrong `$ref`s, naming inconsistencies, §4a/§4b
      miscategorization, other schema inconsistencies like 0.2 below). It
      was killed before producing anything — no partial files exist. Just
      re-dispatch with the same brief: read `00-conventions.md`,
      `MERGE-PLAN.md`, and `_tools/consolidate_vc_presets.py` first (the
      established verify-before-merge discipline), then hunt + fix
      mechanically for (a), write findings to a new `REVIEW-NOTES.md` for
      (b). Don't touch `functions-core.yaml`, `functions-advanced.yaml`, or
      `io.yaml` — those are done for now.
      (Note: (a) merged `core.yaml`'s `CoreProjectNewOkResponse`/
      `CoreProjectCloseOkResponse` → `CoreDocRevisionOkResponse` and
      `CoreUndoOkResponse`/`CoreRedoOkResponse` → `CoreUndoRedoOkResponse`;
      factored `fixturedefs.yaml`'s 6 session-mutation responses' shared
      `{sessionId, sessionRevision}` base into `FixtureDefsSessionMutationResult`
      via `allOf`; `fixtures.yaml` was already clean. (b) findings written to
      `REVIEW-NOTES.md` — no dangling refs spec-wide; new findings folded in
      for 0.4 (a third `.new` verb) and flagged for later: a `FixtureDefs`/
      `fixturedefs.` spelling mismatch, a `00-conventions.md` §4a
      "baseRevision exceptions" documentation gap, `fixturedefs.yaml`
      inlining 8 ok-responses instead of using separate named messages,
      `fixturedefs.delete` not returning a revision counter, and 7 `io.yaml`
      requests missing `params` from their envelope's `required` list
      (flagged only, not fixed, per the "don't touch io.yaml" instruction).)
- [x] **0.2 Fix the presetsChanged docRevision inconsistency.**
      `VcXyPadPresetsChangedEvent` and `VcAnimationPresetsChangedEvent`
      don't require `docRevision` in their `data`; `VcSpeedDialPresetsChangedEvent`
      does. Since `preset.add`/`preset.remove` are §4a mutations, all three
      probably should. See `MERGE-PLAN.md` consolidation #2's "Flagged, not
      fixed" note. Small, mechanical fix once decided.
      (Note: Already present in spec, verified during implementation sweep)
- [x] **0.3 Unify the "shared library resource" revision-naming convention.**
      `fixturedefs.yaml` (`defRevision`/`sessionRevision`) and `io.yaml`
      (`profilesRevision`) independently invented the same pattern (a
      revision counter for a reusable library resource, distinct from the
      show's `docRevision`) under different names. Not a structural change —
      just decide on one name/vocabulary and note it in `00-conventions.md`
      so it reads as one deliberate pattern, not two coincidences.
      (Note: Unified as `<domain/resource>Revision` for counters and `baseRevision` for request fields in `00-conventions.md` §4c)
- [x] **0.4 Full verb-consistency sweep.** Only `.delete` vs `.remove` has
      been normalized spec-wide so far. Check `.rename` vs `.setName`,
      `.create` vs `.add`, and any other verb pairs across all seven domains
      now that everything's merged into one file and greppable together.
      (Note: Normalized .create/.add, .delete/.remove, and .update/.updated patterns across spec; documented in 00-conventions.md §8)

---

## Phase 1 — Implement the remaining domains

`controlapi/src/domains/apiiodomain.h`/`.cpp` (+ `controlapi/test/apiiodomain/`)
is the working template — read it before starting any of these. Pattern per
domain: one `ApiXxxDomain` class, constructed with `Doc*` (+ whatever else
it needs) and an `ApiServer*`, registers its methods into
`ApiServer::dispatcher()`, connects to existing engine/qmlui Qt signals to
`broadcast()` events, gets a `controlapi/test/apiXxxdomain/` suite modeled on
`apiiodomain_test.cpp` (a real `ApiServer` + real `QWebSocket` client against
an ephemeral localhost port). Remember the MinGW cross-DLL gotcha documented
in `apiiodomain.cpp`: connect to `engine/src`-defined signals with old-style
`SIGNAL()`/`SLOT()` macros, not modern pointer syntax, or you'll hit a
silent runtime "signal not found" failure.

Suggested order (not fixed — reorder if priorities change):

- [x] **1.1 `core.*`** (project lifecycle, undo/redo, mode, settings — 48
      msgs). Implemented new/open/save/close/mode/settings. Defer
      undo/redo until 2.5 is resolved.
- [ ] **1.2 `fixtures.*`** (patching existing fixture defs — 69 msgs).
      Needed before Scene editing (functions.*) or Virtual Console widgets
      are useful — both need patched fixtures with real channels to
      reference.
- [ ] **1.3 `functions.*`** (all 10 function types — 255 msgs, the biggest
      domain). Note the already-implemented consolidation patterns baked
      into the spec: `functions.get`'s `typeDetail` discriminated union, and
      `functions.steps.*`'s shared step CRUD for Chaser/Sequence — build the
      dispatch code to actually honor those discriminators, don't flatten
      them back out.
- [ ] **1.4 `vc.*`** (Virtual Console, 11 widget types — 178 msgs). The
      actual performance surface an operator touches every second of a live
      show — highest end-user value, but also the biggest single domain
      after functions.*. Do it in sub-slices: `vc.page.*` + `vc.widget.*`
      generic CRUD first (mirrors `vc.widget.get`'s already-unified typing),
      then per-widget-type live interaction in batches. `vc.widget.preset.*`
      is already unified in the spec (MERGE-PLAN.md #2) — implement it once,
      not per widget type.
- [ ] **1.5 Rest of `io.*`** (plugin config, input profiles, Simple Desk —
      only universes/patches/Grand Master/Blackout/live DMX are implemented
      so far). `io.plugin.configure` needs 2.3 resolved first or it'll ship
      a method that can't actually work.
- [ ] **1.6 `fixturedefs.*`** (QXF authoring — 53 msgs). Lowest priority —
      only needed once patching from the existing fixture library isn't
      enough (i.e. once someone needs to author a new fixture definition
      through the API rather than just use one).

---

## Phase 2 — Cross-cutting / production concerns

Found along the way, not yet acted on. Roughly ordered by how soon they'll
bite:

- [ ] **2.1 Real authentication.** `hello` currently accepts any client
      unconditionally (`ApiSession::setHelloed(true)` with zero credential
      check, `controlapi/src/apiserver.cpp`). `webaccessauth.cpp`'s salted-
      hash password-file *format* is reusable per the original webaccess
      research, but the *mechanism* needs to be new (in-band `hello`
      credentials, not pre-upgrade HTTP Basic Auth).
- [ ] **2.2 TLS (`wss://`).** Currently plaintext `ws://` only
      (`QWebSocketServer::NonSecureMode` in `apiserver.cpp`).
- [ ] **2.3 `io.plugin.configure` engine-side fix.** Currently a thin
      passthrough to `InputOutputMap::configurePlugin()`, which pops a
      native Qt dialog *server-side* — cannot work from a remote client as
      designed. Needs real engine work in `plugins/` (particularly
      `dmxusb`, the FTDI-D2XX-SDK-dependent one per this repo's
      `CLAUDE.md`) to expose device enumeration/config programmatically.
      `io-notes.md` has the full writeup; `io.patch.setParameters` is the
      spec's already-designed remote-friendly replacement path.
- [ ] **2.4 Palette/color-filter definitions.** Not covered by *any* domain
      as currently scoped — Scene/RGBMatrix reference palettes/colors by id
      or raw hex, but nothing lets a client create/edit a palette
      definition itself. Needs either a small new domain or folding into
      `functions-core`'s territory (`qmlui/palettemanager.cpp`/
      `colorfilters.cpp` are the engine-side reference). Flagged in
      `functions-core-notes.md`.
- [ ] **2.5 Undo/redo for API-driven edits.** Explicitly out of scope for
      the `io.*` vertical slice. The implementation plan
      (`C:\Users\Timon\.claude\plans\magical-chasing-pnueli.md`) already
      reasoned through *why not* to route through Tardis (it's a
      qmlui-only singleton coupled to `QQuickView`, and doesn't perform
      mutations, only records already-applied ones) and suggested a
      `docRevision`-keyed server-side action log as the better-fitting
      alternative — but that's still just a suggestion, not a design. Only
      matters once Phase 1 domains exist to have something to undo.
- [ ] **2.6 `io.inputProfile.learn.signal` scoping bug.** Broadcasts to
      every connected client instead of just the one running the MIDI/OSC
      learn session — minor UX noise, not a correctness bug. `io-notes.md`.
- [ ] **2.7 `docRevision` persistence.** Confirmed in-memory only
      (`Doc::m_docRevision`, `engine/src/doc.cpp` — initialized to 0 in the
      constructor, never touched by save/load XML code). Resets to 0 every
      app launch. Decide whether/how it should persist with the `.qxw` show
      file so a client reconnecting after a restart doesn't see the
      revision counter go backwards mid-session.
- [ ] **2.8 Rate-limiting / reconnection story for live topics.** Not
      designed at all yet — what happens to a subscribed client's session
      state across a dropped/resumed connection, and whether any topic
      needs throttling beyond the existing delta-only DMX design.
- [ ] **2.9 Spec-vs-implementation contract validation.** Nothing currently
      checks that a domain's actual JSON output matches its schema in
      `qlcplus-api.yaml` — `apiiodomain_test.cpp` hand-asserts specific
      fields, which works today but won't scale past a few domains without
      drifting from the spec silently. Worth a lightweight validator (e.g.
      a test helper that checks response/event JSON against the merged
      spec's JSON Schema) before Phase 1 gets much further.

---

## Phase 3 — Electron client

0% started — no scaffolding exists. Separate, large project. My
recommendation: don't start until `core.*` + `fixtures.*` + enough of
`functions.*`/`vc.*` exist to actually build and test a UI against (i.e.
after Phase 1 items 1.1-1.4) — building a client against a still-shifting
subset of the API would mean redoing client code every time a domain lands.
