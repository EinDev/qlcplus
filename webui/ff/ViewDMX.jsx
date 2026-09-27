/**
 * ViewDMX.jsx — the Fixtures & Functions "DMX View" (qmlui DMXView.qml / FixtureDMXItem.qml /
 * SettingsViewDMX.qml): one card per fixture, one column per channel showing the channel icon,
 * an optional address label and the live DMX value, lit up in proportion to the value.
 *
 * Live values come from the client's 'channels' stream. The Control API lets one client follow
 * only ONE universe live (client.watchUniverse), so with the universe filter on "All" every
 * universe is seeded once from io.dmx.universe.get and only the first universe keeps streaming.
 * Double-clicking a value opens a tiny inline editor writing through io.simpleDesk.setChannel
 * (a desk override, exactly like the channel tool of the Qt view).
 *
 * Registers window.QLCFFViews.dmx; the view switcher in FixturesFunctions.jsx mounts it with
 * { qlc, fixtures, universes, selectedFixtureIds, onSelectFixtures, universeFilter, setUniverseFilter }.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, CustomComboBox, CustomCheckBox } = window.PatchDesignSystem_5432c9;

  const COL_W = 34;           /* px per channel column (UISettings.iconSizeMedium in the Qt view) */
  const ALL = -1;             /* combo value for "no universe filter" */

  function valueText(v, percent) { return percent ? Math.round(v / 255 * 100) + '%' : String(v); }
  /* Tint proportional to the value; the colour is --highlight (#0978FF) as rgba, since CSS
     variables cannot carry an alpha channel. */
  function tint(v) { return 'rgba(9,120,255,' + (v / 255 * 0.85).toFixed(3) + ')'; }

  /** Inline slider + number editor for one channel value (opened by double-clicking a value). */
  function ValueEditor({ value, onChange, onClose }) {
    const [v, setV] = React.useState(value);
    const commit = (n) => { const c = Math.max(0, Math.min(255, Math.round(Number(n) || 0))); setV(c); onChange(c); };
    const keys = (e) => { if (e.key === 'Enter' || e.key === 'Escape') { e.preventDefault(); onClose(); } };
    return (
      <div data-ff-channel-editor="1" onClick={e => e.stopPropagation()} onDoubleClick={e => e.stopPropagation()}
        style={{ position: 'absolute', left: 0, top: '100%', zIndex: 5, display: 'flex', flexDirection: 'column', gap: 4, padding: 6,
          background: 'var(--bg-control)', border: 'var(--border-dark)', boxShadow: '0 2px 8px rgba(0,0,0,0.5)' }}>
        <input type="range" min={0} max={255} value={v} autoFocus onChange={e => commit(e.target.value)} onKeyDown={keys}
          title="Drag to set the channel value (desk override)" style={{ width: 120 }} />
        <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
          <input type="number" min={0} max={255} value={v} onChange={e => commit(e.target.value)} onKeyDown={keys}
            title="Channel value 0-255, Enter or Escape closes"
            style={{ width: 56, height: 22, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-mono)', fontSize: 13, padding: '0 4px' }} />
          <button type="button" onClick={onClose} title="Close the channel editor"
            style={{ height: 22, padding: '0 8px', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', cursor: 'pointer', fontFamily: 'var(--font-roboto)', fontSize: 12 }}>Close</button>
        </div>
      </div>
    );
  }

  /**
   * One fixture card. Memoised: it receives its universe's whole value array by reference, so it
   * re-renders only when that universe (or its own props) changed, not on every DMX delta elsewhere.
   */
  const FixtureCard = React.memo(function FixtureCard({ fixture: f, detail, values, selected, showAddresses, relative, percent, onSelect, onSetChannel }) {
    const D = window.QLCData;
    const Icons = window.QLCIcons;
    const [editing, setEditing] = React.useState(null); /* channel index with the editor open */
    const typeIcon = D.icon(Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture');
    const channels = [];
    for (let i = 0; i < f.channels; i++) channels.push(i);
    const list = (detail && detail.channelList) || [];
    return (
      <div data-fx-id={f.id} data-selected={selected ? '1' : '0'} onClick={(e) => onSelect(f, e)}
        title={f.name + '\nUniverse ' + (f.universe + 1) + ', address ' + (f.address + 1) + '-' + (f.address + f.channels) + '\nClick to select, Ctrl-click to add to the selection, double-click a value to set it'}
        style={{ flex: 'none', background: 'var(--bg-lighter)', border: selected ? '2px solid var(--selection)' : '2px solid var(--border-color-dark, #222)',
          cursor: 'pointer', boxSizing: 'border-box', userSelect: 'none' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, height: 22, padding: '0 4px', background: 'var(--bg-strong)', maxWidth: Math.max(f.channels * COL_W, 120), overflow: 'hidden' }}>
          <img src={typeIcon} alt="" style={{ width: 16, height: 16, flex: 'none' }} />
          <RobotoText label={f.name} fontSize={13} height={22} style={{ minWidth: 0, overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }} />
          <RobotoText label={'U' + (f.universe + 1) + '.' + (f.address + 1)} fontSize={12} height={22} labelColor="var(--fg-light)" style={{ flex: 'none', marginLeft: 'auto' }} />
        </div>
        <div style={{ display: 'flex' }}>
          {channels.map(i => {
            const ch = list[i];
            const v = (values && values[f.address + i]) || 0;
            const icon = ch ? Icons.channelIcon(ch) : D.icon('other');
            return (
              <div key={i} data-channel={i} style={{ width: COL_W, flex: 'none', borderRight: i < f.channels - 1 ? '1px solid #222' : 'none', position: 'relative' }}
                title={(ch ? ch.name : 'Channel ' + (i + 1)) + ' · DMX ' + (f.address + i + 1) + ' · ' + v}>
                <img src={icon} alt="" style={{ width: COL_W, height: COL_W, display: 'block' }} />
                {showAddresses ? <RobotoText label={String(relative ? i + 1 : f.address + i + 1)} fontSize={12} fontBold height={18} textHAlign="center" labelColor="#000" /> : null}
                <div data-value={v} onDoubleClick={(e) => { e.stopPropagation(); setEditing(i); }}
                  style={{ height: 20, background: tint(v), color: v > 140 ? '#fff' : '#000', fontFamily: 'var(--font-mono)', fontSize: 12, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                  {valueText(v, percent)}
                </div>
                {editing === i ? <ValueEditor value={v} onChange={(n) => onSetChannel(f, i, n)} onClose={() => setEditing(null)} /> : null}
              </div>
            );
          })}
        </div>
      </div>
    );
  });

  function ViewDMX({ qlc, fixtures, universes, selectedFixtureIds, onSelectFixtures, universeFilter, setUniverseFilter }) {
    const [showAddresses, setShowAddresses] = React.useState(false);
    const [relative, setRelative] = React.useState(false);
    const [percent, setPercent] = React.useState(false);
    /* {universeId: number[512]} */
    const [values, setValues] = React.useState({});

    const uniList = React.useMemo(() => (universes || []).slice().sort((a, b) => a.id - b.id), [universes]);
    const shown = React.useMemo(() => (fixtures || [])
      .filter(f => universeFilter == null || f.universe === universeFilter)
      .slice().sort((a, b) => a.universe - b.universe || a.address - b.address), [fixtures, universeFilter]);
    const ids = React.useMemo(() => shown.map(f => String(f.id)), [shown]);
    const details = FF.useFixtureDetails(qlc, ids);
    const selSet = React.useMemo(() => new Set((selectedFixtureIds || []).map(String)), [selectedFixtureIds]);

    /* Universes to follow: the filtered one, or every universe that has fixtures / exists. */
    const watchIds = React.useMemo(() => {
      if (universeFilter != null) return [universeFilter];
      const set = new Set(uniList.map(u => u.id));
      (fixtures || []).forEach(f => set.add(f.universe));
      return Array.from(set).sort((a, b) => a - b);
    }, [universeFilter, uniList, fixtures]);
    const watchKey = watchIds.join(',');

    /* Seed + live stream. Only one universe can be watched per client, so with "All" the first
       one streams and the others are seeded once. Re-armed after every reconnect (qlc.online). */
    React.useEffect(() => {
      if (!qlc.online) { setValues({}); return undefined; }
      const client = qlc.client();
      if (!client) return undefined;
      setValues({});
      const off = qlc.subscribeTo('channels', (rows) => {
        setValues(prev => {
          let next = null;
          rows.forEach(r => {
            if (!next) next = Object.assign({}, prev);
            if (!next[r.universeId] || next[r.universeId] === prev[r.universeId]) next[r.universeId] = (prev[r.universeId] || new Array(512).fill(0)).slice();
            next[r.universeId][r.channel] = r.value;
          });
          return next || prev;
        });
      });
      watchIds.slice(1).forEach(id => qlc.call('io.dmx.universe.get', { universeId: id }).then(r => {
        const vals = (r && r.values) || [];
        setValues(prev => Object.assign({}, prev, { [id]: new Array(512).fill(0).map((_, ch) => vals[ch] || 0) }));
      }).catch(() => {}));
      if (watchIds.length) client.watchUniverse(watchIds[0]).catch(() => {});
      return () => { off(); client.unwatchUniverse(); };
    }, [qlc.online, watchKey]);

    /* Both stable per selection / connection so the memoised cards do not re-render on every DMX delta. */
    const clickCard = React.useCallback((f, e) => {
      const id = String(f.id);
      if (e.ctrlKey || e.metaKey) onSelectFixtures(selSet.has(id) ? Array.from(selSet).filter(x => x !== id) : Array.from(selSet).concat([id]));
      else onSelectFixtures([id]);
    }, [selSet, onSelectFixtures]);
    const setChannel = React.useCallback((f, i, v) => {
      const client = qlc.client();
      if (!client || !client.connected()) return;
      const addr = FF.flatAddress(f, i);
      setValues(prev => { const arr = (prev[f.universe] || new Array(512).fill(0)).slice(); arr[f.address + i] = v; return Object.assign({}, prev, { [f.universe]: arr }); });
      FF.live('dmxview:' + addr, () => client.setChannel(addr, v));
    }, [qlc]);

    const uniModel = [{ mLabel: 'All universes', mValue: ALL }].concat(uniList.map(u => ({ mLabel: u.name, mValue: u.id })));
    const streaming = watchIds.length ? uniList.find(u => u.id === watchIds[0]) : null;
    const note = !qlc.online ? 'Not connected: no live values.'
      : universeFilter == null && watchIds.length > 1 ? 'Live stream: ' + (streaming ? streaming.name : 'Universe ' + (watchIds[0] + 1)) + ' only (one universe per client); the others show a snapshot. Pick a universe to stream it.'
      : '';
    const check = (label, checked, set, tip) => (
      <div style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
        <CustomCheckBox checked={checked} size={20} onToggled={set} tooltip={tip} />
        <RobotoText label={label} fontSize={13} height={20} title={tip} onClick={() => set(!checked)} style={{ cursor: 'pointer' }} />
      </div>
    );

    return (
      <div data-ff-view="dmx" style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column', background: 'var(--bg-medium)' }}>
        <div style={{ flex: 'none', height: 32, display: 'flex', alignItems: 'center', gap: 12, padding: '0 8px', background: 'var(--bg-strong)', borderBottom: '1px solid var(--bg-stronger)' }}>
          <CustomComboBox width={170} height={24} currValue={universeFilter == null ? ALL : universeFilter} model={uniModel}
            onValueChanged={(v) => setUniverseFilter(v === ALL ? null : v)} title="Universe filter" />
          {check('Show addresses', showAddresses, setShowAddresses, 'Show the DMX address under each channel icon')}
          {check('Relative addresses', relative, setRelative, 'Number channels 1..n within the fixture instead of within the universe')}
          <FF.Choice options={['DMX', '%']} value={percent ? '%' : 'DMX'} onChange={(o) => setPercent(o === '%')} labels={{ DMX: 'DMX (0-255)', '%': 'Percent' }} />
          <RobotoText label={shown.length + (shown.length === 1 ? ' fixture' : ' fixtures')} fontSize={13} height={24} labelColor="var(--fg-light)" style={{ marginLeft: 'auto' }} />
        </div>
        {note ? <div style={{ flex: 'none', padding: '2px 8px' }}><FF.Note text={note} /></div> : null}
        <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12 }}>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 5, alignItems: 'flex-start', alignContent: 'flex-start' }}>
            {shown.map(f => (
              <FixtureCard key={f.id} fixture={f} detail={details[String(f.id)] || null} values={values[f.universe] || null}
                selected={selSet.has(String(f.id))} showAddresses={showAddresses} relative={relative} percent={percent}
                onSelect={clickCard} onSetChannel={setChannel} />
            ))}
          </div>
          {!shown.length ? <FF.Note text={universeFilter == null ? 'No fixtures patched.' : 'No fixtures in this universe.'} /> : null}
        </div>
      </div>
    );
  }

  window.QLCFFViews = Object.assign(window.QLCFFViews || {}, { dmx: { id: 'dmx', icon: 'dmxview', label: 'DMX View', order: 20, component: ViewDMX } });
})();
