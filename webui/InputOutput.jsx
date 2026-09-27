const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, CustomComboBox, CustomCheckBox, GenericButton, IconTextEntry, SectionBox, CustomPopupDialog, CustomTextInput } = window.PatchDesignSystem_5432c9;

/* qmlui/js/GenericHelpers.js::pluginIconFromName */
const PLUGIN_ICONS = { ArtNet: 'artnetplugin', 'E1.31': 'e131plugin', 'DMX USB': 'dmxusbplugin', MIDI: 'midiplugin', OSC: 'oscplugin', HID: 'hidplugin', OLA: 'olaplugin', Loopback: 'loop' };
window.IOPluginIcon = (name) => PLUGIN_ICONS[name] || 'inputoutput';
const pluginIcon = (name) => window.QLCData.icon(window.IOPluginIcon(name));
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
const MOCK_PROFILES = [{ name: 'Generic MIDI', manufacturer: 'Generic', model: 'MIDI', type: 'MIDI' }, { name: 'Novation Launchpad', manufacturer: 'Novation', model: 'Launchpad', type: 'MIDI' }];
const mockUniverses = () => window.QLCData.universes.map(u => {
  /* data.js labels are "<plugin> <line>" ("ArtNet 2.0.0.1") or just the line ("MIDI Controller"). */
  const find = (dir, label) => { for (const p of MOCK_PLUGINS) for (const l of (dir === 'input' ? p.inputLines : p.outputLines)) if (l.name === label || p.name + ' ' + l.name === label) return { plugin: p.name, line: l }; return null; };
  const i = find('input', u.input), o = find('output', u.output), f = find('output', u.feedback);
  return { id: u.id - 1, name: u.name, passthrough: u.passthrough, monitor: false, usedChannels: 0, totalChannels: 512,
    inputPatch: i ? { pluginName: i.plugin, input: i.line.index, inputName: i.line.name, profileName: u.id === 3 ? 'Generic MIDI' : null, parameters: {} } : null,
    outputPatches: o ? [{ index: 0, pluginName: o.plugin, output: o.line.index, outputName: o.line.name, paused: false, blackout: false, parameters: { outputIP: '2.0.0.255', outputUni: 0 } }] : [],
    feedbackPatch: f ? { index: 0, pluginName: f.plugin, output: f.line.index, outputName: f.line.name, parameters: {} } : null };
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
    const offs = ['io.universe.created', 'io.universe.updated', 'io.universe.deleted', 'io.universe.monitorChanged', 'io.patch.output.stateChanged', 'io.plugin.linesChanged', 'core.project.loaded'].map(t => qlc.subscribeTo(t, debounced));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [qlc.online]);
  return [rows, () => reloadRef.current()];
}

/** One list call whose absence (NOT_FOUND "Unknown method") is a first-class state, not an error. */
function useOptionalList(qlc, method, key, topics) {
  const [state, setState] = React.useState({ items: null, raw: null, unsupported: false, error: '' });
  React.useEffect(() => {
    if (!qlc.online) { setState({ items: null, raw: null, unsupported: false, error: '' }); return; }
    let alive = true;
    const load = () => qlc.call(method, {}).then(r => { if (alive) setState({ items: r[key] || [], raw: r, unsupported: false, error: '' }); })
      .catch(e => { if (alive) setState({ items: null, raw: null, unsupported: e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || ''), error: e.message || String(e) }); });
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
  const [props, setProps] = React.useState(null);            // {universeId, direction, index} -> PatchPropertiesDialog
  const [selProfile, setSelProfile] = React.useState(null);
  const [profileEditor, setProfileEditor] = React.useState(null); // {name|null}
  const [pluginNote, setPluginNote] = React.useState({});

  const universes = live ? (liveRows || []) : mockRows;
  const plugins = live ? pluginState.items : MOCK_PLUGINS;
  const profiles = live ? profileState.items : MOCK_PROFILES;
  const pickersOff = live && !plugins;      // io.plugin.list missing or not loaded yet
  const unsupported = (m) => live && qlc.isUnsupported(m);
  const say = (text, error) => setStatus({ text, error: !!error });

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
      .then(() => { say(label, false); reload(); return true; },
        e => {
          const missing = e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || '');
          say(missing ? label + ' — not available: this server has no ' + method + ' yet' : label + ' failed: ' + (e.message || e.code || 'request failed'), true);
          return false;
        });
  };
  /* Live (§4b) calls: no revision, no reload needed beyond the event. */
  const liveCall = (method, params, label) => qlc.call(method, params)
    .then(() => { say(label, false); return true; }, e => { say((e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || '') ? label + ' — not available: this server has no ' + method : label + ' failed: ' + (e.message || e.code)), true); return false; });
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
  const patchLine = (u, direction, value, index) => {
    const slot = direction === 'output' && index > 0 ? ' ' + (index + 1) : '';
    const label = direction.charAt(0).toUpperCase() + direction.slice(1) + slot + ' patch of ' + u.name;
    if (value === NONE) {
      if (!live) { setMock(u.id, direction === 'input' ? { inputPatch: null, feedbackPatch: null } : direction === 'output' ? (r => ({ outputPatches: r.outputPatches.filter((p, i) => i !== (index || 0)).map((p, i) => Object.assign({}, p, { index: i })) })) : { feedbackPatch: null }); return; }
      const params = { universeId: u.id, direction };
      if (direction === 'output') params.index = index || 0;
      mutate('io.patch.remove', params, label + ' removed');
      return;
    }
    const [plugin, lineStr] = value.split('|'); const line = Number(lineStr);
    if (!live) {
      const p = plugins.find(p => p.name === plugin); const l = (direction === 'input' ? p.inputLines : p.outputLines).find(l => l.index === line);
      setMock(u.id, direction === 'input' ? { inputPatch: { pluginName: plugin, input: line, inputName: l.name, profileName: u.inputPatch ? u.inputPatch.profileName : null, parameters: {} } }
        : direction === 'output' ? (r => { const list = r.outputPatches.slice(); list[index || 0] = { index: index || 0, pluginName: plugin, output: line, outputName: l.name, paused: false, blackout: false, parameters: {} }; return { outputPatches: list }; })
        : { feedbackPatch: { index: 0, pluginName: plugin, output: line, outputName: l.name, parameters: {} } });
      return;
    }
    const params = { universeId: u.id, direction, plugin, line };
    if (direction === 'input' && u.inputPatch && u.inputPatch.profileName) params.profile = u.inputPatch.profileName;  // keep the profile across a line change
    if (direction === 'output') params.index = index || 0;   // index == outputPatches.length appends a new output
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
  const setMonitor = (u, on) => {
    if (!live) { setMock(u.id, { monitor: on }); return; }
    liveCall('io.universe.setMonitor', { universeId: u.id, monitor: on }, 'Monitor ' + (on ? 'enabled' : 'disabled') + ' on ' + u.name).then(ok => { if (ok) reload(); });
  };
  const setOutputState = (u, p, patch) => {
    if (!live) { setMock(u.id, r => ({ outputPatches: r.outputPatches.map(o => o.index === p.index ? Object.assign({}, o, patch) : o) })); return; }
    const what = 'paused' in patch ? (patch.paused ? 'paused' : 'resumed') : (patch.blackout ? 'blacked out' : 'restored');
    liveCall('io.patch.output.setState', Object.assign({ universeId: u.id, index: p.index }, patch), 'Output ' + (p.index + 1) + ' of ' + u.name + ' ' + what).then(ok => { if (ok) reload(); });
  };
  const addUniverse = () => {
    setAddOpen(false);
    const name = addName.trim();
    setAddName('');
    if (!live) { setMockRows(p => p.concat([{ id: p.length, name: name || 'Universe ' + (p.length + 1), passthrough: false, monitor: false, usedChannels: 0, totalChannels: 512, inputPatch: null, outputPatches: [], feedbackPatch: null }])); return; }
    mutate('io.universe.create', name ? { name } : {}, 'Universe added');
  };
  const deleteUniverse = (u) => {
    setDelOpen(null);
    if (!live) { setMockRows(p => p.filter(x => x.id !== u.id)); if (sel === u.id) setSel(0); return; }
    mutate('io.universe.delete', { universeId: u.id }, u.name + ' removed').then(ok => { if (ok && sel === u.id) setSel(0); });
  };
  const toggleBlackout = () => {
    if (!live) { setBlackout(!blackout); return; }
    qlc.call('io.blackout.set', { blackout: !blackout }).then(() => setBlackout(!blackout)).catch(e => say('Blackout failed: ' + e.message, true));
  };
  const rescanPlugin = (p) => {
    if (!live) return;
    qlc.call('io.plugin.rescan', { pluginName: p.name })
      .then(() => setPluginNote(n => Object.assign({}, n, { [p.name]: 'rescanned' })), e => setPluginNote(n => Object.assign({}, n, { [p.name]: e.code === 'UNSUPPORTED' ? 'no rescan hook' : (e.message || e.code) })));
  };
  const configurePlugin = (p) => {
    if (!live) return;
    qlc.call('io.plugin.configure', { pluginName: p.name })
      .then(r => setPluginNote(n => Object.assign({}, n, { [p.name]: r && r.openedOnHost ? 'dialog shown on the QLC+ host' : 'configured' })), e => setPluginNote(n => Object.assign({}, n, { [p.name]: e.message || e.code })));
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
  const propsUniverse = props ? universes.find(u => u.id === props.universeId) : null;
  const propsPatch = propsUniverse ? (props.direction === 'input' ? propsUniverse.inputPatch : props.direction === 'feedback' ? propsUniverse.feedbackPatch : (propsUniverse.outputPatches || [])[props.index]) : null;
  const propsPlugin = propsPatch ? (plugins || []).find(p => p.name === propsPatch.pluginName) : null;
  const paramsOff = unsupported('io.patch.setParameters');
  const stateOff = unsupported('io.patch.output.setState');
  const monitorOff = unsupported('io.universe.setMonitor');
  const propsButton = (u, direction, index, patch) => (
    <IconButton imgSource={D.icon('configure')} size={26} data-role={direction + '-properties'} onClick={(e) => { e.stopPropagation(); setProps({ universeId: u.id, direction, index }); }}
      tooltip={'Plugin parameters of this ' + direction + ' line' + (Object.keys(patch.parameters || {}).length ? ' (' + Object.keys(patch.parameters).length + ' set)' : '') + (paramsOff ? ' — read-only: this server has no io.patch.setParameters' : '')} />
  );

  const renderOutputRow = (u, p, i, first) => {
    const val = p ? lineKey(p.pluginName, p.output) : NONE;
    const label = p ? p.pluginName + ' — ' + p.outputName : 'None';
    return (
      <div key={p ? p.index : 'add'} data-output={p ? p.index : 'add'} style={{ display: 'flex', alignItems: 'center', gap: 6, paddingLeft: first ? 0 : 32 }}>
        {first ? <img src={p ? pluginIcon(p.pluginName) : D.icon('inputoutput')} alt="" style={{ width: 26, height: 26, flex: 'none', opacity: p ? 1 : .4 }} /> : null}
        {!first && !p ? <RobotoText label="+ output" fontSize={12} labelColor="var(--fg-light)" height={26} style={{ width: 56 }} title="Patch an additional output line to this universe (the same universe is sent to every output)" /> : null}
        {pickersOff
          ? <RobotoText label={label} fontSize={first ? 14 : 12} height={26} labelColor={p ? 'var(--fg-main)' : noteText} />
          : <CustomComboBox width={first ? 290 : 234} height={26} currValue={val} model={lineModel(plugins, 'output', inUse, u.id)} data-role={p ? 'output-picker' : 'output-add-picker'}
            disabled={unsupported('io.patch.set')} onValueChanged={v => patchLine(u, 'output', v, p ? p.index : (u.outputPatches || []).length)} />}
        {p ? <IconButton faSource={p.paused ? 'fa_play' : 'fa_pause'} faColor="var(--bg-strong)" size={26} checked={!!p.paused} checkedColor="var(--selection)" disabled={stateOff} data-role="output-pause"
          tooltip={(p.paused ? 'Paused — click to resume this output' : 'Pause this output (holds its last frame)') + (stateOff ? ' — not available: this server has no io.patch.output.setState' : '')}
          onClick={(e) => { e.stopPropagation(); setOutputState(u, p, { paused: !p.paused }); }} /> : null}
        {p ? <IconButton imgSource={D.icon('blackout')} size={26} checked={!!p.blackout} checkedColor="var(--override-red)" disabled={stateOff} data-role="output-blackout"
          tooltip={(p.blackout ? 'Blackout on this output — click to restore' : 'Blackout this output only') + (stateOff ? ' — not available: this server has no io.patch.output.setState' : '')}
          onClick={(e) => { e.stopPropagation(); setOutputState(u, p, { blackout: !p.blackout }); }} /> : null}
        {p ? propsButton(u, 'output', p.index, p) : null}
      </div>
    );
  };

  const renderUniverse = (u, i) => {
    const isSel = selected && selected.id === u.id;
    const inputVal = u.inputPatch ? lineKey(u.inputPatch.pluginName, u.inputPatch.input) : NONE;
    const fbVal = u.feedbackPatch ? lineKey(u.feedbackPatch.pluginName, u.feedbackPatch.output) : NONE;
    const inputPlugin = u.inputPatch ? (plugins || []).find(p => p.name === u.inputPatch.pluginName) : null;
    const updateOff = unsupported('io.universe.update');
    const outputs = u.outputPatches || [];
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
              : <CustomComboBox width={u.inputPatch ? 260 : 290} height={26} currValue={inputVal} model={lineModel(plugins, 'input', inUse, u.id)} data-role="input-picker"
                disabled={unsupported('io.patch.set')} onValueChanged={v => patchLine(u, 'input', v)} />}
            {u.inputPatch ? propsButton(u, 'input', 0, u.inputPatch) : null}
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
                : <CustomComboBox width={u.feedbackPatch ? 196 : 228} height={26} currValue={fbVal} data-role="feedback-picker" disabled={unsupported('io.patch.set') || !inputPlugin || !inputPlugin.outputLines.length}
                  model={lineModel(plugins, 'output', inUse, u.id, u.inputPatch.pluginName)} onValueChanged={v => patchLine(u, 'feedback', v)} />}
              {u.feedbackPatch ? propsButton(u, 'feedback', 0, u.feedbackPatch) : null}
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
                onTextConfirmed={t => rename(u, t)} onClick={() => setSel(u.id)} />
              <IconButton faSource="fa_trash_can" faColor="var(--bg-strong)" size={22} disabled={!canDelete(u)} tooltip={deleteTooltip(u)} data-role="delete-universe"
                onClick={(e) => { e.stopPropagation(); setDelOpen(u); }} />
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }} title={updateOff ? 'Passthrough — not available: this server has no io.universe.update yet' : 'Merge the input patch live values onto this universe output'}>
              <CustomCheckBox checked={!!u.passthrough} size={18} disabled={updateOff} onToggled={v => setPassthrough(u, v)} data-role="passthrough" />
              <RobotoText label="Passthrough" fontSize={12} height={20} />
              <ToolbarSpacer />
              <RobotoText label={u.totalChannels ? u.usedChannels + '/' + u.totalChannels : ''} fontSize={12} labelColor="var(--fg-light)" height={20} />
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }} title={monitorOff ? 'Monitor — not available: this server has no io.universe.setMonitor yet' : 'Monitor: also keep this universe\'s values when it is not patched (DMX view / 2D monitor), live only'}>
              <CustomCheckBox checked={!!u.monitor} size={18} disabled={monitorOff} onToggled={v => setMonitor(u, v)} data-role="monitor" />
              <RobotoText label="Monitor" fontSize={12} height={20} />
            </div>
          </div>
        </div>

        {/* Output side: every output patch is editable, plus one "add" row (UniverseIOItem.qml's output list) */}
        <div style={Object.assign({}, col(360), { display: 'flex', flexDirection: 'column', gap: 4, justifyContent: 'center', flex: 1 })}>
          {outputs.length ? outputs.map((p, idx) => renderOutputRow(u, p, idx, idx === 0)) : renderOutputRow(u, null, 0, true)}
          {outputs.length && !pickersOff && !unsupported('io.patch.set') ? renderOutputRow(u, null, outputs.length, false) : null}
        </div>
      </div>
    );
  };

  const PatchPropertiesDialog = window.IOPatchProperties && window.IOPatchProperties.PatchPropertiesDialog;
  const InputProfileEditorDialog = window.IOInputProfileEditor && window.IOInputProfileEditor.InputProfileEditorDialog;
  const AudioDevices = window.IOAudioDevices && window.IOAudioDevices.AudioDevices;
  const GrandMasterPanel = window.IOGrandMasterPanel && window.IOGrandMasterPanel.GrandMasterPanel;
  const profileRow = (p) => (
    <div key={p.name} data-profile={p.name} onClick={() => setSelProfile(p.name)} onDoubleClick={() => live && setProfileEditor({ name: p.name })}
      style={{ display: 'flex', alignItems: 'center', cursor: 'pointer', background: selProfile === p.name ? 'var(--highlight)' : 'transparent', borderRadius: 3 }} title={p.manufacturer + ' ' + p.model + ' · ' + (p.type || '')}>
      <IconTextEntry iSrc={D.icon(p.type === 'OSC' ? 'oscplugin' : p.type === 'HID' ? 'hidplugin' : p.type === 'DMX' || p.type === 'Enttec' ? 'dmxusbplugin' : 'midiplugin')} tLabel={p.name} tFontSize={14} height={26} style={{ flex: 1, minWidth: 0 }} />
      <RobotoText label={p.type || ''} fontSize={12} labelColor="var(--fg-light)" height={26} rightMargin={4} />
    </div>
  );

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
            <RobotoText label="Drag-and-drop patching from the desktop is replaced by the pickers above. Lines already patched elsewhere are marked with the universe using them. The gear on a patched line opens its plugin parameters (IP, port, universe, …); the pause and blackout buttons act on that output only." fontSize={12} labelColor={noteText} wrapText height="auto" />
          </div>
        </div>

        <div style={{ width: 280, minWidth: 280, borderLeft: 'var(--border-dark)', background: 'var(--bg-strong)', overflow: 'auto' }}>
          <SectionBox sectionLabel="Plugins" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {plugins ? plugins.map(p => {
                const used = [].concat(p.inputLines.map(l => ({ dir: 'input', l })), p.outputLines.map(l => ({ dir: 'output', l })))
                  .filter(x => (inUse[x.dir][lineKey(p.name, x.l.index)] || []).length);
                return (
                  <div key={p.name} data-plugin={p.name} style={{ display: 'flex', flexDirection: 'column' }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
                      <IconTextEntry iSrc={pluginIcon(p.name)} tLabel={p.name + ' · ' + p.inputLines.length + ' in / ' + p.outputLines.length + ' out'} tFontSize={14} height={26}
                        tLabelColor={used.length ? 'var(--fg-main)' : 'var(--fg-light)'} style={{ flex: 1, minWidth: 0 }} />
                      <IconButton imgSource={D.icon('loop')} size={22} disabled={!live || unsupported('io.plugin.rescan')} tooltip="Rescan: ask the plugin to re-enumerate its lines (hotplug plugins such as DMX USB)" onClick={() => rescanPlugin(p)} data-role="plugin-rescan" />
                      {p.canConfigure ? <IconButton imgSource={D.icon('configure')} size={22} disabled={!live || unsupported('io.plugin.configure')} tooltip="Open the plugin's native configuration dialog on the QLC+ host machine" onClick={() => configurePlugin(p)} data-role="plugin-configure" /> : null}
                    </div>
                    {used.map(x => (
                      <RobotoText key={x.dir + x.l.index} label={x.l.name + ' → ' + inUse[x.dir][lineKey(p.name, x.l.index)].map(u => u.name).join(', ') + (x.dir === 'input' ? ' (input)' : '')}
                        fontSize={12} labelColor="var(--fg-light)" height={20} leftMargin={30} />
                    ))}
                    {pluginNote[p.name] ? <RobotoText label={pluginNote[p.name]} fontSize={12} labelColor={noteText} height={20} leftMargin={30} data-role="plugin-note" /> : null}
                  </div>
                );
              }) : <RobotoText label={pluginState.unsupported ? 'Not available: no io.plugin.list on this server' : (live ? 'Loading…' : '')} fontSize={12} labelColor={noteText} wrapText height="auto" />}
              {plugins && !plugins.length ? <RobotoText label="No IO plugins are loaded on this server (started without its Plugins folder?). Universes can still be renamed and configured; nothing can be patched." fontSize={12} labelColor={noteText} wrapText height="auto" /> : null}
            </div>
          </SectionBox>
          <SectionBox sectionLabel="Input profiles" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {profiles ? (profiles.length ? profiles.slice().sort((a, b) => a.name.localeCompare(b.name)).map(profileRow) : <RobotoText label="No input profiles" fontSize={12} labelColor={noteText} height={22} />)
                : <RobotoText label={profileState.unsupported ? 'Not available: no io.inputProfile.list on this server' : (live ? 'Loading…' : '')} fontSize={12} labelColor={noteText} wrapText height="auto" />}
              <div style={{ display: 'flex', alignItems: 'center', gap: 4, marginTop: 4 }}>
                <GenericButton label="New" width={60} height={24} disabled={!live || !InputProfileEditorDialog || unsupported('io.inputProfile.save')} onClick={() => setProfileEditor({ name: null })} data-role="profile-new" />
                <GenericButton label="Edit" width={60} height={24} disabled={!live || !InputProfileEditorDialog || !selProfile || unsupported('io.inputProfile.get')} onClick={() => selProfile && setProfileEditor({ name: selProfile })} data-role="profile-edit" />
                <RobotoText label={live ? (profileState.raw ? 'rev ' + profileState.raw.profilesRevision : '') : 'connect to edit'} fontSize={12} labelColor="var(--fg-light)" height={24} style={{ flex: 1 }} />
              </div>
              <RobotoText label="Double-click a profile to edit it. Profiles are .qxi files in the QLC+ host's user profile folder; the editor can auto-detect channels from a patched input line." fontSize={12} labelColor={noteText} wrapText height="auto" />
            </div>
          </SectionBox>
          <SectionBox sectionLabel="Grand Master" isExpanded>
            <div style={{ padding: 6 }}>{GrandMasterPanel ? <GrandMasterPanel qlc={qlc} onStatus={say} /> : null}</div>
          </SectionBox>
          <SectionBox sectionLabel="Audio" isExpanded>
            <div style={{ padding: 6 }}>{AudioDevices ? <AudioDevices qlc={qlc} onStatus={say} /> : null}</div>
          </SectionBox>
          <SectionBox sectionLabel="Desktop only" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {['Plugin configuration dialogs open on the QLC+ host machine, not in this browser', 'Input signal indicator on the input patch (no input event on the API)', 'Audio sample rate, channels, buffer size and input level check', 'Beat generator selection'].map(t => (
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

      {PatchPropertiesDialog ? <PatchPropertiesDialog open={!!props && !!propsPatch} qlc={qlc} universe={propsUniverse} direction={props ? props.direction : 'output'} index={props ? props.index : 0}
        patch={propsPatch} plugin={propsPlugin} onClose={() => setProps(null)} onStatus={say} /> : null}
      {InputProfileEditorDialog ? <InputProfileEditorDialog open={!!profileEditor} qlc={qlc} profileName={profileEditor ? profileEditor.name : null} universes={universes}
        onClose={() => setProfileEditor(null)} onStatus={say} /> : null}
    </div>
  );
}
Object.assign(window, { InputOutput });
