const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, ChannelStrip, KeyPad, CustomComboBox, DMXPercentageButton, ShortcutHint, GenericButton, CustomPopupDialog, CustomTextInput, CustomCheckBox, IconTextEntry, MenuBarEntry } = window.PatchDesignSystem_5432c9;

const SD_HISTORY_MAX = 10;          // MAX_KEYPAD_HISTORY in qmlui/simpledesk.cpp
const SD_TABS_MAX = 8;              // more universes than this fall back to the combo box

/** One strip; memoised so a single channel event re-renders one strip, not 512. */
const Strip = React.memo(function Strip({ index, value, name, icon, display, isOverride, dmx, marked, onValueChanged, onReset, onDebug }) {
  return (
    <ChannelStrip address={index + 1} value={value} channelIcon={icon} channelName={name} display={display}
      isOverride={isOverride} dmxValues={dmx} data-channel={index}
      onValueChanged={(v) => onValueChanged(index, v)} onReset={() => onReset(index)} onDebug={() => onDebug(index)}
      style={{ height: '100%', borderTop: marked ? '3px solid var(--selection)' : undefined }} />
  );
});

/**
 * Fixtures + channel labels for one universe: fixtures.list gives every fixture's footprint straight
 * away, fixtures.get (cached per fixture id) fills in the channel names and groups a moment later.
 */
function useChannelInfo(qlc, universeId) {
  const empty = { names: [], icons: [], display: [], fixtures: [] };
  const [info, setInfo] = React.useState(empty);
  const cache = React.useRef({});
  React.useEffect(() => {
    if (!qlc.online) { setInfo(empty); return; }
    let alive = true, timer = null;
    const D = window.QLCData;
    const build = (fixtures, details) => {
      const names = new Array(512).fill(''), icons = new Array(512).fill(null), display = new Array(512).fill('none');
      fixtures.forEach((f, fi) => {
        const d = details[f.id];
        for (let c = 0; c < f.channels && f.address + c < 512; c++) {
          const ch = f.address + c;
          const chDef = d && d.channelList && d.channelList[c];
          names[ch] = chDef ? f.name + ' · ' + chDef.name : f.name + ' · ch ' + (c + 1);
          icons[ch] = chDef ? window.QLCIcons.channelIcon(chDef) : D.icon('other');
          display[ch] = fi % 2 ? 'even' : 'odd';
        }
      });
      return { names, icons, display, fixtures };
    };
    const load = () => qlc.call('fixtures.list', { universe: universeId }).then(r => {
      const fixtures = (r.fixtures || []).filter(f => f.universe === universeId).sort((a, b) => a.address - b.address);
      if (!alive) return;
      setInfo(build(fixtures, cache.current));
      const missing = fixtures.filter(f => !cache.current[f.id]);
      return Promise.all(missing.map(f => qlc.call('fixtures.get', { fixtureId: f.id }).then(d => { cache.current[f.id] = d; }).catch(() => {})))
        .then(() => { if (alive && missing.length) setInfo(build(fixtures, cache.current)); });
    }).catch(() => {});
    const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
    load();
    const offs = ['fixtures.patched', 'fixtures.unpatched', 'fixtures.updated', 'core.project.loaded'].map(t => qlc.subscribeTo(t, () => { cache.current = {}; debounced(); }));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [qlc.online, universeId]);
  return info;
}

/** Mock fixture rows for the offline preview, derived from data.js' "1.001"-style addresses. */
function mockFixtures(universeId) {
  const rows = [];
  (window.QLCData.fixtures || []).forEach(g => (g.children || []).forEach(f => {
    const parts = String(f.address).split('.');
    if (Number(parts[0]) - 1 !== universeId) return;
    rows.push({ id: f.id, name: f.name, address: Number(parts[1]) - 1, channels: f.channels, icon: f.icon });
  }));
  return rows.sort((a, b) => a.address - b.address);
}

function SimpleDesk() {
  const D = window.QLCData;
  const qlc = useQLC();
  const live = qlc.online;
  const [universes, setUniverses] = React.useState(null);
  const [universeId, setUniverseId] = React.useState(0);
  const [values, setValues] = React.useState(() => new Array(512).fill(0));
  const [overrides, setOverrides] = React.useState(() => new Array(512).fill(false));
  const [mockValues, setMockValues] = React.useState(D.channels.map(c => c.value));
  const [mockOverrides, setMockOverrides] = React.useState([5]);
  const [dmx, setDmx] = React.useState(true);
  const [cmd, setCmd] = React.useState('');
  const [cmdNote, setCmdNote] = React.useState({ text: '', error: false });
  const [history, setHistory] = React.useState([]);
  const [selFixture, setSelFixture] = React.useState(null);
  const [dumpOpen, setDumpOpen] = React.useState(false);
  const [dumpName, setDumpName] = React.useState('');
  const [dumpNonZero, setDumpNonZero] = React.useState(true);
  const [dumpNote, setDumpNote] = React.useState('');
  const [debugCh, setDebugCh] = React.useState(null);
  const parser = React.useRef(null);
  if (!parser.current) parser.current = new window.QLCKeypadParser();   // one parser per desk: it remembers the last channel list
  const stripsRef = React.useRef(null);
  const info = useChannelInfo(qlc, universeId);
  const fixtures = live ? info.fixtures : mockFixtures(universeId);

  /* Universe list (live), kept current across create / rename / delete from the I/O screen or the desktop. */
  React.useEffect(() => {
    if (!live) { setUniverses(null); return; }
    let alive = true;
    const load = () => qlc.call('io.universe.list').then(r => {
      if (!alive) return;
      const list = (r.universes || []).slice().sort((a, b) => a.id - b.id);
      setUniverses(list);
      setUniverseId(cur => (list.some(u => u.id === cur) || !list.length) ? cur : list[0].id);
    }).catch(() => {});
    load();
    const offs = ['io.universe.created', 'io.universe.updated', 'io.universe.deleted', 'core.project.loaded'].map(t => qlc.subscribeTo(t, load));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [live]);

  /* Follow the shown universe: seed + live DMX stream + override events, re-armed after every reconnect. */
  React.useEffect(() => {
    if (!live) return;
    const client = qlc.client();
    if (!client) return;
    setValues(new Array(512).fill(0));
    setOverrides(new Array(512).fill(false));
    setSelFixture(null);
    const off = qlc.subscribeTo('channels', (rows) => {
      const mine = rows.filter(r => r.universeId === universeId);
      if (!mine.length) return;
      setValues(prev => { const next = prev.slice(); mine.forEach(r => { next[r.channel] = r.value; }); return next; });
      if (mine.some(r => r.overridden !== null))
        setOverrides(prev => { const next = prev.slice(); mine.forEach(r => { if (r.overridden !== null) next[r.channel] = r.overridden; }); return next; });
    });
    client.watchUniverse(universeId).catch(() => {});
    return () => { off(); client.unwatchUniverse(); };
  }, [live, universeId]);

  /* Keypad history: the server's (shared by every client, io.simpleDesk.get + commandHistoryChanged)
     when it evaluates keypad commands itself; the per-tab list stays as the fallback. */
  const serverKeypad = live && !qlc.isUnsupported('io.simpleDesk.sendKeypadCommand');
  React.useEffect(() => {
    if (!live) { setHistory([]); return; }
    let alive = true;
    qlc.call('io.simpleDesk.get', { universeId }).then(r => { if (alive && Array.isArray(r.commandHistory)) setHistory(r.commandHistory.slice(0, SD_HISTORY_MAX)); }).catch(() => {});
    const off = qlc.subscribeTo('io.simpleDesk.commandHistoryChanged', (data) => { if (alive && data && Array.isArray(data.history)) setHistory(data.history.slice(0, SD_HISTORY_MAX)); });
    return () => { alive = false; off(); };
  }, [live]);

  const applyLocal = (items) => {
    if (live) {
      setValues(p => { const n = p.slice(); items.forEach(i => { n[i.channel] = i.value; }); return n; });
      setOverrides(p => { const n = p.slice(); items.forEach(i => { n[i.channel] = true; }); return n; });
    } else {
      setMockValues(p => p.map((x, j) => { const hit = items.find(i => i.channel === j); return hit ? hit.value : x; }));
      setMockOverrides(p => { const n = p.slice(); items.forEach(i => { if (i.channel < D.channels.length && n.indexOf(i.channel) === -1) n.push(i.channel); }); return n; });
    }
  };
  const setChannel = React.useCallback((i, v) => {
    if (live) {
      setValues(p => { const n = p.slice(); n[i] = v; return n; });
      setOverrides(p => { if (p[i]) return p; const n = p.slice(); n[i] = true; return n; });
      qlc.client().setChannel(universeId * 512 + i, v);
    } else {
      setMockValues(p => p.map((x, j) => j === i ? v : x));
      setMockOverrides(p => p.indexOf(i) === -1 ? p.concat([i]) : p);
    }
  }, [live, universeId]);
  /* Reset releases the manual override back to whatever the playback is doing — not "write 0". */
  const reset = React.useCallback((i) => {
    if (live) {
      setOverrides(p => { const n = p.slice(); n[i] = false; return n; });
      qlc.client().resetChannel(universeId * 512 + i);
    } else {
      setMockValues(p => p.map((x, j) => j === i ? 0 : x));
      setMockOverrides(p => p.filter(x => x !== i));
    }
  }, [live, universeId]);
  const resetAll = () => {
    if (live) { setOverrides(new Array(512).fill(false)); qlc.client().resetUniverse(universeId); }
    else { setMockValues(D.channels.map(() => 0)); setMockOverrides([]); }
  };
  const showDebug = React.useCallback((i) => setDebugCh(i), []);

  /* Keypad: the server evaluates the command with the engine's own KeyPadParser
     (io.simpleDesk.sendKeypadCommand, results arrive as channelChanged events, the shared history as
     commandHistoryChanged). Servers without it fall back to the browser port of the grammar
     (io/keypad-parser.js) applied via io.simpleDesk.setChannels, with a per-tab history. */
  const executeLocally = (normalized) => {
    const items = parser.current.parseCommand(normalized, live ? values : mockValues);
    setHistory(h => [normalized].concat(h).slice(0, SD_HISTORY_MAX));
    if (!items.length) { setCmdNote({ text: 'Nothing applied: name a channel first (e.g. 1 THRU 12 AT 255)', error: true }); return; }
    applyLocal(items);
    if (live) qlc.client().setChannels(items.map(i => ({ address: universeId * 512 + i.channel, value: i.value })));
    setCmdNote({ text: items.length + (items.length === 1 ? ' channel' : ' channels') + ' set', error: false });
  };
  const execute = (text) => {
    const normalized = window.QLCKeypadParser.normalize(text);
    if (!normalized) return;
    setCmd('');
    if (!serverKeypad) { executeLocally(normalized); return; }
    qlc.call('io.simpleDesk.sendKeypadCommand', { command: normalized, universeId })
      .then(r => {
        if (Array.isArray(r.history)) setHistory(r.history.slice(0, SD_HISTORY_MAX));
        const n = r.channelsChanged || 0;
        setCmdNote(n ? { text: n + (n === 1 ? ' channel' : ' channels') + ' set (server)', error: false }
          : { text: 'Channels selected — now give a value (AT <0-255>, FULL, ZERO, + / - <n>)', error: false });
      })
      .catch(e => {
        if (e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || '')) { executeLocally(normalized); return; }
        setCmdNote({ text: 'Keypad command failed: ' + (e.message || e.code || 'request failed'), error: true });
      });
  };
  const dump = () => {
    if (!live) { setDumpOpen(false); return; }
    setDumpNote('');
    qlc.call('io.simpleDesk.dump', { baseRevision: qlc.docRevision(), name: dumpName || undefined, nonZeroOnly: dumpNonZero, channelGroups: [] })
      .then(r => { setDumpOpen(false); setDumpName(''); setCmdNote({ text: 'Scene ' + (r && r.sceneId != null ? r.sceneId + ' ' : '') + 'created from the DMX dump', error: false }); })
      .catch(e => setDumpNote('Cannot dump: ' + (e.message || e.code || 'request failed')));
  };
  const selectFixture = (f) => {
    setSelFixture(f.id);
    const el = stripsRef.current && stripsRef.current.querySelector('[data-channel="' + f.address + '"]');
    if (el && el.scrollIntoView) el.scrollIntoView({ inline: 'start', block: 'nearest' });
  };

  const overriddenCount = live ? overrides.filter(Boolean).length : mockOverrides.length;
  const universeRows = live ? (universes || []) : D.universes.map(u => ({ id: u.id - 1, name: u.name }));
  const universeModel = universeRows.map(u => ({ mLabel: u.name, mValue: u.id }));
  const selected = fixtures.find(f => f.id === selFixture) || null;
  const inSelected = (i) => selected && i >= selected.address && i < selected.address + selected.channels;
  const debugInfo = debugCh !== null ? {
    value: live ? values[debugCh] : (mockValues[debugCh] || 0),
    overridden: live ? overrides[debugCh] : mockOverrides.indexOf(debugCh) !== -1,
    fixture: fixtures.find(f => debugCh >= f.address && debugCh < f.address + f.channels) || null,
    name: live ? info.names[debugCh] : (D.channels[debugCh] ? D.channels[debugCh].channelName : '')
  } : null;
  const head = { display: 'flex', alignItems: 'center', height: 26, background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)', padding: '0 6px', flex: 'none' };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0, position: 'relative' }}>
      <ViewToolbar variant="sub">
        <RobotoText label="Universe" fontSize={14} height={30} style={{ paddingLeft: 4 }} />
        {universeRows.length > SD_TABS_MAX
          ? <CustomComboBox width={160} height={26} currValue={universeId} onValueChanged={setUniverseId} model={universeModel} />
          : universeRows.map(u => (
            <MenuBarEntry key={u.id} entryText={u.name} checked={u.id === universeId} height={30} checkedColor="var(--toolbar-selection-sub)"
              onClick={() => setUniverseId(u.id)} title={'Universe id ' + u.id} />
          ))}
        {live && universes && !universes.length ? <RobotoText label="No universes" fontSize={14} labelColor="var(--fg-medium)" height={30} /> : null}
        <ShortcutHint keys="Ctrl R" placement="corner">
          <IconButton faSource="fa_xmark" faColor="var(--bg-control)" size={26} tooltip="Reset the whole universe" onClick={resetAll} />
        </ShortcutHint>
        <ToolbarSpacer />
        <RobotoText label={live ? (fixtures.length ? fixtures.length + ' fixtures · ' : 'no fixtures · ') + 'live DMX + desk overrides' : 'Offline — local preview'} fontSize={14}
          labelColor={live ? 'var(--check-lime)' : 'var(--fg-medium)'} height={30} />
        <RobotoText label={overriddenCount + ' overridden'} fontSize={14} height={30}
          labelColor={overriddenCount ? 'var(--override-red)' : 'var(--fg-light)'} />
        <IconButton imgSource={D.icon('network')} size={26}
          tooltip={live ? 'Refresh values from the desk' : 'Not connected'} disabled={!live}
          onClick={() => { const c = qlc.client(); if (c) c.getUniverseValues(universeId).catch(() => {}); }} />
        <IconButton imgSource={D.icon('dmxdump')} size={26} disabled={!live} onClick={() => setDumpOpen(true)}
          tooltip={live ? 'Dump DMX values to a scene' : 'Dump to scene — connect first'} />
        <DMXPercentageButton dmxMode={dmx} onClick={() => setDmx(!dmx)} height={26} />
      </ViewToolbar>

      {/* Top: the 512 channel strips */}
      <div ref={stripsRef} style={{ flex: '1 1 60%', minHeight: 220, overflowX: 'auto', overflowY: 'hidden', display: 'flex', alignItems: 'stretch', background: 'var(--bg-stronger)' }}>
        {live
          ? values.map((v, i) => (
            <Strip key={i} index={i} value={v} name={info.names[i] || ''} icon={info.icons[i] || undefined}
              display={info.display[i] || 'none'} isOverride={overrides[i]} dmx={dmx} marked={!!inSelected(i)}
              onValueChanged={setChannel} onReset={reset} onDebug={showDebug} />
          ))
          : D.channels.map((c, i) => (
            <Strip key={i} index={i} value={mockValues[i]} name={c.channelName} icon={c.channelIcon} display={c.display}
              isOverride={mockOverrides.indexOf(i) !== -1} dmx={dmx} marked={!!inSelected(i)}
              onValueChanged={setChannel} onReset={reset} onDebug={showDebug} />
          ))}
      </div>

      {/* Bottom: fixture list | commands history | keypad (SimpleDesk.qml's lower SplitView pane) */}
      <div style={{ flex: '1 1 40%', minHeight: 200, display: 'flex', borderTop: '2px solid var(--bg-lighter)', background: 'var(--bg-medium)' }}>
        <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', borderRight: '2px solid var(--bg-light)' }}>
          <div style={head}><RobotoText label="Fixture List" fontSize={14} fontBold height={26} /></div>
          <div style={{ flex: 1, minHeight: 0, overflow: 'auto' }}>
            {fixtures.map(f => (
              <div key={f.id} onClick={() => selectFixture(f)} data-fixture={f.id}
                style={{ display: 'flex', alignItems: 'center', height: 26, cursor: 'pointer', padding: '0 5px',
                  background: selFixture === f.id ? 'var(--highlight)' : 'transparent', borderBottom: '1px solid var(--bg-light)' }}>
                <IconTextEntry iSrc={f.icon || D.icon(window.QLCIcons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture')} tLabel={f.name} tFontSize={14} height={26} style={{ flex: 1, minWidth: 0 }} />
                <RobotoText label={(f.address + 1) + ' - ' + (f.address + f.channels)} fontSize={14} height={26} rightMargin={5} />
              </div>
            ))}
            {!fixtures.length ? <RobotoText label={live ? 'No fixtures in this universe' : 'No fixtures'} fontSize={14} labelColor="var(--fg-medium)" height={26} leftMargin={5} /> : null}
          </div>
        </div>
        <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', borderRight: '2px solid var(--bg-light)' }}>
          <div style={head}><RobotoText label="Commands history" fontSize={14} fontBold height={26} style={{ flex: 1 }} />
            <RobotoText label={serverKeypad ? 'server' : (live ? 'this tab' : '')} fontSize={12} labelColor="var(--fg-light)" height={26} title={serverKeypad ? 'Commands are evaluated by the QLC+ engine; the history is shared with the desktop and every other client' : 'Commands are parsed in this browser; the history is local to this tab'} data-role="history-source" />
          </div>
          <div style={{ flex: 1, minHeight: 0, overflow: 'auto' }}>
            {history.map((h, i) => (
              <div key={i} onClick={() => setCmd(h)} title="Load into the command line" data-history={i}
                style={{ height: 26, cursor: 'pointer', borderBottom: '1px solid var(--bg-light)', padding: '0 7px' }}>
                <RobotoText label={h} fontSize={14} height={26} />
              </div>
            ))}
            {!history.length ? <RobotoText label="No commands yet" fontSize={14} labelColor="var(--fg-medium)" height={26} leftMargin={7} /> : null}
          </div>
          <RobotoText label={cmdNote.text || window.QLCKeypadParser.HINT} fontSize={12} wrapText height="auto" leftMargin={7} rightMargin={7}
            labelColor={cmdNote.error ? 'var(--override-red)' : 'var(--fg-medium)'} style={{ flex: 'none', paddingBottom: 4 }} />
        </div>
        <div style={{ flex: 'none', padding: 4, display: 'flex', flexDirection: 'column' }}>
          <KeyPad commandString={cmd} onCommandChange={(t) => { setCmd(t); if (cmdNote.error) setCmdNote({ text: '', error: false }); }} onExecuteCommand={execute} itemHeight={30} style={{ flex: 'none' }} />
        </div>
      </div>

      <CustomPopupDialog open={dumpOpen} title="Dump DMX values to a scene" width={380}
        standardButtons={['Cancel', 'Dump']} onClicked={(b) => { if (b === 'Dump') dump(); else { setDumpOpen(false); setDumpNote(''); } }} onClose={() => setDumpOpen(false)}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Scene name" fontSize={14} style={{ width: 90 }} />
            <span style={{ flex: 1, height: 26, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
              <CustomTextInput text={dumpName} editing autoFocus placeholder="New Scene" width="100%" height={22} onTextConfirmed={setDumpName}
                onInput={(e) => setDumpName(e.target.value)} onKeyDown={(e) => { if (e.key === 'Enter') dump(); }} />
            </span>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <CustomCheckBox checked={dumpNonZero} onToggled={setDumpNonZero} />
            <RobotoText label="Only non-zero channels" fontSize={14} />
          </div>
          <RobotoText label="Creates a Scene from the current DMX output of every universe (io.simpleDesk.dump). The desktop's channel-group filter is not available here: all channel groups are dumped." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" />
          {dumpNote ? <RobotoText label={dumpNote} fontSize={12} labelColor="var(--override-red)" wrapText height="auto" /> : null}
        </div>
      </CustomPopupDialog>

      <CustomPopupDialog open={debugCh !== null} title="Channel value debug" width={420} standardButtons={['Ok']}
        onClicked={() => setDebugCh(null)} onClose={() => setDebugCh(null)}>
        {debugInfo ? (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, fontFamily: 'var(--font-mono)' }}>
            <RobotoText label={'Universe ' + (universeId + 1) + ' · channel ' + (debugCh + 1) + ' (address ' + (universeId * 512 + debugCh) + ')'} fontSize={14} height={22} />
            <RobotoText label={'Value: ' + debugInfo.value + ' (' + Math.round(debugInfo.value / 255 * 100) + '%)'} fontSize={14} height={22} />
            <RobotoText label={'Desk override: ' + (debugInfo.overridden ? 'yes — this value is held by Simple Desk' : 'no — value comes from playback / default')} fontSize={14} height={22}
              labelColor={debugInfo.overridden ? 'var(--override-red)' : 'var(--fg-main)'} />
            <RobotoText label={'Fixture: ' + (debugInfo.fixture ? debugInfo.fixture.name + ' (channel ' + (debugCh - debugInfo.fixture.address + 1) + ' of ' + debugInfo.fixture.channels + ')' : 'none patched here')} fontSize={14} height={22} />
            {debugInfo.name ? <RobotoText label={'Channel: ' + debugInfo.name} fontSize={14} height={22} /> : null}
            <RobotoText label="Not available in the web UI: the engine's full value trace (fader stack, pre/post Grand Master, last writer) is produced by the desktop app only." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" />
          </div>
        ) : null}
      </CustomPopupDialog>
    </div>
  );
}
Object.assign(window, { SimpleDesk });
