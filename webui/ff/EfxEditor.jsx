/**
 * EfxEditor.jsx — the EFX editor, modelled on qmlui/qml/fixturesfunctions/EFXEditor.qml +
 * EFXPreview.qml (2D view): live preview canvas, fixture-head list (mode / reverse / start offset,
 * add / remove / reorder, "set an offset on all fixtures"), pattern parameters (algorithm, relative,
 * width, height, X/Y offset, rotation, start offset, X/Y frequency and phase for Lissajous, dimmer
 * control), propagation mode, and the shared timing / run-order block.
 *
 * Server: functions.efx.setParameters / addFixture / removeFixture / setFixtureParameters /
 * reorderFixture / setFixturesOffset / getPreview (controlapi/src/domains/apiefxcollectiondomain.cpp).
 * Edits are optimistic like the other editors (typeDetail patched first, FF.mutate queued with a
 * per-control key so a spin box firing per keystroke coalesces, rejection -> reload()). The preview
 * is re-fetched from the server after each own mutation settles (never from the optimistic copy -
 * the queue lags it) and whenever the detail is reloaded; it is what EFXEditor::algorithmData /
 * fixturesData compute in the desktop app, so the drawn pattern and head positions match.
 *
 * Not mirrored: the fake-3D sphere view of EFXPreview.qml (2D only here), and adding a single
 * specific head of a multi-head fixture from the picker (fixtures.list carries no head count, so
 * the picker adds every head of the fixture - what dropping a fixture on the Qt editor does too).
 *
 * Registered into window.QLCEditors.EFX (see FixturesFunctions.jsx editorFor).
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;
  const Icons = window.QLCIcons;

  const MODES = ['PanTilt', 'Dimmer', 'RGB'];
  const MODE_LABELS = { PanTilt: 'Position', Dimmer: 'Dimmer', RGB: 'RGB' };
  const PROPAGATION = ['Parallel', 'Serial', 'Asymmetric'];
  const OFFSET_MODES = ['Absolute', 'Increasing', 'Random'];
  /* EFX::algorithmList(); the server sends the authoritative list in typeDetail.algorithms. */
  const FALLBACK_ALGORITHMS = ['Circle', 'Eight', 'Line', 'Line2', 'Diamond', 'Square', 'SquareChoppy', 'SquareTrue', 'Leaf', 'Lissajous'];

  /* ---- preview canvas (EFXPreview.qml, 2D) --------------------------------------------------- */
  /** `preview` is functions.efx.getPreview's result; heads walk the pattern one point per
   *  duration/512 ms exactly like the QML Timer (no animation when the duration is 0/infinite). */
  function EfxPreview({ preview, durationMs, size = 280 }) {
    const patternRef = React.useRef(null);
    const headsRef = React.useRef(null);
    const anim = React.useRef({ idx: [], acc: 0 });
    const pts = (preview && preview.pattern) || [];
    const heads = (preview && preview.fixtures) || [];

    React.useEffect(() => {
      const c = patternRef.current; if (!c) return;
      const ctx = c.getContext('2d'), s = size, k = s / 255;
      ctx.clearRect(0, 0, s, s);
      ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, s, s);
      ctx.strokeStyle = '#c9c9c9'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(s / 2, 0); ctx.lineTo(s / 2, s); ctx.moveTo(0, s / 2); ctx.lineTo(s, s / 2); ctx.stroke();
      if (pts.length < 2) return;
      ctx.strokeStyle = '#000000'; ctx.beginPath(); ctx.moveTo(pts[0][0] * k, pts[0][1] * k);
      for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0] * k, pts[i][1] * k);
      ctx.closePath(); ctx.stroke();
    }, [preview, size]);

    React.useEffect(() => {
      anim.current.idx = heads.map(h => h.startIndex || 0);
      anim.current.acc = 0;
      const c = headsRef.current; if (!c) return undefined;
      const ctx = c.getContext('2d'), s = size, k = s / 255, r = s / 20;
      const interval = durationMs > 0 && durationMs < FF.INFINITE && pts.length ? durationMs / pts.length : 0;
      const draw = () => {
        ctx.clearRect(0, 0, s, s);
        if (!pts.length) return;
        ctx.lineWidth = 1; ctx.font = Math.round(r * 0.8) + 'px "Roboto Condensed", sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        const idx = anim.current.idx;
        for (let i = heads.length - 1; i >= 0; i--) {         /* first head painted last = on top, like the QML */
          const p = pts[idx[i]]; if (!p) continue;
          const x = p[0] * k, y = p[1] * k;
          ctx.fillStyle = '#ffffff'; ctx.strokeStyle = '#000000';
          ctx.beginPath(); ctx.arc(x, y, r / 2, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
          ctx.fillStyle = '#000000'; ctx.fillText(String(i + 1), x, y + 1);
        }
      };
      draw();
      if (interval <= 0 || !heads.length) return undefined;
      let raf = 0, last = performance.now();
      const tick = (now) => {
        anim.current.acc += now - last; last = now;
        let steps = Math.floor(anim.current.acc / interval);
        if (steps > 0) {
          anim.current.acc -= steps * interval;
          steps = Math.min(steps, pts.length);
          const idx = anim.current.idx;
          for (let i = 0; i < heads.length; i++) {
            const v = idx[i] + (heads[i].step || 1) * steps;
            idx[i] = ((v % pts.length) + pts.length) % pts.length;
          }
          draw();
        }
        raf = requestAnimationFrame(tick);
      };
      raf = requestAnimationFrame(tick);
      return () => cancelAnimationFrame(raf);
    }, [preview, durationMs, size]);

    return (
      <div style={{ position: 'relative', width: size, height: size, flex: 'none', border: 'var(--border-dark)', boxSizing: 'content-box' }} data-e2e="efx-preview" data-points={pts.length} data-heads={heads.length}>
        <canvas ref={patternRef} width={size} height={size} style={{ position: 'absolute', left: 0, top: 0 }} />
        <canvas ref={headsRef} width={size} height={size} style={{ position: 'absolute', left: 0, top: 0 }} />
      </div>
    );
  }

  /* ---- editor ------------------------------------------------------------------------------- */
  function EfxEditor({ qlc, detail, reload, setDetail, fixtures, selectedFixtureIds, onSelectFixtures }) {
    const D = window.QLCData;
    const td = detail.typeDetail || {};
    const fid = String(detail.id);
    const heads = (td.fixtures || []).map(h => Object.assign({}, h, { fixture: String(h.fixture) }));
    const algorithms = td.algorithms && td.algorithms.length ? td.algorithms : FALLBACK_ALGORITHMS;
    const lissajous = td.algorithm === 'Lissajous';
    const keyOf = (h) => h.fixture + ':' + h.head;
    const [sel, setSel] = React.useState([]);            /* selected head keys */
    const [picker, setPicker] = React.useState(false);
    const [offsetDlg, setOffsetDlg] = React.useState(false);
    const [offsetVal, setOffsetVal] = React.useState(0);
    const [offsetMode, setOffsetMode] = React.useState('Increasing');
    const [preview, setPreview] = React.useState(null);
    const [previewTick, setPreviewTick] = React.useState(0);
    React.useEffect(() => { setSel(s => s.filter(k => heads.some(h => keyOf(h) === k))); }, [heads.map(keyOf).join(',')]);

    FF.useForeignEvents(qlc, ['functions.efx.changed', 'functions.efx.fixturesChanged'], (topic, d) => {
      if (d && String(d.functionId) === fid) reload();
    }, [fid]);

    /* Preview: on load, after every own mutation has been applied (tick), after a reload (docRevision). */
    React.useEffect(() => {
      if (!qlc.online) return undefined;
      let alive = true;
      qlc.call('functions.efx.getPreview', { functionId: fid }).then(p => { if (alive) setPreview(p); }).catch(() => {});
      return () => { alive = false; };
    }, [qlc.online, fid, previewTick, td.docRevision]);
    const refreshPreview = () => setPreviewTick(t => t + 1);

    const patchTd = (p) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, p) }));
    const setParams = (p, key) => {
      patchTd(p);
      FF.mutate(qlc, 'functions.efx.setParameters', Object.assign({ functionId: fid }, p), { key: 'efx:' + fid + ':' + (key || Object.keys(p).join(',')) })
        .then(refreshPreview).catch(() => reload());
    };
    /* EFXEditor::setIsRelative re-centres the pattern when switching to relative. */
    const setRelative = (on) => setParams(on ? { isRelative: true, xOffset: 127, yOffset: 127 } : { isRelative: false }, 'relative');
    const patchHeads = (next) => patchTd({ fixtures: next });
    const setHeadParam = (h, p) => {
      patchHeads(heads.map(x => keyOf(x) === keyOf(h) ? Object.assign({}, x, p) : x));
      FF.mutate(qlc, 'functions.efx.setFixtureParameters', Object.assign({ functionId: fid, fixture: h.fixture, head: h.head }, p), { key: 'efxhead:' + fid + ':' + keyOf(h) + ':' + Object.keys(p).join(',') })
        .then(refreshPreview).catch(() => reload());
    };
    const addFixtures = (ids) => {
      const fresh = ids.map(String).filter(id => !heads.some(h => h.fixture === id));
      if (!fresh.length) return;
      FF.mutateSeq(qlc, fresh.map(id => ['functions.efx.addFixture', { functionId: fid, fixture: id, allHeads: true }])).then(reload).catch(() => reload());
    };
    const removeSelected = () => {
      const gone = heads.filter(h => sel.indexOf(keyOf(h)) !== -1);
      if (!gone.length) return;
      patchHeads(heads.filter(h => sel.indexOf(keyOf(h)) === -1));
      setSel([]);
      FF.mutateSeq(qlc, gone.map(h => ['functions.efx.removeFixture', { functionId: fid, fixture: h.fixture, head: h.head }])).then(refreshPreview).catch(() => reload());
    };
    const move = (dir) => {
      if (sel.length !== 1) return;
      const i = heads.findIndex(h => keyOf(h) === sel[0]), j = i + dir;
      if (i < 0 || j < 0 || j >= heads.length) return;
      const next = heads.slice(); const [h] = next.splice(i, 1); next.splice(j, 0, h);
      patchHeads(next);
      FF.mutate(qlc, 'functions.efx.reorderFixture', { functionId: fid, fixture: h.fixture, head: h.head, move: dir < 0 ? 'raise' : 'lower' }).then(refreshPreview).catch(() => reload());
    };
    const applyOffsets = () => {
      FF.mutate(qlc, 'functions.efx.setFixturesOffset', { functionId: fid, offset: offsetVal, mode: offsetMode }).then(reload).catch(() => reload());
    };
    const fixtureOf = (id) => fixtures.find(f => String(f.id) === String(id));
    const headName = (h) => {
      const f = fixtureOf(h.fixture);
      const name = f ? f.name : 'Fixture ' + h.fixture + ' (unpatched)';
      const multi = h.head > 0 || heads.filter(x => x.fixture === h.fixture).length > 1;
      return multi ? name + ' [' + h.head + ']' : name;
    };
    const fixtureItems = fixtures.filter(f => !heads.some(h => h.fixture === String(f.id)))
      .map(f => ({ id: String(f.id), name: f.name, icon: D.icon(Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture'), hint: 'U' + (f.universe + 1) + '.' + (f.address + 1) }));
    const selectable = (selectedFixtureIds || []).map(String).filter(id => !heads.some(h => h.fixture === id));
    const onRowClick = (e, k) => setSel(e.ctrlKey || e.metaKey ? (sel.indexOf(k) !== -1 ? sel.filter(x => x !== k) : sel.concat([k]))
      : e.shiftKey && sel.length ? (() => { const keys = heads.map(keyOf); const a = keys.indexOf(sel[0]), b = keys.indexOf(k); return keys.slice(Math.min(a, b), Math.max(a, b) + 1); })() : [k]);
    const selIndex = sel.length === 1 ? heads.findIndex(h => keyOf(h) === sel[0]) : -1;
    const stop = (e) => e.stopPropagation();

    const spin = (label, field, max, opts) => {
      const off = opts && opts.lissajousOnly && !lissajous;
      return (
        <FF.Row label={label} width={88} key={field}>
          <CustomSpinBox value={Number(td[field]) || 0} from={0} to={max} width={82} height={24} suffix={(opts && opts.suffix) || ''} disabled={off}
            onValueModified={v => setParams({ [field]: v }, field)} data-e2e={'efx-' + field} title={off ? 'Only used by the Lissajous algorithm' : undefined} />
        </FF.Row>
      );
    };

    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }} data-e2e="efx-editor">
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 8px', background: 'var(--bg-strong)', flex: 'none' }}>
          <IconButton imgSource={D.icon('fixture')} size={26} tooltip="Add a fixture (every head)" onClick={() => setPicker(true)} data-e2e="efx-add" />
          {selectable.length ? <GenericButton label={'Add ' + selectable.length + ' selected'} width={120} height={26} onClick={() => addFixtures(selectable)} /> : null}
          <IconButton faSource={FF.GLYPH.minus} size={26} tooltip="Remove the selected fixture head(s)" disabled={!sel.length} onClick={removeSelected} data-e2e="efx-remove" />
          <IconButton faSource={FF.GLYPH.arrowUp} size={26} tooltip="Move the selected head up (Serial order)" disabled={selIndex <= 0} onClick={() => move(-1)} data-e2e="efx-up" />
          <IconButton faSource={FF.GLYPH.arrowDown} size={26} tooltip="Move the selected head down (Serial order)" disabled={selIndex < 0 || selIndex >= heads.length - 1} onClick={() => move(1)} data-e2e="efx-down" />
          <IconButton faSource={FF.GLYPH.rotateLeft} size={26} tooltip="Set an offset on all fixtures" disabled={!heads.length} onClick={() => setOffsetDlg(true)} data-e2e="efx-offsets" />
          <div style={{ width: 1, height: 20, background: 'var(--border-color-dark)', margin: '0 3px' }} />
          <IconButton faSource={FF.GLYPH.crosshairs} size={26} tooltip="Select the EFX's fixtures in the tree" disabled={!heads.length} onClick={() => onSelectFixtures(Array.from(new Set(heads.map(h => h.fixture))))} />
          <div style={{ flex: 1 }} />
          <RobotoText label={heads.length + ' heads · ' + (td.algorithm || '—') + ' · ' + (td.propagationMode || '—')} fontSize={13} labelColor="var(--fg-light)" />
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 10, display: 'flex', flexDirection: 'column', gap: 10 }}>
            <div style={{ display: 'flex', gap: 14, flexWrap: 'wrap', alignItems: 'flex-start' }}>
              <EfxPreview preview={preview} durationMs={detail.duration} />
              <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 330 }}>
                <FF.Heading text="Pattern" />
                <FF.Row label="Algorithm" width={88}>
                  <CustomComboBox width={150} height={24} currValue={td.algorithm} onValueChanged={v => { if (v !== td.algorithm) setParams({ algorithm: v }, 'algorithm'); }}
                    model={algorithms.map(a => ({ mLabel: a, mValue: a }))} data-e2e="efx-algorithm" />
                  <CustomCheckBox checked={!!td.isRelative} size={22} onToggled={setRelative} tooltip="Relative movement: the pattern is centred on each fixture's current pan/tilt" data-e2e="efx-relative" />
                  <RobotoText label="Relative" fontSize={13} />
                </FF.Row>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', columnGap: 12, rowGap: 2 }}>
                  {spin('Width', 'width', 127)}
                  {spin('Height', 'height', 127)}
                  {spin('X offset', 'xOffset', 255)}
                  {spin('Y offset', 'yOffset', 255)}
                  {spin('Rotation', 'rotation', 359, { suffix: '°' })}
                  {spin('Start offset', 'startOffset', 359, { suffix: '°' })}
                  {spin('X frequency', 'xFrequency', 32, { lissajousOnly: true })}
                  {spin('Y frequency', 'yFrequency', 32, { lissajousOnly: true })}
                  {spin('X phase', 'xPhase', 359, { suffix: '°', lissajousOnly: true })}
                  {spin('Y phase', 'yPhase', 359, { suffix: '°', lissajousOnly: true })}
                </div>
                <FF.Row label="Propagation" width={88}>
                  <FF.Choice options={PROPAGATION} value={td.propagationMode} onChange={v => setParams({ propagationMode: v }, 'propagation')} />
                </FF.Row>
                <FF.Row label="" width={88}>
                  <CustomCheckBox checked={!!td.dimmerControlEnabled} size={22} onToggled={b => setParams({ dimmerControlEnabled: b }, 'dimmer')} data-e2e="efx-dimmer" />
                  <RobotoText label="Dimmer control (fade the intensity along the movement)" fontSize={13} />
                </FF.Row>
              </div>
            </div>
            <FF.Heading text="Fixtures" />
            <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--font-roboto)', fontSize: 13, color: 'var(--fg-main)' }}>
              <thead>
                <tr style={{ background: 'var(--bg-strong)', height: 24, textAlign: 'left' }}>
                  <th style={{ width: 34, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>#</th>
                  <th style={{ padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Fixture</th>
                  <th style={{ width: 120, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Mode</th>
                  <th style={{ width: 70, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Reverse</th>
                  <th style={{ width: 110, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Start offset</th>
                </tr>
              </thead>
              <tbody data-e2e="efx-heads">
                {heads.map((h, i) => {
                  const k = keyOf(h), on = sel.indexOf(k) !== -1, f = fixtureOf(h.fixture);
                  const modes = h.availableModes && h.availableModes.length ? h.availableModes : MODES;
                  return (
                    <tr key={k} data-e2e-head={k} onClick={(e) => onRowClick(e, k)}
                      style={{ height: 30, background: on ? 'var(--highlight)' : (i % 2 ? 'var(--bg-medium)' : 'var(--bg-stronger)'), cursor: 'pointer' }}>
                      <td style={{ padding: '0 6px', color: 'var(--fg-light)' }}>{i + 1}</td>
                      <td style={{ padding: '0 6px' }}>
                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
                          <img src={D.icon(f ? (Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture') : 'other')} alt="" style={{ width: 16, height: 16 }} />
                          <span style={{ color: f ? 'var(--fg-main)' : 'var(--fg-medium)' }}>{headName(h)}</span>
                        </span>
                      </td>
                      <td style={{ padding: '0 6px' }} onClick={stop}>
                        <CustomComboBox width={108} height={22} currValue={h.mode} onValueChanged={v => { if (v !== h.mode) setHeadParam(h, { mode: v }); }}
                          model={modes.map(m => ({ mLabel: MODE_LABELS[m] || m, mValue: m }))} data-e2e="efx-head-mode" />
                      </td>
                      <td style={{ padding: '0 6px' }} onClick={stop}>
                        <CustomCheckBox checked={h.direction === 'Backward'} size={20} onToggled={b => setHeadParam(h, { direction: b ? 'Backward' : 'Forward' })} data-e2e="efx-head-reverse" />
                      </td>
                      <td style={{ padding: '0 6px' }} onClick={stop}>
                        <CustomSpinBox value={Number(h.startOffset) || 0} from={0} to={359} width={82} height={22} suffix="°" onValueModified={v => setHeadParam(h, { startOffset: v })} data-e2e="efx-head-offset" />
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
            {!heads.length ? <RobotoText label="No fixtures. Add some with the fixture button (every head of the fixture joins) or select fixtures in the tree and use 'Add selected'." fontSize={14} labelColor="var(--fg-medium)" wrapText height="auto" /> : null}
          </div>
          <div style={{ width: 300, flex: 'none', overflow: 'auto', borderLeft: 'var(--border-dark)', padding: 10, display: 'flex', flexDirection: 'column', gap: 4 }}>
            <FF.TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} />
            <FF.Note text="Duration is one full cycle of the pattern; the preview heads move at that speed. Fade in scales the pattern up from the centre while the EFX starts." style={{ marginTop: 6 }} />
          </div>
        </div>
        <FF.PickerDialog open={picker} title="Add fixtures to the EFX" items={fixtureItems} onPick={addFixtures} onClose={() => setPicker(false)} />
        <CustomPopupDialog open={offsetDlg} title="Set an offset on all fixtures" width={360} standardButtons={['Cancel', 'Apply']}
          onClicked={(b) => { if (b === 'Apply') applyOffsets(); setOffsetDlg(false); }} onClose={() => setOffsetDlg(false)}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }} data-e2e="efx-offset-dialog">
            <FF.Row label="Offset" width={70}><CustomSpinBox value={offsetVal} from={0} to={360} width={90} height={24} suffix="°" onValueModified={setOffsetVal} data-e2e="efx-offset-value" /></FF.Row>
            <FF.Row label="Mode" width={70}><FF.Choice options={OFFSET_MODES} value={offsetMode} onChange={setOffsetMode} /></FF.Row>
            <FF.Note text="Absolute: the same offset on every fixture. Increasing: 0, offset, 2×offset, … in list order. Random: the increasing set shuffled over the fixtures." />
          </div>
        </CustomPopupDialog>
      </div>
    );
  }

  window.QLCEditors = Object.assign(window.QLCEditors || {}, { EFX: EfxEditor });
  Object.assign(FF, { EfxEditor, EfxPreview });
})();
