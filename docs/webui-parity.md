# Web UI parity with the Qt/QML UI - definition of done

**Goal**: the browser web UI (`webui/`) lets an operator do everything the Qt/QML UI (`qmlui/`)
lets them do. Parity means *the operator can do the same things*; small differences in UI
structure (a picker instead of drag-and-drop, a panel instead of a popup) are fine. One row per
operator-facing action of the Qt UI. This file is the checklist that decides when the goal is
reached: it is done when no row is `missing` or `partial`.

Status vocabulary (the status cell contains exactly one of these words, nothing else):

- `live` - works against the real server, per the verified list in `webui/README.md` ("What is live
  and what is not"), or a later real-browser test against a real server.
- `partial` - some of it works; the notes say what is missing.
- `missing` - not available in the web UI.
- `n/a` - cannot or should not exist in a browser; the notes say why.

Blocker column vocabulary, for `missing`/`partial` rows: `S: <method(s)>` when the server does not
register methods the spec already defines (`grep registerMethod controlapi/src/domains/*.cpp` is
the source of truth), `U` when only web UI work is needed (the server already has what is needed),
`spec+S+U` when `docs/api-spec/fragments/*.yaml` has to be extended first. `live` and `n/a` rows
carry `-`. Free-form notes follow the token after a semicolon.

Baseline: server methods and spec as of 2026-09-27 (`webui/README.md` live pass of 2026-09-26,
old audit `docs/agent-reports/2026-09-26-webui-import-gap-list.md`). The domain wrappers in
`webui/api/domains/*.js` wrap the *whole* spec; a wrapper existing is not evidence of support.

## How to update this file

- One row per action, four columns: `Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes`.
  Keep the status cell a bare word so the Summary can be recomputed with a grep such as
  `grep -cE "^\|[^|]*\|[^|]*\| live \|" docs/webui-parity.md`.
- Keep statuses honest: `live` only after a real browser test against a real server (the
  `qlcplus5 --api --webui` instance with SF3.qxw, or the sandbox copy described in the
  `project_webui_e2e_sandbox_testing` memory - sandbox-verified counts as `live`, but write
  "sandbox" in the notes). Code merely present in a `.jsx` file is `partial` (or `missing`) with
  "code present, not live-verified" in the notes until someone tests it.
- When a slice lands: change the status, update the notes, drop the `S:` methods that are now
  registered, and recompute the Summary. Do not delete rows; do not merge rows.
- No `|` characters inside cells (write "Play / Stop", not "Play|Stop").
- Popup rows in "Popups & tools" duplicate rows of the feature sections on purpose (the task asks
  for every popup to be classified); keep both in sync.

## Main toolbar & Actions menu

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Switch context: Fixtures & Functions / Virtual Console / Simple Desk / Input-Output | MainView.qml | live | - |
| Switch context: Show Manager | MainView.qml | live | - ; sandbox 2026-09-27, screen registered (Ctrl+5), driver webui/tools/e2e/show.js |
| Keyboard shortcuts for toolbar actions (Ctrl+1..5 contexts, Ctrl+S, Ctrl+B, Ctrl+Z / Ctrl+Y) | MainView.qml, ShortcutsEditor.qml | partial | U; App.jsx has Ctrl+digit and "Hold Ctrl to see shortcuts" hints, not in the README verified list; the full QML shortcut set is not mapped |
| New project (with "save changes first?" prompt) | ActionsMenu.qml | live | - |
| Open project by server-side path | ActionsMenu.qml, popup/PopupFolderBrowser.qml | live | - |
| Open recent file | ActionsMenu.qml | live | - |
| Open a project file from the local machine (native file dialog, drag-and-drop onto the window) | ActionsMenu.qml, MainView.qml ("Drop a project or fixture file") | missing | U; core.project.open already specs `source: upload` with base64 content, so a browser file picker / drop zone is possible; server support of `upload` unverified. The native dialog itself is n/a, server-side path + recent files cover the desktop use |
| Save project (Ctrl+S) | ActionsMenu.qml | live | - |
| Save project as (server-side path) | ActionsMenu.qml | live | - |
| Import fixtures / functions from another project | ActionsMenu.qml, popup/PopupImportProject.qml, importmanager.cpp | missing | spec+S+U; nothing in the spec for project import |
| Collect media into project (fork asset store) | ActionsMenu.qml, MainView.qml | missing | spec+S+U; only functions.media.reload is specced and registered |
| Reload changed media | ActionsMenu.qml, MainView.qml | partial | U; per-function Reload button wired to functions.media.reload (not browser-verified); no bulk action or changed-on-disk list yet |
| Remove unused media | ActionsMenu.qml, MainView.qml | missing | spec+S+U |
| Undo / Redo (with history labels) | ActionsMenu.qml | live | - |
| Blackout toggle (Ctrl+B) | MainView.qml | live | - |
| Stop all running functions | MainView.qml | live | - |
| BPM display, set, tap, off, beat indicator | MainView.qml, KeyPad.qml (Tap) | live | - |
| Beat generator source selection (disabled / internal / plugin / audio) | BeatGeneratorsPanel.qml, MainView.qml | missing | U; core.bpm.set has a `generator` enum with exactly these values; the web UI only sends bpm / off |
| Dump DMX values on a Scene (toolbar button) | MainView.qml, popup/PopupDMXDump.qml | partial | U for "dump to existing Scene" (io.simpleDesk.dump has targetSceneId, the web UI creates a new Scene only); spec+S+U for the popup's fixture / universe / channel-type filters (io.simpleDesk.dump only has nonZeroOnly) |
| Operate / Design mode toggle | MainView.qml | live | - |
| Toggle fullscreen | ActionsMenu.qml | n/a | - ; browser-native (F11) |
| Language switch | ActionsMenu.qml | n/a | - ; browser / OS locale is native; the web UI has no translations yet, which is a separate concern |
| UI Settings editor (theme colours, scaling factor, save to file) | ActionsMenu.qml, UISettingsEditor.qml | missing | U; purely client-side (App.jsx has a `window.QLCUISettingsDialog` hook, nothing registers it); core.settings covers engine settings only |
| Keyboard Shortcuts editor (rebind, import / export JSON, load defaults, shortcut hints toggle) | ActionsMenu.qml, ShortcutsEditor.qml | missing | U; client-side |
| Network: client setup (connect to another QLC+) | ActionsMenu.qml, popup/PopupNetworkClient.qml | n/a | - ; the web UI is the remote client |
| Network: server setup (native server, web server, encryption key) | ActionsMenu.qml, popup/PopupNetworkServer.qml | n/a | - ; server process settings of the machine running QLC+ |
| Client access request (allow / deny a connecting client) | MainView.qml, popup/PopupNetworkConnect.qml | n/a | - ; belongs to the host session |
| DMX Address tool (DIP switch calculator) | ActionsMenu.qml, DMXAddressTool.qml | missing | U; pure client-side |
| About dialog | ActionsMenu.qml, popup/PopupAbout.qml | live | - |
| First-run disclaimer | MainView.qml, popup/PopupDisclaimer.qml | n/a | - ; desktop first-run notice |
| Legacy Show timing warning and conversion (ADR 0001) | MainView.qml, showmanager/LegacyShowTimingDialog.qml, showmanager/LegacyShowTimingConvertDialog.qml | missing | spec+S+U; fork-specific, nothing in functions.show.* covers the conversion |
| Key-cast toast (shows the shortcut that just fired) and click hints | MainView.qml, FeedbackToast.qml, ShortcutsEditor.qml | missing | U |
| Network / connection button (host, port, connect, disconnect) | - (web UI only) | live | - ; no Qt equivalent, listed for completeness |

## Fixtures & Functions - fixture side

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Browse fixtures per universe (name, address, mode, channel list) | fixturesfunctions/FixtureGroupManager.qml, fixturesfunctions/FixtureNodeRow.qml | live | - |
| Fixture search filter (group / fixture / channel) | fixturesfunctions/FixtureGroupManager.qml | live | - ; client-side filter |
| Toggle multiple selection, Ctrl / Shift click | fixturesfunctions/LeftPanel.qml | live | - |
| Select / deselect all fixtures | fixturesfunctions/LeftPanel.qml | partial | U; button present in FixturesFunctions.jsx, not in the README verified list |
| Select every odd / even / Nth fixture of the selection | fixturesfunctions/FixturesAndFunctions.qml, popup/PopupInputNumber.qml | missing | U; client-side selection maths |
| Add fixtures (manufacturer / model / mode / universe / address / quantity / gap / name, overlap check) | fixturesfunctions/FixtureBrowser.qml, fixturesfunctions/FixtureProperties.qml | live | - ; Add is blocked while the range overlaps |
| Add a generic dimmer | fixturesfunctions/FixtureBrowser.qml | live | - |
| Add a generic RGB panel (columns, component order, displacement, start corner) | fixturesfunctions/RGBPanelProperties.qml | missing | spec+S+U; fixtures-notes.md explicitly cuts it |
| Rename fixture | fixturesfunctions/FixtureNodeRow.qml | live | - |
| Re-address fixture / move to another universe | fixturesfunctions/FixtureGroupManager.qml (Address / Universe fields) | live | - |
| Change fixture mode after patching | fixturesfunctions/FixtureGroupManager.qml (Mode field) | missing | spec+S+U; fixtures.update only takes universe / address / name; workaround is unpatch + patch |
| Unpatch / delete fixture | fixturesfunctions/FixtureGroupManager.qml | live | - ; confirm dialog |
| Per-channel behaviour: forced HTP / LTP, can-fade, channel modifier | fixturesfunctions/FixtureChannelDelegate.qml, fixturesfunctions/FixtureGroupManager.qml | missing | spec+S+U; fixtures-notes.md explicitly leaves these out of fixtures.update |
| Channel modifier templates editor | popup/PopupChannelModifiers.qml | missing | spec+S+U |
| Apply changes to fixtures of the same type | fixturesfunctions/FixtureGroupManager.qml | missing | U; client-side loop once the per-channel row above exists |
| Invert Pan / Invert Tilt per fixture | fixturesfunctions/FixtureNodeRow.qml | missing | spec+S+U; monitor property |
| Lock / unlock fixture position, show / hide fixture in the views | fixturesfunctions/FixtureNodeRow.qml | missing | spec+S+U; monitor property |
| Fixture summary (definition, physical block, address range) and print | fixturesfunctions/FixtureSummary.qml | partial | U; the web detail shows definition / mode / channels / addressing; physical block (fixtures.defs.getModel returns it) and print (browser print) missing |
| Universe summary (channels used, power, weight, DIP switch) and print | fixturesfunctions/UniverseSummary.qml | missing | U; derivable from fixtures.list + fixtures.defs.getModel |
| Fixture groups: create / rename / assign / unassign / delete | fixturesfunctions/FixtureGroupManager.qml | live | - |
| Multi-head fixtures: select individual heads for live tools and group assignment | fixturesfunctions/FixtureHeadDelegate.qml, fixturesfunctions/FixtureNodeDelegate.qml | missing | U; fixtures.group.assignHead / unassignHead are registered; head-level live tools are client-side channel-offset maths over fixtures.defs.getMode |
| Fixture group grid editor (group size / rows, rotate, flip, regenerate, reset, swap heads, per-head assign) | fixturesfunctions/FixtureGroupEditor.qml, fixturesfunctions/GridEditor.qml | missing | U; fixtures.group.setSize / swapHeads / reset / assignHead / unassignHead are registered, none is called |
| Invert selection in group(s) | MainView.qml, popup/PopupInvertGroupSelection.qml | missing | U |
| Rename items with numbering (start number, digits) | popup/PopupRenameItems.qml | missing | U; loop over fixtures.update / functions.rename |
| Fixture remap (drag new fixtures, map channels, clone, apply and save) | fixturesfunctions/FixtureRemap.qml, fixturesfunctions/RemapRowDelegate.qml | missing | S: fixtures.remap.apply, fixtures.remap.suggestChannelMap; button present, disabled |
| Create / edit a fixture definition (opens the Fixture Editor) | fixturesfunctions/FixtureBrowser.qml | missing | S: fixturedefs.*; see Fixture Editor section |
| Live tool: Intensity | fixturesfunctions/IntensityTool.qml, fixturesfunctions/LeftPanel.qml | live | - ; writes Simple Desk overrides at the fixture addresses |
| Live tool: Colour (basic palette, full picker, typed hex, RGB / CMY / WAUV) | ColorTool.qml, ColorToolBasic.qml, ColorToolFull.qml, ColorToolPrimary.qml | live | - |
| Live tool: Colour filters tab (named colour filter lists) | ColorToolFilters.qml | missing | U; filter definitions come from the engine's colour filter files, palette.* has no listing, so possibly spec+S+U |
| Live tool: Position (pan / tilt XY pad, spin boxes, centre, snap, rotate preview) | fixturesfunctions/PositionTool.qml | live | - ; snap-to-next / rotate-preview not checked |
| Live tool: Colour wheel / Gobos / Shutter / Beam / Speed / Prism / Effect / Maintenance capability presets | fixturesfunctions/PresetsTool.qml, fixturesfunctions/BeamTool.qml, fixturesfunctions/ShutterAnimator.qml, fixturesfunctions/MaintenanceTool.qml | live | - ; grouped presets from fixtures.defs.getMode; the Beam tool's projected-diameter maths is not ported |
| Live tool: Highlight (locate selected fixtures) | fixturesfunctions/LeftPanel.qml | missing | U; io.simpleDesk.setChannels |
| Live tool: Pick a 3D point (aim fixtures at a stage position) | fixturesfunctions/Position3DTool.qml | missing | spec+S+U; needs fixture 3D monitor positions |
| Release fixtures / reset dump channels | fixturesfunctions/RightPanel.qml ("Reset dump channels"), fixturesfunctions/LeftPanel.qml | live | - |
| Bottom-panel fixture console (plain per-channel faders for the selection) | fixturesfunctions/BottomPanel.qml, FixtureConsole.qml | live | - ; Fixture Tools show a plain slider per channel |
| Bottom-panel extras: copy values to all fixtures of the same type, pan-tilt fader mode, multi-channel selection, fader window shift | fixturesfunctions/BottomPanel.qml | missing | U |
| Bottom-panel external controller mapping of the fixture faders | fixturesfunctions/BottomPanel.qml | missing | spec+S+U; nothing maps an input line to the F&F console remotely |
| Palettes: list and search | fixturesfunctions/PaletteManager.qml | live | - ; search box not checked |
| Palettes: create Dimmer / Colour palette | popup/PopupCreatePalette.qml, PaletteFanningBox.qml | live | - |
| Palettes: create Position / Pan / Tilt / Shutter / Gobo / Position 3D palette | popup/PopupCreatePalette.qml | missing | U; the palette.create type enum already has them; FixtureDialogs.jsx handles Dimmer and Color only |
| Palettes: "Also create a Scene" on create | popup/PopupCreatePalette.qml | missing | U; palette.create + functions.create + functions.scene.setValues |
| Palettes: apply to the selected fixtures | fixturesfunctions/PaletteManager.qml | live | - ; double-click |
| Palettes: edit (rename, change value) | fixturesfunctions/PaletteManager.qml | missing | U; palette.update is registered, not called |
| Palettes: delete | fixturesfunctions/PaletteManager.qml | live | - |
| Palette fanning (type, layout, amount, per-axis ordering) | PaletteFanningBox.qml | missing | spec+S+U; palette.yaml has no fanning fields |

## Fixtures & Functions - function editors

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Browse functions per folder and type (type, speeds, contents) | fixturesfunctions/FunctionManager.qml | live | - |
| Function search filter | fixturesfunctions/FunctionManager.qml | live | - |
| Create function of every type (Scene, Chaser, Sequence, EFX, Collection, RGB Matrix, Show, Script, Audio, Video) in a folder | fixturesfunctions/AddFunctionMenu.qml, fixturesfunctions/RightPanel.qml | live | - |
| New folder / move to folder / move to top level | fixturesfunctions/RightPanel.qml | live | - |
| Rename function | fixturesfunctions/RightPanel.qml | live | - |
| Delete function(s) (multi-select, confirm) | fixturesfunctions/RightPanel.qml | live | - |
| Start / stop a function, running state shown | FunctionDelegate.qml | live | - ; state from functions.status.changed |
| Pause / resume a function | FunctionDelegate.qml | live | - |
| Clone the selected functions | fixturesfunctions/RightPanel.qml | missing | spec+S+U; no clone method (a client-side re-create needs every type's editor methods first) |
| Show function usage (functions and widgets using it) | fixturesfunctions/RightPanel.qml, UsageList.qml | missing | spec+S+U; vc.widget.usage is specced (widget side) but not registered, function-side usage is not specced |
| Set / unset autostart function | fixturesfunctions/RightPanel.qml | missing | spec+S+U; core.yaml only mentions the startup function in the mode description |
| Function preview toggle (edited function runs on the output while editing) | fixturesfunctions/RightPanel.qml ("Function Preview") | partial | U; starting the function with functions.start works, previewing unsaved Scene values does not (FunctionEditors.jsx says so); use Fixture Tools with target live output |
| Select fixtures in function(s) | fixturesfunctions/FunctionManager.qml | partial | U; code present in FixturesFunctions.jsx (selectFixtures), not live-verified |
| Timing settings: fade in / fade out / duration, tempo type (time / beats) | fixturesfunctions/RightPanel.qml (Timing Settings), TimeEditTool.qml | live | - ; tempo type (beats) not live-verified |
| Run order and direction (loop / single shot / ping pong / random, forward / backward) | fixturesfunctions/ChaserEditor.qml, fixturesfunctions/EFXEditor.qml, fixturesfunctions/RGBMatrixEditor.qml | live | - ; Chaser, EFX and RGB Matrix share the TimingEditor block; EFX/RGB run order not clicked in the sandbox runs, duration was |
| Scene editor: add fixtures, inline values, console faders, remove channel, fade times | fixturesfunctions/SceneEditor.qml, fixturesfunctions/SceneFixtureConsole.qml | live | - |
| Scene editor: add a fixture group / head as member | fixturesfunctions/SceneEditor.qml ("Add a fixture/group") | partial | U; fixtures are added one by one, groups not checked |
| Scene editor: add / remove palettes | fixturesfunctions/SceneEditor.qml | live | - |
| Scene editor: speed (fade in / out) | fixturesfunctions/SceneEditor.qml | live | - |
| Chaser editor: add / remove / move / duplicate steps, per-step fade in / hold / fade out / duration / note | fixturesfunctions/ChaserEditor.qml, ChaserStepDelegate.qml | live | - ; per-step times editable only where the speed mode is Per Step, like the desktop; duplicate and note are code present, not in the README list |
| Chaser editor: speed modes Common / Per Step / Default | fixturesfunctions/ChaserEditor.qml | missing | S: functions.chaser.setSpeedModes |
| Chaser editor: preview next / previous step | fixturesfunctions/ChaserEditor.qml | missing | S: functions.chaser.setAction |
| Chaser editor: randomize step order, auto-set step durations from total, print steps | fixturesfunctions/ChaserEditor.qml | missing | U; moveStep / replaceStep loops, browser print |
| Sequence editor: steps and per-channel step values via the bound Scene | fixturesfunctions/SequenceEditor.qml | partial | U; same editor as Chaser (verified) plus a per-channel value list; the per-channel list and the Sequence path are not live-verified |
| Sequence editor: add / remove fixtures of the bound Scene | fixturesfunctions/SequenceEditor.qml | partial | U; functions.scene.setMembers on the bound Scene; not checked from the Sequence editor |
| Sequence editor: capture live output into a step, preview channel changes on the output | fixturesfunctions/SequenceEditor.qml | missing | S: functions.sequence.applyDumpValues, functions.sequence.setBoundScene |
| Collection editor: add / remove member functions | fixturesfunctions/CollectionEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/efx-collection.js; add via picker, remove, move up / down; loops and self-membership refused by the server |
| EFX editor: pattern, width / height / rotation / offsets / frequencies / phases, speed, relative movement, preview | fixturesfunctions/EFXEditor.qml, fixturesfunctions/EFXPreview.qml | live | - ; sandbox 2026-09-27; 2D preview only (no fake-3D sphere view); preview data from functions.efx.getPreview so it matches the Qt canvas |
| EFX editor: fixture list (add / remove / reorder heads, per-fixture mode / direction / offset / reverse / dimmer control) and fixture order (parallel / serial / asymmetric) | fixturesfunctions/EFXEditor.qml | live | - ; sandbox 2026-09-27; adding a fixture adds every head (no per-head picker); Asymmetric and raise not clicked in the sandbox run |
| RGB Matrix editor: fixture group, algorithm, colours, blend / control mode, speed, preview | fixturesfunctions/RGBMatrixEditor.qml, fixturesfunctions/RGBMatrixPreview.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/rgbmatrix.js; Beats tempo preview approximated at 500 ms |
| RGB Matrix editor: script algorithm properties, text / image / animation parameters, font | fixturesfunctions/RGBMatrixEditor.qml | partial | U; list/range script properties, text, font family and text animation live (sandbox 2026-09-27); Image parameters, font size/bold/italic, offsets, float/string properties implemented + server-tested, not exercised in the browser; image path / font are server-side text fields |
| RGB Matrix editor: save this matrix to a Sequence | fixturesfunctions/RGBMatrixEditor.qml | missing | spec+S+U |
| Script editor: edit source, syntax check, insert command at cursor, fixture / function trees | fixturesfunctions/ScriptEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/media.js; the QML side trees are replaced by function / fixture ID pickers |
| Audio editor: file, output device, volume, fade in / out, looped / single shot, mute, file info | fixturesfunctions/AudioEditor.qml | live | - ; sandbox 2026-09-27; mute is read-only (no API setter); media info needs the decoder plugins (absent in the sandbox) |
| Audio editor: detect BPM, replace file (fork media store) | fixturesfunctions/AudioEditor.qml | partial | spec+S+U for Detect BPM (no method); Replace file live (sandbox 2026-09-27, media store copy + origin recorded) |
| Video editor: file / URL, output screen, windowed / fullscreen, geometry, rotation, layer, looped, mute | fixturesfunctions/VideoEditor.qml, fixturesfunctions/VideoContext.qml | live | - ; sandbox 2026-09-27; volume / mute read-only (no API setter) |
| Video editor: Spout output mode, sender name / size, replace file (fork) | fixturesfunctions/VideoEditor.qml | partial | spec+S+U for sender name / size (read-only); Spout selectable via outputMode and Replace file live (sandbox 2026-09-27) |
| Show function: edit on the timeline | fixturesfunctions/FunctionManager.qml | live | - ; sandbox 2026-09-27 on the Show Manager screen |
| Adjust a running function's intensity attribute | FunctionDelegate.qml (via the VC "Adjust" slider mode), functions.adjustAttribute | missing | S: functions.adjustAttribute |
| Show Wizard (stage wizard: show type, fixture roles, venue, effects, controller, generate) | fixturesfunctions/RightPanel.qml, stagewizard/ShowWizard.qml, stagewizard/WizardStep1ShowType.qml ... WizardStep6Summary.qml | missing | spec+S+U; the generator runs in the desktop process |

## 2D / 3D / DMX / Universe grid views

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| 2D top-down view of the rig with live colour / intensity per fixture | fixturesfunctions/2DView.qml, fixturesfunctions/Fixture2DItem.qml | missing | spec+S+U; fixture monitor properties (position, rotation, gel) are not in the spec |
| 2D / 3D: move and rotate fixtures (drag, position / rotation fields, invert axes, rotation scale) | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | missing | spec+S+U |
| 2D / 3D: align left / top, distribute horizontally / vertically | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | missing | spec+S+U |
| 2D / 3D: arrange fixtures (circle / grid / line, detect from placement, face centre, summon) | popup/PopupArrangeFixtures.qml | missing | spec+S+U |
| 2D / 3D: environment size, grid units (metres / feet), position range | fixturesfunctions/SettingsView2D.qml | missing | spec+S+U |
| 2D: point of view (top / front / left / right) and initial POV prompt | fixturesfunctions/SettingsView2D.qml, popup/PopupMonitor.qml | missing | spec+S+U |
| 2D: custom background image, reset background | fixturesfunctions/SettingsView2D.qml | missing | spec+S+U; the image lives on the server |
| 2D: gel colour per fixture | fixturesfunctions/SettingsView2D.qml | missing | spec+S+U |
| 2D / 3D: show fixture groups overlay | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | missing | spec+S+U |
| 2D / 3D: DMX-driven position / rotation | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | missing | spec+S+U |
| 2D: fixed zoom, zoom in / out | fixturesfunctions/SettingsView2D.qml, ZoomItem.qml | missing | U; client-side once the 2D view exists |
| 3D rendering (stage type, ambient light, smoke, quality, FPS, custom meshes, lock / normalize items) | fixturesfunctions/3DView/3DView.qml, fixturesfunctions/3DView/SettingsView3D.qml | n/a | - ; Qt3D rendering is desktop-only; parity is the 2D view plus the position / rotation editing rows above |
| DMX view (per-fixture channel values, show addresses, relative addresses) | fixturesfunctions/DMXView.qml, fixturesfunctions/FixtureDMXItem.qml, fixturesfunctions/SettingsViewDMX.qml | missing | S: io.universe.setMonitor; U for the view itself (io.dmx.universe.get and its changed stream exist) |
| Universe grid view (address map, cut / paste fixtures to the first free address) | fixturesfunctions/UniverseGridView.qml | missing | U; fixtures.update + fixtures.findAvailableAddress |
| Show / hide the view settings panel | fixturesfunctions/FixturesAndFunctions.qml | n/a | - ; UI structure |

## Virtual Console - live widgets

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| See pages and switch page | virtualconsole/VirtualConsole.qml | live | - |
| Enter a page PIN when the page is protected | popup/PopupPINRequest.qml, virtualconsole/VirtualConsole.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-layout.js; lock glyph on the tab via vc.page.updated, prompt on switch |
| Enter a frame PIN when a frame is protected | virtualconsole/VCFrameItem.qml, popup/PopupPINRequest.qml | live | - ; sandbox 2026-09-27; wrong PIN refused, correct PIN unlocks (the QML only checks page PINs, the web UI also locks frames) |
| See widgets at their geometry, nesting, captions, colours, fonts | virtualconsole/VCPageArea.qml, virtualconsole/VCWidgetItem.qml | live | - |
| Button: press, toggle, flash, state colouring | virtualconsole/VCButtonItem.qml | live | - |
| Button: Blackout and Stop-all action buttons | virtualconsole/VCButtonItem.qml | live | - ; sandbox 2026-09-27 (configured and pressed through the same vc.button.press path) |
| Button: adjust function intensity on press (startup intensity) | virtualconsole/VCButtonItem.qml | live | - ; sandbox 2026-09-27; Enable + 50% configured, XML <Intensity>50 |
| Slider / Knob: move, value pushed to every client | virtualconsole/VCSliderItem.qml, QLCPlusFader.qml, QLCPlusKnob.qml | live | - |
| Slider: flash button ("Flash the controlled Function") | virtualconsole/VCSliderItem.qml | live | - ; sandbox 2026-09-27 via vc.slider.flash + Show flash button config |
| Slider: monitor channel levels display | virtualconsole/VCSliderItem.qml | partial | U; monitor mode configurable and on/off exercised (sandbox 2026-09-27); the live monitorValueChanged feed is not shown in the widget body yet |
| Cue list: play / pause / stop / next / previous / jump to step, current step highlighted | virtualconsole/VCCueListItem.qml | live | - |
| Cue list: side fader (crossfade / steps) | virtualconsole/VCCueListItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-cue.js; both modes driven |
| XY pad: move the position | virtualconsole/VCXYPadItem.qml | live | - |
| XY pad: presets (position / fixture group / Scene / EFX preset buttons) | virtualconsole/VCXYPadItem.qml, virtualconsole/VCXYPadPresets.qml | missing | S: vc.xyPad.preset.move, vc.xyPad.preset.rename, vc.xyPad.activePresetChanged |
| XY pad: floor control (point fixtures at a stage floor position) | virtualconsole/VCXYPadItem.qml | missing | S: vc.xyPad.setFloorPosition, vc.xyPad.floorPositionChanged |
| Speed dial: set time, tap | virtualconsole/VCSpeedDialItem.qml | live | - |
| Speed dial: plus / minus buttons, multiplier factors, apply, reset tap, preset buttons | virtualconsole/VCSpeedDialItem.qml, virtualconsole/VCSpeedDialPresets.qml | live | - ; sandbox 2026-09-27; the stray speedDial.setCurrentTime wrapper is deprecated (preset.apply covers it) |
| Frame / Solo frame: multipage next / previous | virtualconsole/VCFrameItem.qml | live | - |
| Frame: enable / disable, expand / collapse | virtualconsole/VCFrameItem.qml | partial | U; enable button + header configured and exercised (sandbox 2026-09-27); Collapsed toggle server-tested only |
| Frame: page shortcuts by keyboard | virtualconsole/VCFrameProperties.qml | missing | S: vc.widget.keySequence.set; see editing section |
| Label | virtualconsole/VCLabelItem.qml | live | - |
| Clock: clock / stopwatch / countdown display, play / pause, reset, enable schedule | virtualconsole/VCClockItem.qml | missing | S: vc.clock.playPause, vc.clock.reset, vc.clock.timeChanged; view-only body |
| Animation: level fader, preset buttons, colour knobs | virtualconsole/VCAnimationItem.qml | missing | S: vc.animation.setFaderLevel, vc.animation.setPresetKnobValue, vc.animation.activePresetChanged, vc.animation.faderLevelChanged; view-only body |
| Audio triggers: enable / disable capture, live bars | virtualconsole/VCAudioTriggersItem.qml | missing | S: vc.audioTriggers.setCaptureEnabled, vc.audioTriggers.levelsChanged, vc.audioTriggers.captureEnabledChanged; capture runs on the host, which is fine |
| Grand Master value | virtualconsole/VirtualConsole.qml | live | - |
| Fire widgets by keyboard sequences in Operate mode | virtualconsole/VirtualConsole.qml, KeyboardSequenceDelegate.qml | missing | S: vc.widget.keySequence.set, vc.widget.keySequence.remove, vc.widget.keySequencesChanged; the browser would map keys client-side to vc.button.press etc. once the bindings are readable |
| Fire widgets from an external controller (input sources) | ExternalControls.qml | n/a | - ; input lines are patched on the host and drive the engine directly; the web UI only needs to *configure* them (editing section) |

## Virtual Console - editing

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Toggle widget edit mode | virtualconsole/VCRightPanel.qml | live | - ; Design mode |
| Page: add (left / right), rename, delete | virtualconsole/VCPageProperties.qml | live | - ; left / right placement not checked |
| Page: width / height | virtualconsole/VCPageProperties.qml | missing | spec+S+U; vc.page.* carries no size |
| Page: set / change PIN | virtualconsole/VCPageProperties.qml, popup/PopupPINSetup.qml | live | - ; sandbox 2026-09-27 via vc.page.setPin (+ new vc.page.updated event) |
| Add widget from the palette (Frame, Solo Frame, Button, Slider, Knob, Cue List, Speed, XY Pad, Animation, Label, Audio Triggers, Clock) | virtualconsole/WidgetsList.qml, virtualconsole/WidgetDragItem.qml | live | - ; click-to-place instead of drag |
| Add a Button matrix / Slider matrix (rows, columns, frame type) | virtualconsole/WidgetsList.qml, virtualconsole/VirtualConsole.qml ("Widget matrix setup") | live | - ; sandbox 2026-09-27 (3x3 button matrix through the dialog; slider / solo-frame variants server-tested) |
| Create widgets by dropping functions on the page | virtualconsole/VCPageArea.qml, virtualconsole/VCRightPanel.qml (Function Manager panel) | live | - ; sandbox 2026-09-27 as an "Add widgets from functions" dialog over vc.widget.createFromFunctions (3 scenes -> 3 buttons); adjust-slider / cue-list hints server-tested |
| Move / resize widgets, snapping toggle | virtualconsole/VCWidgetItem.qml, virtualconsole/VirtualConsole.qml | live | - |
| Copy / cut / paste / delete widgets | virtualconsole/VCRightPanel.qml | live | - ; cut is copy + delete |
| Widget caption | virtualconsole/VCWidgetProperties.qml | live | - |
| Widget foreground / background colour, font, bold | virtualconsole/VCWidgetProperties.qml | partial | U; controls present in vc-edit.jsx, not in the README verified list |
| Widget background image | virtualconsole/VCWidgetProperties.qml | missing | U; VcWidgetStyle has backgroundImage (server-side path) |
| Widget z-index | virtualconsole/VCWidgetProperties.qml | missing | U; VcWidgetStyle has zIndex |
| Align selected widgets left / right / top / bottom | virtualconsole/VCWidgetProperties.qml | partial | U; top / left exercised (sandbox 2026-09-27), right / bottom server-tested only; selection must share one parent (INVALID_PARAMS otherwise, by design) |
| Distribute selected widgets horizontally / vertically | virtualconsole/VCWidgetProperties.qml | partial | U; horizontal exercised (sandbox 2026-09-27), vertical server-tested only |
| Bulk style on a multi-selection (caption, colours, font) | virtualconsole/VCWidgetProperties.qml | partial | U; caption exercised via one vc.widget.bulkStyle (sandbox 2026-09-27); colours / font server-tested only |
| Widget presets (save / apply / remove a style preset) | virtualconsole/VCWidgetProperties.qml | partial | U; generic vc.widget.preset.add/apply/remove + per-type host dispatch live for Speed (sandbox 2026-09-27); XYPad / Animation preset payloads are stubs (INVALID_STATE) until the live-widgets slice fills them |
| External controls: add / remove an input source, auto-detect, manual selection | ExternalControls.qml, ExternalControlDelegate.qml, popup/PopupManualInputSource.qml | missing | S: vc.widget.inputSource.set, vc.widget.inputSource.remove, vc.widget.inputDetect.start, vc.widget.inputDetect.stop |
| External controls: custom feedback values / colours per input source | popup/PopupCustomFeedback.qml | missing | S: vc.widget.inputSource.set (carries feedback fields) |
| External controls: add / remove a keyboard combination, auto-detect | ExternalControls.qml, KeyboardSequenceDelegate.qml | missing | S: vc.widget.keySequence.set, vc.widget.keySequence.remove |
| Button properties: attached function, pressure behaviour (Toggle / Flash / Blackout / Stop all) | virtualconsole/VCButtonProperties.qml | live | - |
| Button properties: flash override priority / force LTP, stop-all fade out, adjust function intensity | virtualconsole/VCButtonProperties.qml | live | - ; sandbox 2026-09-27, every VcButtonConfig field exercised |
| Slider properties: display style (DMX / percent, normal / inverted, slider / knob), mode (Level / Adjust / Submaster / Grand Master), value limits | virtualconsole/VCSliderProperties.qml | partial | U; Level and Adjust modes exercised (sandbox 2026-09-27); Submaster / Grand Master, knob / percent / inverted radios and range limits server-tested only |
| Slider properties: Level mode channel list (add / remove, all / intensity / RGB / gobo groups) | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27; channel picker over fixtures.list (expand fixture, tick channels), vc.slider.setLevelChannels; group shortcuts (all / intensity / RGB / gobo) not offered |
| Slider properties: monitor channel levels, catch up with external input | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27 (Monitor on/off/on, catch-up) |
| Slider properties: click & go button type | virtualconsole/VCSliderProperties.qml | partial | U; type combobox implemented (server-tested); no colour / preset picker UI for it |
| Slider properties: Function Control (Adjust) mode - attached function and attribute | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27 (function picker + attribute + Show flash button) |
| Slider properties: Grand Master mode (value / channel mode) | virtualconsole/VCSliderProperties.qml | partial | U; GM modes in VcSliderConfig, server-tested only; io.grandMaster.setMode is another slice |
| Slider properties: show flash button | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27 |
| Cue list properties: attached chaser, next / previous behaviour, playback layout, side fader mode | virtualconsole/VCCueListProperties.qml | live | - ; sandbox 2026-09-27 |
| Frame properties: header, enable button, pages (count, labels, loop, clone first page), shortcut names, solo options | virtualconsole/VCFrameProperties.qml | live | - ; sandbox 2026-09-27; pages 3, label, circular scrolling, enable button, header, clone first page, solo exclude-monitored + mixing; Collapsed server-tested only |
| XY pad properties: fixtures / heads (add, remove, pan-tilt range, reverse), axis ranges, inverted Y, floor control, display units | virtualconsole/VCXYPadProperties.qml | missing | S: vc.widget.setConfig for XYPad, vc.xyPad.fixture.add, vc.xyPad.fixture.remove, vc.xyPad.setHeadsRange |
| XY pad properties: presets (create from position, drop Scene / EFX / fixture group, rename, reorder, remove) | virtualconsole/VCXYPadPresets.qml | missing | S: vc.xyPad.preset.move, vc.xyPad.preset.rename (add / remove not specced: spec+S+U) |
| Speed dial properties: functions list, multipliers, dial time range, visibility of parts, tap controls BPM, reset on change | virtualconsole/VCSpeedDialProperties.qml | live | - ; sandbox 2026-09-27; tap-controls-BPM checkbox rendered, not clicked in the driver |
| Speed dial properties: presets (add / remove, name, time) | virtualconsole/VCSpeedDialPresets.qml | live | - ; sandbox 2026-09-27 via vc.widget.preset.* + vc.speedDial.preset.update |
| Clock properties: clock type, schedules (add / remove / update, function, start / stop time, weekdays) | virtualconsole/VCClockProperties.qml | missing | S: vc.widget.setConfig for Clock, vc.clock.schedule.add, vc.clock.schedule.remove, vc.clock.schedule.update |
| Animation properties: attached function, visibility, instant changes, presets list (colour / text / algorithm presets) | virtualconsole/VCAnimationProperties.qml, virtualconsole/VCAnimationPresets.qml, popup/PopupAnimationPreset.qml | missing | S: vc.widget.setConfig for Animation, vc.animation.preset.move (preset add / remove not specced: spec+S+U) |
| Audio triggers properties: number of bars, per-bar type (DMX / function / widget), thresholds, targets | virtualconsole/VCAudioTriggersProperties.qml | missing | S: vc.widget.setConfig for AudioTriggers, vc.audioTriggers.setBarConfig |
| Label properties (caption / style only) | virtualconsole/VCLabelItem.qml | live | - ; generic caption / style rows |
| Widget usage (functions referenced by a widget) | virtualconsole/VCWidgetProperties.qml, UsageList.qml | live | - ; sandbox 2026-09-27, Usage popup over vc.widget.usage |
| Function Manager side panel inside the VC (browse functions to attach) | virtualconsole/VCRightPanel.qml | live | - ; the FunctionPicker in the properties panel |

## Simple Desk

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Pick the universe | SimpleDesk.qml | live | - |
| See live DMX values and override flags for 512 channels, channel group icons | SimpleDesk.qml | live | - |
| Set a channel value (fader) | SimpleDesk.qml, QLCPlusFader.qml | live | - |
| DMX / percent value display | SimpleDesk.qml, DMXPercentageButton.qml | live | - ; verified read-only in the 2026-09-26 audit pass |
| Reset a channel | SimpleDesk.qml | live | - |
| Reset the whole universe | SimpleDesk.qml | live | - |
| Keypad commands (THRU, AT / @, FULL, ZERO, BY, +, -, +%, -%, CLR) | KeyPad.qml, SimpleDesk.qml | live | - ; since 2026-09-27 through the engine parser (io.simpleDesk.sendKeypadCommand), browser parser as fallback; sandbox-verified |
| Commands history (reload a past command) | SimpleDesk.qml | live | - ; sandbox 2026-09-27; the server's shared history (io.simpleDesk.get commandHistory + commandHistoryChanged) |
| Channel value debug (which functions / sources contribute to a channel) | SimpleDesk.qml | missing | spec+S+U |
| Fixture list side panel | SimpleDesk.qml | live | - |
| Dump to a new Scene (name, non-zero only) | SimpleDesk.qml, popup/PopupDMXDump.qml | live | - |
| Dump into an existing Scene | popup/PopupDMXDump.qml | missing | U; io.simpleDesk.dump has targetSceneId |
| Dump filters (fixtures, universes, channel types) | popup/PopupDMXDump.qml | missing | spec+S+U |
| Tap tempo from the keypad | KeyPad.qml | live | - ; same core.bpm.tap as the toolbar |

## Input / Output

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| See universes with input / output / feedback patches | inputoutput/InputOutputManager.qml, inputoutput/UniverseIOItem.qml | live | - |
| Add a universe | inputoutput/IORightPanel.qml | live | - |
| Remove the selected universe | inputoutput/IORightPanel.qml | live | - ; last universe only, engine constraint on both UIs |
| Rename universe | inputoutput/UniverseIOItem.qml | live | - |
| Passthrough toggle | inputoutput/UniverseIOItem.qml | live | - |
| Patch input / output / feedback line (plugin + line pickers) | inputoutput/PluginsList.qml, inputoutput/InputPatchItem.qml, inputoutput/OutputPatchItem.qml, inputoutput/PatchWireBox.qml | live | - ; pickers replace drag-and-drop; verified on an instance without IO plugins, so real plugin lines are sandbox-unverified |
| Multiple output patches per universe | inputoutput/UniverseIOItem.qml | partial | U; server + editable UI incl. add / remove / pause / blackout of extra output lines (2026-09-27, unit-tested with the stub plugin); not live-verified without an IO plugin |
| Enable / disable feedback | inputoutput/UniverseIOItem.qml | partial | U; feedback picker present, not live-verified with a real line |
| Refresh plugin lines / rescan | inputoutput/PluginsList.qml | partial | U; io.plugin.rescan / getLines / linesChanged implemented + Rescan buttons (2026-09-27); unit-tested only; DMXUSB gains rescan once Plugins dir is rebuilt |
| Plugin line parameters for network plugins (ArtNet / E1.31 / OSC IP, port, transmission mode ...) | inputoutput/IOLeftPanel.qml, inputoutput/IORightPanel.qml ("Open the plugin configuration") | partial | U; io.patch.setParameters + parameter dialog (io/PatchProperties.jsx) with ArtNet / E1.31 / OSC key suggestions (2026-09-27); unit-tested only, no plugin in the sandbox |
| Plugin configuration dialog for native-hardware plugins (dmxusb, MIDI device dialogs ...) | inputoutput/IOLeftPanel.qml, inputoutput/IORightPanel.qml | partial | U; io.plugin.configure opens the plugin's native dialog on the QLC+ host from a browser button (2026-09-27); the dialog itself stays on the host by nature |
| Per-output-patch blackout, play / pause an output patch | inputoutput/OutputPatchItem.qml | partial | U; io.patch.output.setState + buttons (2026-09-27); unit-tested only |
| Blackout on all output patches | inputoutput/IORightPanel.qml | live | - ; global blackout |
| Assign an input profile to a universe | inputoutput/ProfilesList.qml, inputoutput/InputPatchItem.qml | partial | U; profile list is live, assignment via io.patch.set profile field present but not live-verified |
| Input profile: create / edit / save / delete (channels, MIDI channels, colours, behaviour, sensitivity) | inputoutput/InputProfileEditor.qml, inputoutput/ProfilesList.qml, popup/PopupInputChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/io.js; io/InputProfileEditor.jsx, .qxi lands in the host's user profile folder (QLCPLUS_USER_INPUTPROFILE_DIR override in sandboxes / tests) |
| Input profile: channel auto-detection (learn) | inputoutput/ProfilesList.qml, inputoutput/InputProfileEditor.qml | partial | U; io.inputProfile.learn.* scoped to the requesting client + Detect button (2026-09-27); unit-tested only (no input line in the sandbox) |
| Input profile: custom feedback and MIDI global settings | inputoutput/InputProfileEditor.qml, popup/PopupCustomFeedback.qml | partial | U; MIDI note-off, colour table, MIDI channel labels live via the editor (sandbox 2026-09-27); custom feedback values unit-tested only (Button-only in the .qxi format) |
| Audio input / output device selection, sample rate, channels, buffer size | inputoutput/AudioCardsList.qml, inputoutput/AudioIOItem.qml, popup/PopupAudioConfiguration.qml | partial | U; device lists live and io.audio.setDevice built (2026-09-27, not exercised: it writes the host's real settings); sample rate / channels / buffer size not exposed |
| Audio input signal level check | popup/PopupAudioConfiguration.qml | n/a | - ; host audio capture |

## Show Manager

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Show Manager context with timeline, header and cursor | showmanager/ShowManager.qml, showmanager/HeaderAndCursor.qml | live | - ; sandbox 2026-09-27 |
| Pick / create the Show function being edited | showmanager/ShowManager.qml | partial | U; pick + rename driven in the sandbox (2026-09-27); create "+" implemented via functions.create, not browser-driven |
| Tracks: create, rename, delete, move up / down | showmanager/ShowManager.qml, showmanager/TrackDelegate.qml | partial | U; create + rename live (sandbox 2026-09-27); delete-with-confirm and move up / down implemented + server-tested, not browser-driven |
| Tracks: mute / solo | showmanager/TrackDelegate.qml | live | - ; sandbox 2026-09-27 |
| Items: drop a function on the timeline, move, resize / stretch, delete | showmanager/ShowItem.qml, showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27; items are added from a function picker at the cursor instead of drag-drop from the tree; overlap rejected with nearest-free-spot placement; stretch mode not offered |
| Items: colour, lock / unlock | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27 |
| Items: copy / paste at cursor | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27 |
| Timing panel: start / duration / end edit, align start / end to cursor | showmanager/TimingUtils.qml | partial | U; implemented over item.move / item.resize, not browser-driven |
| Ripple: cut time / insert time | showmanager/TimingUtils.qml | partial | U; insert live (sandbox 2026-09-27); cut server-tested only |
| Time division (Time, BPM 2/4, 3/4, 4/4) | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27 |
| Playback: play from cursor, pause, stop / rewind, cursor follows playback | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27; functions.start startTime + the gated functions.show.<id>.playhead stream |
| Preview the Show at the cursor while stopped / paused | showmanager/ShowManager.qml | missing | spec+S+U |
| Snap to grid, markers, zoom | showmanager/ShowManager.qml | partial | U; implemented client-side, not browser-driven |
| Track Spout output size (set, unset, mismatch prompt) | showmanager/TrackDelegate.qml, popup/PopupTrackSpoutSize.qml, popup/PopupSpoutSizeMismatch.qml | missing | spec+S+U; the Spout sender is desktop-only, but its size is document state that the operator sets in this dialog (arguably n/a) |
| Legacy Show timing conversion | showmanager/LegacyShowTimingDialog.qml, showmanager/LegacyShowTimingConvertDialog.qml | missing | spec+S+U; duplicate of the toolbar row |

## Fixture Editor

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Open the Fixture Editor window, back to QLC+ | fixtureeditor/FixtureEditor.qml, WindowLoader.qml | missing | S: fixturedefs.session.list, fixturedefs.session.open, fixturedefs.session.close |
| New definition | fixtureeditor/FixtureEditor.qml | missing | S: fixturedefs.session.create |
| Open an existing definition (user or system) | fixtureeditor/FixtureEditor.qml, popup/PopupFolderBrowser.qml | missing | S: fixturedefs.list, fixturedefs.get, fixturedefs.session.open, fixturedefs.session.forkToUser |
| Save / save as | fixtureeditor/FixtureEditor.qml | missing | S: fixturedefs.save, fixturedefs.export |
| General: manufacturer, model, type, author | fixtureeditor/EditorView.qml | missing | S: fixturedefs.session.update |
| Channels: add / remove / edit (name, group, preset, default value, coarse / fine, colours) | fixtureeditor/EditorView.qml, fixtureeditor/ChannelEditor.qml | missing | S: fixturedefs.channel.add, fixturedefs.channel.update, fixturedefs.channel.remove |
| Capabilities: add / remove / edit (range, description, preset, values, colours) | fixtureeditor/ChannelEditor.qml | missing | S: fixturedefs.channel.capability.add, fixturedefs.channel.capability.update, fixturedefs.channel.capability.remove |
| Capabilities: gobo picture | fixtureeditor/ChannelEditor.qml | missing | spec+S+U; picture upload from the browser |
| Capabilities: automatic colour assignment | fixtureeditor/ChannelEditor.qml | missing | S: fixturedefs.channel.capability.autoPatchColors |
| Channel / capability wizard | popup/PopupChannelWizard.qml | missing | spec+S+U |
| Modes: add / remove / rename, channel list, per-mode physical override | fixtureeditor/EditorView.qml, fixtureeditor/ModeEditor.qml | missing | S: fixturedefs.mode.add, fixturedefs.mode.remove, fixturedefs.mode.rename, fixturedefs.mode.setChannels, fixturedefs.mode.setPhysical |
| Modes: heads / emitters (create, remove, acts-on channels) | fixtureeditor/ModeEditor.qml | missing | S: fixturedefs.mode.head.add, fixturedefs.mode.head.remove |
| Physical properties (bulb, dimensions, lens, focus, layout, electrical) | fixtureeditor/PhysicalProperties.qml | missing | S: fixturedefs.session.setPhysical |
| Aliases (replace channel with another while a capability is active, apply to all modes) | fixtureeditor/AliasEditor.qml | missing | S: fixturedefs.channel.capability.alias.add, fixturedefs.channel.capability.alias.update, fixturedefs.channel.capability.alias.remove, fixturedefs.channel.capability.alias.applyToAllModes |
| Import a definition file | fixtureeditor/FixtureEditor.qml | missing | S: fixturedefs.session.import; browser-side upload is spec+S+U |
| Validation errors and warnings | fixtureeditor/EditorView.qml | missing | S: fixturedefs.session.validate |

## Popups & tools

One row per `qmlui/qml/popup/*.qml` and per tool component in the `qmlui/qml` root. Rows that
duplicate a feature row above say so.

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Generic popup dialog frame | popup/CustomPopupDialog.qml | n/a | - ; internal building block, the web UI has CustomPopupDialog in the design-system bundle |
| Import project: function rows | popup/ImportFunctionsFlatDelegate.qml | n/a | - ; delegate of PopupImportProject, not an action |
| Import project: group rows | popup/ImportGroupsFlatDelegate.qml | n/a | - ; delegate of PopupImportProject, not an action |
| Input profile: channel rows | popup/InputChannelFlatDelegate.qml | n/a | - ; delegate of the profile editor, not an action |
| About | popup/PopupAbout.qml | live | - ; duplicate of the toolbar row |
| Animation algorithm preset | popup/PopupAnimationPreset.qml | missing | S: vc.widget.setConfig for Animation; duplicate of the Animation properties row |
| Arrange fixtures | popup/PopupArrangeFixtures.qml | missing | spec+S+U; duplicate of the 2D / 3D arrange row |
| Audio configuration | popup/PopupAudioConfiguration.qml | n/a | - ; host audio devices |
| Channel modifiers editor | popup/PopupChannelModifiers.qml | missing | spec+S+U; duplicate |
| Channel wizard | popup/PopupChannelWizard.qml | missing | spec+S+U; duplicate |
| Create palette | popup/PopupCreatePalette.qml | partial | U; Dimmer / Colour live, other types and "Also create a Scene" missing |
| Custom feedback | popup/PopupCustomFeedback.qml | missing | S: vc.widget.inputSource.set, io.inputProfile.save |
| DMX channel dump | popup/PopupDMXDump.qml | partial | U for the target Scene, spec+S+U for the filters |
| Disclaimer | popup/PopupDisclaimer.qml | n/a | - ; desktop first-run notice |
| Folder browser (server-side file picker) | popup/PopupFolderBrowser.qml | live | - ; sandbox 2026-09-27 as window.ServerFileBrowser over core.fs.list, used by the Audio / Video editors; the Open-project dialog still takes a typed path plus recent files |
| Import from project | popup/PopupImportProject.qml | missing | spec+S+U |
| Input channel editor | popup/PopupInputChannelEditor.qml | missing | S: io.inputProfile.save |
| Enter a number (select every Nth) | popup/PopupInputNumber.qml | missing | U |
| Invert selection in group(s) | popup/PopupInvertGroupSelection.qml | missing | U |
| Manual input source selection | popup/PopupManualInputSource.qml | missing | S: vc.widget.inputSource.set |
| 2D point of view selection | popup/PopupMonitor.qml | missing | spec+S+U |
| Network client setup | popup/PopupNetworkClient.qml | n/a | - ; the web UI is the remote |
| Client access request | popup/PopupNetworkConnect.qml | n/a | - ; host session |
| Network server setup | popup/PopupNetworkServer.qml | n/a | - ; host process settings |
| Page / frame PIN request | popup/PopupPINRequest.qml | live | - ; sandbox 2026-09-27 |
| PIN setup | popup/PopupPINSetup.qml | live | - ; sandbox 2026-09-27 (frame via dialog, page via vc.page.setPin) |
| Rename items with numbering | popup/PopupRenameItems.qml | missing | U |
| Spout size mismatch | popup/PopupSpoutSizeMismatch.qml | missing | spec+S+U; duplicate of the track Spout row |
| Track Spout output size | popup/PopupTrackSpoutSize.qml | missing | spec+S+U; duplicate of the track Spout row |
| DMX Address tool | DMXAddressTool.qml, DMXAddressWidget.qml | missing | U |
| Usage list | UsageList.qml | missing | spec+S+U; duplicate of the function usage row |
| UI Settings editor | UISettingsEditor.qml, UISettings.qml | missing | U |
| Shortcuts editor | ShortcutsEditor.qml | missing | U |
| External controls panel (input sources + key sequences of a widget) | ExternalControls.qml, ExternalControlDelegate.qml, KeyboardSequenceDelegate.qml | missing | S: vc.widget.inputSource.*, vc.widget.inputDetect.*, vc.widget.keySequence.* |
| Beat generators panel | BeatGeneratorsPanel.qml | missing | U; core.bpm.set generator enum |
| Colour tool (basic / full / filters) | ColorTool.qml, ColorToolBasic.qml, ColorToolFull.qml, ColorToolFilters.qml, ColorToolPrimary.qml, MultiColorBox.qml | partial | U; filters tab missing, see the fixture-side row |
| Time edit tool (typed time, tap, infinite) | TimeEditTool.qml | live | - ; inline "500, 1.5s, 2m, inf" fields |
| Day-time tool (clock schedule times) | DayTimeTool.qml | missing | S: vc.clock.schedule.* |
| Keypad | KeyPad.qml | live | - |
| Fixture console (per-channel faders) | FixtureConsole.qml, ChannelToolLoader.qml | live | - ; see the bottom-panel rows |
| Palette fanning box | PaletteFanningBox.qml | missing | spec+S+U |
| Single-axis tool (pan or tilt only fixtures) | SingleAxisTool.qml | partial | U; pan / tilt spin boxes exist, a single-axis layout is not checked |
| Chaser step widget (per-step times popup) | ChaserWidget.qml, ChaserStepDelegate.qml | live | - |
| Zoom item (view zoom controls) | ZoomItem.qml | missing | U; only relevant once the 2D view exists |
| Feedback toast | FeedbackToast.qml | missing | U; the web UI has its own notices (VCNotice, Note), the key-cast toast is missing |

## Settings

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| UI settings: theme colours, scaling factor, reset to default, save to file | UISettingsEditor.qml | missing | U; client-side (localStorage or a downloadable file) |
| Shortcuts editor: rebind, load defaults, import / export, show shortcut hints | ShortcutsEditor.qml | missing | U; client-side |
| Beat generator source (disabled / internal / plugin / audio) | BeatGeneratorsPanel.qml | missing | U; core.bpm.set generator enum; duplicate of the toolbar row |
| Engine settings: working path, engine log locale | - (no QML UI; core.settings.get / set) | n/a | - ; no Qt UI equivalent, spec-only, listed so nobody re-adds it as a gap |

## Summary

Counts are rows per status across every table above (popup rows duplicate some feature rows, so
the total is larger than the number of distinct actions). Recompute after editing:
`grep -cE "^\|[^|]*\|[^|]*\| (live|partial|missing|n/a) \|" docs/webui-parity.md` per word.

| Status | Rows |
| --- | --- |
| live | 128 |
| partial | 42 |
| missing | 124 |
| n/a | 20 |
| total | 314 |
