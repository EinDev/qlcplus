# Project-local media asset store - design (2026-09-15)

Read-only investigation of how Audio/Video sources are set, persisted and saved, and a design for
copying imported media into a folder owned by the show file so originals can be moved.

## 0. Where the original brief contradicts the code

1. File drag-and-drop does not exist: no `hasUrls`/`drop.urls` in `qmlui/qml`; the Show Manager
   DropAreas (`qmlui/qml/showmanager/ShowManager.qml:670`, `:749`) accept only `keys: ["function"]`.
2. The control API has no source setter: `functions.create` builds an empty Audio/Video
   (`controlapi/src/domains/apifunctionsdomain.cpp:371-372`); `functions.update` (`:714-767`) handles
   only runOrder/direction/tempo/speeds/blendMode.
3. There is no separate Save As / Save Copy: `qmlui/qml/ActionsMenu.qml:106-109` calls
   `qlcplus.saveWorkspace(file)` for both modes; `App::saveXML(..., autosave=false)` rebinds
   `m_fileName` (`qmlui/app.cpp:1795-1800`).
4. No autosave recovery exists; autosave files are only deleted (`qmlui/app.cpp:165-168`, `:1554-1557`).
   `autoSaveFileName()` (`:1311-1324`) is `NewProject.autosave.qxw` relative to CWD when untitled.

## 1. How sources are set and persisted

- `engine/src/video.cpp:263-296` `Video::setSourceUrl`: stores the raw path, names the function from
  `QFileInfo::fileName()` (`:284`), logs "not found" (`:288`); `://` URLs kept verbatim.
  Save `:516-519` / load `:608-612` go through `normalizeComponentPath` / `denormalizeComponentPath`.
- `engine/audio/src/audio.cpp:159-204` `Audio::setSourceFileName` has side effects: `resetBpmResult()`
  (`:161`), decoder deleted (`:166-170`) and re-created (`:193-199`), name set (`:177`). Save `:364`,
  load `:412`. `Audio::copyFrom` (`:130-133`) restores the BPM the setter wiped.
- `engine/src/doc.cpp:211-244`: `normalizeComponentPath` (`:228`) is a case-sensitive
  `startsWith(workspacePath())`; with an empty workspace path everything relativizes against the CWD.
- Workspace path set in `App::loadXML` (`app.cpp:1594`), `App::saveWorkspace` (`:1550`, before `saveXML`
  at `:1552`), `slotSaveAutostart` (`:1530`), `ImportManager::loadWorkspace` (`importmanager.cpp:120`).
- Precedents for workspace-relative lookup: `engine/src/fixture.cpp:1289-1298`, `qmlui/mainview3d.cpp:2105-2116`.

The Save As trap: `saveWorkspace` sets the new workspace path before writing while functions still hold
absolute paths into the old folder; relocation must run between `:1550` and `:1552`.

## 2. Import entry points today

- RightPanel "New Audio/Video" FileDialog (`RightPanel.qml:179-212`) ->
  `FunctionManager::createAudioVideoFunction` (`functionmanager.cpp:388-463`; setters at `:418`, `:451`).
- Audio editor (`AudioEditor.qml:70-77` -> `audioeditor.cpp:53-64`, Tardis `AudioSetSource` enqueued at
  `:61` before the setter at `:62`). Video editor (`VideoEditor.qml:61-68`, URL box `:148` ->
  `videoeditor.cpp:95-108`). Tardis undo/redo `tardis.cpp:1316-1320`, `:1338-1342` call the raw setters.
- Import from another project: `importmanager.cpp:426-470` -> `createCopy` -> `Audio::copyFrom`; source
  becomes the absolute path resolved against the import doc (`:120`).
- Control API: none. File DnD: none.

## 3. Save/Load/autosave

`App::saveWorkspace` (`app.cpp:1536-1565`) is the single funnel for Save, Save As and save-before-exit;
`App::saveXML` (`:1718-1804`) writes `<file>.temp` then renames. Autosave: Doc timer
(`engine/src/doc.cpp:60, 93-96, 338-376`, 30 s) -> `slotDocAutosave` -> `saveXML(autoSaveFileName(), true)`,
same directory as the .qxw when titled.

## 4. Proposed engine API

New `engine/src/mediaassets.{h,cpp}` owned by `Doc` (`Doc::assets()`):

```cpp
class MediaAssets {
public:
    QString assetsDirName() const;          // "<qxw-basename>.qxw.assets"
    QString assetsDir() const;              // absolute; QTemporaryDir while workspacePath() is empty
    QString importFile(const QString &src, QString *error);   // returns ABSOLUTE stored path
    bool isManaged(const QString &absPath) const;             // inside assetsDir()/<sha12>/
    QStringList referenced() const;         // Audio/Video sources over doc->functionsByType
    QStringList unreferenced() const;       // managed files minus referenced()
    QStringList externalSources() const;    // sources outside the store
    int collectExternal(QString *error);    // importFile each external source, relink
    bool relocateTo(const QString &newWorkspaceDir, QString *error); // copy referenced assets, relink
    bool removeUnreferenced(const QStringList &files);
};
```

Decisions the code forces:
- Layout `<name>.qxw.assets/<sha1-12>/<original basename>`: audio.cpp:177 / video.cpp:284 name the
  function from the basename, so a hash prefix would leak into function names; the hash dir is the
  "managed" marker. Warn on paths > 240 chars.
- Do not relink through `Audio::setSourceFileName` (BPM reset, decoder rebuild): add
  `Audio::relinkSource(path)` / `Video::relinkSource(path)` that only swap the string and emit the
  change signal. Editors keep the full setters (the file really changes).
- Untitled project: stage in a `QTemporaryDir`; a single `relocateTo(newDir)` between `app.cpp:1550`
  and `:1552` covers first save, Save As and autostart save.
- Replace: editor setters become import -> enqueue Tardis(old, managedPath) -> set (pass the stored
  path, not the picked one). Undo/redo only repoint; never delete on undo.
- Cleanup "Remove unused media": list `unreferenced()` in a confirmation popup; delete only
  store-owned `<sha>/` dirs.
- Migration "Collect media into project" = `collectExternal()`; on load, if `externalSources()` is
  non-empty show a non-blocking notice offering it.
- API: add `source` to `functions.create`/`functions.update` for Audio/Video and the detail getter,
  routed through `importFile`.
- ImportManager: after `createCopy` (`importmanager.cpp:460`) importFile + relinkSource for Audio/Video.
- Dedupe: hash while copying to `<sha>.partial`, rename into `<sha12>/<basename>`; drop the partial if
  the dir exists. Large files (> ~50 MB): copy on a QThread with a progress popup; the function can
  point at the source immediately and be relinked on completion.

## 5. Tests

`engine/test/mediaassets/` (link `qlcplusengine`, `QTEST_GUILESS_MAIN`, register in
`engine/test/CMakeLists.txt` `:16`, `:97`, `:153`), all with `QTemporaryDir`: import creates
`<sha>/<basename>`; same content twice -> one file; different content same basename -> two dirs;
XML round-trip relative/absolute; `relocateTo` copies only referenced files; `unreferenced()` after
deleting a function; `relinkSource` preserves BPM; `collectExternal` on external paths.

## 6. Effort, risks, increments

~5-7 dev days. Increment 1: `MediaAssets`, `relinkSource`, editor/RightPanel import, temp-dir staging,
tests. Increment 2: `relocateTo` in `saveWorkspace`, Replace with corrected Tardis ordering, progress
UI. Increment 3: collect/cleanup actions + popups, API `source` param, ImportManager hook, load-time
notice, optional file drag-and-drop.

Risks: large copies blocking the UI; Windows 260-char paths; case-insensitive filesystems vs the
case-sensitive `startsWith` at `doc.cpp:228`; users copying only the `.qxw`; disk-space doubling;
`slotSaveAutostart` bypassing relocation if hooked in the wrong function.
