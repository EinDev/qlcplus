const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, CustomComboBox, CustomCheckBox, GenericButton, IconTextEntry, SectionBox, CustomPopupDialog, CustomTextInput } = window.PatchDesignSystem_5432c9;

/* qmlui/js/GenericHelpers.js::pluginIconFromName */
const PLUGIN_ICONS = { ArtNet: 'artnetplugin', 'E1.31': 'e131plugin', 'DMX USB': 'dmxusbplugin', MIDI: 'midiplugin', OSC: 'oscplugin', HID: 'hidplugin', OLA: 'olaplugin', Loopback: 'loop' };
const pluginIcon = (name) => window.QLCData.icon(PLUGIN_ICONS[name] || 'inputoutput');
const NONE = 'none';
const lineKey = (plugin, line) => plugin + '|' + line;

/* Offline preview data (the mock workspace in data.js has no plugin lines). */
const MOCK_PLUGINS = [
  { name: 'ArtNet', inputLines: [{ index: 0, name: '127.0.0.1' }], outputLines: [{ index: 0, name: '2.0.0.1' }, { index: 1, name: '10.0.0.255' }], canConfigure: true },
  { name: 'E1.31', inputLines: [], outputLines: [{ index: 0, name: '239.255.0.2' }], canConfigure: true },
  { name: 'DMX USB', inputLines: [], outputLines: [{ index: 0, name: 'DMX USB Pro' }], canConfigure: false },
  { name: 'MIDI', inputLines: [{ index: 0, name: 'MIDI Controller' }], outputLines: [{ index: 0, name: 'MIDI Controller' }], canConfigure: false },
  { name: 'OSC', inputLines: [{ index: 0, name: 'OSC 9000' }], outputLines: [{ index: 0, name: 'OSC 9000' }], canConfigure: true }
];
const MOCK_PROFILES = [{ name: 'Generic MIDI', manufacturer: 'Generic', model: 'MIDI' }, { name: 'Novation Launchpad', manufacturer: 'Novation', model: 'Launchpad' }];
const mockUniverses = () => window.QLCData.universes.map(u => {
  /* data.js labels are "<plugin> <line>" ("ArtNet 2.0.0.1") or just the line ("MIDI Controller"). */
  const find = (dir, label) => { for (const p of MOCK_PLUGINS) for (const l of (dir === 'input' ? p.inputLines : p.outputLines)) if (l.name === label || p.name + ' ' + l.name === label) return { plugin: p.name, line: l }; return null; };
  const i = find('input', u.input), o = find('output', u.output), f = find('output', u.feedback);
  return { id: u.id - 1, name: u.name, passthrough: u.passthrough, usedChannels: 0, totalChannels: 512,
    inputPatch: i ? { pluginName: i.plugin, input: i.line.index, inputName: i.line.name, profileName: u.id === 3 ? 'Generic MIDI' : null } : null,
    outputPatches: o ? [{ index: 0, pluginName: o.plugin, output: o.line.index, outputName: o.line.name, paused: false, blackout: false }] : [],
    feedbackPatch: f ? { index: 0, pluginName: f.plugin, output: f.line.index, outputName: f.line.name } : null };
});

/** io.universe.list + one io.universe.get per universe (the list has no patch details). */
function useUniverses(qlc) {
  const [rows, setRows] = React.useState(null);
  const reloadRef = React.useRef(() => {});
  React.useEffect(() => {
    if (!qlc.online) { setRows(null); reloadRef.current = () => {}; return; }
    let alive = true, timer = null;
    const load = () => qlc.call('io.universe.list').then(r => Promise.all((r.universes || []).map(u => qlc.call('io.universe.get', { universeId: u.id }).catch(() => u))))
      .then(list => { if (alive) setRows(list.slice().sort((a, b) => a.id - b.id)); }).catch(() => {});
    const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
    reloadRef.current = debounced;
    load();
    const offs = ['io.universe.created', 'io.universe.updated', 'io.universe.deleted', 'io.patch.output.stateChanged', 'core.project.loaded'].map(t => qlc.subscribeTo(t, debounced));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [qlc.online]);
  return [rows, () => reloadRef.current()];
}

/** One list call whose absence (NOT_FOUND "Unknown method") is a first-class state, not an error. */
function useOptionalList(qlc, method, key, topics) {
  const [state, setState] = React.useState({ items: null, unsupported: false, error: '' });
  React.useEffect(() => {
    if (!qlc.online) { setState({ items: null, unsupported: false, error: '' }); return; }
    let alive = true;
    const load = () => qlc.call(method, {}).then(r => { if (alive) setState({ items: r[key] || [], unsupported: false, error: '' }); })
      .catch(e => { if (alive) setState({ items: null, unsupported: e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || ''), error: e.message || String(e) }); });
    load();
    const offs = (topics || []).map(t => qlc.subscribeTo(t, load));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [qlc.online, method]);
  return state;
}

/** Combo model of every plugin line for one direction, with "in use" annotations. */
function lineModel(plugins, dir, inUse, universeId, onlyPlugin) {
  const model = [{ mLabel: 'None', mValue: NONE }];
  (plugins || []).forEach(p => {
    if (onlyPlugin && p.name !== onlyPlugin) return;
    (dir === 'input' ? p.inputLines : p.outputLines).forEach(l => {
      const users = (inUse[dir === 'input' ? 'input' : 'output'][lineKey(p.name, l.index)] || []).filter(u => u.id !== universeId);
      model.push({ mLabel: p.name + ' — ' + l.name + (users.length ? ' · in use: ' + users.map(u => u.name).join(', ') : ''), mValue: lineKey(p.name, l.index), mIcon: pluginIcon(p.name) });
    });
  });
  return model;
}

function InputOutput() {
  const D = window.QLCData;
  const qlc = useQLC();
  const live = qlc.online;
  const [liveRows, reload] = useUniverses(qlc);
  const pluginState = useOptionalList(qlc, 'io.plugin.list', 'plugins', ['io.plugin.linesChanged']);
  const profileState = useOptionalList(qlc, 'io.inputProfile.list', 'profiles', ['io.inputProfile.changed', 'io.inputProfile.deleted']);
  const [mockRows, setMockRows] = React.useState(mockUniverses);
  const [sel, setSel] = React.useState(0);
  const [addOpen, setAddOpen] = React.useState(false);
  const [addName, setAddName] = React.useState('');
  const [delOpen, setDelOpen] = React.useState(null);
  const [status, setStatus] = React.useState({ text: '', error: false });
  const [blackout, setBlackout] = React.useState(false);

  const universes = live ? (liveRows || []) : mockRows;
  const plugins = live ? pluginState.items : MOCK_PLUGINS;
  const profiles = live ? profileState.items : MOCK_PROFILES;
  const pickersOff = live && !plugins;      // io.plugin.list missing or not loaded yet
  const unsupported = (m) => live && qlc.isUnsupported(m);

  React.useEffect(() => {
    if (!live) return;
    qlc.call('io.blackout.get').then(r => setBlackout(!!r.blackout)).catch(() => {});
    return qlc.subscribeTo('blackout', (v) => setBlackout(!!v));
  }, [live]);

  /* Every §4a mutation: task contract params + baseRevision, one retry after a CONFLICT (the client
     learns the fresh revision from error.details), then a human-readable status line. */
  const mutate = (method, params, label) => {
    const send = () => qlc.call(method, Object.assign({ baseRevision: qlc.docRevision() }, params));
    return send().catch(e => { if (e.code === 'CONFLICT') return send(); throw e; })
      .then(() => { setStatus({ text: label, error: false }); reload(); return true; },
        e => {
          const missing = e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || '');
          setStatus({ text: missing ? label + ' — not available: this server has no ' + method + ' yet' : label + ' failed: ' + (e.message || e.code || 'request failed'), error: true });
          return false;
        });
  };
  const setMock = (id, patch) => setMockRows(p => p.map(u => u.id === id ? Object.assign({}, u, typeof patch === 'function' ? patch(u) : patch) : u));

  /* Which plugin lines are taken, and by which universes. Feedback rides on output lines. */
  const inUse = React.useMemo(() => {
    const map = { input: {}, output: {} };
    const add = (dir, plugin, line, u) => { const k = lineKey(plugin, line); (map[dir][k] = map[dir][k] || []).push(u); };
    universes.forEach(u => {
      if (u.inputPatch) add('input', u.inputPatch.pluginName, u.inputPatch.input, u);
      (u.outputPatches || []).forEach(p => add('output', p.pluginName, p.output, u));
      if (u.feedbackPatch) add('output', u.feedbackPatch.pluginName, u.feedbackPatch.output, u);
    });
    return map;
  }, [universes]);

  /* --- actions ------------------------------------------------------------------------------- */
  const patchLine = (u, direction, value) => {
    const label = direction.charAt(0).toUpperCase() + direction.slice(1) + ' patch of ' + u.name;
    if (value === NONE) {
      if (!live) { setMock(u.id, direction === 'input' ? { inputPatch: null, feedbackPatch: null } : direction === 'output' ? { outputPatches: [] } : { feedbackPatch: null }); return; }
      mutate('io.patch.remove', { universeId: u.id, direction }, label + ' removed');
      return;
    }
    const [plugin, lineStr] = value.split('|'); const line = Number(lineStr);
    if (!live) {
      const p = plugins.find(p => p.name === plugin); const l = (direction === 'input' ? p.inputLines : p.outputLines).find(l => l.index === line);
      setMock(u.id, direction === 'input' ? { inputPatch: { pluginName: plugin, input: line, inputName: l.name, profileName: u.inputPatch ? u.inputPatch.profileName : null } }
        : direction === 'output' ? { outputPatches: [{ index: 0, pluginName: plugin, output: line, outputName: l.name, paused: false, blackout: false }] }
        : { feedbackPatch: { index: 0, pluginName: plugin, output: line, outputName: l.name } });
      return;
    }
    const params = { universeId: u.id, direction, plugin, line };
    if (direction === 'input' && u.inputPatch && u.inputPatch.profileName) params.profile = u.inputPatch.profileName;  // keep the profile across a line change
    mutate('io.patch.set', params, label + ' set to ' + plugin + ' line ' + line);
  };
  const setProfile = (u, name) => {
    if (!u.inputPatch) return;
    if (!live) { setMock(u.id, r => ({ inputPatch: Object.assign({}, r.inputPatch, { profileName: name === NONE ? null : name }) })); return; }
    mutate('io.patch.set', { universeId: u.id, direction: 'input', plugin: u.inputPatch.pluginName, line: u.inputPatch.input, profile: name === NONE ? '' : name },
      name === NONE ? 'Input profile removed from ' + u.name : 'Input profile of ' + u.name + ' set to ' + name);
  };
  const rename = (u, name) => {
    const text = (name || '').trim();
    if (!text || text === u.name) return;
    if (!live) { setMock(u.id, { name: text }); return; }
    mutate('io.universe.update', { universeId: u.id, name: text }, 'Renamed to ' + text);
  };
  const setPassthrough = (u, on) => {
    if (!live) { setMock(u.id, { passthrough: on }); return; }
    mutate('io.universe.update', { universeId: u.id, passthrough: on }, 'Passthrough ' + (on ? 'enabled' : 'disabled') + ' on ' + u.name);
  };
  const addUniverse = () => {
    setAddOpen(false);
    const name = addName.trim();
    setAddName('');
    if (!live) { setMockRows(p => p.concat([{ id: p.length, name: name || 'Universe ' + (p.length + 1), passthrough: false, usedChannels: 0, totalChannels: 512, inputPatch: null, outputPatches: [], feedbackPatch: null }])); return; }
    mutate('io.universe.create', name ? { name } : {}, 'Universe added');
  };
  const deleteUniverse = (u) => {
    setDelOpen(null);
    if (!live) { setMockRows(p => p.filter(x => x.id !== u.id)); if (sel === u.id) setSel(0); return; }
    mutate('io.universe.delete', { universeId: u.id }, u.name + ' removed').then(ok => { if (ok && sel === u.id) setSel(0); });
  };
  const toggleBlackout = () => {
    if (!live) { setBlackout(!blackout); return; }
    qlc.call('io.blackout.set', { blackout: !blackout }).then(() => setBlackout(!blackout)).catch(e => setStatus({ text: 'Blackout failed: ' + e.message, error: true }));
  };

  /* --- render ---------------------------------------------------------------------------------- */
  const lastId = universes.length ? Math.max.apply(null, universes.map(u => u.id)) : -1;
  const canDelete = (u) => universes.length > 1 && u.id === lastId && !unsupported('io.universe.delete');
  const deleteTooltip = (u) => universes.length <= 1 ? 'Remove universe — the last universe cannot be removed'
    : u.id !== lastId ? 'Remove universe — only the last universe can be removed (ids must stay contiguous)'
    : unsupported('io.universe.delete') ? 'Remove universe — not available: this server has no io.universe.delete yet' : 'Remove this universe';
  const pluginsInUse = Object.keys(inUse.input).concat(Object.keys(inUse.output)).map(k => k.split('|')[0]).filter((v, i, a) => a.indexOf(v) === i);
  const summary = live
    ? (liveRows ? universes.length + ' universes · ' + universes.filter(u => u.inputPatch || (u.outputPatches || []).length).length + ' patched · ' + pluginsInUse.length + ' plugins in use' : 'Loading…')
    : universes.length + ' universes · offline preview (mock)';
  const col = (w) => ({ width: w, minWidth: w, padding: '4px 6px', boxSizing: 'border-box' });
  const head = { display: 'flex', alignItems: 'center', height: 30, background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)', padding: '0 6px', flex: 'none' };
  const noteText = 'var(--fg-medium)';
  const selected = universes.find(u => u.id === sel) || universes[0] || null;

  const renderUniverse = (u, i) => {
    const isSel = selected && selected.id === u.id;
    const inputVal = u.inputPatch ? lineKey(u.inputPatch.pluginName, u.inputPatch.input) : NONE;
    const out0 = (u.outputPatches || [])[0] || null;
    const outputVal = out0 ? lineKey(out0.pluginName, out0.output) : NONE;
    const fbVal = u.feedbackPatch ? lineKey(u.feedbackPatch.pluginName, u.feedbackPatch.output) : NONE;
    const inputPlugin = u.inputPatch ? (plugins || []).find(p => p.name === u.inputPatch.pluginName) : null;
    const outPlugin = out0 ? (plugins || []).find(p => p.name === out0.pluginName) : null;
    const updateOff = unsupported('io.universe.update');
    return (
      <div key={u.id} data-universe={u.id} onClick={() => setSel(u.id)}
        style={{ display: 'flex', alignItems: 'stretch', cursor: 'pointer', minHeight: 94,
          background: isSel ? 'var(--highlight-pressed)' : (i % 2 ? 'var(--bg-strong)' : 'var(--bg-stronger)'),
          border: '2px solid ' + (isSel ? 'var(--selection)' : 'transparent'), borderBottom: '2px solid ' + (isSel ? 'var(--selection)' : 'var(--bg-light)') }}>
        {/* Input side */}
        <div style={Object.assign({}, col(340), { display: 'flex', flexDirection: 'column', gap: 4, justifyContent: 'center' })}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <img src={u.inputPatch ? pluginIcon(u.inputPatch.pluginName) : D.icon('inputoutput')} alt="" style={{ width: 26, height: 26, flex: 'none', opacity: u.inputPatch ? 1 : .4 }} />
            {pickersOff
              ? <RobotoText label={u.inputPatch ? u.inputPatch.pluginName + ' — ' + u.inputPatch.inputName : 'None'} fontSize={14} height={26} labelColor={u.inputPatch ? 'var(--fg-main)' : noteText} />
              : <CustomComboBox width={290} height={26} currValue={inputVal} model={lineModel(plugins, 'input', inUse, u.id)} data-role="input-picker"
                disabled={unsupported('io.patch.set')} onValueChanged={v => patchLine(u, 'input', v)} />}
          </div>
          {u.inputPatch ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 6, paddingLeft: 32 }}>
              <RobotoText label="Profile" fontSize={12} labelColor="var(--fg-light)" height={26} style={{ width: 56 }} />
              {live && !profiles
                ? <RobotoText label={(u.inputPatch.profileName || 'None') + (profileState.unsupported ? ' · list not available' : '')} fontSize={12} height={26} labelColor={noteText} title="io.inputProfile.list is not available on this server: the profile cannot be changed here" />
                : <CustomComboBox width={228} height={26} currValue={u.inputPatch.profileName || NONE} data-role="profile-picker" disabled={unsupported('io.patch.set')}
                  model={[{ mLabel: 'None', mValue: NONE }].concat((profiles || []).map(p => ({ mLabel: p.name, mValue: p.name })))} onValueChanged={v => setProfile(u, v)} />}
            </div>
          ) : null}
          {u.inputPatch ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 6, paddingLeft: 32 }}>
              <RobotoText label="Feedback" fontSize={12} labelColor="var(--fg-light)" height={26} style={{ width: 56 }} />
              {pickersOff
                ? <RobotoText label={u.feedbackPatch ? u.feedbackPatch.outputName : 'None'} fontSize={12} height={26} labelColor={noteText} />
                : <CustomComboBox width={228} height={26} currValue={fbVal} data-role="feedback-picker" disabled={unsupported('io.patch.set') || !inputPlugin || !inputPlugin.outputLines.length}
                  model={lineModel(plugins, 'output', inUse, u.id, u.inputPatch.pluginName)} onValueChanged={v => patchLine(u, 'feedback', v)} />}
            </div>
          ) : null}
        </div>

        {/* Universe block (UniverseIOItem.qml's uniBox) */}
        <div style={Object.assign({}, col(230), { display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', gap: 4 })}>
          <div style={{ width: 200, background: 'linear-gradient(to bottom, var(--highlight-pressed), var(--highlight))', border: '2px solid var(--border-color-dark)', borderRadius: 5, padding: '4px 6px', display: 'flex', flexDirection: 'column', gap: 2 }}
            title={'Universe id ' + u.id + (u.totalChannels ? ' · ' + u.usedChannels + '/' + u.totalChannels + ' channels used' : '')}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
              <img src={D.icon('uniview')} alt="" style={{ width: 22, height: 22, flex: 'none' }} />
              <CustomTextInput text={u.name} width="100%" height={24} allowDoubleClick align="center" data-role="universe-name"
                title={updateOff ? 'Rename — not available: this server has no io.universe.update yet' : 'Double-click or F2 to rename'}
                onTextConfirmed={t => { if (!updateOff) rename(u, t); }} onClick={() => setSel(u.id)} />
              <IconButton faSource="fa_trash_can" faColor="var(--bg-strong)" size={22} disabled={!canDelete(u)} tooltip={deleteTooltip(u)} data-role="delete-universe"
                onClick={(e) => { e.stopPropagation(); setDelOpen(u); }} />
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }} title={updateOff ? 'Passthrough — not available: this server has no io.universe.update yet' : 'Merge the input patch live values onto this universe output'}>
              <CustomCheckBox checked={!!u.passthrough} size={18} disabled={updateOff} onToggled={v => setPassthrough(u, v)} data-role="passthrough" />
              <RobotoText label="Passthrough" fontSize={12} height={20} />
              <ToolbarSpacer />
              <RobotoText label={u.totalChannels ? u.usedChannels + '/' + u.totalChannels : ''} fontSize={12} labelColor="var(--fg-light)" height={20} />
            </div>
          </div>
        </div>

        {/* Output side */}
        <div style={Object.assign({}, col(360), { display: 'flex', flexDirection: 'column', gap: 4, justifyContent: 'center', flex: 1 })}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <img src={out0 ? pluginIcon(out0.pluginName) : D.icon('inputoutput')} alt="" style={{ width: 26, height: 26, flex: 'none', opacity: out0 ? 1 : .4 }} />
            {pickersOff
              ? <RobotoText label={out0 ? out0.pluginName + ' — ' + out0.outputName : 'None'} fontSize={14} height={26} labelColor={out0 ? 'var(--fg-main)' : noteText} />
              : <CustomComboBox width={290} height={26} currValue={outputVal} model={lineModel(plugins, 'output', inUse, u.id)} data-role="output-picker"
                disabled={unsupported('io.patch.set')} onValueChanged={v => patchLine(u, 'output', v)} />}
            {out0 && out0.paused ? <RobotoText label="paused" fontSize={12} labelColor="var(--selection)" height={26} title="Read-only: io.patch.output.setState is not available on this server" /> : null}
            {out0 && out0.blackout ? <RobotoText label="blackout" fontSize={12} labelColor="var(--override-red)" height={26} title="Read-only: io.patch.output.setState is not available on this server" /> : null}
            {outPlugin && outPlugin.canConfigure ? <IconButton imgSource={D.icon('configure')} size={26} disabled tooltip={outPlugin.name + ' configuration opens a native dialog in the desktop app — not available in the browser'} /> : null}
          </div>
          {(u.outputPatches || []).slice(1).map(p => (
            <div key={p.index} style={{ display: 'flex', alignItems: 'center', gap: 6, paddingLeft: 32 }} title="Additional output patches are shown read-only: the API patches one output line per universe from here">
              <IconTextEntry iSrc={pluginIcon(p.pluginName)} tLabel={p.pluginName + ' — ' + p.outputName + ' · read-only'} tFontSize={12} height={22} tLabelColor="var(--fg-light)" />
              {p.paused ? <RobotoText label="paused" fontSize={12} labelColor="var(--selection)" height={22} /> : null}
              {p.blackout ? <RobotoText label="blackout" fontSize={12} labelColor="var(--override-red)" height={22} /> : null}
            </div>
          ))}
        </div>
      </div>
    );
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0, position: 'relative' }}>
      <ViewToolbar variant="sub">
        <IconButton faSource="fa_plus" faColor="limegreen" size={26} onClick={() => setAddOpen(true)} tooltip="Add a new universe" data-role="add-universe" />
        <IconButton faSource="fa_trash_can" faColor="crimson" size={26} disabled={!selected || !canDelete(selected)} onClick={() => selected && setDelOpen(selected)}
          tooltip={selected ? deleteTooltip(selected) : 'Remove the selected universe'} data-role="delete-selected" />
        <IconButton imgSource={D.icon('blackout')} size={26} checked={blackout} checkedColor="var(--override-red)" onClick={toggleBlackout}
          tooltip={blackout ? 'Blackout is on — click to restore output' : 'Enable blackout on all the output patches'} data-role="blackout" />
        <ToolbarSpacer />
        <RobotoText label={status.text} fontSize={14} labelColor={status.error ? 'var(--override-red)' : 'var(--check-lime)'} height={30} data-role="status" />
        <RobotoText label={summary} fontSize={14} labelColor={live ? 'var(--check-lime)' : 'var(--fg-light)'} height={30} />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto', display: 'flex', flexDirection: 'column' }}>
          <div style={head}>
            <RobotoText label="Input" fontSize={14} fontBold style={col(340)} />
            <RobotoText label="Universe" fontSize={14} fontBold textHAlign="center" style={col(230)} />
            <RobotoText label="Output" fontSize={14} fontBold style={col(360)} />
          </div>
          {universes.map(renderUniverse)}
          {live && !liveRows ? <RobotoText label="Loading universes…" fontSize={14} labelColor={noteText} height={38} leftMargin={8} /> : null}
          <div style={{ padding: '8px 10px', display: 'flex', flexDirection: 'column', gap: 4 }}>
            {live && pluginState.unsupported ? <RobotoText label="Patch pickers are not available: this server has no io.plugin.list yet, so plugin lines cannot be enumerated. Patches are shown read-only." fontSize={12} labelColor={noteText} wrapText height="auto" /> : null}
            {live && pluginState.error && !pluginState.unsupported ? <RobotoText label={'io.plugin.list failed: ' + pluginState.error} fontSize={12} labelColor="var(--override-red)" wrapText height="auto" /> : null}
            {live && profileState.unsupported ? <RobotoText label="Input profiles cannot be listed: this server has no io.inputProfile.list yet." fontSize={12} labelColor={noteText} wrapText height="auto" /> : null}
            <RobotoText label="Drag-and-drop patching from the desktop is replaced by the pickers above. Lines already patched elsewhere are marked with the universe using them." fontSize={12} labelColor={noteText} wrapText height="auto" />
          </div>
        </div>

        <div style={{ width: 260, minWidth: 260, borderLeft: 'var(--border-dark)', background: 'var(--bg-strong)', overflow: 'auto' }}>
          <SectionBox sectionLabel="Plugins" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {plugins ? plugins.map(p => {
                const used = [].concat(p.inputLines.map(l => ({ dir: 'input', l })), p.outputLines.map(l => ({ dir: 'output', l })))
                  .filter(x => (inUse[x.dir][lineKey(p.name, x.l.index)] || []).length);
                return (
                  <div key={p.name} style={{ display: 'flex', flexDirection: 'column' }}>
                    <IconTextEntry iSrc={pluginIcon(p.name)} tLabel={p.name + ' · ' + p.inputLines.length + ' in / ' + p.outputLines.length + ' out'} tFontSize={14} height={26}
                      tLabelColor={used.length ? 'var(--fg-main)' : 'var(--fg-light)'} />
                    {used.map(x => (
                      <RobotoText key={x.dir + x.l.index} label={x.l.name + ' → ' + inUse[x.dir][lineKey(p.name, x.l.index)].map(u => u.name).join(', ') + (x.dir === 'input' ? ' (input)' : '')}
                        fontSize={12} labelColor="var(--fg-light)" height={20} leftMargin={30} />
                    ))}
                    {p.canConfigure ? <RobotoText label="Configuration: desktop app only" fontSize={12} labelColor={noteText} height={20} leftMargin={30} /> : null}
                  </div>
                );
              }) : <RobotoText label={pluginState.unsupported ? 'Not available: no io.plugin.list on this server' : (live ? 'Loading…' : '')} fontSize={12} labelColor={noteText} wrapText height="auto" />}
            </div>
          </SectionBox>
          <SectionBox sectionLabel="Input profiles" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {profiles ? (profiles.length ? profiles.map(p => (
                <IconTextEntry key={p.name} iSrc={D.icon('midiplugin')} tLabel={p.name} tFontSize={14} height={26} title={p.manufacturer + ' ' + p.model} />
              )) : <RobotoText label="No input profiles" fontSize={12} labelColor={noteText} height={22} />)
                : <RobotoText label={profileState.unsupported ? 'Not available: no io.inputProfile.list on this server' : (live ? 'Loading…' : '')} fontSize={12} labelColor={noteText} wrapText height="auto" />}
              <RobotoText label="Profile editor and MIDI/OSC learn: desktop app only" fontSize={12} labelColor={noteText} wrapText height="auto" />
            </div>
          </SectionBox>
          <SectionBox sectionLabel="Audio" isExpanded>
            <div style={{ padding: 6 }}><IconTextEntry iSrc={D.icon('audiocard')} tLabel="Audio devices: not available in the web UI" tFontSize={12} tLabelColor={noteText} height={26} /></div>
          </SectionBox>
          <SectionBox sectionLabel="Desktop only" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {['Plugin configuration dialogs (native, desktop only)', 'Input signal indicator (no input event on the API)', 'Pause / blackout per output patch (read-only here)', 'More than one output line per universe (read-only here)', 'Input profile editor and wizard'].map(t => (
                <RobotoText key={t} label={'· ' + t} fontSize={12} labelColor={noteText} wrapText height="auto" />
              ))}
            </div>
          </SectionBox>
        </div>
      </div>

      <CustomPopupDialog open={addOpen} title="Add universe" width={360}
        standardButtons={['Cancel', 'Add']} onClicked={(b) => { if (b === 'Add') addUniverse(); else setAddOpen(false); }} onClose={() => setAddOpen(false)}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <RobotoText label="Name" fontSize={14} style={{ width: 60 }} />
          <span style={{ flex: 1, height: 26, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
            <CustomTextInput text={addName} editing autoFocus placeholder={'Universe ' + (universes.length + 1)} width="100%" height={22} onTextConfirmed={setAddName}
              onInput={(e) => setAddName(e.target.value)} onKeyDown={(e) => { if (e.key === 'Enter') addUniverse(); }} />
          </span>
        </div>
      </CustomPopupDialog>

      <CustomPopupDialog open={!!delOpen} title="Remove universe" width={380}
        message={delOpen ? 'Are you sure you want to remove ' + delOpen.name + '? Its input, output and feedback patches are removed with it.' : ''}
        standardButtons={['Cancel', 'Remove']} onClicked={(b) => { if (b === 'Remove' && delOpen) deleteUniverse(delOpen); else setDelOpen(null); }} onClose={() => setDelOpen(null)} />
    </div>
  );
}
Object.assign(window, { InputOutput });
