const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, ChannelStrip, KeyPad, CustomComboBox, DMXPercentageButton, ShortcutHint, GenericButton, CustomPopupDialog, CustomTextInput, CustomCheckBox } = window.PatchDesignSystem_5432c9;

/** One strip; memoised so a single channel event re-renders one strip, not 512. */
const Strip = React.memo(function Strip({ index, value, name, icon, display, isOverride, dmx, onValueChanged, onReset }) {
  return (
    <ChannelStrip address={index + 1} value={value} channelIcon={icon} channelName={name} display={display}
      isOverride={isOverride} dmxValues={dmx}
      onValueChanged={(v) => onValueChanged(index, v)} onReset={() => onReset(index)}
      style={{ height: '100%' }} />
  );
});

/**
 * Keypad syntax (a subset of qmlui's SimpleDesk keypad, evaluated locally because the server has
 * no io.simpleDesk.sendKeypadCommand yet): "<ch> [THRU <ch>] [BY <step>] [+ <ch>…] @ <value|FULL>".
 * Channels are 1-based within the shown universe. Returns [{channel, value}] or null.
 */
function parseKeypad(cmd) {
  const m = /^(.+?)@\s*(FULL|\d+)\s*$/i.exec(cmd.trim());
  if (!m) return null;
  const value = /^full$/i.test(m[2]) ? 255 : Math.max(0, Math.min(255, Number(m[2])));
  const channels = [];
  for (const part of m[1].split('+')) {
    const r = /^\s*(\d+)(?:\s*THRU\s*(\d+))?(?:\s*BY\s*(\d+))?\s*$/i.exec(part);
    if (!r) return null;
    const a = Number(r[1]), b = r[2] ? Number(r[2]) : a, step = r[3] ? Number(r[3]) : 1;
    if (a < 1 || b < 1 || a > 512 || b > 512 || step < 1) return null;
    for (let c = Math.min(a, b); c <= Math.max(a, b); c += step) channels.push(c - 1);
  }
  return channels.map(channel => ({ channel, value }));
}

/**
 * Channel labels for one universe: fixtures.list gives every fixture's footprint straight away,
 * fixtures.get (cached per fixture id) fills in the channel names and groups a moment later.
 */
function useChannelInfo(qlc, universeId) {
  const [info, setInfo] = React.useState({ names: [], icons: [], display: [], fixtures: 0 });
  const cache = React.useRef({});
  React.useEffect(() => {
    if (!qlc.online) { setInfo({ names: [], icons: [], display: [], fixtures: 0 }); return; }
    let alive = true, timer = null;
    const D = window.QLCData;
    const build = (fixtures, details) => {
      const names = new Array(512).fill(''), icons = new Array(512).fill(null), display = new Array(512).fill('none');
      fixtures.slice().sort((a, b) => a.address - b.address).forEach((f, fi) => {
        const d = details[f.id];
        for (let c = 0; c < f.channels && f.address + c < 512; c++) {
          const ch = f.address + c;
          const chDef = d && d.channelList && d.channelList[c];
          names[ch] = chDef ? f.name + ' · ' + chDef.name : f.name + ' · ch ' + (c + 1);
          icons[ch] = chDef ? window.QLCIcons.channelIcon(chDef) : D.icon('other');
          display[ch] = fi % 2 ? 'even' : 'odd';
        }
      });
      return { names, icons, display, fixtures: fixtures.length };
    };
    const load = () => qlc.call('fixtures.list', { universe: universeId }).then(r => {
      const fixtures = (r.fixtures || []).filter(f => f.universe === universeId);
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
  const [cmdError, setCmdError] = React.useState('');
  const [dumpOpen, setDumpOpen] = React.useState(false);
  const [dumpName, setDumpName] = React.useState('');
  const [dumpNonZero, setDumpNonZero] = React.useState(true);
  const info = useChannelInfo(qlc, universeId);

  /* Universe list (live). */
  React.useEffect(() => {
    if (!live) { setUniverses(null); return; }
    let alive = true;
    const load = () => qlc.call('io.universe.list').then(r => { if (alive) setUniverses((r.universes || []).slice().sort((a, b) => a.id - b.id)); }).catch(() => {});
    load();
    const offs = ['io.universe.created', 'core.project.loaded'].map(t => qlc.subscribeTo(t, load));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [live]);

  /* Follow the shown universe: seed + live DMX stream + override events, re-armed after every reconnect. */
  React.useEffect(() => {
    if (!live) return;
    const client = qlc.client();
    if (!client) return;
    setValues(new Array(512).fill(0));
    setOverrides(new Array(512).fill(false));
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
  const execute = (text) => {
    const items = parseKeypad(text);
    if (!items) { setCmdError('Syntax: <ch> [THRU <ch>] [BY <n>] [+ <ch>…] @ <0-255|FULL>'); return; }
    setCmdError('');
    if (live) {
      qlc.client().setChannels(items.map(i => ({ address: universeId * 512 + i.channel, value: i.value })));
      setValues(p => { const n = p.slice(); items.forEach(i => { n[i.channel] = i.value; }); return n; });
      setOverrides(p => { const n = p.slice(); items.forEach(i => { n[i.channel] = true; }); return n; });
    } else {
      setMockValues(p => p.map((x, j) => { const hit = items.find(i => i.channel === j); return hit ? hit.value : x; }));
    }
    setCmd('');
  };
  const dump = () => {
    setDumpOpen(false);
    if (!live) return;
    qlc.call('io.simpleDesk.dump', { baseRevision: qlc.docRevision(), name: dumpName || undefined, nonZeroOnly: dumpNonZero, channelGroups: [] }).catch(() => {});
    setDumpName('');
  };

  const overriddenCount = live ? overrides.filter(Boolean).length : mockOverrides.length;
  const universeModel = live
    ? (universes || []).map(u => ({ mLabel: u.name, mValue: u.id }))
    : D.universes.map(u => ({ mLabel: u.name, mValue: u.id - 1 }));

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <CustomComboBox width={150} height={26} currValue={universeId} onValueChanged={setUniverseId} model={universeModel} />
        <DMXPercentageButton dmxMode={dmx} onClick={() => setDmx(!dmx)} height={26} />
        <IconButton imgSource={D.icon('dmxdump')} size={26} disabled={!live} onClick={() => setDumpOpen(true)}
          tooltip={live ? 'Dump DMX values to a new scene' : 'Dump to scene — connect first'} />
        <ShortcutHint keys="Ctrl R" placement="corner">
          <IconButton imgSource={D.icon('uncheck')} size={26} tooltip="Release every override in this universe" onClick={resetAll} />
        </ShortcutHint>
        <IconButton imgSource={D.icon('network')} size={26}
          tooltip={live ? 'Refresh values from the desk' : 'Not connected'} disabled={!live}
          onClick={() => { const c = qlc.client(); if (c) c.getUniverseValues(universeId).catch(() => {}); }} />
        <ToolbarSpacer />
        <RobotoText label={live ? 'Live — ' + (info.fixtures ? info.fixtures + ' fixtures patched' : 'no fixtures patched') + ' · DMX output + desk overrides' : 'Offline — local preview'} fontSize={14}
          labelColor={live ? 'var(--check-lime)' : 'var(--fg-medium)'} />
        <RobotoText label={overriddenCount + ' overridden'} fontSize={14}
          labelColor={overriddenCount ? 'var(--override-red)' : 'var(--fg-light)'} />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ flex: 1, minWidth: 0, overflowX: 'auto', display: 'flex', alignItems: 'stretch', background: 'var(--bg-stronger)' }}>
          {live
            ? values.map((v, i) => (
              <Strip key={i} index={i} value={v} name={info.names[i] || ''} icon={info.icons[i] || undefined}
                display={info.display[i] || 'none'} isOverride={overrides[i]} dmx={dmx}
                onValueChanged={setChannel} onReset={reset} />
            ))
            : D.channels.map((c, i) => (
              <Strip key={i} index={i} value={mockValues[i]} name={c.channelName} icon={c.channelIcon} display={c.display}
                isOverride={mockOverrides.indexOf(i) !== -1} dmx={dmx} onValueChanged={setChannel} onReset={reset} />
            ))}
        </div>
        <div style={{ width: 250, minWidth: 250, borderLeft: 'var(--border-dark)', background: 'var(--bg-medium)', padding: 6, display: 'flex', flexDirection: 'column', gap: 6 }}>
          <KeyPad commandString={cmd} onCommandChange={(t) => { setCmd(t); setCmdError(''); }} onExecuteCommand={execute} showTapButton />
          <RobotoText label={cmdError || 'e.g. 1 THRU 12 @ FULL · 5 + 9 @ 128'} fontSize={12} wrapText height="auto"
            labelColor={cmdError ? 'var(--override-red)' : 'var(--fg-medium)'} />
          <GenericButton label="Dump to new scene" iconSource={D.icon('scene')} width="100%" disabled={!live} onClick={() => setDumpOpen(true)} />
        </div>
      </div>

      <CustomPopupDialog open={dumpOpen} title="Dump DMX values to a scene" width={380}
        standardButtons={['Cancel', 'Dump']} onClicked={(b) => { if (b === 'Dump') dump(); else setDumpOpen(false); }} onClose={() => setDumpOpen(false)}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Scene name" fontSize={14} style={{ width: 90 }} />
            <span style={{ flex: 1, height: 26, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
              <CustomTextInput text={dumpName} editing autoFocus placeholder="New Scene" width="100%" height={22} onTextConfirmed={setDumpName}
                onKeyDown={(e) => { setTimeout(() => setDumpName(e.target.value), 0); if (e.key === 'Enter') dump(); }} />
            </span>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <CustomCheckBox checked={dumpNonZero} onToggled={setDumpNonZero} />
            <RobotoText label="Only non-zero channels" fontSize={14} />
          </div>
          <RobotoText label="Creates a Scene from the current DMX output of every universe (io.simpleDesk.dump)." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" />
        </div>
      </CustomPopupDialog>
    </div>
  );
}
Object.assign(window, { SimpleDesk, parseKeypad });
