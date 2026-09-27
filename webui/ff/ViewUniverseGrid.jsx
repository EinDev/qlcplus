/**
 * ViewUniverseGrid.jsx — the Fixtures & Functions "Universe Grid" (qmlui UniverseGridView.qml +
 * GridEditor): the 512 addresses of one universe as a 32 x 16 grid, fixtures drawn as coloured
 * blocks. Click selects, Ctrl-click toggles, dragging a block moves the fixture to a new start
 * address (fixtures.update), Cut / Paste re-addresses the selected fixtures to the first free
 * block of the shown universe — the free-address search FixtureManager::pasteFromClipboard does
 * server-side is computed here from the current fixture list.
 *
 * Registers window.QLCFFViews.grid; mounted by the F&F view switcher with
 * { qlc, fixtures, universes, selectedFixtureIds, onSelectFixtures, universeFilter, setUniverseFilter }.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, CustomComboBox, GenericButton } = window.PatchDesignSystem_5432c9;

  const COLS = 32, ROWS = 16, ROW_H = 24;

  function hue(id) { let h = 0; const s = String(id); for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0; return (h * 137.508) % 360; }
  function colour(id, sel) { return 'hsl(' + hue(id).toFixed(0) + ', 55%, ' + (sel ? 42 : 32) + '%)'; }

  /** Occupancy of one universe: occ[address] = fixture summary or null (footprints clamped at 512). */
  function occupancy(fixtures, universeId) {
    const occ = new Array(512).fill(null);
    (fixtures || []).filter(f => f.universe === universeId).forEach(f => {
      for (let c = 0; c < f.channels && f.address + c < 512; c++) occ[f.address + c] = f;
    });
    return occ;
  }
  /** Is [start, start+channels) free in occ, ignoring cells owned by `self`? */
  function fits(occ, start, channels, self) {
    if (start < 0 || start + channels > 512) return false;
    for (let c = 0; c < channels; c++) { const o = occ[start + c]; if (o && (!self || String(o.id) !== String(self.id))) return false; }
    return true;
  }
  function firstFree(occ, channels, self) {
    for (let s = 0; s + channels <= 512; s++) if (fits(occ, s, channels, self)) return s;
    return -1;
  }

  function ViewUniverseGrid({ qlc, fixtures, universes, selectedFixtureIds, onSelectFixtures, universeFilter, setUniverseFilter }) {
    const D = window.QLCData;
    const Icons = window.QLCIcons;
    const uniList = React.useMemo(() => (universes || []).slice().sort((a, b) => a.id - b.id), [universes]);
    const [pick, setPick] = React.useState(null);
    /* The shown universe follows the screen-wide filter while one is set; otherwise the combo (or the first universe). */
    React.useEffect(() => { if (universeFilter != null) setPick(universeFilter); }, [universeFilter]);
    const shown = pick != null ? pick : (universeFilter != null ? universeFilter : (uniList.length ? uniList[0].id : 0));
    const chooseUniverse = (id) => { setPick(id); if (universeFilter != null) setUniverseFilter(id); };

    const [clipboard, setClipboard] = React.useState([]);
    const [note, setNote] = React.useState('');
    const [drag, setDrag] = React.useState(null);   /* {id, target, valid} while a block is dragged */
    const gridRef = React.useRef(null);
    const dragRef = React.useRef(null);
    const movedRef = React.useRef(false);

    const inUniverse = React.useMemo(() => (fixtures || []).filter(f => f.universe === shown).slice().sort((a, b) => a.address - b.address), [fixtures, shown]);
    const occ = React.useMemo(() => occupancy(fixtures, shown), [fixtures, shown]);
    const ids = React.useMemo(() => inUniverse.map(f => String(f.id)), [inUniverse]);
    const details = FF.useFixtureDetails(qlc, ids);
    const selSet = React.useMemo(() => new Set((selectedFixtureIds || []).map(String)), [selectedFixtureIds]);
    const used = occ.reduce((n, o) => n + (o ? 1 : 0), 0);
    const uniName = (id) => { const u = uniList.find(x => x.id === id); return u ? u.name : 'Universe ' + (id + 1); };

    const select = (f, e) => {
      const id = String(f.id);
      if (e.ctrlKey || e.metaKey) onSelectFixtures(selSet.has(id) ? Array.from(selSet).filter(x => x !== id) : Array.from(selSet).concat([id]));
      else onSelectFixtures([id]);
    };
    const move = (f, universe, address) => FF.mutate(qlc, 'fixtures.update', { fixtureId: String(f.id), universe, address })
      .then(() => setNote(''), (e) => setNote('Move refused: ' + ((e && e.message) || 'request failed')));

    /* ---- drag a block to a new start address (mousedown on a fixture cell, window-level move/up) ---- */
    const cellAt = (e) => {
      const r = gridRef.current && gridRef.current.getBoundingClientRect();
      if (!r) return null;
      const col = Math.floor((e.clientX - r.left) / r.width * COLS), row = Math.floor((e.clientY - r.top) / ROW_H);
      if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return null;
      return row * COLS + col;
    };
    const startDrag = (f, address, e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      dragRef.current = { f, grab: address - f.address, target: f.address };
      movedRef.current = false;
      const onMove = (ev) => {
        const d = dragRef.current;
        if (!d) return;
        const cell = cellAt(ev);
        if (cell == null) return;
        const target = cell - d.grab;
        if (target === d.target) return;
        d.target = target;
        if (target !== d.f.address) movedRef.current = true;
        setDrag(movedRef.current ? { id: String(d.f.id), target, channels: d.f.channels, valid: fits(occ, target, d.f.channels, d.f) } : null);
      };
      const onUp = () => {
        window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp);
        const d = dragRef.current;
        dragRef.current = null;
        setDrag(null);
        /* The click that follows a real drag must not change the selection; if the mouse was
           released off the grid no click comes at all, so the flag is cleared right after. */
        setTimeout(() => { movedRef.current = false; }, 0);
        if (!d || !movedRef.current || d.target === d.f.address) return;
        if (fits(occ, d.target, d.f.channels, d.f)) move(d.f, shown, d.target);
        else setNote('Cannot move ' + d.f.name + ' to ' + (d.target + 1) + ': the block overlaps another fixture or runs past 512.');
      };
      window.addEventListener('mousemove', onMove); window.addEventListener('mouseup', onUp);
    };
    const clickCell = (f, e) => { if (movedRef.current) return; if (f) select(f, e); };

    /* ---- cut / paste ---- */
    const cut = () => { setClipboard(Array.from(selSet)); setNote(selSet.size + (selSet.size === 1 ? ' fixture' : ' fixtures') + ' in the clipboard — Paste places them in ' + uniName(shown) + '.'); };
    const paste = async () => {
      const local = occ.slice();
      const calls = [], missed = [];
      clipboard.forEach(id => {
        const f = (fixtures || []).find(x => String(x.id) === id);
        if (!f) return;
        /* Its own current cells (if in this universe) are freed first, then the first fitting block is taken. */
        for (let i = 0; i < 512; i++) if (local[i] && String(local[i].id) === id) local[i] = null;
        const s = firstFree(local, f.channels, null);
        if (s < 0) { missed.push(f.name); return; }
        for (let c = 0; c < f.channels; c++) local[s + c] = f;
        /* Already exactly there: no call (a no-op update could be refused as an overlap by the server). */
        if (f.universe === shown && f.address === s) return;
        calls.push(['fixtures.update', { fixtureId: id, universe: shown, address: s }]);
      });
      setClipboard([]);
      let failed = '';
      try { await FF.mutateSeq(qlc, calls); } catch (e) { failed = (e && e.message) || 'request failed'; }
      setNote((calls.length ? calls.length + ' moved into ' + uniName(shown) + '. ' : '') + (missed.length ? 'No free block for: ' + missed.join(', ') + '. ' : '') + (failed ? 'Server refused: ' + failed : ''));
    };

    const tooltip = (address, f) => {
      if (!f) return 'Address ' + (address + 1);
      const d = details[String(f.id)];
      const idx = address - f.address;
      const ch = d && d.channelList && d.channelList[idx];
      const def = d ? [d.manufacturer, d.model].filter(Boolean).join(' ') + (d.mode ? ' (' + d.mode + ')' : '') : '';
      return f.name + (def ? '\n' + def : '') + '\nCh ' + (idx + 1) + (ch ? ': ' + ch.name : '') + '\nDrag to move, click to select, Ctrl-click to add to the selection';
    };
    const dragRange = (a) => !!drag && a >= drag.target && a < drag.target + drag.channels;
    const uniModel = uniList.map(u => ({ mLabel: u.name, mValue: u.id }));

    const cells = [];
    for (let a = 0; a < 512; a++) {
      const f = occ[a];
      const sel = !!f && selSet.has(String(f.id));
      const inDrag = dragRange(a);
      cells.push(
        <div key={a} data-address={a} data-fx-id={f ? f.id : undefined} title={tooltip(a, f)}
          onMouseDown={f ? (e) => startDrag(f, a, e) : undefined} onClick={(e) => clickCell(f, e)}
          style={{ height: ROW_H, boxSizing: 'border-box', border: '1px solid var(--bg-medium)', position: 'relative', cursor: f ? (drag ? 'grabbing' : 'grab') : 'default',
            background: inDrag ? (drag.valid ? 'rgba(0,255,0,0.45)' : 'rgba(255,0,0,0.55)') : f ? colour(f.id, sel) : 'var(--bg-stronger)',
            boxShadow: sel ? 'inset 0 0 0 2px var(--selection)' : 'none',
            fontFamily: 'var(--font-mono)', fontSize: 10, color: f ? 'rgba(255,255,255,0.75)' : 'var(--fg-medium)', display: 'flex', alignItems: 'flex-end', justifyContent: 'flex-end', padding: '0 2px 1px 0', userSelect: 'none' }}>
          {a + 1}
        </div>
      );
    }
    /* Name overlays over the first row segment of every fixture spanning at least 3 cells. */
    const overlays = inUniverse.filter(f => f.channels >= 3 && f.address < 512).map(f => {
      const col = f.address % COLS, row = Math.floor(f.address / COLS), span = Math.min(f.channels, COLS - col);
      return (
        <div key={f.id} data-fx-label={f.id} style={{ position: 'absolute', pointerEvents: 'none', left: (col * 100 / COLS) + '%', width: (span * 100 / COLS) + '%', top: row * ROW_H, height: ROW_H,
          display: 'flex', alignItems: 'center', gap: 3, padding: '0 3px', boxSizing: 'border-box', overflow: 'hidden' }}>
          <img src={D.icon(Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture')} alt="" style={{ width: 14, height: 14, flex: 'none' }} />
          <span style={{ fontFamily: 'var(--font-roboto)', fontSize: 12, color: '#fff', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', textShadow: '0 0 2px #000' }}>{f.name}</span>
        </div>
      );
    });

    return (
      <div data-ff-view="grid" style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column', background: 'var(--bg-medium)' }}>
        <div style={{ flex: 'none', height: 32, display: 'flex', alignItems: 'center', gap: 8, padding: '0 8px', background: 'var(--bg-strong)', borderBottom: '1px solid var(--bg-stronger)' }}>
          <RobotoText label="Universe" fontSize={13} height={24} labelColor="var(--fg-light)" />
          <CustomComboBox width={170} height={24} currValue={shown} model={uniModel} onValueChanged={chooseUniverse} title="Universe shown in the grid" disabled={!uniList.length} />
          <GenericButton label="Cut" width={70} height={24} disabled={!selSet.size} onClick={cut} data-action="cut"
            title={selSet.size ? 'Put the ' + selSet.size + ' selected fixture(s) in the clipboard' : 'Select fixtures first'} />
          <GenericButton label={'Paste' + (clipboard.length ? ' (' + clipboard.length + ')' : '')} width={90} height={24} disabled={!clipboard.length || !qlc.online} onClick={paste} data-action="paste"
            title={clipboard.length ? 'Move the clipboard fixtures to the first free addresses of ' + uniName(shown) : 'Clipboard is empty — Cut first'} />
          <RobotoText label={inUniverse.length + (inUniverse.length === 1 ? ' fixture · ' : ' fixtures · ') + used + ' channels used'} fontSize={13} height={24} labelColor="var(--fg-light)" style={{ marginLeft: 'auto' }} data-legend="1" />
        </div>
        {note ? <div style={{ flex: 'none', padding: '2px 8px' }}><FF.Note text={note} /></div> : null}
        <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12 }}>
          <div ref={gridRef} data-grid="1" style={{ position: 'relative', display: 'grid', gridTemplateColumns: 'repeat(' + COLS + ', minmax(0, 1fr))', gridAutoRows: ROW_H, minWidth: COLS * 20, background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
            {cells}
            {overlays}
          </div>
          {!qlc.online ? <FF.Note text="Not connected: moves are disabled." style={{ marginTop: 6 }} /> : null}
        </div>
      </div>
    );
  }

  window.QLCFFViews = Object.assign(window.QLCFFViews || {}, { grid: { id: 'grid', icon: 'uniview', label: 'Universe Grid', order: 30, component: ViewUniverseGrid } });
})();
