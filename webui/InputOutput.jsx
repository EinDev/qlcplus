const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, CustomComboBox, CustomCheckBox, GenericButton, IconTextEntry, SectionBox, CustomPopupDialog, CustomTextInput } = window.PatchDesignSystem_5432c9;

const PLUGIN_ICONS = { ArtNet: 'artnetplugin', 'E1.31': 'e131plugin', 'DMX USB': 'dmxusbplugin', MIDI: 'midiplugin', OSC: 'oscplugin', HID: 'hidplugin', OLA: 'olaplugin' };
const KNOWN_PLUGINS = [['artnetplugin', 'ArtNet'], ['e131plugin', 'E1.31'], ['dmxusbplugin', 'DMX USB'], ['midiplugin', 'MIDI'], ['oscplugin', 'OSC'], ['hidplugin', 'HID'], ['olaplugin', 'OLA']];

/** io.universe.list + one io.universe.get per universe (the list has no patch details). */
function useUniverses(qlc) {
  const [rows, setRows] = React.useState(null);
  React.useEffect(() => {
    if (!qlc.online) { setRows(null); return; }
    let alive = true, timer = null;
    const load = () => qlc.call('io.universe.list').then(r => Promise.all((r.universes || []).map(u => qlc.call('io.universe.get', { universeId: u.id }).catch(() => u))))
      .then(list => { if (alive) setRows(list.slice().sort((a, b) => a.id - b.id)); }).catch(() => {});
    const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
    load();
    const offs = ['io.universe.created', 'io.universe.updated', 'io.universe.deleted', 'io.patch.output.stateChanged', 'core.project.loaded'].map(t => qlc.subscribeTo(t, debounced));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [qlc.online]);
  return rows;
}

function patchLabel(p, kind) {
  if (!p) return 'None';
  const line = kind === 'input' ? p.inputName : p.outputName;
  return p.pluginName + (line ? ' — ' + line : '') + (p.profileName ? ' (' + p.profileName + ')' : '');
}

function InputOutput() {
  const D = window.QLCData;
  const qlc = useQLC();
  const live = qlc.online;
  const universes = useUniverses(qlc);
  const [mockRows, setMockRows] = React.useState(D.universes);
  const [sel, setSel] = React.useState(live ? 0 : 1);
  const [addOpen, setAddOpen] = React.useState(false);
  const [addName, setAddName] = React.useState('');
  const setMock = (id, patch) => setMockRows(p => p.map(u => u.id === id ? Object.assign({}, u, patch) : u));
  const head = { display: 'flex', alignItems: 'center', height: 30, background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)', padding: '0 6px' };
  const cell = (w) => ({ width: w, minWidth: w, padding: '0 6px', overflow: 'hidden' });

  const addUniverse = () => {
    setAddOpen(false);
    if (!live) return;
    qlc.call('io.universe.create', { baseRevision: qlc.docRevision(), name: addName || undefined }).catch(() => {});
    setAddName('');
  };

  const pluginsInUse = React.useMemo(() => {
    const set = {};
    (universes || []).forEach(u => {
      if (u.inputPatch) set[u.inputPatch.pluginName] = true;
      (u.outputPatches || []).forEach(p => { set[p.pluginName] = true; });
      if (u.feedbackPatch) set[u.feedbackPatch.pluginName] = true;
    });
    return set;
  }, [universes]);

  const summary = live
    ? (universes ? universes.length + ' universes · ' + universes.filter(u => u.isPatched).length + ' patched · ' + Object.keys(pluginsInUse).length + ' plugins in use' : 'Loading…')
    : '4 universes · 3 plugins active (mock)';

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <IconButton imgSource={D.icon('add')} size={26} disabled={!live} onClick={() => setAddOpen(true)}
          tooltip={live ? 'Add universe (io.universe.create)' : 'Add universe — connect first'} />
        <IconButton faSource="fa_trash_can" size={26} disabled tooltip="Remove universe — not available: the server has no io.universe.delete yet" />
        <IconButton imgSource={D.icon('network')} size={26} disabled tooltip="Network settings — not available in the web UI" />
        <ToolbarSpacer />
        <RobotoText label={summary} fontSize={14} labelColor={live ? 'var(--check-lime)' : 'var(--fg-light)'} />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto' }}>
          <div style={head}>
            <RobotoText label="Universe" fontSize={14} fontBold style={cell(150)} />
            <RobotoText label="Input" fontSize={14} fontBold style={cell(220)} />
            <RobotoText label="Output" fontSize={14} fontBold style={cell(260)} />
            <RobotoText label="Feedback" fontSize={14} fontBold style={cell(170)} />
            <RobotoText label="Passthrough" fontSize={14} fontBold style={cell(100)} />
          </div>
          {live ? (universes || []).map((u, i) => (
            <div key={u.id} onClick={() => setSel(u.id)}
              style={{ display: 'flex', alignItems: 'center', minHeight: 38, cursor: 'pointer', padding: '4px 6px',
                background: sel === u.id ? 'var(--highlight)' : (i % 2 ? 'var(--bg-strong)' : 'var(--bg-stronger)'), borderBottom: 'var(--border-dark)' }}>
              <div style={cell(150)} title={'Universe id ' + u.id + ' · ' + u.usedChannels + '/' + u.totalChannels + ' channels used'}>
                <IconTextEntry iSrc={D.icon('uniview')} tLabel={u.name} tFontSize={14} height={26} />
              </div>
              <div style={cell(220)}>
                <IconTextEntry iSrc={u.inputPatch ? D.icon(PLUGIN_ICONS[u.inputPatch.pluginName] || 'inputoutput') : undefined}
                  tLabel={patchLabel(u.inputPatch, 'input')} tFontSize={14} height={26} tLabelColor={u.inputPatch ? 'var(--fg-main)' : 'var(--fg-medium)'} />
              </div>
              <div style={Object.assign({}, cell(260), { display: 'flex', flexDirection: 'column', gap: 2 })}>
                {(u.outputPatches && u.outputPatches.length) ? u.outputPatches.map(p => (
                  <div key={p.index} style={{ display: 'flex', alignItems: 'center', gap: 4 }} title={JSON.stringify(p.parameters || {})}>
                    <IconTextEntry iSrc={D.icon(PLUGIN_ICONS[p.pluginName] || 'inputoutput')} tLabel={patchLabel(p, 'output')} tFontSize={14} height={26}
                      tLabelColor={p.paused ? 'var(--fg-medium)' : 'var(--fg-main)'} />
                    {p.paused ? <RobotoText label="paused" fontSize={12} labelColor="var(--selection)" height={26} /> : null}
                    {p.blackout ? <RobotoText label="blackout" fontSize={12} labelColor="var(--override-red)" height={26} /> : null}
                  </div>
                )) : <RobotoText label="None" fontSize={14} labelColor="var(--fg-medium)" height={26} />}
              </div>
              <div style={cell(170)}>
                <RobotoText label={patchLabel(u.feedbackPatch, 'output')} fontSize={14} height={26} labelColor={u.feedbackPatch ? 'var(--fg-main)' : 'var(--fg-medium)'} />
              </div>
              <div style={Object.assign({}, cell(100), { display: 'flex', justifyContent: 'center' })} title="Read-only: the server has no io.universe.update yet">
                <CustomCheckBox checked={!!u.passthrough} disabled />
              </div>
            </div>
          )) : mockRows.map((u, i) => (
            <div key={u.id} onClick={() => setSel(u.id)}
              style={{ display: 'flex', alignItems: 'center', height: 38, cursor: 'pointer', padding: '0 6px',
                background: sel === u.id ? 'var(--highlight)' : (i % 2 ? 'var(--bg-strong)' : 'var(--bg-stronger)'), borderBottom: 'var(--border-dark)' }}>
              <div style={cell(150)}><IconTextEntry iSrc={D.icon('uniview')} tLabel={u.name} tFontSize={14} height={26} /></div>
              <div style={cell(220)}><CustomComboBox width={200} height={26} currValue={u.input} onValueChanged={v => setMock(u.id, { input: v })}
                model={[{ mLabel: 'None', mValue: 'None' }, { mLabel: 'MIDI Controller', mValue: 'MIDI Controller' }, { mLabel: 'OSC 9000', mValue: 'OSC 9000' }]} /></div>
              <div style={cell(260)}><CustomComboBox width={240} height={26} currValue={u.output} onValueChanged={v => setMock(u.id, { output: v })}
                model={[{ mLabel: 'None', mValue: 'None' }, { mLabel: 'ArtNet 2.0.0.1', mValue: 'ArtNet 2.0.0.1' }, { mLabel: 'E1.31 239.255.0.2', mValue: 'E1.31 239.255.0.2' }, { mLabel: 'DMX USB Pro', mValue: 'DMX USB Pro' }]} /></div>
              <div style={cell(170)}><RobotoText label={u.feedback} fontSize={14} labelColor="var(--fg-light)" height={26} /></div>
              <div style={Object.assign({}, cell(100), { display: 'flex', justifyContent: 'center' })}>
                <CustomCheckBox checked={u.passthrough} onToggled={v => setMock(u.id, { passthrough: v })} />
              </div>
            </div>
          ))}
          {live ? (
            <div style={{ padding: 10 }}>
              <RobotoText label="Patching is read-only here: the Control API server has no io.patch.set / io.patch.remove / io.plugin.list / io.plugin.getLines yet, so plugin lines cannot be enumerated or assigned from the web UI. Universe rename and passthrough (io.universe.update) are also not implemented server-side."
                fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" />
            </div>
          ) : null}
        </div>

        <div style={{ width: 240, minWidth: 240, borderLeft: 'var(--border-dark)', background: 'var(--bg-strong)', overflow: 'auto' }}>
          <SectionBox sectionLabel={live ? 'Plugins in use' : 'Plugins'} isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {KNOWN_PLUGINS.map(([ic, n]) => (
                <IconTextEntry key={n} iSrc={D.icon(ic)} tLabel={n + (live && pluginsInUse[n] ? ' — patched' : '')} tFontSize={14} height={26}
                  tLabelColor={!live || pluginsInUse[n] ? 'var(--fg-main)' : 'var(--fg-medium)'} />
              ))}
              {live ? Object.keys(pluginsInUse).filter(n => !PLUGIN_ICONS[n]).map(n => (
                <IconTextEntry key={n} iSrc={D.icon('inputoutput')} tLabel={n + ' — patched'} tFontSize={14} height={26} />
              )) : null}
              {live ? <RobotoText label="Availability of unpatched plugins is unknown: no io.plugin.list on the server." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" style={{ marginTop: 6 }} /> : null}
            </div>
          </SectionBox>
          <SectionBox sectionLabel="Audio">
            <div style={{ padding: 6 }}><IconTextEntry iSrc={D.icon('audiocard')} tLabel={live ? 'Not exposed by the API' : 'Default output'} tFontSize={14} height={26} /></div>
          </SectionBox>
        </div>
      </div>

      <CustomPopupDialog open={addOpen} title="Add universe" width={360}
        standardButtons={['Cancel', 'Add']} onClicked={(b) => { if (b === 'Add') addUniverse(); else setAddOpen(false); }} onClose={() => setAddOpen(false)}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <RobotoText label="Name" fontSize={14} style={{ width: 60 }} />
          <span style={{ flex: 1, height: 26, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
            <CustomTextInput text={addName} editing autoFocus placeholder={'Universe ' + ((universes || []).length + 1)} width="100%" height={22} onTextConfirmed={setAddName}
              onKeyDown={(e) => { setTimeout(() => setAddName(e.target.value), 0); if (e.key === 'Enter') addUniverse(); }} />
          </span>
        </div>
      </CustomPopupDialog>
    </div>
  );
}
Object.assign(window, { InputOutput });
