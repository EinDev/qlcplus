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
