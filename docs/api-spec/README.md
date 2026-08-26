# QLC+ Control API — spec

**`qlcplus-api.yaml`** is the deliverable: one AsyncAPI 3.0 document
specifying a WebSocket JSON API for QLC+, scoped to let a future Electron UI
fully replace the current Qt/QML UI (`qmlui/`). 741 messages, 109 schemas,
627 operations, one bidirectional channel.

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

## Known gaps / follow-up work flagged during this pass

Worth reading in full in the relevant notes file, summarized here:

- **`io.plugin.configure` can't actually work remotely as designed** — it's
  a thin passthrough to `InputOutputMap::configurePlugin()`, which pops a
  **native Qt dialog server-side** on the current engine. `io.patch.
  setParameters` (generic key-value) is the real remote-friendly path, but
  full parity for plugins that need live hardware enumeration (`dmxusb`
  notably, given this repo's FTDI D2XX dependency) likely needs engine-side
  work, not just an API-spec decision. See `io-notes.md`.
- **Scene/RGBMatrix palette and color-filter *definitions*** (as opposed to
  referencing them by id) aren't covered by any of the seven domains as
  scoped — a real gap for "100% of the functionality" if palettes/color
  filters are meant to be user-editable via the API. See
  `functions-core-notes.md`.
- **Undo/redo under multi-client editing** (`core.*`) has a real, only
  partially-resolved design question: what should "undo" mean once another
  client has made changes since your last action? See `core-notes.md`.
- **`vc.inputProfile.learn.signal`-style event broadcast scoping** — the
  MIDI/OSC "learn" signal event in `io.yaml` currently has no
  requester-scoping field, so as specified it'd broadcast to every
  connected client instead of just the one doing the learn. Minor, flagged
  in `io-notes.md`.

## Repo owner's standing guidance for future edits to this spec

Prefer fewer, more general methods over one bespoke method per resource
type where the *shape* of the operation is genuinely the same (e.g. the
`functions.get` merge). Don't over-apply this to genuinely distinct
mutations (see `MERGE-PLAN.md`'s "where not to over-consolidate") — the
target is redundant reads/round-trips and copy-pasted CRUD shapes, not
collapsing meaningfully different operations into one polymorphic blob.
