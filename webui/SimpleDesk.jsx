const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, ChannelStrip, KeyPad, CustomComboBox, DMXPercentageButton, ShortcutHint, GenericButton } = window.PatchDesignSystem_5432c9;

function SimpleDesk() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [values, setValues] = React.useState(D.channels.map(c => c.value));
  const [overrides, setOverrides] = React.useState([5]);
  const [dmx, setDmx] = React.useState(true);
  const [cmd, setCmd] = React.useState('');
  const [universe, setUniverse] = React.useState(1);

  const count = D.channels.length;
  const abs = (i) => ((universe - 1) * 512) + i + 1;
  /* Unlike the legacy API (which never pushed values and had to be polled), the Control
     API pushes io.simpleDesk.channelChanged on every change — getChannelsValues() below
     just seeds the initial state once, api/qlcplus-api.js keeps it live from there. */
  React.useEffect(() => {
    if (!qlc.online) return;
    const off = qlc.subscribeTo('channels', (rows) => {
      const base = (universe - 1) * 512;
      setValues(prev => {
        const next = prev.slice();
        rows.forEach(r => {
          const i = r.channel - base - 1;
          if (i >= 0 && i < next.length) next[i] = r.value;
        });
        return next;
      });
      setOverrides(rows.filter(r => r.overriding).map(r => r.channel - base - 1).filter(i => i >= 0 && i < count));
    });
    qlc.startPolling(universe, 1, count);
    return () => { off(); qlc.stopPolling(); };
  }, [qlc.online, universe, count]);

  const setChannel = (i, v) => {
    setValues(p => p.map((x, j) => j === i ? v : x));
    setOverrides(p => p.indexOf(i) === -1 ? p.concat([i]) : p);
    qlc.setChannel(abs(i), v);
  };
  /* Reset releases the manual override back to whatever the playback is doing — which is
     not the same as writing 0, so it needs sdResetChannel rather than CH. */
  const reset = (i) => {
    setValues(p => p.map((x, j) => j === i ? 0 : x));
    setOverrides(p => p.filter(x => x !== i));
    qlc.resetChannel(abs(i));
  };
  const resetAll = () => {
    setValues(D.channels.map(() => 0));
    setOverrides([]);
    qlc.resetUniverse(universe);
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <CustomComboBox width={150} height={26} currValue={universe} onValueChanged={setUniverse}
          model={D.universes.map(u => ({ mLabel: u.name, mValue: u.id }))} />
        <DMXPercentageButton dmxMode={dmx} onClick={() => setDmx(!dmx)} height={26} />
        <IconButton imgSource={D.icon('dmxdump')} size={26} tooltip="Dump DMX values to a scene" />
        <ShortcutHint keys="Ctrl R" placement="corner">
          <IconButton imgSource={D.icon('uncheck')} size={26} tooltip="Reset all channels" onClick={resetAll} />
        </ShortcutHint>
        <IconButton imgSource={D.icon('network')} size={26}
          tooltip={qlc.online ? 'Refresh values from the desk' : 'Not connected'}
          disabled={!qlc.online}
          onClick={() => { const c = qlc.client(); if (c) c.getChannelsValues(universe, 1, count); }} />
        <ToolbarSpacer />
        <RobotoText label={qlc.online ? 'Live — universe ' + universe : 'Offline — local preview'} fontSize={14}
          labelColor={qlc.online ? 'var(--check-lime)' : 'var(--fg-medium)'} />
        <RobotoText label={overrides.length + ' overridden'} fontSize={14}
          labelColor={overrides.length ? 'var(--override-red)' : 'var(--fg-light)'} />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ flex: 1, minWidth: 0, overflowX: 'auto', display: 'flex', alignItems: 'stretch', background: 'var(--bg-stronger)' }}>
          {D.channels.map((c, i) => (
            <ChannelStrip key={c.address} address={c.address} value={values[i]}
              channelIcon={c.channelIcon} channelName={c.channelName} display={c.display}
              isOverride={overrides.indexOf(i) !== -1} dmxValues={dmx}
              onValueChanged={v => setChannel(i, v)} onReset={() => reset(i)}
              style={{ height: '100%' }} />
          ))}
        </div>
        <div style={{ width: 250, minWidth: 250, borderLeft: 'var(--border-dark)', background: 'var(--bg-medium)', padding: 6, display: 'flex', flexDirection: 'column', gap: 6 }}>
          <KeyPad commandString={cmd} onCommandChange={setCmd} onExecuteCommand={() => setCmd('')} showTapButton />
          <GenericButton label="Dump to new scene" iconSource={D.icon('scene')} width="100%" />
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { SimpleDesk });
