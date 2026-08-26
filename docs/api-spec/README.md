# QLC+ Control API — spec

**`qlcplus-api.yaml`** is the deliverable: one AsyncAPI 3.0 document
specifying a WebSocket JSON API for QLC+, scoped to let a future Electron UI
fully replace the current Qt/QML UI (`qmlui/`). 712 messages, 112 schemas,
599 operations, one bidirectional channel.

## Reading order

1. **`00-conventions.md`** — read this first. The message envelope,
   request/response correlation, the two-tier document-state-vs-live-state
   concurrency model (optimistic-concurrency `baseRevision`/`docRevision`
   for structural edits, no-locking last-write-wins for live/runtime
   control), subscriptions, advisory locking, error codes, session
   handshake. Every other file assumes you've read this.
2. **`qlcplus-api.yaml`** — the merged spec itself.
3. **`fragments/*-notes.md`** — one per domain (core, fixtures, fixturedefs,
   functions-core, functions-advanced, io, virtualconsole). Open questions,
   scope decisions and their rationale, real gaps found in the engine along
   the way (see "Known gaps" below), things deliberately left out and why.
4. **`MERGE-PLAN.md`** — what got consolidated during the merge and why,
   e.g. the `functions.<type>.get` → single `functions.get` fold.
5. **`TODO.md`** — the standing to-do list for everything left: spec
   cleanup, implementing the remaining domains, cross-cutting concerns
   (auth, TLS, the `io.plugin.configure` engine gap, etc.), and the not-yet-
   started Electron client. Point at a numbered item to work on it.

## How this was built

Seven domains were researched and drafted independently (each grounded in
the actual `engine/src`/`qmlui` C++, not guessed) against the shared
conventions in `00-conventions.md`, then mechanically merged. `fragments/`
holds the seven source-of-truth domain files plus the generic skeleton
(`_skeleton.yaml`: session handshake, subscribe/unsubscribe, advisory
locking — every domain builds on these, none redefine them) and a worked
example (`_example.yaml`) showing the exact shape a fragment must have.

`_tools/` holds the Python scripts used to build and validate the merge
(`merge.py` is the one to re-run after editing any fragment — it splices
every fragment's messages/schemas/operations into the skeleton, generates
the channel message-key list, and hard-fails on any naming collision or
unresolved `$ref` before writing `qlcplus-api.yaml`). Needs PyYAML; on this
repo's MSYS2 toolchain: `pacman -S mingw-w64-x86_64-python-yaml`, then run
scripts via the MSYS2 `python3`, not Windows' python launcher.

## Known gaps / follow-up work

Tracked as a checklist in **`TODO.md`** now (Phase 2), not duplicated here —
that's the file to read for current status and the file to edit as items
get resolved or new ones turn up. Briefly, the standouts: `io.plugin.configure`
can't actually work remotely as designed (needs engine-side work, not just
an API decision — `io-notes.md`), palette/color-filter *definitions* aren't
covered by any domain yet, undo/redo under multi-client editing is an open
design question, and `io.inputProfile.learn.signal` has a minor broadcast-
scoping bug.

## Repo owner's standing guidance for future edits to this spec

Prefer fewer, more general methods over one bespoke method per resource
type where the *shape* of the operation is genuinely the same (e.g. the
`functions.get` merge). Don't over-apply this to genuinely distinct
mutations (see `MERGE-PLAN.md`'s "where not to over-consolidate") — the
target is redundant reads/round-trips and copy-pasted CRUD shapes, not
collapsing meaningfully different operations into one polymorphic blob.
