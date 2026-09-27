# core domain — notes

> Reconstructed by the coordinator, not the original authoring agent: that
> agent's run was killed by an account spend-limit error right after writing
> `core.yaml` but before writing this file. This is a summary derived from
> reading the finished YAML and its inline comments, not the agent's own
> reasoning trail — treat it as a lighter-weight pass than the other domains'
> notes files.

## Scope

Project (Doc) lifecycle (`core.project.*`: new/open/save/saveAs/close/get/
recentFiles), engine Design/Operate mode (`core.mode.*`), undo/redo via
Tardis (`core.undo`/`core.redo`/`core.history.get`), global engine settings
(`core.settings.*`), and a generic log/error event stream. Grounded per the
file header in `engine/src/doc.{h,cpp}`, `qmlui/app.{h,cpp}`,
`qmlui/tardis/tardis.{h,cpp}`, `qmlui/main.cpp`, `engine/src/qlcfile.cpp`,
`engine/src/mastertimer.cpp`, and `webaccess/src/webaccess.cpp`.

Explicitly out of scope here (owned elsewhere per the conventions doc):
Grand Master and Blackout live on `io.*` (InputOutputMap-owned in the
engine).

## Open questions for the merge pass

- **Undo/redo under multi-client editing**: the conventions doc explicitly
  flagged this as a hard case worth a deliberate answer (§3 of
  00-conventions.md's task brief), and `core.yaml` does define
  `core.history.get` + `core.undo`/`core.redo` with a `core.history.changed`
  event, but *how* it reconciles a local Tardis undo stack against changes
  another client made in the meantime isn't visible from the schema alone —
  worth a close read of `core.yaml`'s `CoreUndoRequest`/`CoreRedoResponse`
  payloads before finalizing, and probably worth an explicit call-out in the
  merged spec's prose either way.
- **File transfer for open/import**: `core.project.open` takes some form of
  path or payload reference — confirm whether it assumes a server-local
  filesystem path (simplest, matches today's single-machine desktop-app
  model) or expects file bytes over the wire (needed if the Electron client
  and engine process ever run on different machines). Pick one explicitly in
  the merged spec if `core.yaml` doesn't already say.
- **`docRevision` ownership**: `core.project.*` responses/events are the
  natural place documenting *what* `docRevision` is (every other domain's
  §4a mutations bump the same counter) - confirm the merged spec's top-level
  description references this domain as the source of truth for that
  concept, since a reader encountering `baseRevision` in, say, `fixtures.yaml`
  first won't otherwise know where it's defined.

## Implemented 2026-09-26: beat generator and undo/redo (web UI slice)

Server: `controlapi/src/domains/apicoredomain.cpp`. Tests:
`controlapi/test/apicoredomain/`.

- `core.bpm.get/set/tap`, `core.bpm.changed`, `core.beat` - see the
  message descriptions. Deviations from the web UI contract: no
  `beatsPerBar` (the engine has no bar concept at generator level);
  `core.bpm.set` with a `plugin`/`audio` generator active is refused with
  `INVALID_STATE` instead of silently being overwritten by the source;
  `bpm: 0` disables the generator. `core.beat` is deliberately not
  subscribe-gated (contract clients just listen for it).
- `core.undo`/`core.redo`/`core.history.get`/`core.history.changed` are
  implemented on top of Tardis through `ApiProjectHost`'s undo hooks
  (`controlapi/src/apiprojecthost.h`; `App` implements them, `Tardis`
  gained `canUndo()/canRedo()/undoActionName()/redoActionName()` and a
  `historyChanged()` signal). The result shapes are a superset of the
  contract's (`{ok, description?}` / `{canUndo, canRedo, undoText?,
  redoText?}`) and of this spec's earlier draft.
- **Limits (documented in the messages too):** Tardis only records edits
  made through the qmlui UI, so nothing changed via this API's own
  structural methods is undoable (TODO.md 2.5 stays open for that); undo
  re-invokes engine setters, so no domain change event fires - clients
  must refetch on the `docRevision` carried by `core.history.changed`;
  `entries` in `core.history.get` is always empty (Tardis exposes only the
  next step); everything answers `UNSUPPORTED` when the server runs
  without a qmlui host, which is also why `controlapi/test` can only cover
  that path - the happy path needs the real app.

## Implemented 2026-09-27: `core.fs.list` (server-side file browser)

Server: the end of `ApiCoreDomain::registerMethods()` in
`controlapi/src/domains/apicoredomain.cpp`. Tests: `fsList*` cases in
`controlapi/test/apicoredomain/`. Web UI: `webui/ff/ServerFileBrowser.jsx`
(`window.ServerFileBrowser`), used by the Audio and Video editors' "Replace
file" buttons; `App.jsx`'s Open dialog still takes a typed path plus recent
files and can adopt the same component.

- Read-only listing of one host directory, modelled on
  `qmlui/folderbrowser.cpp` (`QDir::AllDirs | Files | NoDotAndDotDot`,
  dirs first, case-insensitive name order, hidden entries never listed).
  `path` empty lists the roots (home + `QDir::drives()`) and every response
  also carries `roots`, so a picker needs one call per navigation step.
- Filtering is by glob patterns (`extensions: ["*.mp3", ...]`), the same
  form `functions.audio.listCapabilities` / `functions.video.listCapabilities`
  report, applied through `QDir::setNameFilters` - directories always pass.
- Errors: relative path -> `INVALID_PARAMS`; missing or not a directory ->
  `NOT_FOUND`. No path is ever created; a directory the process cannot read
  simply lists empty.
- Deliberately no `core.fs.read`/`write`: media enters the project through
  the setSource methods (which copy into the media store), project files
  through `core.project.open`.

## Implemented 2026-09-27: autostart function

- NEW `core.project.setStartupFunction` (4a, `functionId` null clears) and
  `core.project.startupFunctionChanged`; `core.project.get` / `core.project.loaded` carry
  `startupFunctionId`. Doc::setStartupFunction() does not mark the document modified on its own,
  so the handler calls setModified() (the id is saved as `<Workspace Autostart>`). Registered by
  `ApiFunctionsMiscDomain` (not this domain's file) to keep the slice in one place.
