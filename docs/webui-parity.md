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
| Keyboard shortcuts for toolbar actions (Ctrl+1..5 contexts, Ctrl+S, Ctrl+B, Ctrl+Z / Ctrl+Y) | MainView.qml, ShortcutsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js; table-driven and rebindable (web layout: Ctrl+4 I/O, Ctrl+5 Show Manager); per-screen keys (VC bindings, keypad, Show Manager) are not in the editor |
| New project (with "save changes first?" prompt) | ActionsMenu.qml | live | - |
| Open project by server-side path | ActionsMenu.qml, popup/PopupFolderBrowser.qml | live | - |
| Open recent file | ActionsMenu.qml | live | - |
| Open a project file from the local machine (native file dialog, drag-and-drop onto the window) | ActionsMenu.qml, MainView.qml ("Drop a project or fixture file") | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (upload button + drag-and-drop, unsaved-changes guard) |
| Save project (Ctrl+S) | ActionsMenu.qml | live | - |
| Save project as (server-side path) | ActionsMenu.qml | live | - |
| Import fixtures / functions from another project | ActionsMenu.qml, popup/PopupImportProject.qml, importmanager.cpp | live | - ; sandbox 2026-09-27; Script / Show ids are not remapped (upstream limitation, same as the desktop) |
| Collect media into project (fork asset store) | ActionsMenu.qml, MainView.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/functions-misc.js (functions.media.collect) |
| Reload changed media | ActionsMenu.qml, MainView.qml | live | - ; sandbox 2026-09-27 (Actions menu dialog over functions.media.status / reload) |
| Remove unused media | ActionsMenu.qml, MainView.qml | live | - ; sandbox 2026-09-27 (functions.media.removeUnused) |
| Undo / Redo (with history labels) | ActionsMenu.qml | live | - |
| Blackout toggle (Ctrl+B) | MainView.qml | live | - |
| Stop all running functions | MainView.qml | live | - |
| BPM display, set, tap, off, beat indicator | MainView.qml, KeyPad.qml (Tap) | live | - |
| Beat generator source selection (disabled / internal / plugin / audio) | BeatGeneratorsPanel.qml, MainView.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js; the source switch is live; plugin / audio beat delivery unverified in the plugin-less sandbox |
| Dump DMX values on a Scene (toolbar button) | MainView.qml, popup/PopupDMXDump.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (non-zero / selected fixtures / channel-type filters, new or existing Scene) |
| Operate / Design mode toggle | MainView.qml | live | - |
| Toggle fullscreen | ActionsMenu.qml | n/a | - ; browser-native (F11) |
| Language switch | ActionsMenu.qml | n/a | - ; browser / OS locale is native; the web UI has no translations yet, which is a separate concern |
| UI Settings editor (theme colours, scaling factor, save to file) | ActionsMenu.qml, UISettingsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js; colours / scale / reset / save / load (desktop qlcplusUiStyle.json shape); engine settings write built but not exercised |
| Keyboard Shortcuts editor (rebind, import / export JSON, load defaults, shortcut hints toggle) | ActionsMenu.qml, ShortcutsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| Network: client setup (connect to another QLC+) | ActionsMenu.qml, popup/PopupNetworkClient.qml | n/a | - ; the web UI is the remote client |
| Network: server setup (native server, web server, encryption key) | ActionsMenu.qml, popup/PopupNetworkServer.qml | n/a | - ; server process settings of the machine running QLC+ |
| Client access request (allow / deny a connecting client) | MainView.qml, popup/PopupNetworkConnect.qml | n/a | - ; belongs to the host session |
| DMX Address tool (DIP switch calculator) | ActionsMenu.qml, DMXAddressTool.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| About dialog | ActionsMenu.qml, popup/PopupAbout.qml | live | - |
| First-run disclaimer | MainView.qml, popup/PopupDisclaimer.qml | n/a | - ; desktop first-run notice |
| Legacy Show timing warning and conversion (ADR 0001) | MainView.qml, showmanager/LegacyShowTimingDialog.qml, showmanager/LegacyShowTimingConvertDialog.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (upload path live; detection from a file on disk unit-tested; conversion is not on the undo stack) |
| Key-cast toast (shows the shortcut that just fired) and click hints | MainView.qml, FeedbackToast.qml, ShortcutsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (shared toast for App shortcuts and VC key bindings, click hints) |
| Network / connection button (host, port, connect, disconnect) | - (web UI only) | live | - ; no Qt equivalent, listed for completeness |

## Fixtures & Functions - fixture side

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Browse fixtures per universe (name, address, mode, channel list) | fixturesfunctions/FixtureGroupManager.qml, fixturesfunctions/FixtureNodeRow.qml | live | - |
| Fixture search filter (group / fixture / channel) | fixturesfunctions/FixtureGroupManager.qml | live | - ; client-side filter |
| Toggle multiple selection, Ctrl / Shift click | fixturesfunctions/LeftPanel.qml | live | - |
| Select / deselect all fixtures | fixturesfunctions/LeftPanel.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| Select every odd / even / Nth fixture of the selection | fixturesfunctions/FixturesAndFunctions.qml, popup/PopupInputNumber.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Add fixtures (manufacturer / model / mode / universe / address / quantity / gap / name, overlap check) | fixturesfunctions/FixtureBrowser.qml, fixturesfunctions/FixtureProperties.qml | live | - ; Add is blocked while the range overlaps |
| Add a generic dimmer | fixturesfunctions/FixtureBrowser.qml | live | - |
| Add a generic RGB panel (columns, component order, displacement, start corner) | fixturesfunctions/RGBPanelProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-misc.js; rows that do not fit roll into the next existing universe (no universe creation) |
| Rename fixture | fixturesfunctions/FixtureNodeRow.qml | live | - |
| Re-address fixture / move to another universe | fixturesfunctions/FixtureGroupManager.qml (Address / Universe fields) | live | - |
| Change fixture mode after patching | fixturesfunctions/FixtureGroupManager.qml (Mode field) | live | - ; sandbox 2026-09-27 (library fixture; no SF3 type has two modes), atomic and validated |
| Unpatch / delete fixture | fixturesfunctions/FixtureGroupManager.qml | live | - ; confirm dialog |
| Per-channel behaviour: forced HTP / LTP, can-fade, channel modifier | fixturesfunctions/FixtureChannelDelegate.qml, fixturesfunctions/FixtureGroupManager.qml | live | - ; sandbox 2026-09-27 (forced HTP / LTP, can-fade, modifier) |
| Channel modifier templates editor | popup/PopupChannelModifiers.qml | live | - ; sandbox 2026-09-27 (adds rename and delete) |
| Apply changes to fixtures of the same type | fixturesfunctions/FixtureGroupManager.qml | live | - ; sandbox 2026-09-27 |
| Invert Pan / Invert Tilt per fixture | fixturesfunctions/FixtureNodeRow.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| Lock / unlock fixture position, show / hide fixture in the views | fixturesfunctions/FixtureNodeRow.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Fixture summary (definition, physical block, address range) and print | fixturesfunctions/FixtureSummary.qml | live | - ; sandbox 2026-09-27, print via the browser |
| Universe summary (channels used, power, weight, DIP switch) and print | fixturesfunctions/UniverseSummary.qml | live | - ; sandbox 2026-09-27, print via the browser |
| Fixture groups: create / rename / assign / unassign / delete | fixturesfunctions/FixtureGroupManager.qml | live | - |
| Multi-head fixtures: select individual heads for live tools and group assignment | fixturesfunctions/FixtureHeadDelegate.qml, fixturesfunctions/FixtureNodeDelegate.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js; heads are picked with alt-click in the 2D view (the desktop picks them in the tree); live tools and group assignment act on the selected heads only |
| Fixture group grid editor (group size / rows, rotate, flip, regenerate, reset, swap heads, per-head assign) | fixturesfunctions/FixtureGroupEditor.qml, fixturesfunctions/GridEditor.qml | live | - ; sandbox 2026-09-27 |
| Invert selection in group(s) | MainView.qml, popup/PopupInvertGroupSelection.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| Rename items with numbering (start number, digits) | popup/PopupRenameItems.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (functions and fixtures) |
| Fixture remap (drag new fixtures, map channels, clone, apply and save) | fixturesfunctions/FixtureRemap.qml, fixturesfunctions/RemapRowDelegate.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js; Scene references and 2D placement follow the remap |
| Create / edit a fixture definition (opens the Fixture Editor) | fixturesfunctions/FixtureBrowser.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Live tool: Intensity | fixturesfunctions/IntensityTool.qml, fixturesfunctions/LeftPanel.qml | live | - ; writes Simple Desk overrides at the fixture addresses |
| Live tool: Colour (basic palette, full picker, typed hex, RGB / CMY / WAUV) | ColorTool.qml, ColorToolBasic.qml, ColorToolFull.qml, ColorToolPrimary.qml | live | - |
| Live tool: Colour filters tab (named colour filter lists) | ColorToolFilters.qml | live | - ; sandbox 2026-09-27 (read-only list, no filter-file editing) |
| Live tool: Position (pan / tilt XY pad, spin boxes, centre, snap, rotate preview) | fixturesfunctions/PositionTool.qml | live | - ; snap-to-next / rotate-preview not checked |
| Live tool: Colour wheel / Gobos / Shutter / Beam / Speed / Prism / Effect / Maintenance capability presets | fixturesfunctions/PresetsTool.qml, fixturesfunctions/BeamTool.qml, fixturesfunctions/ShutterAnimator.qml, fixturesfunctions/MaintenanceTool.qml | live | - ; grouped presets from fixtures.defs.getMode; the Beam tool's projected-diameter maths is not ported |
| Live tool: Highlight (locate selected fixtures) | fixturesfunctions/LeftPanel.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Live tool: Pick a 3D point (aim fixtures at a stage position) | fixturesfunctions/Position3DTool.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js (fixtures.monitor.aimAt writes pan / tilt) |
| Release fixtures / reset dump channels | fixturesfunctions/RightPanel.qml ("Reset dump channels"), fixturesfunctions/LeftPanel.qml | live | - |
| Bottom-panel fixture console (plain per-channel faders for the selection) | fixturesfunctions/BottomPanel.qml, FixtureConsole.qml | live | - ; Fixture Tools show a plain slider per channel |
| Bottom-panel extras: copy values to all fixtures of the same type, pan-tilt fader mode, multi-channel selection, fader window shift | fixturesfunctions/BottomPanel.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (copy to same type, multi-channel selection, fader window shift, Pan & Tilt mode) |
| Bottom-panel external controller mapping of the fixture faders | fixturesfunctions/BottomPanel.qml | n/a | - ; the desktop's mapping lives entirely in the qmlui SceneEditor on inputValueChanged with no engine hook; external controllers drive the rig through VC input sources, which the web UI configures |
| Palettes: list and search | fixturesfunctions/PaletteManager.qml | live | - ; search box not checked |
| Palettes: create Dimmer / Colour palette | popup/PopupCreatePalette.qml, PaletteFanningBox.qml | live | - |
| Palettes: create Position / Pan / Tilt / Shutter / Gobo / Position 3D palette | popup/PopupCreatePalette.qml | live | - ; sandbox 2026-09-27 (partials-ff.js, palette-apply.js); units as on the desktop: Pan / Tilt degrees, Position 3D metres, Zoom beam degrees, Dimmer edited in % and stored as DMX |
| Palettes: "Also create a Scene" on create | popup/PopupCreatePalette.qml | live | - ; sandbox 2026-09-27 |
| Palettes: apply to the selected fixtures | fixturesfunctions/PaletteManager.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/palette-apply.js; every type plus fanning through palette.apply (the engine's valuesFromFixtures, the desktop's own maths), DMX read back, Release clears it; Gobo palettes now write the gobo wheel (engine fix, desktop too) |
| Palettes: edit (rename, change value) | fixturesfunctions/PaletteManager.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Palettes: delete | fixturesfunctions/PaletteManager.qml | live | - |
| Palette fanning (type, layout, amount, per-axis ordering) | PaletteFanningBox.qml | live | - ; sandbox 2026-09-27 (colour palette fanned Linear 60%, saved in the .qxw) |

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
| Clone the selected functions | fixturesfunctions/RightPanel.qml | live | - ; sandbox 2026-09-27 (functions.clone) |
| Show function usage (functions and widgets using it) | fixturesfunctions/RightPanel.qml, UsageList.qml | live | - ; sandbox 2026-09-27 (functions.usage lists functions and VC widgets) |
| Set / unset autostart function | fixturesfunctions/RightPanel.qml | live | - ; sandbox 2026-09-27 (core.project.setStartupFunction) |
| Function preview toggle (edited function runs on the output while editing) | fixturesfunctions/RightPanel.qml ("Function Preview") | live | - ; start / stop is the engine's preview (no separate preview mode exists) |
| Select fixtures in function(s) | fixturesfunctions/FunctionManager.qml | live | - ; sandbox 2026-09-27 |
| Timing settings: fade in / fade out / duration, tempo type (time / beats) | fixturesfunctions/RightPanel.qml (Timing Settings), TimeEditTool.qml | live | - ; tempo type (beats) not live-verified |
| Run order and direction (loop / single shot / ping pong / random, forward / backward) | fixturesfunctions/ChaserEditor.qml, fixturesfunctions/EFXEditor.qml, fixturesfunctions/RGBMatrixEditor.qml | live | - ; Chaser, EFX and RGB Matrix share the TimingEditor block; EFX/RGB run order not clicked in the sandbox runs, duration was |
| Scene editor: add fixtures, inline values, console faders, remove channel, fade times | fixturesfunctions/SceneEditor.qml, fixturesfunctions/SceneFixtureConsole.qml | live | - |
| Scene editor: add a fixture group / head as member | fixturesfunctions/SceneEditor.qml ("Add a fixture/group") | live | - ; sandbox 2026-09-27; a single head of a group is not addable on its own |
| Scene editor: add / remove palettes | fixturesfunctions/SceneEditor.qml | live | - |
| Scene editor: speed (fade in / out) | fixturesfunctions/SceneEditor.qml | live | - |
| Chaser editor: add / remove / move / duplicate steps, per-step fade in / hold / fade out / duration / note | fixturesfunctions/ChaserEditor.qml, ChaserStepDelegate.qml | live | - ; per-step times editable only where the speed mode is Per Step, like the desktop; duplicate and note are code present, not in the README list |
| Chaser editor: speed modes Common / Per Step / Default | fixturesfunctions/ChaserEditor.qml | live | - ; sandbox 2026-09-27; Fade Out has no Default option (existing behaviour) |
| Chaser editor: preview next / previous step | fixturesfunctions/ChaserEditor.qml | live | - ; sandbox 2026-09-27 (functions.chaser.setAction + currentStepChanged) |
| Chaser editor: randomize step order, auto-set step durations from total, print steps | fixturesfunctions/ChaserEditor.qml | live | - ; sandbox 2026-09-27 |
| Sequence editor: steps and per-channel step values via the bound Scene | fixturesfunctions/SequenceEditor.qml | live | - ; sandbox 2026-09-27 |
| Sequence editor: add / remove fixtures of the bound Scene | fixturesfunctions/SequenceEditor.qml | live | - ; sandbox 2026-09-27 |
| Sequence editor: capture live output into a step, preview channel changes on the output | fixturesfunctions/SequenceEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (bound-Scene picker rebinds and back) |
| Collection editor: add / remove member functions | fixturesfunctions/CollectionEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/efx-collection.js; add via picker, remove, move up / down; loops and self-membership refused by the server |
| EFX editor: pattern, width / height / rotation / offsets / frequencies / phases, speed, relative movement, preview | fixturesfunctions/EFXEditor.qml, fixturesfunctions/EFXPreview.qml | live | - ; sandbox 2026-09-27; 2D preview only (no fake-3D sphere view); preview data from functions.efx.getPreview so it matches the Qt canvas |
| EFX editor: fixture list (add / remove / reorder heads, per-fixture mode / direction / offset / reverse / dimmer control) and fixture order (parallel / serial / asymmetric) | fixturesfunctions/EFXEditor.qml | live | - ; sandbox 2026-09-27; adding a fixture adds every head (no per-head picker); Asymmetric and raise not clicked in the sandbox run |
| RGB Matrix editor: fixture group, algorithm, colours, blend / control mode, speed, preview | fixturesfunctions/RGBMatrixEditor.qml, fixturesfunctions/RGBMatrixPreview.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/rgbmatrix.js; Beats tempo preview approximated at 500 ms |
| RGB Matrix editor: script algorithm properties, text / image / animation parameters, font | fixturesfunctions/RGBMatrixEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (float / string script properties, text font size / bold / italic / offsets, image path via the file browser) |
| RGB Matrix editor: save this matrix to a Sequence | fixturesfunctions/RGBMatrixEditor.qml | live | - ; sandbox 2026-09-27 (functions.rgbmatrix.saveToSequence) |
| Script editor: edit source, syntax check, insert command at cursor, fixture / function trees | fixturesfunctions/ScriptEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/media.js; the QML side trees are replaced by function / fixture ID pickers |
| Audio editor: file, output device, volume, fade in / out, looped / single shot, mute, file info | fixturesfunctions/AudioEditor.qml | live | - ; sandbox 2026-09-27, mute now settable (functions.audio.setMuted) |
| Audio editor: detect BPM, replace file (fork media store) | fixturesfunctions/AudioEditor.qml | live | - ; sandbox 2026-09-27 (detectBpm + bpmChanged; Replace via setSource) |
| Video editor: file / URL, output screen, windowed / fullscreen, geometry, rotation, layer, looped, mute | fixturesfunctions/VideoEditor.qml, fixturesfunctions/VideoContext.qml | live | - ; sandbox 2026-09-27, volume 0-100 and mute settable |
| Video editor: Spout output mode, sender name / size, replace file (fork) | fixturesfunctions/VideoEditor.qml | live | - ; sandbox 2026-09-27; sender size settable, sender name follows the function name (read-only as on the desktop) |
| Show function: edit on the timeline | fixturesfunctions/FunctionManager.qml | live | - ; sandbox 2026-09-27 on the Show Manager screen |
| Adjust a running function's intensity attribute | FunctionDelegate.qml (via the VC "Adjust" slider mode), functions.adjustAttribute | live | - ; sandbox 2026-09-27 (functions.adjustAttribute intensity slider) |
| Show Wizard (stage wizard: show type, fixture roles, venue, effects, controller, generate) | fixturesfunctions/RightPanel.qml, stagewizard/ShowWizard.qml, stagewizard/WizardStep1ShowType.qml ... WizardStep6Summary.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/wizard-import.js; controller step tested with no controller only; click a box to pick the target instead of drag-and-drop; API generation is not on the desktop undo stack |

## 2D / 3D / DMX / Universe grid views

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| 2D top-down view of the rig with live colour / intensity per fixture | fixturesfunctions/2DView.qml, fixturesfunctions/Fixture2DItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js; head colouring drawn, not asserted |
| 2D / 3D: move and rotate fixtures (drag, position / rotation fields, invert axes, rotation scale) | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (drag, fields, per-axis inverts, rotation scale) |
| 2D / 3D: align left / top, distribute horizontally / vertically | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| 2D / 3D: arrange fixtures (circle / grid / line, detect from placement, face centre, summon) | popup/PopupArrangeFixtures.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (circle / grid / line, detect, face centre, rotate, move to centre) |
| 2D / 3D: environment size, grid units (metres / feet), position range | fixturesfunctions/SettingsView2D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (metres / feet, position range) |
| 2D: point of view (top / front / left / right) and initial POV prompt | fixturesfunctions/SettingsView2D.qml, popup/PopupMonitor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (including the initial prompt on an empty project) |
| 2D: custom background image, reset background | fixturesfunctions/SettingsView2D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js; the image is picked from files on the QLC+ host (no browser upload) |
| 2D: gel colour per fixture | fixturesfunctions/SettingsView2D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| 2D / 3D: show fixture groups overlay | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| 2D / 3D: DMX-driven position / rotation | fixturesfunctions/SettingsView2D.qml, fixturesfunctions/3DView/SettingsView3D.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| 2D: fixed zoom, zoom in / out | fixturesfunctions/SettingsView2D.qml, ZoomItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js; fixed zoom is API-only |
| 3D rendering (stage type, ambient light, smoke, quality, FPS, custom meshes, lock / normalize items) | fixturesfunctions/3DView/3DView.qml, fixturesfunctions/3DView/SettingsView3D.qml | n/a | - ; Qt3D rendering is desktop-only; parity is the 2D view plus the position / rotation editing rows above |
| DMX view (per-fixture channel values, show addresses, relative addresses) | fixturesfunctions/DMXView.qml, fixturesfunctions/FixtureDMXItem.qml, fixturesfunctions/SettingsViewDMX.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| Universe grid view (address map, cut / paste fixtures to the first free address) | fixturesfunctions/UniverseGridView.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
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
| Slider: monitor channel levels display | virtualconsole/VCSliderItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Cue list: play / pause / stop / next / previous / jump to step, current step highlighted | virtualconsole/VCCueListItem.qml | live | - |
| Cue list: side fader (crossfade / steps) | virtualconsole/VCCueListItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-cue.js; both modes driven |
| XY pad: move the position | virtualconsole/VCXYPadItem.qml | live | - |
| XY pad: presets (position / fixture group / Scene / EFX preset buttons) | virtualconsole/VCXYPadItem.qml, virtualconsole/VCXYPadPresets.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-live.js; EFX presets untested (SF3 has no EFX) |
| XY pad: floor control (point fixtures at a stage floor position) | virtualconsole/VCXYPadItem.qml | live | - ; sandbox 2026-09-27 (target drag + height fader) |
| Speed dial: set time, tap | virtualconsole/VCSpeedDialItem.qml | live | - |
| Speed dial: plus / minus buttons, multiplier factors, apply, reset tap, preset buttons | virtualconsole/VCSpeedDialItem.qml, virtualconsole/VCSpeedDialPresets.qml | live | - ; sandbox 2026-09-27; the stray speedDial.setCurrentTime wrapper is deprecated (preset.apply covers it) |
| Frame / Solo frame: multipage next / previous | virtualconsole/VCFrameItem.qml | live | - |
| Frame: enable / disable, expand / collapse | virtualconsole/VCFrameItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Frame: page shortcuts by keyboard | virtualconsole/VCFrameProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-input.js; F flips the frame in Operate mode |
| Label | virtualconsole/VCLabelItem.qml | live | - |
| Clock: clock / stopwatch / countdown display, play / pause, reset, enable schedule | virtualconsole/VCClockItem.qml | live | - ; sandbox 2026-09-27; Stopwatch server-tested only |
| Animation: level fader, preset buttons, colour knobs | virtualconsole/VCAnimationItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Audio triggers: enable / disable capture, live bars | virtualconsole/VCAudioTriggersItem.qml | live | - ; sandbox 2026-09-27 (levelsChanged stream from the host's capture) |
| Grand Master value | virtualconsole/VirtualConsole.qml | live | - |
| Fire widgets by keyboard sequences in Operate mode | virtualconsole/VirtualConsole.qml, KeyboardSequenceDelegate.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Fire widgets from an external controller (input sources) | ExternalControls.qml | n/a | - ; input lines are patched on the host and drive the engine directly; the web UI only needs to *configure* them (editing section) |

## Virtual Console - editing

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Toggle widget edit mode | virtualconsole/VCRightPanel.qml | live | - ; Design mode |
| Page: add (left / right), rename, delete | virtualconsole/VCPageProperties.qml | live | - ; left / right placement not checked |
| Page: width / height | virtualconsole/VCPageProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js (vc.page.setSize) |
| Page: set / change PIN | virtualconsole/VCPageProperties.qml, popup/PopupPINSetup.qml | live | - ; sandbox 2026-09-27 via vc.page.setPin (+ new vc.page.updated event) |
| Add widget from the palette (Frame, Solo Frame, Button, Slider, Knob, Cue List, Speed, XY Pad, Animation, Label, Audio Triggers, Clock) | virtualconsole/WidgetsList.qml, virtualconsole/WidgetDragItem.qml | live | - ; click-to-place instead of drag |
| Add a Button matrix / Slider matrix (rows, columns, frame type) | virtualconsole/WidgetsList.qml, virtualconsole/VirtualConsole.qml ("Widget matrix setup") | live | - ; sandbox 2026-09-27 (3x3 button matrix through the dialog; slider / solo-frame variants server-tested) |
| Create widgets by dropping functions on the page | virtualconsole/VCPageArea.qml, virtualconsole/VCRightPanel.qml (Function Manager panel) | live | - ; sandbox 2026-09-27 as an "Add widgets from functions" dialog over vc.widget.createFromFunctions (3 scenes -> 3 buttons); adjust-slider / cue-list hints server-tested |
| Move / resize widgets, snapping toggle | virtualconsole/VCWidgetItem.qml, virtualconsole/VirtualConsole.qml | live | - |
| Copy / cut / paste / delete widgets | virtualconsole/VCRightPanel.qml | live | - ; cut is copy + delete |
| Widget caption | virtualconsole/VCWidgetProperties.qml | live | - |
| Widget foreground / background colour, font, bold | virtualconsole/VCWidgetProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Widget background image | virtualconsole/VCWidgetProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js; image on the QLC+ host picked with the file browser, read back as a data URL (vc.widget.getBackgroundImage); UNC paths refused |
| Widget z-index | virtualconsole/VCWidgetProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js |
| Align selected widgets left / right / top / bottom | virtualconsole/VCWidgetProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Distribute selected widgets horizontally / vertically | virtualconsole/VCWidgetProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Bulk style on a multi-selection (caption, colours, font) | virtualconsole/VCWidgetProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Widget presets (save / apply / remove a style preset) | virtualconsole/VCWidgetProperties.qml | live | - ; generic vc.widget.preset.* live for Speed, XY Pad and Animation (sandbox 2026-09-27) |
| External controls: add / remove an input source, auto-detect, manual selection | ExternalControls.qml, ExternalControlDelegate.qml, popup/PopupManualInputSource.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| External controls: custom feedback values / colours per input source | popup/PopupCustomFeedback.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| External controls: add / remove a keyboard combination, auto-detect | ExternalControls.qml, KeyboardSequenceDelegate.qml | live | - ; sandbox 2026-09-27 (keys recorded by pressing them in the browser; re-recording leaves exactly one entry) |
| Button properties: attached function, pressure behaviour (Toggle / Flash / Blackout / Stop all) | virtualconsole/VCButtonProperties.qml | live | - |
| Button properties: flash override priority / force LTP, stop-all fade out, adjust function intensity | virtualconsole/VCButtonProperties.qml | live | - ; sandbox 2026-09-27, every VcButtonConfig field exercised |
| Slider properties: display style (DMX / percent, normal / inverted, slider / knob), mode (Level / Adjust / Submaster / Grand Master), value limits | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Slider properties: Level mode channel list (add / remove, all / intensity / RGB / gobo groups) | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27; channel picker over fixtures.list (expand fixture, tick channels), vc.slider.setLevelChannels; group shortcuts (all / intensity / RGB / gobo) not offered |
| Slider properties: monitor channel levels, catch up with external input | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27 (Monitor on/off/on, catch-up) |
| Slider properties: click & go button type | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Slider properties: Function Control (Adjust) mode - attached function and attribute | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27 (function picker + attribute + Show flash button) |
| Slider properties: Grand Master mode (value / channel mode) | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Slider properties: show flash button | virtualconsole/VCSliderProperties.qml | live | - ; sandbox 2026-09-27 |
| Cue list properties: attached chaser, next / previous behaviour, playback layout, side fader mode | virtualconsole/VCCueListProperties.qml | live | - ; sandbox 2026-09-27 |
| Frame properties: header, enable button, pages (count, labels, loop, clone first page), shortcut names, solo options | virtualconsole/VCFrameProperties.qml | live | - ; sandbox 2026-09-27; pages 3, label, circular scrolling, enable button, header, clone first page, solo exclude-monitored + mixing; Collapsed server-tested only |
| XY pad properties: fixtures / heads (add, remove, pan-tilt range, reverse), axis ranges, inverted Y, floor control, display units | virtualconsole/VCXYPadProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| XY pad properties: presets (create from position, drop Scene / EFX / fixture group, rename, reorder, remove) | virtualconsole/VCXYPadPresets.qml | live | - ; sandbox 2026-09-27; preset remove server-tested only |
| Speed dial properties: functions list, multipliers, dial time range, visibility of parts, tap controls BPM, reset on change | virtualconsole/VCSpeedDialProperties.qml | live | - ; sandbox 2026-09-27; tap-controls-BPM checkbox rendered, not clicked in the driver |
| Speed dial properties: presets (add / remove, name, time) | virtualconsole/VCSpeedDialPresets.qml | live | - ; sandbox 2026-09-27 via vc.widget.preset.* + vc.speedDial.preset.update |
| Clock properties: clock type, schedules (add / remove / update, function, start / stop time, weekdays) | virtualconsole/VCClockProperties.qml | live | - ; sandbox 2026-09-27; schedule remove server-tested only |
| Animation properties: attached function, visibility, instant changes, presets list (colour / text / algorithm presets) | virtualconsole/VCAnimationProperties.qml, virtualconsole/VCAnimationPresets.qml, popup/PopupAnimationPreset.qml | live | - ; sandbox 2026-09-27 |
| Audio triggers properties: number of bars, per-bar type (DMX / function / widget), thresholds, targets | virtualconsole/VCAudioTriggersProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
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
| Channel value debug (which functions / sources contribute to a channel) | SimpleDesk.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (io.dmx.channel.inspect); the desktop Simple Desk's own overrides are not visible to the API |
| Fixture list side panel | SimpleDesk.qml | live | - |
| Dump to a new Scene (name, non-zero only) | SimpleDesk.qml, popup/PopupDMXDump.qml | live | - |
| Dump into an existing Scene | popup/PopupDMXDump.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| Dump filters (fixtures, universes, channel types) | popup/PopupDMXDump.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (fixtures and channel types; the desktop's separate RGB/CMY/WAUV box maps onto channel types) |
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
| Multiple output patches per universe | inputoutput/UniverseIOItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Enable / disable feedback | inputoutput/UniverseIOItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Refresh plugin lines / rescan | inputoutput/PluginsList.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O); Rescan and line listing driven; real re-enumeration of a hot-plugged device cannot happen in a sandbox |
| Plugin line parameters for network plugins (ArtNet / E1.31 / OSC IP, port, transmission mode ...) | inputoutput/IOLeftPanel.qml, inputoutput/IORightPanel.qml ("Open the plugin configuration") | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/plugin-params.js: the generic per-line parameter editor driven end to end against the engine I/O stub (add / edit string, number, boolean / remove / reopen / save to <PluginParameters> / reload); the ArtNet, E1.31 and OSC key lists and value formats verified against each plugin's setParameter() source, not driven (network plugins are excluded from sandboxes because the live video chain listens for ArtNet and VRChat for OSC on this machine) - verify once on the real rig |
| Plugin configuration dialog for native-hardware plugins (dmxusb, MIDI device dialogs ...) | inputoutput/IOLeftPanel.qml, inputoutput/IORightPanel.qml | n/a | - ; the plugin's own configuration dialog is a native window that opens on the QLC+ machine by nature; the web UI's Configure button opens it there (io.plugin.configure) |
| Per-output-patch blackout, play / pause an output patch | inputoutput/OutputPatchItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Blackout on all output patches | inputoutput/IORightPanel.qml | live | - ; global blackout |
| Assign an input profile to a universe | inputoutput/ProfilesList.qml, inputoutput/InputPatchItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Input profile: create / edit / save / delete (channels, MIDI channels, colours, behaviour, sensitivity) | inputoutput/InputProfileEditor.qml, inputoutput/ProfilesList.qml, popup/PopupInputChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/io.js; io/InputProfileEditor.jsx, .qxi lands in the host's user profile folder (QLCPLUS_USER_INPUTPROFILE_DIR override in sandboxes / tests) |
| Input profile: channel auto-detection (learn) | inputoutput/ProfilesList.qml, inputoutput/InputProfileEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Input profile: custom feedback and MIDI global settings | inputoutput/InputProfileEditor.qml, popup/PopupCustomFeedback.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Audio input / output device selection, sample rate, channels, buffer size | inputoutput/AudioCardsList.qml, inputoutput/AudioIOItem.qml, popup/PopupAudioConfiguration.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Audio input signal level check | popup/PopupAudioConfiguration.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O); level events stream to the previewing client (io.audio.inputLevel); the sandbox host's default input was silent, so only zero levels were observed |

## Show Manager

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Show Manager context with timeline, header and cursor | showmanager/ShowManager.qml, showmanager/HeaderAndCursor.qml | live | - ; sandbox 2026-09-27 |
| Pick / create the Show function being edited | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Tracks: create, rename, delete, move up / down | showmanager/ShowManager.qml, showmanager/TrackDelegate.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Tracks: mute / solo | showmanager/TrackDelegate.qml | live | - ; sandbox 2026-09-27 |
| Items: drop a function on the timeline, move, resize / stretch, delete | showmanager/ShowItem.qml, showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27; items are added from a function picker at the cursor instead of drag-drop from the tree; overlap rejected with nearest-free-spot placement; stretch mode not offered |
| Items: colour, lock / unlock | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27 |
| Items: copy / paste at cursor | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27 |
| Timing panel: start / duration / end edit, align start / end to cursor | showmanager/TimingUtils.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Ripple: cut time / insert time | showmanager/TimingUtils.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Time division (Time, BPM 2/4, 3/4, 4/4) | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27 |
| Playback: play from cursor, pause, stop / rewind, cursor follows playback | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27; functions.start startTime + the gated functions.show.<id>.playhead stream |
| Preview the Show at the cursor while stopped / paused | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js (functions.show.preview / endPreview) |
| Snap to grid, markers, zoom | showmanager/ShowManager.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Track Spout output size (set, unset, mismatch prompt) | showmanager/TrackDelegate.qml, popup/PopupTrackSpoutSize.qml, popup/PopupSpoutSizeMismatch.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js; document state, header label and mismatch prompt verified; resizing a running Spout sender not observed |
| Legacy Show timing conversion | showmanager/LegacyShowTimingDialog.qml, showmanager/LegacyShowTimingConvertDialog.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (upload path live; detection from a file on disk unit-tested; conversion is not on the undo stack) |

## Fixture Editor

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| Open the Fixture Editor window, back to QLC+ | fixtureeditor/FixtureEditor.qml, WindowLoader.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| New definition | fixtureeditor/FixtureEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Open an existing definition (user or system) | fixtureeditor/FixtureEditor.qml, popup/PopupFolderBrowser.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Save / save as | fixtureeditor/FixtureEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js; save as = Save as user copy, Export download, or a manufacturer / model rename (definitions always land in the host's user fixture folder) |
| General: manufacturer, model, type, author | fixtureeditor/EditorView.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Channels: add / remove / edit (name, group, preset, default value, coarse / fine, colours) | fixtureeditor/EditorView.qml, fixtureeditor/ChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Capabilities: add / remove / edit (range, description, preset, values, colours) | fixtureeditor/ChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Capabilities: gobo picture | fixtureeditor/ChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js; picked from the host's Gobos folder, as the desktop editor does |
| Capabilities: automatic colour assignment | fixtureeditor/ChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Channel / capability wizard | popup/PopupChannelWizard.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Modes: add / remove / rename, channel list, per-mode physical override | fixtureeditor/EditorView.qml, fixtureeditor/ModeEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js; ordering driven by up / down arrows |
| Modes: heads / emitters (create, remove, acts-on channels) | fixtureeditor/ModeEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Physical properties (bulb, dimensions, lens, focus, layout, electrical) | fixtureeditor/PhysicalProperties.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Aliases (replace channel with another while a capability is active, apply to all modes) | fixtureeditor/AliasEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Import a definition file | fixtureeditor/FixtureEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js; browser file upload; Avolites D4 import out of scope |
| Validation errors and warnings | fixtureeditor/EditorView.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |

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
| Animation algorithm preset | popup/PopupAnimationPreset.qml | live | - ; sandbox 2026-09-27 (script-algorithm preset with parameters through the dialog) |
| Arrange fixtures | popup/PopupArrangeFixtures.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js (circle) |
| Audio configuration | popup/PopupAudioConfiguration.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| Channel modifiers editor | popup/PopupChannelModifiers.qml | live | - ; sandbox 2026-09-27 |
| Channel wizard | popup/PopupChannelWizard.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixture-editor.js |
| Create palette | popup/PopupCreatePalette.qml | live | - ; see the palette create row |
| Custom feedback | popup/PopupCustomFeedback.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-vc.js (Loopback plugin for I/O) |
| DMX channel dump | popup/PopupDMXDump.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| Disclaimer | popup/PopupDisclaimer.qml | n/a | - ; desktop first-run notice |
| Folder browser (server-side file picker) | popup/PopupFolderBrowser.qml | live | - ; sandbox 2026-09-27 as window.ServerFileBrowser over core.fs.list, used by the Audio / Video editors; the Open-project dialog still takes a typed path plus recent files |
| Import from project | popup/PopupImportProject.qml | live | - ; sandbox 2026-09-27 (server path, browse or upload; imported scenes reference the imported fixtures) |
| Input channel editor | popup/PopupInputChannelEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js (type-dependent sensitivity ranges, extra press, movement, custom feedback, MIDI mapping) |
| Enter a number (select every Nth) | popup/PopupInputNumber.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Invert selection in group(s) | popup/PopupInvertGroupSelection.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| Manual input source selection | popup/PopupManualInputSource.qml | live | - ; sandbox 2026-09-27 (universe / channel); picking a profile channel needs a patched profile, not driven |
| 2D point of view selection | popup/PopupMonitor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Network client setup | popup/PopupNetworkClient.qml | n/a | - ; the web UI is the remote |
| Client access request | popup/PopupNetworkConnect.qml | n/a | - ; host session |
| Network server setup | popup/PopupNetworkServer.qml | n/a | - ; host process settings |
| Page / frame PIN request | popup/PopupPINRequest.qml | live | - ; sandbox 2026-09-27 |
| PIN setup | popup/PopupPINSetup.qml | live | - ; sandbox 2026-09-27 (frame via dialog, page via vc.page.setPin) |
| Rename items with numbering | popup/PopupRenameItems.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js (functions and fixtures) |
| Spout size mismatch | popup/PopupSpoutSizeMismatch.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js |
| Track Spout output size | popup/PopupTrackSpoutSize.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/vc-show-leftovers.js; document state, header label and mismatch prompt verified; resizing a running Spout sender not observed |
| DMX Address tool | DMXAddressTool.qml, DMXAddressWidget.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| Usage list | UsageList.qml | live | - ; sandbox 2026-09-27 |
| UI Settings editor | UISettingsEditor.qml, UISettings.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js; colours / scale / reset / save / load (desktop qlcplusUiStyle.json shape); engine settings write built but not exercised |
| Shortcuts editor | ShortcutsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (rebind, collision warning, defaults, import / export in the desktop format, hints toggle) |
| External controls panel (input sources + key sequences of a widget) | ExternalControls.qml, ExternalControlDelegate.qml, KeyboardSequenceDelegate.qml | live | - ; sandbox 2026-09-27, webui/vc/vc-external.jsx |
| Beat generators panel | BeatGeneratorsPanel.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| Colour tool (basic / full / filters) | ColorTool.qml, ColorToolBasic.qml, ColorToolFull.qml, ColorToolFilters.qml, ColorToolPrimary.qml, MultiColorBox.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Time edit tool (typed time, tap, infinite) | TimeEditTool.qml | live | - ; inline "500, 1.5s, 2m, inf" fields |
| Day-time tool (clock schedule times) | DayTimeTool.qml | live | - ; sandbox 2026-09-27 |
| Keypad | KeyPad.qml | live | - |
| Fixture console (per-channel faders) | FixtureConsole.qml, ChannelToolLoader.qml | live | - ; see the bottom-panel rows |
| Palette fanning box | PaletteFanningBox.qml | live | - ; sandbox 2026-09-27 (colour palette fanned Linear 60%, saved in the .qxw) |
| Single-axis tool (pan or tilt only fixtures) | SingleAxisTool.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/partials-ff.js |
| Chaser step widget (per-step times popup) | ChaserWidget.qml, ChaserStepDelegate.qml | live | - |
| Zoom item (view zoom controls) | ZoomItem.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/fixtures-views.js |
| Feedback toast | FeedbackToast.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |

## Settings

| Qt UI action | Where in qmlui (file) | Web UI status | Blocker / notes |
| --- | --- | --- | --- |
| UI settings: theme colours, scaling factor, reset to default, save to file | UISettingsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js |
| Shortcuts editor: rebind, load defaults, import / export, show shortcut hints | ShortcutsEditor.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js (rebind, collision warning, defaults, import / export in the desktop format, hints toggle) |
| Beat generator source (disabled / internal / plugin / audio) | BeatGeneratorsPanel.qml | live | - ; sandbox 2026-09-27, driver webui/tools/e2e/tools-misc.js; the source switch is live; plugin / audio beat delivery unverified in the plugin-less sandbox |
| Engine settings: working path, engine log locale | - (no QML UI; core.settings.get / set) | n/a | - ; no Qt UI equivalent, spec-only, listed so nobody re-adds it as a gap |

## Summary

Counts are rows per status across every table above (popup rows duplicate some feature rows, so
the total is larger than the number of distinct actions). Recompute after editing:
`grep -cE "^\|[^|]*\|[^|]*\| (live|partial|missing|n/a) \|" docs/webui-parity.md` per word.

| Status | Rows |
| --- | --- |
| live | 294 |
| partial | 0 |
| missing | 0 |
| n/a | 20 |
| total | 314 |
