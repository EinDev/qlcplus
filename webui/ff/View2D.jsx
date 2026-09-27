/**
 * View2D.jsx — the Fixtures & Functions "2D View" (qmlui 2DView.qml / Fixture2DItem.qml /
 * SettingsView2D.qml / PopupArrangeFixtures.qml / PopupMonitor.qml / Position3DTool.qml /
 * ZoomItem.qml) for the web UI.
 *
 * Data: fixtures.monitor.get (stage settings + every fixture preview item, positions in mm,
 * rotations in degrees) through FF.useMonitor(), a store shared with the fixture properties
 * panel (FF.FixturePlacementProps, also here). Every placement change goes through
 * fixtures.monitor.setPlacement / arrange (the server runs the very same MonitorLayout code the
 * Qt UI runs), stage settings through fixtures.monitor.setStage, "Pick a 3D point" through
 * fixtures.monitor.aimAt. Head colours come from the live DMX stream of the watched universe
 * (one universe at a time, like the DMX view).
 *
 * Registers window.QLCFFViews['2d']; the switcher in FixturesFunctions.jsx mounts it with
 * { qlc, fixtures, universes, selectedFixtureIds, onSelectFixtures, universeFilter, setUniverseFilter }.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

  const POVS = ['TopView', 'FrontView', 'RightSideView', 'LeftSideView'];
  const POV_LABELS = { TopView: 'Top view', FrontView: 'Front view', RightSideView: 'Right side view', LeftSideView: 'Left side view' };
  const ALL = -1;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };

  /* ---- geometry (ports of engine/src/monitorlayout.cpp's projections) ------------------------ */
  const unitsMm = (stage) => stage && stage.gridUnits === 'Feet' ? 304.8 : 1000;
  const effectivePov = (stage) => (stage && POVS.indexOf(stage.pointOfView) !== -1) ? stage.pointOfView : 'TopView';
  /** Stage plane size in mm for the point of view. */
  function stageSize(stage) {
    const u = unitsMm(stage), g = stage.gridSize;
    switch (effectivePov(stage)) {
      case 'TopView': return { w: g.x * u, h: g.z * u };
      case 'RightSideView': case 'LeftSideView': return { w: g.z * u, h: g.y * u };
      default: return { w: g.x * u, h: g.y * u };
    }
  }
  function project(stage, pos) {
    const u = unitsMm(stage), g = stage.gridSize;
    switch (effectivePov(stage)) {
      case 'TopView': return { x: pos.x, y: pos.z };
      case 'RightSideView': return { x: g.x * u - pos.z, y: g.y * u - pos.y };
      case 'LeftSideView': return { x: pos.z, y: g.y * u - pos.y };
      default: return { x: pos.x, y: g.y * u - pos.y };
    }
  }
  /** Inverse of project(): the third axis is kept from prev. */
  function unproject(stage, pt, prev) {
    const u = unitsMm(stage), g = stage.gridSize;
    switch (effectivePov(stage)) {
      case 'TopView': return { x: pt.x, y: prev.y, z: pt.y };
      case 'RightSideView': return { x: prev.x, y: g.y * u - pt.y, z: g.x * u - pt.x };
      case 'LeftSideView': return { x: prev.x, y: g.y * u - pt.y, z: pt.x };
      default: return { x: pt.x, y: g.y * u - pt.y, z: prev.z };
    }
  }
  function rotation2D(stage, rot) {
    switch (effectivePov(stage)) { case 'TopView': return rot.y; case 'RightSideView': case 'LeftSideView': return rot.x; default: return rot.z; }
  }
  function size2D(stage, item) {
    const p = item.physical || {};
    const w = p.width || 300, h = p.height || 300, d = p.depth || 300;
    switch (effectivePov(stage)) { case 'TopView': return { w, h: d }; case 'RightSideView': case 'LeftSideView': return { w: d, h }; default: return { w, h }; }
  }
  /** Fixture2DItem.calculateHeadSize()'s layout guess. */
  function headLayout(w, h, heads) {
    if (heads <= 1) return { cols: 1, rows: 1 };
    const areaSqrt = Math.sqrt((w * h) / heads);
    let cols = Math.max(1, Math.round(w / areaSqrt)), rows = Math.max(1, Math.round(h / areaSqrt));
    if (rows === 1) cols = heads;
    if (cols === 1) rows = heads;
    if (cols > heads) cols = heads;
    return { cols, rows: Math.max(rows, Math.ceil(heads / cols)) };
  }
  const itemKey = (it) => it.fixtureId + ':' + (it.headIndex || 0) + ':' + (it.linkedIndex || 0);
  const keyOf = (it) => ({ fixtureId: String(it.fixtureId), headIndex: it.headIndex || 0, linkedIndex: it.linkedIndex || 0 });

  /* ---- individually selected heads (FixtureHeadDelegate.qml) ---------------------------------
     Alt-click on a head in the 2D view selects that head only; the live tools then write only the
     head's channels and the Fixture Groups panel assigns only those heads. Shared through FF so the
     tools panel (a sibling of the view) sees it: {keys: ['fid:head:linked'], byFixture: {fid:
     [head, ...]}, channels: {fid: [channel index, ...]}}. */
  const headStore = { value: { keys: [], byFixture: {}, channels: {} }, listeners: new Set() };
  FF.setHeadSelection = function (keys, headChannelsOf) {
    const byFixture = {}, channels = {};
    (keys || []).forEach(k => {
      const [fid, head] = k.split(':');
      const h = Number(head);
      (byFixture[fid] = byFixture[fid] || []).indexOf(h) === -1 && byFixture[fid].push(h);
      const chs = headChannelsOf ? headChannelsOf(fid, h) : null;
      (chs || []).forEach(c => { (channels[fid] = channels[fid] || []).indexOf(c) === -1 && channels[fid].push(c); });
    });
    headStore.value = { keys: (keys || []).slice(), byFixture, channels };
    headStore.listeners.forEach(fn => fn(headStore.value));
  };
  FF.useHeadSelection = function () {
    const [v, setV] = React.useState(headStore.value);
    React.useEffect(() => { headStore.listeners.add(setV); setV(headStore.value); return () => { headStore.listeners.delete(setV); }; }, []);
    return v;
  };
  /** Does item `it` belong to the head selection `keys` (true when none of its fixture's heads is picked)? */
  function headMatch(keys, it) {
    const fid = String(it.fixtureId), linked = it.linkedIndex || 0;
    const mine = keys.filter(k => k.split(':')[0] === fid);
    if (!mine.length) return true;
    return mine.some(k => { const p = k.split(':'); return Number(p[2]) === linked && ((it.heads || 1) > 1 || Number(p[1]) === (it.headIndex || 0)); });
  }

  /* ---- shared monitor store ------------------------------------------------------------------ */
  const stores = new WeakMap();
  function storeFor(client) {
    let s = stores.get(client);
    if (!s) { s = { data: null, loading: null, listeners: new Set() }; stores.set(client, s); }
    return s;
  }
  function emit(s) { s.listeners.forEach(fn => fn(s.data)); }
  function mergeItems(data, items, removed) {
    const map = new Map(data.items.map(it => [itemKey(it), it]));
    (removed || []).forEach(k => map.delete(itemKey(k)));
    (items || []).forEach(it => {
      /* A newly placed base item replaces the synthetic unplaced entry */
      map.set(itemKey(it), Object.assign({}, map.get(itemKey(it)) || {}, it));
    });
    return Object.assign({}, data, { items: Array.from(map.values()) });
  }
  /** Monitor state shared by the 2D view and the fixture properties panel. */
  FF.useMonitor = function (qlc) {
    const client = qlc.client();
    const [data, setData] = React.useState(() => (client ? storeFor(client).data : null));
    const load = React.useCallback(() => {
      const c = qlc.client();
      if (!c || !qlc.online) return Promise.resolve(null);
      const s = storeFor(c);
      if (!s.loading) s.loading = qlc.call('fixtures.monitor.get').then(r => { s.data = r; s.loading = null; emit(s); return r; }).catch(() => { s.loading = null; return null; });
      return s.loading;
    }, [qlc.online]);
    React.useEffect(() => {
      const c = qlc.client();
      if (!c || !qlc.online) { setData(null); return undefined; }
      const s = storeFor(c);
      s.listeners.add(setData);
      if (s.data) setData(s.data); else load();
      let timer = null;
      const reload = () => { clearTimeout(timer); timer = setTimeout(load, 120); };
      const offs = ['fixtures.patched', 'fixtures.unpatched', 'fixtures.updated', 'fixtures.remap.applied', 'core.project.loaded']
        .map(t => qlc.subscribeTo(t, reload));
      offs.push(qlc.subscribeTo('core.history.changed', (d) => { if (!FF.isOwnHistory(d)) reload(); }));
      offs.push(qlc.subscribeTo('fixtures.monitor.changed', (d) => {
        if (!s.data || !d) return;
        let next = s.data;
        if (d.stage) next = Object.assign({}, next, { stage: d.stage });
        if (d.items || d.removed) next = mergeItems(next, d.items, d.removed);
        if (d.docRevision != null) next = Object.assign({}, next, { docRevision: d.docRevision });
        s.data = next; emit(s);
      }));
      return () => { clearTimeout(timer); s.listeners.delete(setData); offs.forEach(f => f()); };
    }, [qlc.online, load]);
    /** Apply a setPlacement/arrange result (or an optimistic guess) locally. */
    const patch = React.useCallback((items, removed, stage) => {
      const c = qlc.client(); if (!c) return;
      const s = storeFor(c); if (!s.data) return;
      let next = mergeItems(s.data, items, removed);
      if (stage) next = Object.assign({}, next, { stage });
      s.data = next; emit(s);
    }, []);
    return { monitor: data, reload: load, patch };
  };
  /** setPlacement wrapper: revision-gated queue + local merge of the returned items. */
  FF.setPlacement = function (qlc, patch, items, opts) {
    return FF.mutate(qlc, 'fixtures.monitor.setPlacement', { items }, opts).then(r => { if (r && r.items) patch(r.items, items.filter(i => i.remove)); return r; });
  };

  /* ---- the properties block (FixtureProperties.qml's placement half, for the detail panel) ---- */
  function Vec3Row({ label, value, step = 10, onCommit, suffix }) {
    const axes = ['x', 'y', 'z'];
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 4, height: 26 }}>
        <RobotoText label={label} fontSize={13} labelColor="var(--fg-light)" style={{ width: 70, flex: 'none' }} height={26} />
        {axes.map(a => (
          <span key={a} style={{ display: 'inline-flex', alignItems: 'center', gap: 2 }}>
            <RobotoText label={a.toUpperCase()} fontSize={11} labelColor="var(--fg-medium)" height={26} />
            <input type="number" step={step} value={Math.round(value[a] * 10) / 10} title={label + ' ' + a.toUpperCase() + (suffix ? ' (' + suffix + ')' : '')} data-axis={a}
              onChange={e => { const n = Number(e.target.value); if (!isNaN(n)) onCommit(Object.assign({}, value, { [a]: n })); }}
              style={Object.assign({ width: 72 }, inputStyle)} />
          </span>
        ))}
      </div>
    );
  }
  function FlagCheck({ label, checked, onToggled, id }) {
    return (
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }} data-flag={id}>
        <CustomCheckBox checked={!!checked} size={16} onToggled={onToggled} />
        <RobotoText label={label} fontSize={13} height={22} />
      </span>
    );
  }
  /* ---- DMX-driven position / rotation (SettingsView2D.qml "DMX Position/Rotation") -------------
     Only for fixtures with Position X/Y/Z or Rotation X/Y/Z channels
     (ContextManager::selectedFixtureHasDmxTransform); the flags / scale / range live on the
     fixture's base item (fid, 0, 0), like ContextManager::setFixtureDmxTransformFlags(). */
  const DMX_GROUPS = ['Position X', 'Position Y', 'Position Z', 'Rotation X', 'Rotation Y', 'Rotation Z'];
  const hasDmxTransform = (det) => !!(det && (det.channelList || []).some(c => DMX_GROUPS.indexOf(c.group) !== -1));
  const DMX_FLAGS = [['invertPositionX', 'Invert Position X'], ['invertPositionY', 'Invert Position Y'], ['invertPositionZ', 'Invert Position Z'],
    ['invertRotationX', 'Invert Rotation X'], ['invertRotationY', 'Invert Rotation Y'], ['invertRotationZ', 'Invert Rotation Z']];
  function DmxTransformBox({ items, onWrite }) {
    if (!items.length) return null;
    const first = items[0];
    const all = (k) => items.every(it => it.flags && it.flags[k]);
    return (
      <div data-ff-dmx-transform="1" style={{ display: 'flex', flexDirection: 'column', gap: 4, paddingTop: 4 }}>
        <RobotoText label={'DMX Position/Rotation' + (items.length > 1 ? ' · ' + items.length + ' fixtures' : '')} fontBold fontSize={13} />
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, auto)', gap: '2px 14px', justifyContent: 'start' }}>
          {DMX_FLAGS.map(([k, l]) => <FlagCheck key={k} id={k} label={l} checked={all(k)} onToggled={v => onWrite({ [k]: v })} />)}
        </div>
        <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 10 }}>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }} data-dmx-field="rotationScale" title="Rotation scale: how far the Rotation channels turn the fixture (100% = the channel's full range)">
            <RobotoText label="Rotation scale" fontSize={13} labelColor="var(--fg-light)" />
            <CustomSpinBox value={Math.round((first.rotationScale != null ? first.rotationScale : 1) * 100)} from={10} to={1000} width={90} height={24} suffix="%" onValueModified={v => onWrite({ rotationScale: v / 100 })} data-dmx="rotationScale" />
          </span>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }} data-dmx-field="positionRange" title="Position range: the distance in metres the full Position channel range covers">
            <RobotoText label="Position range" fontSize={13} labelColor="var(--fg-light)" />
            <CustomSpinBox value={Math.round(first.positionRange != null ? first.positionRange : 800)} from={1} to={1000000} width={110} height={24} suffix="m" onValueModified={v => onWrite({ positionRange: v })} data-dmx="positionRange" />
          </span>
        </div>
      </div>
    );
  }

  function FixturePlacementProps({ qlc, fixtureId, fixtures }) {
    const { monitor, patch } = FF.useMonitor(qlc);
    const detMap = FF.useFixtureDetails(qlc, [String(fixtureId)]);
    if (!monitor) return <RobotoText label="Loading placement…" fontSize={13} labelColor="var(--fg-medium)" />;
    const items = monitor.items.filter(it => String(it.fixtureId) === String(fixtureId)).sort((a, b) => a.headIndex - b.headIndex || a.linkedIndex - b.linkedIndex);
    if (!items.length) return null;
    const others = (fixtures || []).filter(f => String(f.id) !== String(fixtureId));
    const write = (it, fields) => FF.setPlacement(qlc, patch, [Object.assign(keyOf(it), fields)], { key: 'place:' + itemKey(it) + ':' + Object.keys(fields).join(',') }).catch(() => {});
    const addLinked = () => {
      const base = items[0];
      const next = Math.max(0, ...items.map(i => i.linkedIndex || 0)) + 1;
      const pos = Object.assign({}, base.position, { x: base.position.x + (base.physical && base.physical.width || 300) + 100 });
      FF.setPlacement(qlc, patch, [{ fixtureId: String(fixtureId), headIndex: 0, linkedIndex: next, position: pos }]).catch(() => {});
    };
    return (
      <div data-ff-placement="1" style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <RobotoText label="Placement (2D / 3D view)" fontBold fontSize={14} style={{ marginTop: 8 }} />
        {items.map(it => (
          <div key={itemKey(it)} data-item={itemKey(it)} style={{ display: 'flex', flexDirection: 'column', gap: 3, padding: '4px 0', borderTop: it.linkedIndex ? 'var(--border-dark)' : 'none' }}>
            {it.linkedIndex ? (
              <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <RobotoText label={'Linked copy ' + it.linkedIndex + (it.headIndex ? ' · head ' + (it.headIndex + 1) : '')} fontSize={13} labelColor="var(--fg-light)" />
                <GenericButton label="Remove" width={70} height={22} onClick={() => write(it, { remove: true })} />
              </div>
            ) : (it.headIndex ? <RobotoText label={'Head ' + (it.headIndex + 1)} fontSize={13} labelColor="var(--fg-light)" /> : null)}
            {!it.placed ? <FF.Note text="Not placed yet: the first change creates the monitor entry at the stage centre." /> : null}
            <Vec3Row label="Position" suffix="mm" value={it.position} step={10} onCommit={v => write(it, { position: v })} />
            <Vec3Row label="Rotation" suffix="deg" value={it.rotation} step={5} onCommit={v => write(it, { rotation: v })} />
            <div style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26 }}>
              <RobotoText label="Gel colour" fontSize={13} labelColor="var(--fg-light)" style={{ width: 70, flex: 'none' }} height={26} />
              <input type="color" value={it.gelColor || '#ffffff'} title="Gel colour" data-gel="1" onChange={e => write(it, { gelColor: e.target.value })}
                style={{ width: 40, height: 24, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)', cursor: 'pointer' }} />
              <RobotoText label={it.gelColor || 'none'} fontSize={12} labelColor="var(--fg-medium)" height={26} />
              {it.gelColor ? <GenericButton label="Clear" width={50} height={22} onClick={() => write(it, { gelColor: null })} /> : null}
            </div>
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: 10 }}>
              <FlagCheck id="invertPan" label="Invert Pan" checked={it.flags.invertPan} onToggled={v => write(it, { invertPan: v })} />
              <FlagCheck id="invertTilt" label="Invert Tilt" checked={it.flags.invertTilt} onToggled={v => write(it, { invertTilt: v })} />
              <FlagCheck id="locked" label="Lock position" checked={it.flags.locked} onToggled={v => write(it, { locked: v })} />
              <FlagCheck id="hidden" label="Hidden in views" checked={it.flags.hidden} onToggled={v => write(it, { hidden: v })} />
            </div>
            {!it.headIndex && !it.linkedIndex && hasDmxTransform(detMap[String(fixtureId)]) ? <DmxTransformBox items={[it]} onWrite={fields => write(it, fields)} /> : null}
          </div>
        ))}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <GenericButton label="Add linked copy" width={120} height={24} onClick={addLinked} />
          <FF.Note text="A linked copy shows the same fixture a second time in the 2D / 3D views (e.g. a mirrored patch)." />
        </div>
        {others.length ? null : null}
      </div>
    );
  }
  FF.FixturePlacementProps = FixturePlacementProps;

  /* ---- live head colours ---------------------------------------------------------------------- */
  function useUniverseDmx(qlc, universeId, enabled) {
    const [values, setValues] = React.useState(null);
    React.useEffect(() => {
      if (!qlc.online || !enabled || universeId == null) { setValues(null); return undefined; }
      const client = qlc.client();
      if (!client) return undefined;
      const buf = new Uint8Array(512);
      let alive = true;
      const off = qlc.subscribeTo('channels', (rows) => {
        let touched = false;
        (rows || []).forEach(r => { if (r.universeId === universeId) { buf[r.channel] = r.value; touched = true; } });
        if (touched && alive) setValues(new Uint8Array(buf));
      });
      client.watchUniverse(universeId).catch(() => {});
      return () => { alive = false; off(); client.unwatchUniverse(); };
    }, [qlc.online, universeId, enabled]);
    return values;
  }
  /** Per head fill colour + alpha from the live values of the fixture's channels. */
  function headColour(item, detail, dmx) {
    if (!dmx || !detail || detail.universe == null) return null;
    const chans = (item.headChannels && item.headChannels[item.headIndex || 0]) || null;
    const list = detail.channelList || [];
    let dim = null, r = null, g = null, b = null, w = null, any = false;
    const use = (ch) => {
      const v = dmx[detail.address + ch.index];
      if (ch.group !== 'Intensity') return;
      any = true;
      if (!ch.colour) { dim = dim == null ? v : Math.max(dim, v); }
      else if (ch.colour === 'Red') r = v; else if (ch.colour === 'Green') g = v; else if (ch.colour === 'Blue') b = v; else if (ch.colour === 'White') w = v;
    };
    (chans ? list.filter(ch => chans.indexOf(ch.index) !== -1) : list).forEach(use);
    if (!any) return null;
    const hasRgb = r != null || g != null || b != null;
    let alpha = dim != null ? dim / 255 : (hasRgb ? 1 : 0);
    let colour = hasRgb ? [r || 0, g || 0, b || 0] : (w != null ? [w, w, w] : [255, 255, 255]);
    if (hasRgb && dim == null) alpha = Math.max(r || 0, g || 0, b || 0) / 255;
    if (hasRgb && dim == null) { const m = Math.max(1, r || 0, g || 0, b || 0); colour = colour.map(c => Math.round(c / m * 255)); }
    if (!hasRgb && item.gelColor) { const c = FF.parseHex(item.gelColor); if (c) colour = [c.r, c.g, c.b]; }
    return 'rgba(' + colour.join(',') + ',' + alpha.toFixed(3) + ')';
  }

  /* ---- dialogs -------------------------------------------------------------------------------- */
  function NumberField({ label, value, onChange, min = 0, max = 100000, step = 1, suffix, width = 90 }) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26 }}>
        <RobotoText label={label} fontSize={13} labelColor="var(--fg-light)" style={{ width: 120, flex: 'none' }} height={26} />
        <input type="number" value={value} min={min} max={max} step={step} title={label} onChange={e => { const n = Number(e.target.value); if (!isNaN(n)) onChange(n); }} style={Object.assign({ width }, inputStyle)} />
        {suffix ? <RobotoText label={suffix} fontSize={12} labelColor="var(--fg-medium)" height={26} /> : null}
      </div>
    );
  }
  /** PopupArrangeFixtures.qml: circle / grid / line, detect from placement, face centre. */
  function ArrangeDialog({ open, onClose, onApply, detect, count }) {
    const [mode, setMode] = React.useState('circle');
    const [diameter, setDiameter] = React.useState(2000);
    const [width, setWidth] = React.useState(2000);
    const [height, setHeight] = React.useState(2000);
    const [columns, setColumns] = React.useState(0);
    const [gridAngle, setGridAngle] = React.useState(0);
    const [length, setLength] = React.useState(2000);
    const [lineAngle, setLineAngle] = React.useState(0);
    const [detectOn, setDetectOn] = React.useState(false);
    const [lookAtCenter, setLookAtCenter] = React.useState(false);
    const applyDetected = () => detect().then(d => { if (!d) return; setDiameter(Math.round(d.circleDiameter)); setLength(Math.round(d.lineLength)); setLineAngle(Math.round(d.lineAngle * 10) / 10); }).catch(() => {});
    React.useEffect(() => { if (open && detectOn) applyDetected(); }, [open, detectOn, mode]);
    const apply = () => {
      if (mode === 'circle') onApply('circle', { diameter, lookAtCenter });
      else if (mode === 'grid') onApply('grid', { width, height, columns, angle: gridAngle });
      else onApply('line', { length, angle: lineAngle, lookAtCenter });
      onClose();
    };
    return (
      <CustomPopupDialog open={open} title="Arrange fixtures" width={420} standardButtons={['Cancel', 'Apply']} onClicked={(b) => { if (b === 'Apply') apply(); else onClose(); }} onClose={onClose}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }} data-ff-arrange="1">
          <RobotoText label={count + ' item' + (count === 1 ? '' : 's') + ' selected, laid out around their centroid in the current point of view'} fontSize={12} labelColor="var(--fg-light)" />
          <FF.Choice options={['circle', 'grid', 'line']} value={mode} onChange={setMode} labels={{ circle: 'Circle', grid: 'Grid', line: 'Line' }} />
          {mode !== 'grid' ? (
            <div style={{ display: 'flex', gap: 16 }}>
              <FlagCheck id="detect" label="Detect from placement" checked={detectOn} onToggled={setDetectOn} />
              <FlagCheck id="face" label="Face centre" checked={lookAtCenter} onToggled={setLookAtCenter} />
            </div>
          ) : null}
          {mode === 'circle' ? <NumberField label="Diameter" value={diameter} onChange={setDiameter} min={100} max={2000000} step={100} suffix="mm" /> : null}
          {mode === 'grid' ? <>
            <NumberField label="Width" value={width} onChange={setWidth} min={100} max={100000} step={100} suffix="mm" />
            <NumberField label="Height" value={height} onChange={setHeight} min={100} max={100000} step={100} suffix="mm" />
            <NumberField label="Columns (0 = auto)" value={columns} onChange={setColumns} min={0} max={64} />
            <NumberField label="Angle" value={gridAngle} onChange={setGridAngle} min={-180} max={180} suffix="deg" />
            <FF.Note text="The grid follows the selection's Fixture Group order when every fixture is in the same group, else DMX address order." />
          </> : null}
          {mode === 'line' ? <>
            <NumberField label="Length" value={length} onChange={setLength} min={100} max={2000000} step={100} suffix="mm" />
            <NumberField label="Angle" value={lineAngle} onChange={setLineAngle} min={-180} max={180} step={1} suffix="deg" />
          </> : null}
        </div>
      </CustomPopupDialog>
    );
  }
  /** PopupInputNumber.qml */
  function NumberDialog({ open, title, label, value, min, max, onClose, onApply, suffix }) {
    const [v, setV] = React.useState(value);
    React.useEffect(() => { if (open) setV(value); }, [open]);
    return (
      <CustomPopupDialog open={open} title={title || 'Enter a number'} width={340} standardButtons={['Cancel', 'OK']} onClicked={(b) => { if (b === 'OK') onApply(v); onClose(); }} onClose={onClose}>
        <NumberField label={label} value={v} onChange={setV} min={min} max={max} suffix={suffix} />
      </CustomPopupDialog>
    );
  }
  /** PopupInvertGroupSelection.qml */
  function InvertGroupsDialog({ open, groups, onClose, onApply }) {
    const [picked, setPicked] = React.useState([]);
    React.useEffect(() => { if (open) setPicked(groups.map(g => String(g.id))); }, [open]);
    return (
      <CustomPopupDialog open={open} title="Invert selection in group(s)" width={380} standardButtons={['Cancel', 'Invert']} onClicked={(b) => { if (b === 'Invert') onApply(picked); onClose(); }} onClose={onClose}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }} data-ff-invert-groups="1">
          <RobotoText label="The current selection spans more than one Fixture Group. Choose which group(s) to invert:" fontSize={13} labelColor="var(--fg-light)" />
          {groups.map(g => (
            <div key={g.id} style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <CustomCheckBox checked={picked.indexOf(String(g.id)) !== -1} size={16} onToggled={() => setPicked(p => p.indexOf(String(g.id)) === -1 ? p.concat([String(g.id)]) : p.filter(x => x !== String(g.id)))} />
              <RobotoText label={g.name} fontSize={13} height={22} />
            </div>
          ))}
        </div>
      </CustomPopupDialog>
    );
  }
  /** PopupMonitor.qml: initial point of view */
  function PovDialog({ open, onPick }) {
    return (
      <CustomPopupDialog open={open} title="2D view point of view" width={420} standardButtons={[]} onClose={() => onPick('TopView')}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }} data-ff-pov="1">
          <RobotoText label="Please select the initial point of view for your 2D preview" fontSize={13} />
          <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap' }}>
            {POVS.map(p => <GenericButton key={p} label={POV_LABELS[p]} width={110} height={26} onClick={() => onPick(p)} />)}
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- the view -------------------------------------------------------------------------------- */
  function View2D({ qlc, fixtures, universes, selectedFixtureIds, onSelectFixtures, universeFilter, setUniverseFilter }) {
    const D = window.QLCData, Icons = window.QLCIcons;
    const { monitor, patch, reload } = FF.useMonitor(qlc);
    const stage = monitor && monitor.stage;
    const [scale, setScale] = React.useState(null);       /* px per mm; null = fit */
    const headSel = FF.useHeadSelection().keys;           /* item keys "fid:head:linked" selected individually */
    const headChannelsOf = (fid, h) => { const it = monitor && monitor.items.find(i => String(i.fixtureId) === String(fid) && ((i.heads || 1) > 1 || (i.headIndex || 0) === h)); return it && it.headChannels ? it.headChannels[(it.heads || 1) > 1 ? h : (it.headIndex || 0)] || it.headChannels[h] || null : null; };
    const setHeadSel = (next) => { const keys = typeof next === 'function' ? next(headSel) : next; FF.setHeadSelection(keys, headChannelsOf); };
    const [drag, setDrag] = React.useState(null);          /* {startX, startY, dx, dy} in px */
    const [band, setBand] = React.useState(null);          /* {x0,y0,x1,y1} in mm */
    const [dlg, setDlg] = React.useState(null);            /* 'arrange' | 'rotate' | 'nth' | 'invert' | 'settings' | 'aim' */
    const [showGroups, setShowGroups] = React.useState(false);
    const [groups, setGroups] = React.useState([]);        /* [{id,name,fixtureIds}] */
    const [invertCandidates, setInvertCandidates] = React.useState([]);
    const [aimPoint, setAimPoint] = React.useState({ x: 0, y: 0, z: 0 });
    const [aimPick, setAimPick] = React.useState(false);
    const [status, setStatus] = React.useState('');
    const svgRef = React.useRef(null);
    const wrapRef = React.useRef(null);
    const selected = React.useMemo(() => new Set((selectedFixtureIds || []).map(String)), [(selectedFixtureIds || []).join(',')]);

    /* Items shown: not hidden, in the universe filter */
    const items = React.useMemo(() => {
      if (!monitor) return [];
      return monitor.items.filter(it => !(it.flags && it.flags.hidden) && (universeFilter == null || it.universe === universeFilter));
    }, [monitor, universeFilter]);
    const selectedItems = React.useMemo(() => items.filter(it => selected.has(String(it.fixtureId)) && headMatch(headSel, it)), [items, selected, headSel.join('|')]);
    /* A head of a fixture that is no longer selected (tree click, select all ...) drops out */
    React.useEffect(() => {
      const keep = headSel.filter(k => selected.has(k.split(':')[0]));
      if (keep.length !== headSel.length) setHeadSel(keep);
    }, [selected, headSel.join('|')]);
    const selectedKeys = React.useMemo(() => selectedItems.map(keyOf), [selectedItems]);

    /* Live colours for the watched universe */
    const watched = universeFilter != null ? universeFilter : (universes && universes.length ? universes[0].id : null);
    const dmx = useUniverseDmx(qlc, watched, !!monitor);
    const liveIds = React.useMemo(() => items.filter(it => it.universe === watched).map(it => String(it.fixtureId)).filter((v, i, a) => a.indexOf(v) === i), [items, watched]);
    const details = FF.useFixtureDetails(qlc, liveIds);
    /* Fixture details of the selection, for the DMX Position/Rotation settings */
    const selIds = React.useMemo(() => Array.from(new Set(selectedItems.map(it => String(it.fixtureId)))), [selectedItems]);
    const selDetails = FF.useFixtureDetails(qlc, selIds);

    /* Stage background picture: a file on the QLC+ host, fetched as bytes to draw it here */
    const [bgPick, setBgPick] = React.useState(false);
    const [bgUrl, setBgUrl] = React.useState(null);
    const bgPath = stage ? stage.backgroundImage || '' : '';
    React.useEffect(() => {
      setBgUrl(null);
      if (!bgPath || !qlc.online) return undefined;
      let alive = true;
      qlc.call('fixtures.monitor.getBackground').then(r => { if (alive && r && r.contentBase64) setBgUrl('data:' + r.mimeType + ';base64,' + r.contentBase64); })
        .catch(e => { if (alive) setStatus('Background: ' + ((e && e.message) || 'not readable')); });
      return () => { alive = false; };
    }, [bgPath, qlc.online]);

    /* Groups (for the overlay and "invert selection in groups") */
    const loadGroups = React.useCallback(() => {
      if (!qlc.online) return Promise.resolve([]);
      return qlc.call('fixtures.group.list').then(r => Promise.all(((r && r.groups) || []).map(g => qlc.call('fixtures.group.get', { groupId: String(g.id) }).then(d => ({ id: String(g.id), name: g.name, fixtureIds: Array.from(new Set((d.heads || []).map(h => String(h.fixtureId)))) })))))
        .then(list => { setGroups(list); return list; }).catch(() => []);
    }, [qlc.online]);
    React.useEffect(() => { if (showGroups) loadGroups(); }, [showGroups, loadGroups]);
    React.useEffect(() => {
      if (!qlc.online || !showGroups) return undefined;
      const offs = ['fixtures.group.created', 'fixtures.group.deleted', 'fixtures.group.updated', 'fixtures.group.renamed'].map(t => qlc.subscribeTo(t, () => loadGroups()));
      return () => offs.forEach(f => f());
    }, [qlc.online, showGroups, loadGroups]);

    const liveRef = React.useRef({});
    /* Layout effect: the window listeners must exist before the next input event (mousedown is a
       discrete event, so its render and layout effects flush synchronously; a passive effect could
       miss a quick mouseup and leave the drag stuck). */
    React.useLayoutEffect(() => {
      if (!drag && !band) return undefined;
      const L = () => liveRef.current;
      const move = (e) => {
        if (drag) setDrag(d => Object.assign({}, d, { dx: e.clientX - d.startX, dy: e.clientY - d.startY }));
        if (band) { const p = L().toMm(e); setBand(b => Object.assign({}, b, { x1: p.x, y1: p.y })); }
      };
      const up = (e) => {
        if (drag) {
          const dx = (e.clientX - drag.startX) / L().pxPerMm, dy = (e.clientY - drag.startY) / L().pxPerMm;
          const { items, stage, headSel, patch, commitPlacement } = L();
          if (Math.abs(e.clientX - drag.startX) > 2 || Math.abs(e.clientY - drag.startY) > 2) {
            const moving = items.filter(it => drag.sel.indexOf(String(it.fixtureId)) !== -1 && !(it.flags && it.flags.locked) && headMatch(headSel, it));
            const list = moving.map(it => { const p2 = project(stage, it.position); return Object.assign(keyOf(it), { position: unproject(stage, { x: p2.x + dx, y: p2.y + dy }, it.position) }); });
            if (list.length) { patch(list.map(l => { const it = moving.find(m => itemKey(m) === itemKey(l)); return Object.assign({}, it, { position: l.position, placed: true }); })); commitPlacement(list, 'drag'); }
          }
          setDrag(null);
        }
        if (band) {
          const b = band; setBand(null);
          const x0 = Math.min(b.x0, b.x1), x1 = Math.max(b.x0, b.x1), y0 = Math.min(b.y0, b.y1), y1 = Math.max(b.y0, b.y1);
          if (x1 - x0 < 5 && y1 - y0 < 5) { if (!b.add) { L().onSelectFixtures([]); setHeadSel([]); } return; }
          const { items, stage, onSelectFixtures, selectedFixtureIds } = L();
          const hit = items.filter(it => { const p = project(stage, it.position), s = size2D(stage, it); return p.x < x1 && p.x + s.w > x0 && p.y < y1 && p.y + s.h > y0; }).map(it => String(it.fixtureId));
          const ids = Array.from(new Set(b.add ? (selectedFixtureIds || []).map(String).concat(hit) : hit));
          setHeadSel([]);
          onSelectFixtures(ids);
        }
      };
      window.addEventListener('mousemove', move); window.addEventListener('mouseup', up);
      return () => { window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up); };
    }, [drag, band]);

    if (!qlc.online) return <div style={{ padding: 20 }}><RobotoText label="Connect to a QLC+ instance to see the 2D view." fontSize={14} labelColor="var(--fg-medium)" /></div>;
    if (!monitor) return <div style={{ padding: 20 }}><RobotoText label="Loading placement…" fontSize={14} labelColor="var(--fg-medium)" /></div>;

    const size = stageSize(stage);
    /* The canvas covers the stage plus every item, so fixtures placed off the grid (the Qt view
       allows that) stay visible and clickable instead of being clipped by the <svg>. */
    const vb = (() => {
      let x0 = 0, y0 = 0, x1 = size.w, y1 = size.h;
      items.forEach(it => { const p = project(stage, it.position), s2 = size2D(stage, it); x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y); x1 = Math.max(x1, p.x + s2.w); y1 = Math.max(y1, p.y + s2.h); });
      const pad = unitsMm(stage) / 2;
      if (x0 < 0) x0 -= pad; if (y0 < 0) y0 -= pad; if (x1 > size.w) x1 += pad; if (y1 > size.h) y1 += pad;
      return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
    })();
    const u = unitsMm(stage);
    const fitScale = () => { const el = wrapRef.current; if (!el) return 0.1; return Math.max(0.01, Math.min((el.clientWidth - 20) / size.w, (el.clientHeight - 20) / size.h)); };
    const pxPerMm = scale || fitScale();
    const zoomBy = (f) => setScale(Math.max(0.005, Math.min(5, pxPerMm * f)));

    /* ---- server writes ---- */
    const commitPlacement = (list, key) => FF.setPlacement(qlc, patch, list, key ? { key } : undefined).catch(() => {});
    const arrange = (op, args) => {
      if (!selectedKeys.length) return;
      FF.mutate(qlc, 'fixtures.monitor.arrange', { items: selectedKeys, op, args: args || {} }).then(r => {
        if (r && r.items) patch(r.items);
        setStatus(op + ': ' + (r && r.items ? r.items.length : 0) + ' item(s)' + (r && r.skippedLocked && r.skippedLocked.length ? ', ' + r.skippedLocked.length + ' locked skipped' : ''));
      }).catch(e => setStatus(op + ' failed: ' + (e && e.message)));
    };
    const detect = () => qlc.call('fixtures.monitor.detectArrangement', { items: selectedKeys });
    const setStage = (fields) => FF.mutate(qlc, 'fixtures.monitor.setStage', fields).then(() => reload()).catch(() => {});
    const setGel = (colour) => { if (selectedItems.length) commitPlacement(selectedItems.map(it => Object.assign(keyOf(it), { gelColor: colour })), 'gel'); };
    const aimAt = (pt) => {
      if (!selectedKeys.length) { setStatus('Aim: select fixtures with Pan/Tilt first'); return; }
      qlc.call('fixtures.monitor.aimAt', { items: selectedKeys, point: pt }).then(r => {
        setStatus('Aimed ' + ((r && r.fixtures) || []).length + ' fixture(s) at ' + (pt.x / u).toFixed(2) + ' / ' + (pt.y / u).toFixed(2) + ' / ' + (pt.z / u).toFixed(2) + (stage.gridUnits === 'Feet' ? ' ft' : ' m') + ' (' + ((r && r.channels) || []).length + ' channels written)');
      }).catch(e => setStatus('Aim failed: ' + (e && e.message)));
    };

    /* ---- selection tools ---- */
    const allIds = Array.from(new Set(items.map(it => String(it.fixtureId))));
    const selectAll = () => { setHeadSel([]); onSelectFixtures(allIds.every(id => selected.has(id)) ? [] : allIds); };
    const keepEvery = (n, offset) => { const ids = (selectedFixtureIds || []).map(String); onSelectFixtures(ids.filter((id, i) => (i - offset) % n === 0 && i >= offset)); };
    const invertInGroups = () => {
      loadGroups().then(list => {
        const cands = list.filter(g => g.fixtureIds.some(id => selected.has(id)));
        if (!cands.length) { setStatus('Invert selection: no Fixture Group contains a selected fixture'); return; }
        if (cands.length === 1) applyInvert(cands, cands.map(g => g.id));
        else { setInvertCandidates(cands); setDlg('invert'); }
      });
    };
    const applyInvert = (cands, pickedIds) => {
      const out = new Set();
      cands.filter(g => pickedIds.indexOf(g.id) !== -1).forEach(g => g.fixtureIds.forEach(id => { if (!selected.has(id)) out.add(id); }));
      setHeadSel([]);
      onSelectFixtures(Array.from(out));
    };

    /* ---- mouse ---- */
    const toMm = (e) => {
      const r = svgRef.current.getBoundingClientRect();
      return { x: (e.clientX - r.left) / pxPerMm + vb.x, y: (e.clientY - r.top) / pxPerMm + vb.y };
    };
    const onItemDown = (e, it) => {
      e.stopPropagation(); e.preventDefault();
      if (aimPick) return;
      const id = String(it.fixtureId);
      const add = e.ctrlKey || e.metaKey || e.shiftKey;
      if (e.altKey && (it.headIndex || it.linkedIndex || (it.heads || 1) > 1)) {
        /* Alt-click: toggle this head / linked copy individually (FixtureHeadDelegate.qml). On a
           multi-head item the head is the circle under the pointer. */
        const circle = e.target && e.target.closest ? e.target.closest('circle[data-head]') : null;
        const h = (it.heads || 1) > 1 ? (circle ? Number(circle.getAttribute('data-head')) : 0) : (it.headIndex || 0);
        const k = it.fixtureId + ':' + h + ':' + (it.linkedIndex || 0);
        setHeadSel(h => h.indexOf(k) === -1 ? h.concat([k]) : h.filter(x => x !== k));
        if (!selected.has(id)) onSelectFixtures((selectedFixtureIds || []).map(String).concat([id]));
        return;
      }
      let nextSel;
      if (add) nextSel = selected.has(id) ? (selectedFixtureIds || []).map(String).filter(x => x !== id) : (selectedFixtureIds || []).map(String).concat([id]);
      else if (!selected.has(id)) nextSel = [id];
      else nextSel = (selectedFixtureIds || []).map(String);
      if (!add) setHeadSel(h => h.filter(k => k.split(':')[0] !== id));
      if (nextSel.join(',') !== (selectedFixtureIds || []).map(String).join(',')) onSelectFixtures(nextSel);
      if (nextSel.indexOf(id) !== -1 && e.button === 0) setDrag({ startX: e.clientX, startY: e.clientY, dx: 0, dy: 0, sel: nextSel });
    };
    const onBgDown = (e) => {
      if (e.button !== 0) return;
      const p = toMm(e);
      if (aimPick) { setAimPick(false); const pos = unproject(stage, p, { x: aimPoint.x, y: aimPoint.y, z: aimPoint.z }); setAimPoint(pos); aimAt(pos); return; }
      setBand({ x0: p.x, y0: p.y, x1: p.x, y1: p.y, add: e.ctrlKey || e.metaKey || e.shiftKey });
    };
    liveRef.current = { toMm, pxPerMm, items, stage, headSel, commitPlacement, patch, onSelectFixtures, selectedFixtureIds };
    const onWheel = (e) => { if (e.ctrlKey) { e.preventDefault(); zoomBy(e.deltaY < 0 ? 1.2 : 1 / 1.2); } };

    /* ---- render helpers ---- */
    const dragOffsetMm = drag && (Math.abs(drag.dx) > 2 || Math.abs(drag.dy) > 2) ? { x: drag.dx / pxPerMm, y: drag.dy / pxPerMm } : null;
    const gridLines = [];
    for (let x = 0; x <= size.w + 0.5; x += u) gridLines.push(<line key={'v' + x} x1={x} y1={0} x2={x} y2={size.h} stroke="#3c3c3c" strokeWidth={Math.max(1, 0.8 / pxPerMm)} />);
    for (let y = 0; y <= size.h + 0.5; y += u) gridLines.push(<line key={'h' + y} x1={0} y1={y} x2={size.w} y2={y} stroke="#3c3c3c" strokeWidth={Math.max(1, 0.8 / pxPerMm)} />);
    const font = Math.max(10 / pxPerMm, 60);
    const sel1 = selectedItems.length === 1 ? selectedItems[0] : null;
    const unitLabel = stage.gridUnits === 'Feet' ? 'ft' : 'm';
    const universeModel = [{ mLabel: 'All universes', mValue: ALL }].concat((universes || []).map(un => ({ mLabel: un.name, mValue: un.id })));
    const groupBoxes = showGroups ? groups.map(g => {
      const members = items.filter(it => g.fixtureIds.indexOf(String(it.fixtureId)) !== -1);
      if (!members.length) return null;
      let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
      members.forEach(it => { const p = project(stage, it.position), s = size2D(stage, it); x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y); x1 = Math.max(x1, p.x + s.w); y1 = Math.max(y1, p.y + s.h); });
      const m = u / 10;
      return <g key={'g' + g.id} data-group={g.id}><rect x={x0 - m} y={y0 - m} width={x1 - x0 + 2 * m} height={y1 - y0 + 2 * m} fill="none" stroke="#e0a030" strokeDasharray={(u / 20) + ' ' + (u / 30)} strokeWidth={Math.max(1, 1.5 / pxPerMm)} /><text x={x0 - m} y={y0 - m - font * 0.3} fontSize={font} fill="#e0a030" fontFamily="var(--font-roboto)">{g.name}</text></g>;
    }) : null;

    const isGlyph = (icon) => icon.startsWith('fa_') || icon.length === 1;
    const btn = (icon, tip, onClick, extra) => <IconButton imgSource={isGlyph(icon) ? undefined : D.icon(icon)} faSource={isGlyph(icon) ? icon : undefined} size={26} tooltip={tip} onClick={onClick} {...(extra || {})} />;
    const sep = <div style={{ width: 1, height: 20, background: 'var(--border-color-dark)', margin: '0 3px' }} />;

    return (
      <div data-ff-view="2d" style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column', background: 'var(--bg-medium)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 2, padding: '2px 6px', height: 32, background: 'var(--bg-strong)', borderBottom: 'var(--border-dark)', flex: 'none', flexWrap: 'nowrap', overflow: 'hidden' }}>
          <CustomComboBox width={130} currValue={universeFilter == null ? ALL : universeFilter} model={universeModel} onValueChanged={v => setUniverseFilter(v === ALL ? null : v)} />
          {btn('configure', 'Stage settings: size, units, point of view, labels, groups overlay, background', () => setDlg(dlg === 'settings' ? null : 'settings'), { checked: dlg === 'settings', 'data-tool': 'settings' })}
          {sep}
          {btn(FF.GLYPH.minus, 'Zoom out', () => zoomBy(1 / 1.25), { 'data-tool': 'zoom-out' })}
          <RobotoText label={Math.round(pxPerMm * u) + ' px/' + unitLabel} fontSize={12} labelColor="var(--fg-light)" style={{ width: 62, textAlign: 'center' }} data-zoom="1" />
          {btn('fa_plus', 'Zoom in', () => zoomBy(1.25), { 'data-tool': 'zoom-in' })}
          {btn('resize', 'Fit the stage into the view', () => setScale(null), { 'data-tool': 'zoom-fit' })}
          {sep}
          {btn('selectall', 'Select / deselect all fixtures in the view', selectAll, { 'data-tool': 'select-all' })}
          <span title="Keep every odd fixture of the selection (1st, 3rd, ...)" data-tool="select-odd"><GenericButton label="odd" width={34} height={24} disabled={!selected.size} onClick={() => keepEvery(2, 0)} /></span>
          <span title="Keep every even fixture of the selection (2nd, 4th, ...)" data-tool="select-even"><GenericButton label="even" width={38} height={24} disabled={!selected.size} onClick={() => keepEvery(2, 1)} /></span>
          <span title="Keep every Nth fixture of the selection…" data-tool="select-nth"><GenericButton label="Nth" width={34} height={24} disabled={!selected.size} onClick={() => setDlg('nth')} /></span>
          {btn('group', 'Invert selection in group(s) (select the group members that are not selected)', invertInGroups, { disabled: !selected.size, 'data-tool': 'invert-groups' })}
          {sep}
          {btn('align-left', 'Align the selected items to the left (of the first selected)', () => arrange('align', { edge: 'left' }), { disabled: selectedItems.length < 2, 'data-tool': 'align-left' })}
          {btn('align-top', 'Align the selected items to the top (of the first selected)', () => arrange('align', { edge: 'top' }), { disabled: selectedItems.length < 2, 'data-tool': 'align-top' })}
          {btn('distribute-x', 'Equally distribute the selected items horizontally', () => arrange('distribute', { direction: 'horizontal' }), { disabled: selectedItems.length < 3, 'data-tool': 'distribute-h' })}
          {btn('distribute-y', 'Equally distribute the selected items vertically', () => arrange('distribute', { direction: 'vertical' }), { disabled: selectedItems.length < 3, 'data-tool': 'distribute-v' })}
          {btn('grid', 'Arrange the selected items in a circle, grid or line…', () => setDlg('arrange'), { disabled: !selectedItems.length, 'data-tool': 'arrange' })}
          {btn(FF.GLYPH.rotateLeft, 'Rotate the selected items around their centroid…', () => setDlg('rotate'), { disabled: !selectedItems.length, 'data-tool': 'rotate' })}
          {btn(FF.GLYPH.crosshairs, 'Move the selected items to the stage centre', () => arrange('center', {}), { disabled: !selectedItems.length, 'data-tool': 'center' })}
          {sep}
          <input type="color" title="Gel colour of the selected fixtures" data-tool="gel" disabled={!selectedItems.length} value={(sel1 && sel1.gelColor) || '#ffffff'} onChange={e => setGel(e.target.value)}
            style={{ width: 30, height: 24, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)', cursor: 'pointer' }} />
          {btn(FF.GLYPH.xmark, 'Clear the gel colour of the selected fixtures', () => setGel(null), { disabled: !selectedItems.length, 'data-tool': 'gel-clear' })}
          {sep}
          {btn('3dpoint', 'Pick a 3D point: aim the selected moving heads at a stage position', () => setDlg(dlg === 'aim' ? null : 'aim'), { checked: dlg === 'aim' || aimPick, 'data-tool': 'aim' })}
          <div style={{ flex: 1 }} />
          <RobotoText label={status || (selectedItems.length ? selectedItems.length + ' item' + (selectedItems.length === 1 ? '' : 's') + ' selected' + (headSel.length ? ' (' + headSel.length + ' head' + (headSel.length === 1 ? '' : 's') + ')' : '') + (sel1 ? ' · ' + sel1.name + ' @ ' + (sel1.position.x / u).toFixed(2) + ' / ' + (sel1.position.y / u).toFixed(2) + ' / ' + (sel1.position.z / u).toFixed(2) + ' ' + unitLabel : '') : items.length + ' fixture items · ' + POV_LABELS[effectivePov(stage)] + (stage.pointOfView === 'Undefined' ? ' (not chosen yet)' : ''))}
            fontSize={12} labelColor="var(--fg-light)" style={{ maxWidth: 520, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} data-status="1" />
        </div>

        {dlg === 'settings' ? (
          <div data-ff-2d-settings="1" style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 10, padding: '4px 8px', background: 'var(--bg-stronger)', borderBottom: 'var(--border-dark)', flex: 'none' }}>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}><RobotoText label="Point of view" fontSize={13} labelColor="var(--fg-light)" />
              <CustomComboBox width={140} currValue={effectivePov(stage)} model={POVS.map(p => ({ mLabel: POV_LABELS[p], mValue: p }))} onValueChanged={v => setStage({ pointOfView: v })} data-stage="pov" /></span>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}><RobotoText label="Units" fontSize={13} labelColor="var(--fg-light)" />
              <CustomComboBox width={90} currValue={stage.gridUnits} data-stage="units" model={[{ mLabel: 'Meters', mValue: 'Meters' }, { mLabel: 'Feet', mValue: 'Feet' }]} onValueChanged={v => {
                /* Like SettingsView2D.qml: convert the size so the stage keeps its physical extent */
                const f = v === 'Feet' ? 3.28084 : 1 / 3.28084;
                if (v !== stage.gridUnits) setStage({ gridUnits: v, gridSize: { x: Math.round(stage.gridSize.x * f * 100) / 100, y: Math.round(stage.gridSize.y * f * 100) / 100, z: Math.round(stage.gridSize.z * f * 100) / 100 } });
              }} /></span>
            {['x', 'y', 'z'].map((a, i) => (
              <span key={a} style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}><RobotoText label={['Width', 'Height', 'Depth'][i]} fontSize={13} labelColor="var(--fg-light)" />
                <CustomSpinBox value={Math.round(stage.gridSize[a])} from={1} to={1000} width={70} height={24} suffix={unitLabel} onValueModified={v => setStage({ gridSize: Object.assign({}, stage.gridSize, { [a]: v }) })} data-stage-size={a} /></span>
            ))}
            <FlagCheck id="labels" label="Show labels" checked={stage.showLabels} onToggled={v => setStage({ showLabels: v })} />
            <FlagCheck id="groups" label="Show fixture groups" checked={showGroups} onToggled={setShowGroups} />
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }} data-ff-background="1">
              <RobotoText label={'Background: ' + (stage.backgroundImage ? stage.backgroundImage.split(/[\\/]/).pop() : 'none')} fontSize={12} labelColor="var(--fg-medium)" style={{ maxWidth: 220, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} title={stage.backgroundImage || ''} />
              <GenericButton label="Pick…" width={56} height={22} disabled={!FF.ServerFileBrowser} onClick={() => setBgPick(true)} />
              <GenericButton label="Reset" width={50} height={22} disabled={!stage.backgroundImage} onClick={() => setStage({ backgroundImage: '' })} />
            </span>
            <FF.Note text="The background picture is a file on the QLC+ machine (the browser file picker browses that machine; the Project place is the project's folder). The 3D stage type is edited in the desktop 3D view." />
            {(() => {
              const base = selIds.filter(id => hasDmxTransform(selDetails[id]))
                .map(id => monitor.items.find(i => String(i.fixtureId) === id && !i.headIndex && !i.linkedIndex)).filter(Boolean);
              return base.length ? (
                <div style={{ flexBasis: '100%' }}>
                  <DmxTransformBox items={base} onWrite={fields => commitPlacement(base.map(it => Object.assign(keyOf(it), fields)), 'dmxt:' + Object.keys(fields).join(','))} />
                </div>
              ) : null;
            })()}
          </div>
        ) : null}
        {FF.ServerFileBrowser && bgPick ? <FF.ServerFileBrowser open qlc={qlc} title="2D view background picture"
          filters={[FF.ServerFileBrowser.filter('Pictures', ['*.png', '*.jpg', '*.jpeg', '*.bmp', '*.gif', '*.svg', '*.webp']), FF.ServerFileBrowser.filter('All files', [])]}
          onClose={() => setBgPick(false)} onPick={p => setStage({ backgroundImage: p })} /> : null}

        {dlg === 'aim' ? (
          <div data-ff-aim="1" style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 8, padding: '4px 8px', background: 'var(--bg-stronger)', borderBottom: 'var(--border-dark)', flex: 'none' }}>
            <RobotoText label={'Aim point (' + unitLabel + ')'} fontSize={13} labelColor="var(--fg-light)" />
            {['x', 'y', 'z'].map(a => (
              <span key={a} style={{ display: 'inline-flex', alignItems: 'center', gap: 2 }}><RobotoText label={a.toUpperCase()} fontSize={12} labelColor="var(--fg-medium)" />
                <input type="number" step={0.1} value={Math.round(aimPoint[a] / u * 100) / 100} data-aim-axis={a} onChange={e => { const n = Number(e.target.value); if (!isNaN(n)) setAimPoint(Object.assign({}, aimPoint, { [a]: n * u })); }} style={Object.assign({ width: 70 }, inputStyle)} /></span>
            ))}
            <GenericButton label="Aim" width={60} height={24} disabled={!selectedKeys.length} onClick={() => aimAt(aimPoint)} />
            <GenericButton label={aimPick ? 'Click the stage…' : 'Pick on stage'} width={110} height={24} disabled={!selectedKeys.length} onClick={() => setAimPick(!aimPick)} />
            <FF.Note text="Writes Pan/Tilt as Simple Desk overrides for the selected fixtures (Release fixtures in the tools panel clears them). Y is the height above the floor." />
          </div>
        ) : null}

        <div ref={wrapRef} onWheel={onWheel} style={{ flex: 1, minHeight: 0, overflow: 'auto', position: 'relative', cursor: aimPick ? 'crosshair' : 'default' }}>
          <svg ref={svgRef} data-ff-stage="1" width={vb.w * pxPerMm} height={vb.h * pxPerMm} viewBox={vb.x + ' ' + vb.y + ' ' + vb.w + ' ' + vb.h}
            style={{ display: 'block', margin: 10, background: '#222', userSelect: 'none' }} onMouseDown={onBgDown}>
            <rect x={0} y={0} width={size.w} height={size.h} fill="#2b2b2b" stroke="#555" strokeWidth={Math.max(1, 1 / pxPerMm)} />
            {bgUrl ? <image href={bgUrl} x={0} y={0} width={size.w} height={size.h} preserveAspectRatio="xMidYMid meet" data-ff-bg-image="1" style={{ pointerEvents: 'none' }} /> : null}
            {gridLines}
            {groupBoxes}
            {items.map(it => {
              const p = project(stage, it.position), s = size2D(stage, it);
              const isSel = selected.has(String(it.fixtureId)) && headMatch(headSel, it);
              const moving = dragOffsetMm && isSel && !(it.flags && it.flags.locked) && drag.sel.indexOf(String(it.fixtureId)) !== -1;
              const x = p.x + (moving ? dragOffsetMm.x : 0), y = p.y + (moving ? dragOffsetMm.y : 0);
              const rot = rotation2D(stage, it.rotation);
              const heads = Math.max(1, it.heads || 1);
              const lay = headLayout(s.w, s.h, heads);
              const cellW = s.w / lay.cols, cellH = s.h / lay.rows, r = Math.max(1, Math.min(cellW, cellH) / 2 - Math.min(cellW, cellH) * 0.1);
              const det = details[String(it.fixtureId)];
              const sw = Math.max(1, (isSel ? 2 : 1) / pxPerMm);
              return (
                <g key={itemKey(it)} data-fx-id={it.fixtureId} data-item={itemKey(it)} data-selected={isSel ? '1' : undefined} transform={'translate(' + x + ' ' + y + ') rotate(' + rot + ' ' + (s.w / 2) + ' ' + (s.h / 2) + ')'}
                  onMouseDown={e => onItemDown(e, it)} style={{ cursor: it.flags && it.flags.locked ? 'not-allowed' : 'move' }}>
                  <title>{it.name + (it.linkedIndex ? ' (linked ' + it.linkedIndex + ')' : '') + (it.headIndex ? ' head ' + (it.headIndex + 1) : '') + '\n' + it.fixtureType + ' · U' + (it.universe + 1) + '.' + (it.address + 1) + '\n' + (it.position.x / u).toFixed(2) + ' / ' + (it.position.y / u).toFixed(2) + ' / ' + (it.position.z / u).toFixed(2) + ' ' + unitLabel + (it.flags && it.flags.locked ? '\nlocked' : '') + (!it.placed ? '\nnot placed yet' : '')}</title>
                  <rect x={0} y={0} width={s.w} height={s.h} fill="#2A2A2A" stroke={isSel ? '#0978FF' : (it.flags && it.flags.locked ? '#c0392b' : '#8f8f8f')} strokeWidth={sw} strokeDasharray={it.placed ? undefined : (s.w / 12) + ' ' + (s.w / 20)} />
                  {Array.from({ length: heads }, (_, h) => {
                    const cx = (h % lay.cols) * cellW + cellW / 2, cy = Math.floor(h / lay.cols) * cellH + cellH / 2;
                    const headItem = Object.assign({}, it, { headIndex: heads > 1 ? h : (it.headIndex || 0) });
                    const fill = headColour(headItem, det, dmx);
                    const hk = it.fixtureId + ':' + (heads > 1 ? h : (it.headIndex || 0)) + ':' + (it.linkedIndex || 0);
                    const hSel = headSel.indexOf(hk) !== -1;
                    return <circle key={h} data-head={h} data-head-sel={hSel ? '1' : undefined} cx={cx} cy={cy} r={r} fill={fill || (it.gelColor ? it.gelColor : '#000')} fillOpacity={fill ? 1 : (it.gelColor ? 0.35 : 1)} stroke={hSel ? '#0978FF' : '#aaa'} strokeWidth={Math.max(1, (hSel ? 2 : 0.7) / pxPerMm)} />;
                  })}
                  {stage.showLabels ? <text x={s.w / 2} y={s.h + font} fontSize={font} fill="#ddd" textAnchor="middle" fontFamily="var(--font-roboto)" style={{ pointerEvents: 'none' }}>{it.name}</text> : null}
                </g>
              );
            })}
            {band ? <rect x={Math.min(band.x0, band.x1)} y={Math.min(band.y0, band.y1)} width={Math.abs(band.x1 - band.x0)} height={Math.abs(band.y1 - band.y0)} fill="rgba(9,120,255,0.15)" stroke="#0978FF" strokeWidth={Math.max(1, 1 / pxPerMm)} /> : null}
            {dlg === 'aim' ? (() => { const p = project(stage, aimPoint); return <g data-aim-marker="1"><circle cx={p.x} cy={p.y} r={u / 8} fill="none" stroke="#f0c040" strokeWidth={Math.max(1, 1.5 / pxPerMm)} /><line x1={p.x - u / 5} y1={p.y} x2={p.x + u / 5} y2={p.y} stroke="#f0c040" strokeWidth={Math.max(1, 1 / pxPerMm)} /><line x1={p.x} y1={p.y - u / 5} x2={p.x} y2={p.y + u / 5} stroke="#f0c040" strokeWidth={Math.max(1, 1 / pxPerMm)} /></g>; })() : null}
          </svg>
        </div>

        <ArrangeDialog open={dlg === 'arrange'} onClose={() => setDlg(null)} onApply={arrange} detect={detect} count={selectedItems.length} />
        <NumberDialog open={dlg === 'rotate'} title="Rotate around the centroid" label="Angle" value={90} min={-360} max={360} suffix="deg" onClose={() => setDlg(null)} onApply={v => arrange('rotate', { angle: v })} />
        <NumberDialog open={dlg === 'nth'} title="Enter a number" label="Keep every Nth fixture" value={3} min={2} max={64} onClose={() => setDlg(null)} onApply={v => keepEvery(Math.max(2, Math.round(v)), 0)} />
        <InvertGroupsDialog open={dlg === 'invert'} groups={invertCandidates} onClose={() => setDlg(null)} onApply={ids => applyInvert(invertCandidates, ids)} />
        <PovDialog open={stage.pointOfView === 'Undefined'} onPick={p => setStage({ pointOfView: p })} />
      </div>
    );
  }

  Object.assign(FF, { View2D, monitorProject: project, monitorUnproject: unproject });
  window.QLCFFViews = Object.assign(window.QLCFFViews || {}, { '2d': { id: '2d', icon: '2dview', label: '2D View', order: 10, component: View2D } });
})();
