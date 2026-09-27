/**
 * Toolbar / Actions-menu tools of MainView.qml and ActionsMenu.qml that are not a screen of their own:
 *
 *  - DMX dump (PopupDMXDump.qml): one dialog for the main-toolbar button, Ctrl+Shift+D and the Simple
 *    Desk button - new or existing Scene, all channels / selected fixtures, channel types, non-zero only
 *    (io.simpleDesk.dump {targetSceneId, name, fixtureIds, channelGroups, nonZeroOnly}). Opened with
 *    window.dispatchEvent(new CustomEvent('qlc-open-dmx-dump', { detail: { fixtureIds } })).
 *  - Channel value debug (SimpleDesk.qml's popup): window.QLCChannelInspect({qlc, universeId, channel})
 *    over io.dmx.channel.inspect.
 *  - DMX Address tool (DMXAddressTool.qml / DMXAddressWidget.qml): DIP-switch calculator.
 *  - UI Settings (UISettingsEditor.qml): window.QLCUISettingsDialog - the design-system colour tokens
 *    (webui/tokens/colors.css) and the scaling factor, persisted in localStorage, saved / loaded as the
 *    desktop's qlcplusUiStyle.json shape ({colors: {bgStronger: ...}, sizes: {scalingFactor}}); plus the
 *    engine settings core.settings.get/set exposes (language, working folder, MasterTimer rate).
 *  - Legacy Show timing (LegacyShowTimingDialog.qml / LegacyShowTimingConvertDialog.qml, ADR 0001):
 *    asked after every project load, over functions.show.legacyTiming.get/convert/dismiss.
 *  - Fixture file import: a .qxf dropped on the window (App.jsx) lands in the user fixture library
 *    (fixturedefs.session.import + fixturedefs.save) and can be opened in the Fixture Editor.
 *
 * Everything mounts through window.QLCAppOverlays (rendered by App at the window root),
 * window.QLCToolbarItems and window.QLCMenuItems.
 */
(function () {
  'use strict';
  const { RobotoText, GenericButton, IconButton, CustomCheckBox, CustomComboBox, CustomSpinBox, CustomSlider, CustomTextInput, CustomPopupDialog, ShortcutHint } = window.PatchDesignSystem_5432c9;
  const icon = (n) => window.QLCData.icon(n);

  /* ---------------------------------------------------------------- helpers */
  function readFileBase64(file) {
    return new Promise((resolve, reject) => {
      const r = new FileReader();
      r.onload = () => { const s = String(r.result || ''); resolve(s.slice(s.indexOf(',') + 1)); };
      r.onerror = () => reject(r.error || new Error('Cannot read ' + file.name));
      r.readAsDataURL(file);
    });
  }
  function download(name, text, type) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: type || 'application/json' }));
    a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }
  function useEvent(name, fn) {
    const ref = React.useRef(fn);
    ref.current = fn;
    React.useEffect(() => {
      const h = (e) => ref.current(e && e.detail);
      window.addEventListener(name, h);
      return () => window.removeEventListener(name, h);
    }, [name]);
  }
  const errText = (e) => (e && (e.message || e.code)) || 'request failed';
  /** A CustomCheckBox + label row (QML's CustomCheckBox + RobotoText pairs; radio groups too). */
  function Check({ checked, onToggled, label, disabled, role, title }) {
    return (
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, opacity: disabled ? .5 : 1 }} title={title || ''}>
        <CustomCheckBox checked={checked} disabled={disabled} size={22} onToggled={onToggled} data-role={role} />
        <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" />
      </span>
    );
  }
  const box = { border: 'var(--border-dark)', background: 'var(--bg-strong)' };
  const sectionHead = (text) => <RobotoText label={text} fontSize="var(--text-size-small)" fontBold height="auto" labelColor="var(--fg-light)" />;

  /* ---------------------------------------------------------------- DMX dump */
  /* PopupDMXDump.qml's channel type boxes, as QLCChannel::Group names. The desktop's separate
     "RGB/CMY/WAUV" box has no counterpart: colour mixing channels are Intensity-group channels. */
  const DUMP_GROUPS = [['Intensity', 'Intensity (incl. RGB/CMY/WAUV)'], ['Colour', 'Color macros / wheels'], ['Gobo', 'Gobo'], ['Pan', 'Pan'], ['Tilt', 'Tilt'],
    ['Speed', 'Speed'], ['Shutter', 'Shutter/Strobe'], ['Prism', 'Prism'], ['Beam', 'Beam'], ['Effect', 'Effect'], ['Maintenance', 'Maintenance']];

  function DmxDumpDialog({ open, qlc, preset, onClose }) {
    const [scenes, setScenes] = React.useState([]);
    const [fixtures, setFixtures] = React.useState([]);
    const [universes, setUniverses] = React.useState(0);
    const [existing, setExisting] = React.useState(false);
    const [sceneId, setSceneId] = React.useState('');
    const [name, setName] = React.useState('');
    const [selectedMode, setSelectedMode] = React.useState(false);
    const [sel, setSel] = React.useState([]);
    const [groups, setGroups] = React.useState(() => DUMP_GROUPS.map(g => g[0]));
    const [nonZero, setNonZero] = React.useState(true);
    const [filter, setFilter] = React.useState('');
    const [note, setNote] = React.useState(null);
    const [busy, setBusy] = React.useState(false);

    React.useEffect(() => {
      if (!open) return;
      setNote(null); setBusy(false); setName(''); setFilter(''); setExisting(false);
      const pre = (preset && preset.fixtureIds || []).map(String);
      setSel(pre); setSelectedMode(pre.length > 0);
      if (!qlc.online) return;
      qlc.call('functions.list').then(r => {
        const list = (r.functions || []).filter(f => f.type === 'Scene').sort((a, b) => String(a.name).localeCompare(String(b.name)));
        setScenes(list);
        setSceneId(cur => list.some(s => String(s.id) === String(cur)) ? cur : (list[0] ? String(list[0].id) : ''));
      }).catch(() => setScenes([]));
      qlc.call('fixtures.list').then(r => setFixtures((r.fixtures || []).slice().sort((a, b) => (a.universe - b.universe) || (a.address - b.address)))).catch(() => setFixtures([]));
      qlc.call('io.universe.list').then(r => setUniverses((r.universes || []).length)).catch(() => {});
    }, [open]);

    const toggleFixture = (id) => setSel(s => s.indexOf(id) === -1 ? s.concat([id]) : s.filter(x => x !== id));
    const shown = filter ? fixtures.filter(f => String(f.name).toLowerCase().indexOf(filter.toLowerCase()) !== -1) : fixtures;
    const canDump = qlc.online && !busy && (!existing || sceneId) && (!selectedMode || (sel.length && groups.length));
    const dump = () => {
      if (!canDump) return;
      setBusy(true); setNote(null);
      const params = { baseRevision: qlc.docRevision(), nonZeroOnly: nonZero };
      if (existing) params.targetSceneId = String(sceneId);
      else if (name.trim()) params.name = name.trim();
      if (selectedMode) {
        params.fixtureIds = sel.map(String);
        params.channelGroups = groups.length === DUMP_GROUPS.length ? [] : groups;
      }
      qlc.call('io.simpleDesk.dump', params).then(r => {
        const target = existing ? scenes.find(s => String(s.id) === String(sceneId)) : null;
        setNote({ text: (existing ? 'Values merged into "' + (target ? target.name : 'Scene ' + r.sceneId) + '"' : 'Scene ' + r.sceneId + ' created') + ' from the DMX dump.', error: false, sceneId: r.sceneId });
        setBusy(false);
      }).catch(e => { setNote({ text: 'Cannot dump: ' + errText(e), error: true }); setBusy(false); });
    };
    const done = note && !note.error;

    return (
      <CustomPopupDialog open={open} title="DMX Channel Dump" width={560} standardButtons={done ? ['Close'] : ['Cancel', 'Dump']} disabledButtons={canDump ? [] : ['Dump']}
        onClose={onClose} onClicked={(b) => { if (b === 'Dump') dump(); else onClose(); }}>
        <div data-role="dmx-dump" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          {sectionHead('Target Scene')}
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <Check checked={!existing} onToggled={() => setExisting(false)} label="Dump to a new Scene" role="dump-new" />
            <span style={{ flex: 1, height: 26, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', opacity: existing ? .5 : 1 }}>
              <CustomTextInput text={name} editing placeholder="New Scene" width="100%" height={22} onTextConfirmed={setName} onInput={(e) => setName(e.target.value)} disabled={existing} data-role="dump-name" />
            </span>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <Check checked={existing} onToggled={() => setExisting(true)} label="Dump to existing Scene" role="dump-existing" disabled={!scenes.length} />
            <span style={{ flex: 1, minWidth: 0 }} data-role="dump-scene">
              <CustomComboBox width="100%" height={26} disabled={!existing} currValue={sceneId} onValueChanged={(v) => setSceneId(String(v))}
                model={scenes.length ? scenes.map(s => ({ mLabel: s.name + ' (' + s.id + ')', mValue: String(s.id) })) : [{ mLabel: '(None available)', mValue: '' }]} />
            </span>
          </div>

          {sectionHead('Channels to dump')}
          <Check checked={!selectedMode} onToggled={() => setSelectedMode(false)} role="dump-all"
            label={'Dump all the available channels (' + universes + ' universes, ' + fixtures.length + ' fixtures)'} />
          <Check checked={selectedMode} onToggled={() => setSelectedMode(true)} role="dump-selected"
            label={'Dump the selected fixture channels (' + sel.length + ' selected)'} />
          {selectedMode ? (
            <div style={{ display: 'flex', gap: 8, minHeight: 0 }}>
              <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', gap: 4 }}>
                <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
                  <input value={filter} onChange={(e) => setFilter(e.target.value)} placeholder="Filter fixtures" data-role="dump-filter"
                    style={{ flex: 1, minWidth: 0, height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' }} />
                  <GenericButton label="All" width={44} height={24} fontSize="var(--text-size-menubar)" onClick={() => setSel(s => Array.from(new Set(s.concat(shown.map(f => String(f.id))))))} />
                  <GenericButton label="None" width={50} height={24} fontSize="var(--text-size-menubar)" onClick={() => setSel([])} />
                </div>
                <div style={Object.assign({ height: 200, overflow: 'auto' }, box)}>
                  {shown.map((f, i) => {
                    const id = String(f.id), on = sel.indexOf(id) !== -1;
                    return (
                      <div key={id} data-dump-fixture={id} onClick={() => toggleFixture(id)}
                        style={{ display: 'flex', alignItems: 'center', gap: 6, height: 24, padding: '0 6px', cursor: 'pointer', background: on ? 'var(--highlight)' : (i % 2 ? 'var(--bg-medium)' : 'transparent') }}>
                        <RobotoText label={f.name} fontSize="var(--text-size-menubar)" height={24} style={{ flex: 1, minWidth: 0 }} />
                        <RobotoText label={'U' + (f.universe + 1) + ' · ' + (f.address + 1)} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height={24} />
                      </div>
                    );
                  })}
                </div>
              </div>
              <div style={{ flex: '0 0 190px', display: 'flex', flexDirection: 'column', gap: 2 }}>
                <RobotoText label="Channel types" fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" />
                {DUMP_GROUPS.map(([g, label]) => (
                  <Check key={g} checked={groups.indexOf(g) !== -1} label={label} role={'dump-group-' + g}
                    onToggled={() => setGroups(s => s.indexOf(g) === -1 ? s.concat([g]) : s.filter(x => x !== g))} />
                ))}
              </div>
            </div>
          ) : null}
          <Check checked={nonZero} onToggled={setNonZero} label="Dump only non-zero values" role="dump-nonzero" />
          {note ? <RobotoText label={note.text} fontSize="var(--text-size-small)" labelColor={note.error ? 'var(--override-red)' : 'var(--check-lime)'} wrapText height="auto" data-role="dump-note" /> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  function DmxDumpHost({ qlc }) {
    const [state, setState] = React.useState(null);
    useEvent('qlc-open-dmx-dump', (detail) => { if (qlc.online) setState({ fixtureIds: (detail && detail.fixtureIds) || (window.QLCSelectedFixtureIds || []) }); });
    return <DmxDumpDialog open={!!state} qlc={qlc} preset={state} onClose={() => setState(null)} />;
  }

  /** Main-toolbar button (MainView.qml's DMX dump button, left of the beat indicator on the desktop). */
  function DmxDumpToolbarButton({ qlc }) {
    const S = window.QLCShortcuts;
    const hint = S ? S.hint('app.dmxDump') : 'Ctrl Shift D';
    return (
      <ShortcutHint keys={hint} placement="corner">
        <IconButton imgSource={icon('dmxdump')} disabled={!qlc.online} data-role="toolbar-dmxdump"
          tooltip={qlc.online ? 'Dump DMX values on a Scene' : 'Dump DMX values — connect first'}
          onClick={() => { if (S) S.clickHint('app.dmxDump'); window.dispatchEvent(new CustomEvent('qlc-open-dmx-dump')); }} />
      </ShortcutHint>
    );
  }

  /* ---------------------------------------------------------------- channel value debug */
  const SOURCE_TEXT = {
    function: 'a Function',
    controlApiSimpleDesk: 'the web / API Simple Desk override',
    desktopTool: 'a desktop tool',
    unidentified: 'an untagged source (desktop Simple Desk, a Virtual Console slider in level mode, a CueStack or a Script)'
  };
  function startedByText(p) {
    if (p.type === 'function') return (p.functionType || 'Function') + ' "' + (p.name || p.id) + '"';
    if (p.type === 'manualVCWidget' || p.type === 'autoVCWidget') return 'Virtual Console widget ' + p.id + (p.type === 'autoVCWidget' ? ' (automatic)' : '');
    return 'engine: ' + (p.name || 'master ' + p.id);
  }
  function ChannelInspect({ qlc, universeId, channel }) {
    const [r, setR] = React.useState(null);
    const [err, setErr] = React.useState('');
    const load = React.useCallback(() => {
      if (!qlc.online) return;
      qlc.call('io.dmx.channel.inspect', { universeId, channel }).then(x => { setR(x); setErr(''); })
        .catch(e => setErr(e && e.code === 'NOT_FOUND' ? 'This server has no channel inspection (io.dmx.channel.inspect).' : errText(e)));
    }, [qlc.online, universeId, channel]);
    React.useEffect(() => { setR(null); load(); }, [load]);
    const line = (t, color) => <RobotoText label={t} fontSize="var(--text-size-small)" height="auto" wrapText labelColor={color} />;
    if (!qlc.online) return line('Connect to see which sources drive this channel.', 'var(--fg-medium)');
    if (err) return line(err, 'var(--override-red)');
    if (!r) return line('Reading the engine…', 'var(--fg-medium)');
    return (
      <div data-role="channel-inspect" style={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          {line('Universe pre-GM ' + (r.preGMValue != null ? r.preGMValue : '–') + ' · output (post-GM) ' + (r.postGMValue != null ? r.postGMValue : '–'))}
          <span style={{ flex: 1 }} />
          <GenericButton label="Refresh" width={70} height={22} fontSize="var(--text-size-menubar)" onClick={load} data-role="inspect-refresh" />
        </div>
        {r.fixture ? line('Fixture: ' + r.fixture.name + ' (ID ' + r.fixture.id + '), channel ' + (r.fixture.channelIndex + 1) + (r.fixture.channelName ? ' "' + r.fixture.channelName + '"' : '') + (r.fixture.group ? ' [' + r.fixture.group + ']' : '')) : line('No fixture at this address')}
        {line('Web Simple Desk override: ' + (r.simpleDeskOverride != null ? 'YES, value ' + r.simpleDeskOverride : 'no'), r.simpleDeskOverride != null ? 'var(--override-red)' : undefined)}
        {sectionHead('Active faders touching this channel')}
        {r.faders.length ? r.faders.map((f, i) => (
          <div key={i} data-inspect-fader={f.source} style={{ padding: '3px 6px', background: 'var(--bg-strong)', borderLeft: '3px solid ' + (f.source === 'function' ? 'var(--highlight)' : f.source === 'controlApiSimpleDesk' ? 'var(--override-red)' : 'var(--bg-light)') }}>
            {line((f.function ? f.function.type + ' "' + f.function.name + '" (ID ' + f.function.id + ')' : SOURCE_TEXT[f.source] || f.source) + (f.feature ? ' — ' + f.feature : '')
              + ' · fader "' + f.name + '", priority ' + f.priority + ', intensity ' + Math.round(f.intensity * 100) + '%' + (f.paused ? ', PAUSED' : '') + (f.fadingOut ? ', FADING OUT' : ''))}
            {line('start ' + f.start + ' → current ' + f.current + ' → target ' + f.target + ' · fade ' + f.fadeTimeMs + ' ms, elapsed ' + f.elapsedMs + ' ms · ' + (f.flags.join(' | ') || 'no flags'), 'var(--fg-light)')}
            {f.startedBy ? (f.startedBy.length ? f.startedBy.map((p, j) => <React.Fragment key={j}>{line('Started by: ' + startedByText(p), 'var(--fg-light)')}</React.Fragment>)
              : line('Started by: nothing recorded (its stop is likely already pending)', 'var(--fg-light)')) : null}
          </div>
        )) : line('(none)', 'var(--fg-medium)')}
        {!r.faders.length && r.lastWrite ? line('Last write: value ' + r.lastWrite.value + ' by fader "' + r.lastWrite.faderName + '", ' + Math.round(r.lastWrite.ageMs / 1000) + ' s ago'
          + (r.lastWrite.function ? ' — ' + r.lastWrite.function.type + ' "' + r.lastWrite.function.name + '", ' + (r.lastWrite.function.running ? 'still running' : 'stopped') : ''), 'var(--fg-light)') : null}
      </div>
    );
  }

  /* ---------------------------------------------------------------- DMX Address tool */
  const DIP_COLORS = { red: '#ff0000', blue: '#0000ff', black: '#000000' };
  function DipSwitches({ value, onChange, flipH, flipV, color }) {
    const bits = Array.from({ length: 10 }, (_, i) => flipH ? 9 - i : i);
    const num = (i) => <span style={{ width: 28, textAlign: 'center', font: '400 var(--text-size-menubar)/1 var(--font-roboto)', color: 'var(--fg-main)' }}>{flipH ? 10 - i : i + 1}</span>;
    const toggle = (bit) => {
      const next = value ^ (1 << bit);
      if (next >= 1 && next <= 512) onChange(next);
    };
    return (
      <div data-role="dip-switches" style={{ display: 'inline-flex', flexDirection: 'column', gap: 4, padding: 10, background: DIP_COLORS[color] || color, borderRadius: 4 }}>
        {flipV ? <div style={{ display: 'flex', gap: 8 }}>{bits.map((b, i) => <React.Fragment key={i}>{num(i)}</React.Fragment>)}</div> : <span style={{ font: '700 var(--text-size-menubar)/1 var(--font-roboto)', color: '#fff' }}>ON</span>}
        <div style={{ display: 'flex', gap: 8 }}>
          {bits.map((bit, i) => {
            const high = !!(value & (1 << bit));
            const up = flipV ? !high : high;
            return (
              <button key={i} type="button" data-dip={bit} data-on={high ? '1' : '0'} onClick={() => toggle(bit)} title={'Switch ' + (bit + 1) + ' = ' + (1 << bit)}
                style={{ width: 28, height: 56, padding: 3, border: 'none', background: 'var(--bg-medium)', cursor: 'pointer', display: 'flex', flexDirection: 'column', justifyContent: up ? 'flex-start' : 'flex-end' }}>
                <span style={{ width: 22, height: 22, background: '#fff' }} />
              </button>
            );
          })}
        </div>
        {flipV ? <span style={{ font: '700 var(--text-size-menubar)/1 var(--font-roboto)', color: '#fff', alignSelf: 'flex-start' }}>ON</span> : <div style={{ display: 'flex', gap: 8 }}>{bits.map((b, i) => <React.Fragment key={i}>{num(i)}</React.Fragment>)}</div>}
      </div>
    );
  }
  function AddressToolDialog({ open, onClose }) {
    const [value, setValue] = React.useState(1);
    const [flipH, setFlipH] = React.useState(false);
    const [flipV, setFlipV] = React.useState(false);
    const [color, setColor] = React.useState('red');
    return (
      <CustomPopupDialog open={open} title="DMX Address tool" width={460} standardButtons={['Close']} onClicked={onClose} onClose={onClose}>
        <div data-role="address-tool" style={{ display: 'flex', flexDirection: 'column', gap: 10, alignItems: 'stretch' }}>
          <div style={{ display: 'flex', justifyContent: 'center' }}>
            <DipSwitches value={value} onChange={setValue} flipH={flipH} flipV={flipV} color={color} />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Address" fontSize="var(--text-size-small)" height="auto" style={{ width: 150, textAlign: 'right' }} />
            <CustomSpinBox value={value} from={1} to={512} width={100} height={28} onValueModified={(v) => setValue(Math.max(1, Math.min(512, Math.round(v))))} data-role="address-value" />
            <RobotoText label={'= ' + Array.from({ length: 10 }, (_, b) => (value >> b) & 1).map((x, b) => x ? (1 << b) : null).filter(Boolean).join(' + ')} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" data-role="address-sum" />
          </div>
          <div style={{ display: 'flex', gap: 16, justifyContent: 'center' }}>
            <Check checked={flipV} onToggled={setFlipV} label="Reverse vertically" role="address-flipv" />
            <Check checked={flipH} onToggled={setFlipH} label="Reverse horizontally" role="address-fliph" />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, justifyContent: 'center' }}>
            <RobotoText label="Color" fontSize="var(--text-size-small)" height="auto" />
            {Object.keys(DIP_COLORS).map(c => (
              <button key={c} type="button" onClick={() => setColor(c)} title={c} data-dip-color={c}
                style={{ width: 30, height: 30, background: DIP_COLORS[c], border: color === c ? '2px solid var(--selection)' : '1px solid var(--icon-border)', cursor: 'pointer' }} />
            ))}
          </div>
        </div>
      </CustomPopupDialog>
    );
  }
  function AddressToolHost() {
    const [open, setOpen] = React.useState(false);
    useEvent('qlc-open-address-tool', () => setOpen(true));
    return <AddressToolDialog open={open} onClose={() => setOpen(false)} />;
  }

  /* ---------------------------------------------------------------- UI settings */
  const UI_KEY = 'qlcplus.webui.uiSettings';
  /* UISettingsEditor.qml's rows: [desktop key, CSS token, label] */
  const UI_COLORS = [
    ['bgStronger', '--bg-stronger', 'Background darker'], ['bgStrong', '--bg-strong', 'Background dark'], ['bgMedium', '--bg-medium', 'Background medium'],
    ['bgLight', '--bg-light', 'Background light'], ['bgLighter', '--bg-lighter', 'Background lighter'], ['bgControl', '--bg-control', 'Controls background'],
    ['fgMain', '--fg-main', 'Foreground main'], ['fgMedium', '--fg-medium', 'Foreground medium'], ['fgLight', '--fg-light', 'Foreground light'],
    ['toolbarStartMain', '--toolbar-start-main', 'Toolbar gradient start'], ['toolbarStartSub', '--toolbar-start-sub', 'Sub-toolbar gradient start'],
    ['toolbarEnd', '--toolbar-end', 'Toolbar gradient end'], ['toolbarHoverStart', '--toolbar-hover-start', 'Toolbar hover gradient start'],
    ['toolbarHoverEnd', '--toolbar-hover-end', 'Toolbar hover gradient end'], ['toolbarSelectionMain', '--toolbar-selection-main', 'Toolbar selection'],
    ['toolbarSelectionSub', '--toolbar-selection-sub', 'Sub-toolbar selection'], ['sectionHeader', '--section-header', 'Section header'],
    ['sectionHeaderDiv', '--section-header-div', 'Section header divider'], ['highlight', '--highlight', 'Item highlight'],
    ['highlightPressed', '--highlight-pressed', 'Item highlight pressed'], ['hover', '--hover', 'Item hover'], ['selection', '--selection', 'Item selection'],
    ['activeDropArea', '--active-drop-area', 'VC Frame drop area'], ['borderColorDark', '--border-color-dark', 'Item dark border']];
  const SCALE_MIN = 0.3, SCALE_MAX = 2;

  /* token defaults as the stylesheets define them, read before any override is applied */
  const tokenDefaults = {};
  function readDefaults() {
    const cs = getComputedStyle(document.documentElement);
    UI_COLORS.forEach(([, token]) => { tokenDefaults[token] = toHex(cs.getPropertyValue(token).trim()); });
  }
  function toHex(c) {
    const m = /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(c);
    if (m) return m[1].length === 3 ? '#' + m[1].split('').map(x => x + x).join('').toLowerCase() : c.toLowerCase();
    const rgb = /^rgba?\((\d+),\s*(\d+),\s*(\d+)/i.exec(c);
    return rgb ? '#' + [1, 2, 3].map(i => Number(rgb[i]).toString(16).padStart(2, '0')).join('') : c;
  }
  function loadUi() { try { const v = JSON.parse(localStorage.getItem(UI_KEY) || 'null'); return v && typeof v === 'object' ? v : {}; } catch (e) { return {}; } }
  function saveUi(v) { try { localStorage.setItem(UI_KEY, JSON.stringify(v)); } catch (e) { /* session only */ } }
  /** {colors: {key: '#rrggbb'}, sizes: {scalingFactor}} -> CSS custom properties + zoom */
  function applyUi(v) {
    const root = document.documentElement;
    const colors = (v && v.colors) || {};
    UI_COLORS.forEach(([key, token]) => {
      if (colors[key] && colors[key] !== tokenDefaults[token]) root.style.setProperty(token, colors[key]);
      else root.style.removeProperty(token);
    });
    const f = Number(v && v.sizes && v.sizes.scalingFactor) || 1;
    const scale = Math.max(SCALE_MIN, Math.min(SCALE_MAX, f));
    /* UISettings.scalingFactor multiplies every size; zoom does the same for the whole page. App's
       root sizes itself to 100vh / --ui-zoom so the zoomed page still fills the window exactly. */
    document.body.style.zoom = scale === 1 ? '' : String(scale);
    root.style.setProperty('--ui-zoom', String(scale));
    root.style.setProperty('--scaling-factor', String(scale));
  }
  readDefaults();
  applyUi(loadUi());

  function UISettingsDialog({ open, onClose, qlc }) {
    const [ui, setUi] = React.useState(loadUi);
    const [note, setNote] = React.useState(null);
    const [server, setServer] = React.useState(null);
    const [serverDraft, setServerDraft] = React.useState(null);
    const fileRef = React.useRef(null);
    React.useEffect(() => {
      if (!open) return;
      setUi(loadUi()); setNote(null); setServer(null); setServerDraft(null);
      if (qlc && qlc.online) qlc.call('core.settings.get', {}).then(r => { setServer(r); setServerDraft(r); }).catch(() => setServer(false));
    }, [open]);
    const update = (next) => { setUi(next); saveUi(next); applyUi(next); };
    const colors = ui.colors || {};
    const scale = Number(ui.sizes && ui.sizes.scalingFactor) || 1;
    const setColor = (key, val) => update(Object.assign({}, ui, { colors: Object.assign({}, colors, { [key]: val }) }));
    const resetColor = (key) => { const c = Object.assign({}, colors); delete c[key]; update(Object.assign({}, ui, { colors: c })); };
    const setScale = (s) => update(Object.assign({}, ui, { sizes: { scalingFactor: Math.round(Math.max(SCALE_MIN, Math.min(SCALE_MAX, s)) * 100) / 100 } }));
    /* the desktop file lists every parameter, customised or not */
    const exportJson = () => {
      const out = { colors: {}, sizes: { scalingFactor: scale } };
      UI_COLORS.forEach(([key, token]) => { out.colors[key] = colors[key] || tokenDefaults[token]; });
      return JSON.stringify(out, null, 4);
    };
    const importFile = (file) => {
      if (!file) return;
      const r = new FileReader();
      r.onload = () => {
        try {
          const obj = JSON.parse(String(r.result));
          const c = {};
          UI_COLORS.forEach(([key, token]) => {
            const v = obj && obj.colors && obj.colors[key];
            if (typeof v === 'string' && /^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(v)) { const hex = v.length === 9 ? '#' + v.slice(3) : v; if (hex.toLowerCase() !== tokenDefaults[token]) c[key] = hex.toLowerCase(); }
          });
          const sf = Number(obj && obj.sizes && obj.sizes.scalingFactor);
          update({ colors: c, sizes: { scalingFactor: sf >= SCALE_MIN && sf <= SCALE_MAX ? sf : 1 } });
          setNote({ text: 'Settings loaded from ' + file.name, error: false });
        } catch (e) { setNote({ text: 'Unable to load ' + file.name + ': ' + e.message, error: true }); }
      };
      r.readAsText(file);
    };
    const applyServer = () => {
      const p = {};
      ['locale', 'defaultWorkingPath', 'masterTimerFrequencyHz'].forEach(k => { if (serverDraft[k] !== server[k]) p[k] = serverDraft[k]; });
      if (!Object.keys(p).length) return;
      qlc.call('core.settings.set', p).then(r => { setServer(r); setServerDraft(r); setNote({ text: 'Engine settings saved', error: false }); })
        .catch(e => setNote({ text: 'Engine settings: ' + errText(e), error: true }));
    };
    const serverDirty = server && serverDraft && ['locale', 'defaultWorkingPath', 'masterTimerFrequencyHz'].some(k => serverDraft[k] !== server[k]);
    const input = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };

    return (
      <CustomPopupDialog open={open} title="UI Settings" width={560} standardButtons={['Close']} onClicked={onClose} onClose={onClose}>
        <div data-role="ui-settings" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Scaling factor" fontSize="var(--text-size-small)" height="auto" style={{ width: 150 }} />
            <CustomSlider value={scale} from={SCALE_MIN} to={SCALE_MAX} length={200} onMoved={setScale} data-role="ui-scale-slider" />
            <input type="number" min={SCALE_MIN} max={SCALE_MAX} step={0.05} value={scale} data-role="ui-scale"
              onChange={(e) => { const v = Number(e.target.value); if (v >= SCALE_MIN && v <= SCALE_MAX) setScale(v); }} style={Object.assign({ width: 70 }, input)} />
            <RobotoText label={scale.toFixed(2) + 'x'} fontSize="var(--text-size-small)" height="auto" />
            <IconButton imgSource={icon('undo')} size={24} disabled={scale === 1} tooltip="Reset to default" onClick={() => setScale(1)} />
          </div>
          <div style={Object.assign({ maxHeight: 300, overflow: 'auto' }, box)}>
            {UI_COLORS.map(([key, token, label], i) => {
              const val = colors[key] || tokenDefaults[token] || '#000000';
              return (
                <div key={key} data-ui-color={key} style={{ display: 'flex', alignItems: 'center', gap: 8, height: 30, padding: '0 8px', background: i % 2 ? 'var(--bg-medium)' : 'transparent' }}>
                  <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" style={{ flex: 1 }} />
                  <RobotoText label={val} fontSize="var(--text-size-menubar)" labelColor={colors[key] ? 'var(--selection)' : 'var(--fg-light)'} height="auto" style={{ width: 70 }} />
                  <input type="color" value={val} onChange={(e) => setColor(key, e.target.value.toLowerCase())} title={token}
                    style={{ width: 44, height: 24, padding: 0, border: '1px solid var(--icon-border)', background: 'none', cursor: 'pointer' }} />
                  <IconButton imgSource={icon('undo')} size={24} disabled={!colors[key]} tooltip="Reset to default" onClick={() => resetColor(key)} />
                </div>
              );
            })}
          </div>
          <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
            <GenericButton label="Reset to defaults" width={140} height={26} fontSize="var(--text-size-menubar)" onClick={() => { update({}); setNote(null); }} data-role="ui-reset" />
            <span style={{ flex: 1 }} />
            <GenericButton label="Save to file" width={110} height={26} fontSize="var(--text-size-menubar)" onClick={() => download('qlcplusUiStyle.json', exportJson())} data-role="ui-save" />
            <GenericButton label="Load from file" width={120} height={26} fontSize="var(--text-size-menubar)" onClick={() => fileRef.current && fileRef.current.click()} data-role="ui-load" />
            <input ref={fileRef} type="file" accept=".json,application/json" style={{ display: 'none' }} data-role="ui-load-file"
              onChange={(e) => { importFile(e.target.files && e.target.files[0]); e.target.value = ''; }} />
          </div>
          {note ? <RobotoText label={note.text} fontSize="var(--text-size-menubar)" labelColor={note.error ? 'var(--override-red)' : 'var(--check-lime)'} wrapText height="auto" data-role="ui-note" /> : null}
          <RobotoText label="Colours and scaling are stored in this browser only (the desktop keeps its own in qlcplusUiStyle.json, which Save / Load read and write)."
            fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" />
          {sectionHead('Engine settings (shared with the desktop app on the QLC+ machine)')}
          {server === false ? <RobotoText label="Not available on this server" fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" height="auto" />
            : !serverDraft ? <RobotoText label={qlc && qlc.online ? 'Reading…' : 'Connect first'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" height="auto" /> : (
              <div data-role="ui-engine" style={{ display: 'grid', gridTemplateColumns: '150px 1fr', gap: 4, alignItems: 'center' }}>
                <RobotoText label="Language (locale)" fontSize="var(--text-size-small)" height="auto" />
                <input value={serverDraft.locale || ''} placeholder="system default" onChange={(e) => setServerDraft(Object.assign({}, serverDraft, { locale: e.target.value }))} style={input} data-role="ui-locale" />
                <RobotoText label="Default working folder" fontSize="var(--text-size-small)" height="auto" />
                <input value={serverDraft.defaultWorkingPath || ''} onChange={(e) => setServerDraft(Object.assign({}, serverDraft, { defaultWorkingPath: e.target.value }))} style={input} data-role="ui-workpath" />
                <RobotoText label="Engine rate (Hz, restart)" fontSize="var(--text-size-small)" height="auto" />
                <span style={{ display: 'flex', gap: 6 }}>
                  <CustomSpinBox value={serverDraft.masterTimerFrequencyHz || 50} from={1} to={1000} width={100} height={24} onValueModified={(v) => setServerDraft(Object.assign({}, serverDraft, { masterTimerFrequencyHz: Math.round(v) }))} />
                  <span style={{ flex: 1 }} />
                  <GenericButton label="Apply" width={70} height={24} fontSize="var(--text-size-menubar)" disabled={!serverDirty} onClick={applyServer} data-role="ui-engine-apply" />
                </span>
              </div>
            )}
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---------------------------------------------------------------- legacy Show timing */
  function LegacyTimingHost({ qlc }) {
    const [info, setInfo] = React.useState(null);      // {creatorVersion, shows}
    const [convert, setConvert] = React.useState(null); // show being converted
    const [bpm, setBpm] = React.useState(120);
    const [preview, setPreview] = React.useState([]);
    const [note, setNote] = React.useState('');
    const unsupported = qlc.isUnsupported('functions.show.legacyTiming.get');
    const check = React.useCallback(() => {
      if (!qlc.online || qlc.isUnsupported('functions.show.legacyTiming.get')) return;
      qlc.call('functions.show.legacyTiming.get', {}).then(r => { setInfo(r && r.shows && r.shows.length ? r : null); }).catch(() => {});
    }, [qlc.online]);
    React.useEffect(() => {
      if (!qlc.online) { setInfo(null); setConvert(null); return undefined; }
      check();
      return qlc.subscribeTo('core.project.loaded', () => { setConvert(null); setTimeout(check, 300); });
    }, [qlc.online]);
    React.useEffect(() => {
      if (!convert) return;
      qlc.call('functions.show.legacyTiming.get', { showId: convert.id, bpm }).then(r => setPreview(r.preview || [])).catch(e => setPreview([]));
    }, [convert && convert.id, bpm]);
    if (unsupported) return null;
    const drop = (id) => setInfo(cur => { if (!cur) return cur; const shows = cur.shows.filter(s => s.id !== id); return shows.length ? Object.assign({}, cur, { shows }) : null; });
    const dismiss = (s) => qlc.call('functions.show.legacyTiming.dismiss', { showId: s.id }).then(() => drop(s.id)).catch(e => setNote(errText(e)));
    const doConvert = () => {
      qlc.call('functions.show.legacyTiming.convert', { showId: convert.id, bpm, baseRevision: qlc.docRevision() })
        .then(r => { drop(convert.id); setConvert(null); setNote('"' + convert.name + '": ' + r.itemsChanged + ' items converted'); })
        .catch(e => setNote('Conversion failed: ' + errText(e)));
    };
    const cell = { padding: '2px 6px', font: '400 var(--text-size-menubar)/1.4 var(--font-roboto)', color: 'var(--fg-main)', textAlign: 'right' };
    return (
      <>
        <CustomPopupDialog open={!!info && !convert} title="Show timing may need conversion" width={560} standardButtons={['Later']} onClicked={() => setInfo(null)} onClose={() => setInfo(null)}>
          {info ? (
            <div data-role="legacy-timing" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              <RobotoText wrapText height="auto" fontSize="var(--text-size-small)"
                label={'This project was saved by QLC+ ' + (info.creatorVersion || '(unknown version)') + '. Before 5.3.1, beat-based Show timelines stored beat counts instead of milliseconds, so the Shows below may play at the wrong positions. Convert a Show if its items look compressed or stretched, or mark it as already correct.'} />
              <div style={box}>
                {info.shows.map((s, i) => (
                  <div key={s.id} data-legacy-show={s.id} style={{ display: 'flex', alignItems: 'center', gap: 6, minHeight: 32, padding: '0 8px', background: i % 2 ? 'var(--bg-medium)' : 'transparent' }}>
                    <RobotoText label={s.name + ' — ' + s.itemCount + (s.itemCount === 1 ? ' item, ' : ' items, ') + s.bpm + ' BPM'} fontSize="var(--text-size-small)" height="auto" style={{ flex: 1, minWidth: 0 }} />
                    <GenericButton label="Convert…" width={90} height={26} fontSize="var(--text-size-menubar)" onClick={() => { setBpm(s.bpm || 120); setNote(''); setConvert(s); }} data-role="legacy-convert" />
                    <GenericButton label="Already correct" width={120} height={26} fontSize="var(--text-size-menubar)" onClick={() => dismiss(s)} data-role="legacy-dismiss" />
                  </div>
                ))}
              </div>
              {note ? <RobotoText label={note} fontSize="var(--text-size-menubar)" labelColor="var(--check-lime)" height="auto" wrapText /> : null}
            </div>
          ) : null}
        </CustomPopupDialog>
        <CustomPopupDialog open={!!convert} title={convert ? 'Convert "' + convert.name + '"' : ''} width={560} standardButtons={['Cancel', 'Convert']}
          onClose={() => setConvert(null)} onClicked={(b) => { if (b === 'Convert') doConvert(); else setConvert(null); }}>
          {convert ? (
            <div data-role="legacy-convert-dialog" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <RobotoText label="Tempo the Show was edited at (BPM)" fontSize="var(--text-size-small)" height="auto" />
                <CustomSpinBox value={bpm} from={1} to={1000} width={90} height={26} onValueModified={(v) => setBpm(Math.max(1, Math.min(1000, Math.round(v))))} data-role="legacy-bpm" />
              </div>
              <table style={{ borderCollapse: 'collapse', width: '100%' }}>
                <thead><tr>{['Item', 'Start (old → new ms)', 'Duration (old → new ms)'].map(h => <th key={h} style={Object.assign({}, cell, { textAlign: h === 'Item' ? 'left' : 'right', color: 'var(--fg-light)' })}>{h}</th>)}</tr></thead>
                <tbody data-role="legacy-preview">
                  {preview.map((p, i) => (
                    <tr key={i} style={{ background: i % 2 ? 'var(--bg-medium)' : 'var(--bg-strong)' }}>
                      <td style={Object.assign({}, cell, { textAlign: 'left' })}>{p.name}</td>
                      <td style={cell}>{p.oldStart + ' → ' + p.newStart}</td>
                      <td style={cell}>{p.oldDuration + ' → ' + p.newDuration}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
              <RobotoText label="Every item of the Show is rescaled (first and last three shown). The conversion is not on the undo list." fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" />
              {note ? <RobotoText label={note} fontSize="var(--text-size-menubar)" labelColor="var(--override-red)" height="auto" wrapText /> : null}
            </div>
          ) : null}
        </CustomPopupDialog>
      </>
    );
  }

  /* ---------------------------------------------------------------- fixture file import */
  /** .qxf -> user fixture library. Resolves {manufacturer, model, warnings}. */
  function importFixtureFile(qlc, file) {
    return readFileBase64(file).then(b64 => qlc.call('fixturedefs.session.import', { fileName: file.name, qxfBase64: b64 })).then(s => {
      const close = () => qlc.call('fixturedefs.session.close', { sessionId: s.sessionId }).catch(() => {});
      return qlc.call('fixturedefs.save', { sessionId: s.sessionId, baseRevision: s.baseRevision == null ? null : s.baseRevision })
        .then(r => { close(); return r; }, e => { close(); throw e; });
    });
  }
  function FixtureImportHost({ qlc }) {
    const [state, setState] = React.useState(null); // {busy|done|error, file, result}
    useEvent('qlc-import-fixture-file', (detail) => {
      const file = detail && detail.file;
      if (!file || !qlc.online) return;
      setState({ busy: true, file });
      importFixtureFile(qlc, file).then(r => setState({ done: true, file, result: r })).catch(e => setState({ error: errText(e), file }));
    });
    const open = !!state;
    const canEdit = state && state.done && window.QLCOpenFixtureEditor;
    return (
      <CustomPopupDialog open={open} title="Import fixture definition" width={440} standardButtons={canEdit ? ['Close', 'Open in Fixture Editor'] : ['Close']}
        onClose={() => setState(null)}
        onClicked={(b) => { const r = state && state.result; setState(null); if (b === 'Open in Fixture Editor' && r) window.QLCOpenFixtureEditor(r.manufacturer, r.model); }}>
        {state ? (
          <div data-role="fixture-import" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <RobotoText label={state.busy ? 'Importing ' + state.file.name + '…' : state.error ? 'Cannot import ' + state.file.name + ': ' + state.error
              : '"' + state.result.manufacturer + ' ' + state.result.model + '" was added to the user fixture library of the QLC+ machine.'}
              fontSize="var(--text-size-small)" wrapText height="auto" labelColor={state.error ? 'var(--override-red)' : undefined} />
            {state.done && state.result.warnings && state.result.warnings.length ? <RobotoText label={'Warnings: ' + state.result.warnings.join('; ')} fontSize="var(--text-size-menubar)" labelColor="var(--selection)" wrapText height="auto" /> : null}
          </div>
        ) : null}
      </CustomPopupDialog>
    );
  }

  window.QLCUISettingsDialog = UISettingsDialog;
  window.QLCChannelInspect = ChannelInspect;
  window.QLCReadFileBase64 = readFileBase64;
  window.QLCImportFixtureFile = importFixtureFile;
  window.QLCAppOverlays = (window.QLCAppOverlays || []).concat([DmxDumpHost, AddressToolHost, LegacyTimingHost, FixtureImportHost]);
  window.QLCToolbarItems = (window.QLCToolbarItems || []).concat([DmxDumpToolbarButton]);
  window.QLCMenuItems = (window.QLCMenuItems || []).concat([({ online }) => [
    { label: 'Dump DMX values on a Scene', icon: 'dmxdump', disabled: !online, onClick: () => window.dispatchEvent(new CustomEvent('qlc-open-dmx-dump')) },
    { label: 'DMX Address tool', icon: 'diptool', onClick: () => window.dispatchEvent(new CustomEvent('qlc-open-address-tool')) },
    { label: 'UI Settings', icon: 'configure', onClick: () => window.dispatchEvent(new CustomEvent('qlc-open-ui-settings')) }
  ]]);
  Object.assign(window, { DmxDumpDialog, AddressToolDialog, UISettingsDialog, ChannelInspect, LegacyTimingHost });
})();
