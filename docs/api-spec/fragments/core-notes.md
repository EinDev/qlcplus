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

## Implemented 2026-09-27: Show Wizard (core.wizard.*) and Import from project

Server: `controlapi/src/domains/apiwizarddomain.cpp` + `apiwizardhost.h` (host implemented by
`qmlui/app_apiwizard.cpp`), `controlapi/src/domains/apiimportdomain.cpp` over the engine's
`ProjectImporter` (`engine/src/projectimporter.{h,cpp}`, which qmlui's ImportManager now delegates
to as well), `controlapi/src/domains/apidocchanges.cpp` for the created events. Tests:
`controlapi/test/apiwizarddomain/` (fake host - JSON plumbing only) and
`controlapi/test/apiimportdomain/` (real engine, every remap rule). Web UI: `webui/Wizard.jsx`,
`webui/ff/ImportProject.jsx`; sandbox driver `webui/tools/e2e/wizard-import.js`.

- **Wizard shape.** One choice set (`CoreWizardChoices`) instead of per-step state on the server:
  every call builds a fresh `StageWizard` and walks it forward through the steps like the QML dialog
  (step-entry side effects in order: groups loaded, stage default + suggested size, effect list +
  show-type defaults, controller re-scan), so `preview` is exactly what `generate` builds and the
  desktop's own wizard instance is never touched. Omitted optional fields mean "the wizard's
  default", e.g. no `effects` = the show type's defaults, no `controller` = the auto-picked single
  patched controller. Unavailable effects in `effects` are ignored.
- **Catalogue text** (show type / stage / role descriptions and placement blurbs) lives only in the
  QML today; `ApiWizardDomain` carries a copy, so a wording change there needs the same change here.
- **Seam.** `StageWizard::groupsModel()` now also reports `groupId`, `role` and the capability flags
  of every box, and `fixtureRoleModel()` the `groupId` (additive, the QML ignores them).
- **Events.** A generation or an import is announced with every domain's usual created events
  (`fixtures.patched`, `fixtures.group.created`, `functions.created` in id order, `palette.created`
  from its own signal, `vc.page.created`, `vc.widget.created` for every new widget, and for a
  wizard that placed new groups two `fixtures.monitor.changed`: stage and items), then one summary
  event, `core.wizard.generated` / `core.project.imported`. The responses carry the same data.
- **Not undoable**: both bypass Tardis, like every API edit (see `ApiProjectHost`). The desktop
  wizard's "fully undoable" note does not hold for the API.
- **Import is stateless**: `importList` and `import` each parse the source again (path or
  base64 upload); an upload's relative media paths resolve against this project's folder.
  `dependencies` in `importList` is the full closure; `import` recomputes it server side.
- **Engine bugs fixed on the way** (they affected the desktop popup too): EFX heads were remapped
  through the FUNCTION id map (imported EFX kept the source fixture ids); a palette matched by name
  was recorded in the fixture map (scenes lost the palette); a hidden function (a Sequence's bound
  scene) ended the import function tree instead of being skipped; a new fixture could be placed
  across a universe boundary (negative address); a skipped fixture's linked items wrote a name onto
  fixture id 0; a fixture with no resolvable definition dereferenced null.
- **Known gaps**: Script functions and Show tracks are copied without remapping the ids inside them
  (upstream behaviour); the controller step's mapping could only be exercised with no controller.
## Implemented 2026-09-27: `core.project.open` with `source: upload`

The bytes are validated before the host sees them (`INVALID_PARAMS` for empty / non-base64 content
or anything whose DTD is not `Workspace`): App's in-memory loader clears the current project first
and then gives up silently, so an unchecked bad upload used to leave an empty project behind an `ok`
answer. After a successful upload `filePath` stays null (the host's file name is left empty, so
`core.project.save` answers `INVALID_STATE` and a client routes to Save As - before, the bare client
file name was stored and Save wrote it relative to the engine's working directory) and `fileName`
reports the uploaded name until the next new / open / close. The uploaded `<Creator><Version>` is kept
for `functions.show.legacyTiming.get`. Like the desktop's network project sync, an upload does not
raise the desktop's own legacy-timing popup or error log; the web UI asks through the API instead.

## Implemented 2026-09-27: "Project" and "Gobos" places in `core.fs.list`

- `roots` now ends with two places after the drives: `Project` (the folder of the loaded
  project file, `Doc::workspacePath()`, only when the project has one) and `Gobos` (the gobo
  picture folder of the installation, `QLCFile::systemDirectory(GOBODIR)`, the folder the
  desktop Fixture Editor opens for picture capabilities, only when it exists). Home and the
  drives keep their positions. Used by the web 2D background, RGB Matrix image and gobo picture
  pickers. Test: `fsListRootsIncludeProjectFolder` in `controlapi/test/apicoredomain/`.
