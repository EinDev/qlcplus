/**
 * FixtureMisc.jsx — the fixture-side leftovers of Fixtures & Functions:
 *
 *  - FF.FixtureModeRow: change a patched fixture's mode (fixtures.update {mode}).
 *  - FF.FixtureChannelList: per-channel behaviour rows (can fade, forced HTP/LTP, channel
 *    modifier; fixtures.channel.setBehaviour) with "apply to fixtures of the same type"
 *    (FixtureChannelDelegate.qml / FixtureGroupManager.qml).
 *  - FF.ModifiersEditorDialog: channel modifier templates (PopupChannelModifiers.qml;
 *    fixtures.modifiers.*) with an SVG curve editor.
 *  - FF.FixtureSummaryDialog / FF.UniverseSummaryDialog: FixtureSummary.qml / UniverseSummary.qml,
 *    built from fixtures.get (definition, physical block, addresses, heads), printable.
 *  - FF.RgbPanelDialog: RGBPanelProperties.qml (fixtures.createRgbPanel).
 *  - FF.GroupGridEditor: FixtureGroupEditor.qml / GridEditor.qml over fixtures.group.setSize /
 *    reset / swapHeads / assignHead / unassignHead / assignFixture (rotate / flip / regenerate
 *    are computed here, like FixtureGroupEditor::transformSelection(), and applied as swaps).
 *  - FF.ColorFiltersPicker: ColorToolFilters.qml over fixtures.colorFilters.list.
 *  - FF.FixtureConsole: BottomPanel.qml's per-channel faders with a fader window, pan/tilt mode,
 *    multiple channel selection and "copy to all fixtures of the same type".
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, GenericButton, IconButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomPopupDialog, IconTextEntry, CustomSlider } = window.PatchDesignSystem_5432c9;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' };
  const G = { print: '', rotate: '', flipH: '', flipV: '', copy: '', swap: '', table: '', grid: '', wand: '' };

  /* ---- modifier template list (shared) -------------------------------------------------------- */
  function useModifierTemplates(qlc) {
    const [templates, setTemplates] = React.useState(null);
    React.useEffect(() => {
      if (!qlc.online) { setTemplates(null); return undefined; }
      let alive = true;
      const load = () => qlc.call('fixtures.modifiers.list').then(r => { if (alive) setTemplates((r && r.templates) || []); }).catch(() => { if (alive) setTemplates([]); });
      load();
      const off = qlc.subscribeTo('fixtures.modifiers.changed', load);
      return () => { alive = false; off(); };
    }, [qlc.online]);
    return templates;
  }

  /* ---- mode row --------------------------------------------------------------------------------- */
  function FixtureModeRow({ qlc, detail, f }) {
    const modes = (detail && detail.availableModes) || [];
    const [busy, setBusy] = React.useState(false);
    if (!detail || modes.length < 2) return <FF.Row label="Mode">{f.mode || '—'}</FF.Row>;
    const change = (mode) => {
      if (mode === detail.mode) return;
      setBusy(true);
      FF.mutate(qlc, 'fixtures.update', { fixtureId: String(detail.id), mode }).catch(() => {}).then(() => setBusy(false));
    };
    return (
      <FF.Row label="Mode">
        <span data-fx-mode="1" style={{ display: 'inline-flex' }}>
          <CustomComboBox width={200} currValue={detail.mode} disabled={busy}
            model={modes.map(m => ({ mLabel: m.name + ' (' + m.channelCount + ' ch)', mValue: m.name }))} onValueChanged={change} />
        </span>
      </FF.Row>
    );
  }

  /* ---- per-channel behaviour ------------------------------------------------------------------ */
  let applySameTypeDefault = false; /* survives switching between fixtures, like the Qt checkbox */
  function FixtureChannelList({ qlc, detail, f, channelIcon }) {
    const D = window.QLCData;
    const templates = useModifierTemplates(qlc);
    const [sameType, setSameType] = React.useState(applySameTypeDefault);
    const [editor, setEditor] = React.useState(null); /* {channel, name} */
    const setSame = (v) => { applySameTypeDefault = v; setSameType(v); };
    const set = (ch, patch) => FF.mutate(qlc, 'fixtures.channel.setBehaviour',
      Object.assign({ fixtureId: String(detail.id), channel: ch.index, applyToSameType: sameType }, patch)).catch(() => {});
    const modModel = [{ mLabel: 'None', mValue: '' }].concat((templates || []).map(t => ({ mLabel: t.name, mValue: t.name })));
    const hasBehaviour = detail.channelList.length && detail.channelList[0].precedence !== undefined;
    return (
      <div data-fx-channels="1" style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
        {hasBehaviour ? (
          <label style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 4, cursor: 'pointer' }} data-fx-sametype="1">
            <CustomCheckBox checked={sameType} size={18} onToggled={setSame} />
            <RobotoText label="Apply changes to fixtures of the same type" fontSize={13} height={20} />
          </label>
        ) : null}
        {hasBehaviour ? (
          <div style={{ display: 'flex', gap: 6, height: 18, paddingLeft: 30 }}>
            <RobotoText label="Channel" fontSize={11} labelColor="var(--fg-light)" height={18} style={{ flex: 1 }} />
            <RobotoText label="Fade" fontSize={11} labelColor="var(--fg-light)" height={18} style={{ width: 30 }} />
            <RobotoText label="Behaviour" fontSize={11} labelColor="var(--fg-light)" height={18} style={{ width: 112 }} />
            <RobotoText label="Modifier" fontSize={11} labelColor="var(--fg-light)" height={18} style={{ width: 160 }} />
          </div>
        ) : null}
        {detail.channelList.map(ch => {
          const intensity = ch.group === 'Intensity';
          const precModel = intensity
            ? [{ mLabel: 'Auto (HTP)', mValue: 'auto' }, { mLabel: 'Forced LTP', mValue: 'ltp' }]
            : [{ mLabel: 'Auto (LTP)', mValue: 'auto' }, { mLabel: 'Forced HTP', mValue: 'htp' }];
          return (
            <div key={ch.index} data-fx-channel={ch.index} style={{ display: 'flex', alignItems: 'center', gap: 6, minHeight: 26 }}>
              <RobotoText label={String(ch.index + 1)} fontSize={12} labelColor="var(--fg-medium)" textHAlign="right" style={{ width: 24, flex: 'none' }} height={24} />
              <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column' }}>
                <IconTextEntry iSrc={channelIcon(ch)} tLabel={ch.name} tFontSize={14} height={22} />
                <RobotoText label={'DMX ' + (ch.absoluteAddress - f.universe * 512 + 1)} fontSize={11} labelColor="var(--fg-medium)" height={14} style={{ paddingLeft: 28 }} />
              </div>
              {hasBehaviour ? <>
                <span title="Can fade (unchecked: the channel jumps instead of fading)" data-fx-fade={ch.index} style={{ width: 30, display: 'inline-flex', justifyContent: 'center' }}>
                  <CustomCheckBox checked={ch.canFade !== false} size={18} onToggled={v => set(ch, { canFade: v })} />
                </span>
                <span data-fx-prec={ch.index} style={{ display: 'inline-flex' }} title={intensity ? 'Intensity channels are HTP; they can be forced LTP' : 'Non-intensity channels are LTP; they can be forced HTP'}>
                  <CustomComboBox width={112} currValue={ch.precedence || 'auto'} model={precModel} onValueChanged={v => set(ch, { precedence: v })} />
                </span>
                <span data-fx-mod={ch.index} style={{ display: 'inline-flex', alignItems: 'center', gap: 2 }}>
                  <CustomComboBox width={132} currValue={ch.modifier || ''} model={modModel} onValueChanged={v => set(ch, { modifier: v || null })} />
                  <IconButton imgSource={D.icon('edit')} size={24} tooltip="Channel modifiers editor" onClick={() => setEditor({ channel: ch, name: ch.modifier || '' })} data-fx-modedit={ch.index} />
                </span>
              </> : null}
            </div>
          );
        })}
        {editor ? <ModifiersEditorDialog open qlc={qlc} initialName={editor.name} channelLabel={(editor.channel.index + 1) + ': ' + editor.channel.name}
          onApply={(name) => { set(editor.channel, { modifier: name || null }); setEditor(null); }} onClose={() => setEditor(null)} /> : null}
      </div>
    );
  }

  /* ---- channel modifiers editor ---------------------------------------------------------------- */
  const DEFAULT_POINTS = [{ original: 0, modified: 0 }, { original: 255, modified: 255 }];
  function CurveEditor({ points, selected, onSelect, onChange, size = 280 }) {
    const ref = React.useRef(null);
    const pad = 8, W = size, H = size, s = (W - 2 * pad) / 255;
    const px = (o) => pad + o * s, py = (m) => H - pad - m * s;
    const drag = (i, e) => {
      e.preventDefault(); e.stopPropagation(); onSelect(i);
      const move = (ev) => {
        const r = ref.current.getBoundingClientRect();
        const sx = r.width / W;
        let o = Math.round(((ev.clientX - r.left) / sx - pad) / s), m = Math.round((H - pad - (ev.clientY - r.top) / sx) / s);
        const lo = i === 0 ? 0 : (i === points.length - 1 ? 255 : points[i - 1].original);
        const hi = i === 0 ? 0 : (i === points.length - 1 ? 255 : points[i + 1].original);
        o = Math.max(lo, Math.min(hi, o)); m = Math.max(0, Math.min(255, m));
        onChange(points.map((p, k) => k === i ? { original: o, modified: m } : p));
      };
      const up = () => { window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up); };
      window.addEventListener('mousemove', move); window.addEventListener('mouseup', up);
    };
    /* Double-click on the curve area adds a handler at that original value. */
    const add = (e) => {
      const r = ref.current.getBoundingClientRect();
      const sx = r.width / W;
      const o = Math.max(1, Math.min(254, Math.round(((e.clientX - r.left) / sx - pad) / s)));
      const m = Math.max(0, Math.min(255, Math.round((H - pad - (e.clientY - r.top) / sx) / s)));
      if (points.some(p => p.original === o)) return;
      const next = points.concat([{ original: o, modified: m }]).sort((a, b) => a.original - b.original);
      onChange(next); onSelect(next.findIndex(p => p.original === o));
    };
    const grid = [];
    for (let v = 0; v <= 255; v += 51) grid.push(v);
    return (
      <svg ref={ref} data-mod-curve="1" viewBox={'0 0 ' + W + ' ' + H} width={W} height={H} onDoubleClick={add}
        style={{ background: 'var(--bg-stronger)', border: 'var(--border-control)', cursor: 'crosshair', flex: 'none' }}>
        {grid.map(v => <g key={v}>
          <line x1={px(v)} y1={py(0)} x2={px(v)} y2={py(255)} stroke="var(--bg-control)" strokeWidth="1" />
          <line x1={px(0)} y1={py(v)} x2={px(255)} y2={py(v)} stroke="var(--bg-control)" strokeWidth="1" />
        </g>)}
        <line x1={px(0)} y1={py(0)} x2={px(255)} y2={py(255)} stroke="var(--fg-medium)" strokeDasharray="3 3" strokeWidth="1" />
        <polyline points={points.map(p => px(p.original) + ',' + py(p.modified)).join(' ')} fill="none" stroke="var(--highlight)" strokeWidth="2" />
        {points.map((p, i) => (
          <circle key={i} data-mod-point={i} cx={px(p.original)} cy={py(p.modified)} r={i === selected ? 7 : 5}
            fill={i === selected ? 'var(--highlight)' : 'var(--fg-main)'} stroke="var(--bg-strong)" strokeWidth="1.5"
            style={{ cursor: 'move' }} onMouseDown={(e) => drag(i, e)} />
        ))}
      </svg>
    );
  }

  function ModifiersEditorDialog({ open, qlc, initialName, channelLabel, onApply, onClose }) {
    const templates = useModifierTemplates(qlc);
    const [current, setCurrent] = React.useState(initialName || '');
    const [points, setPoints] = React.useState(DEFAULT_POINTS);
    const [name, setName] = React.useState(initialName || 'New Template');
    const [selected, setSelected] = React.useState(-1);
    const [dirty, setDirty] = React.useState(false);
    const [msg, setMsg] = React.useState('');
    const [confirmDelete, setConfirmDelete] = React.useState(false);
    const info = (templates || []).find(t => t.name === current);
    const isUser = !!(info && info.isUser);
    const load = (n) => {
      setCurrent(n); setSelected(-1); setDirty(false); setMsg('');
      if (!n) { setPoints(DEFAULT_POINTS); setName('New Template'); return; }
      qlc.call('fixtures.modifiers.get', { name: n }).then(r => { setPoints(r.points && r.points.length ? r.points : DEFAULT_POINTS); setName(r.name); }).catch(e => setMsg(e.message || String(e)));
    };
    React.useEffect(() => { if (open) load(initialName || ''); }, [open, initialName]);
    const change = (p) => { setPoints(p); setDirty(true); };
    const addPoint = () => {
      const i = selected >= 0 && selected < points.length - 1 ? selected : Math.max(0, points.length - 2);
      const a = points[i], b = points[i + 1];
      if (!b || b.original - a.original < 2) return;
      const n = { original: a.original + Math.floor((b.original - a.original) / 2), modified: a.modified + Math.floor((b.modified - a.modified) / 2) };
      const next = points.slice(0, i + 1).concat([n], points.slice(i + 1));
      change(next); setSelected(i + 1);
    };
    const removePoint = () => { if (selected <= 0 || selected >= points.length - 1) return; change(points.filter((_, k) => k !== selected)); setSelected(-1); };
    const sel = selected >= 0 ? points[selected] : null;
    const setSel = (field, v) => {
      if (!sel) return;
      const lo = selected === 0 ? 0 : (selected === points.length - 1 ? 255 : points[selected - 1].original);
      const hi = selected === 0 ? 0 : (selected === points.length - 1 ? 255 : points[selected + 1].original);
      const val = field === 'original' ? Math.max(lo, Math.min(hi, v)) : Math.max(0, Math.min(255, v));
      change(points.map((p, k) => k === selected ? Object.assign({}, p, { [field]: val }) : p));
    };
    const save = () => {
      const n = name.trim();
      if (!n) { setMsg('The modifier name cannot be empty.'); return Promise.reject(); }
      return FF.mutate(qlc, 'fixtures.modifiers.save', { name: n, points }).then(r => { setCurrent(r.name); setDirty(false); setMsg('Saved "' + r.name + '".'); return r.name; })
        .catch(e => { setMsg((e && e.message) || 'Could not save'); throw e; });
    };
    const rename = () => {
      const n = name.trim();
      if (!current || !n || n === current) return;
      FF.mutate(qlc, 'fixtures.modifiers.rename', { name: current, newName: n }).then(() => { setCurrent(n); setMsg('Renamed to "' + n + '".'); }).catch(e => setMsg((e && e.message) || 'Could not rename'));
    };
    const remove = () => {
      setConfirmDelete(false);
      FF.mutate(qlc, 'fixtures.modifiers.delete', { name: current }).then(r => { setMsg('Deleted' + (r.detachedFixtureIds && r.detachedFixtureIds.length ? ' (detached from ' + r.detachedFixtureIds.length + ' fixture(s))' : '') + '.'); load(''); })
        .catch(e => setMsg((e && e.message) || 'Could not delete'));
    };
    const buttons = onApply ? ['Cancel', 'Apply to channel'] : ['Close'];
    const clicked = (b) => {
      if (b !== 'Apply to channel') { onClose(); return; }
      if (dirty || !current) save().then(n => onApply(n)).catch(() => {});
      else onApply(current);
    };
    return (
      <CustomPopupDialog open={open} title={'Channel Modifiers Editor' + (channelLabel ? ' — channel ' + channelLabel : '')} width={660} standardButtons={buttons} onClicked={clicked} onClose={onClose}>
        <div data-mod-editor="1" style={{ display: 'flex', gap: 10 }}>
          <div style={{ width: 190, flex: 'none', display: 'flex', flexDirection: 'column', gap: 2, maxHeight: 330, overflow: 'auto', border: 'var(--border-dark)', background: 'var(--bg-stronger)' }}>
            <div onClick={() => load('')} data-mod-template="" style={{ padding: '3px 6px', cursor: 'pointer', background: current === '' ? 'var(--highlight)' : 'transparent' }}>
              <RobotoText label="(new template)" fontSize={13} height={20} labelColor="var(--fg-light)" />
            </div>
            {(templates || []).map(t => (
              <div key={t.name} onClick={() => load(t.name)} data-mod-template={t.name} style={{ padding: '3px 6px', cursor: 'pointer', display: 'flex', gap: 4, background: current === t.name ? 'var(--highlight)' : 'transparent' }}>
                <RobotoText label={t.name} fontSize={13} height={20} style={{ flex: 1 }} />
                <RobotoText label={t.isUser ? 'user' : 'system'} fontSize={11} height={20} labelColor="var(--fg-medium)" />
              </div>
            ))}
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6, flex: 1, minWidth: 0 }}>
            <div style={{ display: 'flex', gap: 10 }}>
              <CurveEditor points={points} selected={selected} onSelect={setSelected} onChange={change} />
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                <RobotoText label="Selected handler" fontSize={12} labelColor="var(--fg-light)" />
                <FF.Row label="Original" width={60}><CustomSpinBox value={sel ? sel.original : 0} from={0} to={255} width={70} disabled={!sel || selected === 0 || selected === points.length - 1} onValueModified={v => setSel('original', v)} /></FF.Row>
                <FF.Row label="Modified" width={60}><CustomSpinBox value={sel ? sel.modified : 0} from={0} to={255} width={70} disabled={!sel} onValueModified={v => setSel('modified', v)} /></FF.Row>
                <GenericButton label="Add handler" width={110} height={24} onClick={addPoint} />
                <GenericButton label="Remove handler" width={110} height={24} disabled={!sel || selected === 0 || selected === points.length - 1} onClick={removePoint} />
                <RobotoText label={points.length + ' handlers'} fontSize={11} labelColor="var(--fg-medium)" />
              </div>
            </div>
            <FF.Note text="Drag a handler to reshape the curve (x: original DMX value, y: output); double-click the grid to add one. The first and last handlers stay at 0 and 255." />
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <RobotoText label="Name" fontSize={13} labelColor="var(--fg-light)" style={{ width: 40 }} />
              <input data-mod-name="1" value={name} onChange={e => setName(e.target.value)} style={Object.assign({ flex: 1 }, inputStyle)} />
            </div>
            <div style={{ display: 'flex', gap: 6 }}>
              <GenericButton label="Save as template" width={130} height={24} onClick={() => save().catch(() => {})} data-mod-save="1" />
              <GenericButton label="Rename" width={80} height={24} disabled={!isUser || !name.trim() || name.trim() === current} onClick={rename} />
              <GenericButton label="Delete" width={70} height={24} disabled={!isUser} onClick={() => setConfirmDelete(true)} />
            </div>
            {info && !info.isUser ? <FF.Note text="System template: read-only. Save it under another name to make a user copy." /> : null}
            {msg ? <span data-mod-msg="1"><RobotoText label={msg} fontSize={12} labelColor="var(--fg-light)" /></span> : null}
          </div>
        </div>
        <CustomPopupDialog open={confirmDelete} title="Delete modifier template" width={380} message={'Delete the template "' + current + '"? Channels using it lose their modifier.'}
          standardButtons={['Cancel', 'Delete']} onClicked={b => { if (b === 'Delete') remove(); else setConfirmDelete(false); }} onClose={() => setConfirmDelete(false)} />
      </CustomPopupDialog>
    );
  }

  /* ---- printing ---------------------------------------------------------------------------------- */
  /** Print only the given HTML: a print-media stylesheet hides the app while #qlc-print-root is
      shown. The root stays in the DOM (hidden on screen) until the next print. */
  function printHtml(title, html) {
    let root = document.getElementById('qlc-print-root');
    if (!root) {
      const style = document.createElement('style');
      style.id = 'qlc-print-style';
      style.textContent = '#qlc-print-root{display:none}@media print{body>*:not(#qlc-print-root){display:none!important}#qlc-print-root{display:block!important;color:#000;background:#fff;font-family:Roboto,Arial,sans-serif;font-size:11pt}'
        + '#qlc-print-root table{border-collapse:collapse;width:100%}#qlc-print-root th,#qlc-print-root td{border:1px solid #888;padding:3px 5px;text-align:left;vertical-align:middle}'
        + '#qlc-print-root th{background:#eee}#qlc-print-root h1{font-size:16pt;margin:0 0 8px}#qlc-print-root h2{font-size:13pt;margin:12px 0 6px}.dip{display:inline-flex;gap:2px}.dip i{display:inline-block;width:9px;height:14px;border:1px solid #333;position:relative}.dip i.on:after{content:"";position:absolute;left:1px;right:1px;top:1px;height:5px;background:#333}.dip i:not(.on):after{content:"";position:absolute;left:1px;right:1px;bottom:1px;height:5px;background:#333}}';
      document.head.appendChild(style);
      root = document.createElement('div');
      root.id = 'qlc-print-root';
      document.body.appendChild(root);
    }
    root.innerHTML = '<h1>' + esc(title) + '</h1>' + html;
    setTimeout(() => window.print(), 0);
  }
  function esc(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }

  function physicalRows(p) {
    if (!p) return [];
    const rows = [];
    const add = (label, v, unit) => { if (v !== undefined && v !== null && v !== '' && v !== 0) rows.push([label, v + (unit || '')]); };
    add('Width', p.width, ' mm'); add('Height', p.height, ' mm'); add('Depth', p.depth, ' mm');
    add('Weight', p.weight, ' kg'); add('Power consumption', p.powerConsumption, ' W');
    add('DMX connector', p.dmxConnector);
    add('Bulb', p.bulbType); add('Lumens', p.bulbLumens); add('Colour temperature', p.bulbColourTemperature, ' K');
    add('Lens', p.lensName);
    if (p.lensDegreesMin || p.lensDegreesMax) rows.push(['Beam angle', (p.lensDegreesMin || 0) + '° – ' + (p.lensDegreesMax || 0) + '°']);
    add('Head type', p.focusType); add('Pan range', p.focusPanMax, '°'); add('Tilt range', p.focusTiltMax, '°');
    if (p.layoutWidth > 1 || p.layoutHeight > 1) rows.push(['Pixel layout', p.layoutWidth + ' × ' + p.layoutHeight]);
    return rows;
  }
  function addrText(f) { return 'U' + (f.universe + 1) + ' · ' + (f.address + 1) + (f.channels > 1 ? '–' + (f.address + f.channels) : ''); }

  /* ---- fixture summary --------------------------------------------------------------------------- */
  function FixtureSummaryDialog({ open, qlc, fixtureId, onClose }) {
    const detail = FF.useFixtureDetail(qlc, open ? fixtureId : null);
    const d = detail;
    const phys = d ? physicalRows(d.physical) : [];
    const print = () => {
      if (!d) return;
      const kv = (rows) => '<table>' + rows.map(r => '<tr><th style="width:35%">' + esc(r[0]) + '</th><td>' + esc(r[1]) + '</td></tr>').join('') + '</table>';
      printHtml(d.name, '<h2>Definition</h2>' + kv([['Manufacturer', d.manufacturer || '—'], ['Model', d.model || '—'], ['Mode', d.mode || '—'], ['Type', d.fixtureType], ['Heads', d.heads]])
        + '<h2>Addressing</h2>' + kv([['Universe', d.universe + 1], ['Address', (d.address + 1) + ' – ' + (d.address + d.channels)], ['Channels', d.channels]])
        + (phys.length ? '<h2>Physical</h2>' + kv(phys) : '')
        + '<h2>Channels</h2><table><tr><th>#</th><th>Name</th><th>Group</th><th>DMX</th></tr>'
        + d.channelList.map(ch => '<tr><td>' + (ch.index + 1) + '</td><td>' + esc(ch.name) + '</td><td>' + esc(ch.group) + '</td><td>' + (ch.absoluteAddress - d.universe * 512 + 1) + '</td></tr>').join('') + '</table>');
    };
    const KV = ({ rows }) => (
      <div style={{ display: 'grid', gridTemplateColumns: '150px 1fr', rowGap: 2, columnGap: 8 }}>
        {rows.map(r => <React.Fragment key={r[0]}><RobotoText label={r[0]} fontSize={13} labelColor="var(--fg-light)" height={20} /><RobotoText label={String(r[1])} fontSize={13} height={20} /></React.Fragment>)}
      </div>
    );
    return (
      <CustomPopupDialog open={open} title={'Fixture summary' + (d ? ' — ' + d.name : '')} width={620} standardButtons={['Close']} onClicked={onClose} onClose={onClose}>
        {!d ? <RobotoText label="Loading…" fontSize={13} /> : (
          <div data-fx-summary="1" style={{ display: 'flex', flexDirection: 'column', gap: 8, maxHeight: '65vh', overflow: 'auto' }}>
            <div style={{ display: 'flex', justifyContent: 'flex-end' }}><IconButton faSource={G.print} size={26} tooltip="Print the fixture summary" onClick={print} data-fx-print="1" /></div>
            <FF.Heading text="Definition" />
            <KV rows={[['Manufacturer', d.manufacturer || '—'], ['Model', d.model || '—'], ['Mode', d.mode || '—'], ['Type', d.fixtureType || '—'], ['Heads', d.heads != null ? d.heads : '—']]} />
            <FF.Heading text="Addressing" />
            <KV rows={[['Universe', d.universe + 1], ['Address range', (d.address + 1) + ' – ' + (d.address + d.channels)], ['Channels', d.channels]]} />
            <FF.Heading text="Physical" />
            {phys.length ? <KV rows={phys} /> : <FF.Note text="The definition has no physical information." />}
            <FF.Heading text="Channels" />
            <div style={{ display: 'grid', gridTemplateColumns: '30px 1fr 110px 60px', rowGap: 1, columnGap: 6 }}>
              {d.channelList.map(ch => <React.Fragment key={ch.index}>
                <RobotoText label={String(ch.index + 1)} fontSize={12} height={18} labelColor="var(--fg-medium)" />
                <RobotoText label={ch.name} fontSize={12} height={18} />
                <RobotoText label={ch.group} fontSize={12} height={18} labelColor="var(--fg-light)" />
                <RobotoText label={'DMX ' + (ch.absoluteAddress - d.universe * 512 + 1)} fontSize={12} height={18} labelColor="var(--fg-light)" />
              </React.Fragment>)}
            </div>
          </div>
        )}
      </CustomPopupDialog>
    );
  }

  /* ---- universe summary -------------------------------------------------------------------------- */
  function DipSwitch({ address }) {
    const v = address + 1; /* DMXAddressWidget shows the 1-based start address, switch 1 = bit 0 */
    return (
      <span style={{ display: 'inline-flex', gap: 2 }} title={'DIP: ' + v}>
        {Array.from({ length: 10 }, (_, i) => {
          const on = !!(v & (1 << i));
          return (
            <span key={i} style={{ width: 9, height: 16, border: '1px solid var(--fg-medium)', position: 'relative', background: 'var(--bg-stronger)', boxSizing: 'border-box' }}>
              <span style={{ position: 'absolute', left: 1, right: 1, height: 6, top: on ? 1 : undefined, bottom: on ? undefined : 1, background: on ? 'var(--highlight)' : 'var(--fg-medium)' }} />
            </span>
          );
        })}
      </span>
    );
  }
  function UniverseSummaryDialog({ open, qlc, universes, fixtures, initialUniverse, onClose }) {
    const [uni, setUni] = React.useState(initialUniverse || 0);
    const [cols, setCols] = React.useState({ manufacturer: true, model: true, weight: true, power: true, dip: false });
    React.useEffect(() => { if (open && initialUniverse != null) setUni(initialUniverse); }, [open, initialUniverse]);
    const list = React.useMemo(() => (fixtures || []).filter(f => f.universe === uni).sort((a, b) => a.address - b.address), [fixtures, uni]);
    const details = FF.useFixtureDetails(qlc, open ? list.map(f => String(f.id)) : []);
    const rows = list.map(f => { const d = details[String(f.id)]; const p = d && d.physical; return { f, weight: p ? p.weight : null, power: p ? p.powerConsumption : null }; });
    const used = list.reduce((a, f) => a + f.channels, 0);
    const weight = rows.reduce((a, r) => a + (r.weight || 0), 0);
    const power = rows.reduce((a, r) => a + (r.power || 0), 0);
    const fuzzy = rows.filter(r => !r.power).length;
    const loading = list.some(f => !details[String(f.id)]);
    const u = (universes || []).find(x => x.id === uni);
    const print = () => {
      const head = ['ID', 'Name'].concat(cols.manufacturer ? ['Manufacturer'] : [], cols.model ? ['Model'] : [], ['Address', 'Channels'], cols.weight ? ['Weight'] : [], cols.power ? ['Consumption'] : [], cols.dip ? ['DIP switch'] : []);
      const dip = (a) => '<span class="dip">' + Array.from({ length: 10 }, (_, i) => '<i class="' + (((a + 1) & (1 << i)) ? 'on' : '') + '"></i>').join('') + '</span>';
      const body = rows.map(r => '<tr><td>' + r.f.id + '</td><td>' + esc(r.f.name) + '</td>' + (cols.manufacturer ? '<td>' + esc(r.f.manufacturer || '') + '</td>' : '') + (cols.model ? '<td>' + esc(r.f.model || '') + '</td>' : '')
        + '<td>' + (r.f.address + 1) + '–' + (r.f.address + r.f.channels) + '</td><td>' + r.f.channels + '</td>' + (cols.weight ? '<td>' + (r.weight ? r.weight + ' kg' : '') + '</td>' : '') + (cols.power ? '<td>' + (r.power ? r.power + ' W' : '') + '</td>' : '') + (cols.dip ? '<td>' + dip(r.f.address) + '</td>' : '') + '</tr>').join('');
      printHtml('Universe summary — ' + (u ? u.name : 'Universe ' + (uni + 1)), '<table><tr>' + head.map(h => '<th>' + h + '</th>').join('') + '</tr>' + body + '</table>'
        + '<h2>Summary</h2><table><tr><th>DMX channels used</th><td>' + used + ' / 512</td></tr><tr><th>Total weight</th><td>' + weight.toFixed(2) + ' kg</td></tr><tr><th>Estimated power consumption</th><td>' + power + ' W' + (fuzzy ? ' (+ ' + fuzzy + ' fixtures without data)' : '') + '</td></tr></table>');
    };
    const colToggle = (k, label) => (
      <label key={k} style={{ display: 'inline-flex', alignItems: 'center', gap: 4, cursor: 'pointer' }}>
        <CustomCheckBox checked={cols[k]} size={16} onToggled={v => setCols(c => Object.assign({}, c, { [k]: v }))} />
        <RobotoText label={label} fontSize={12} height={18} />
      </label>
    );
    const th = (t, w) => <th style={{ textAlign: 'left', padding: '2px 6px', width: w, fontWeight: 'normal', color: 'var(--fg-light)', borderBottom: 'var(--border-dark)' }}>{t}</th>;
    const td = (c, extra) => <td style={Object.assign({ padding: '2px 6px', borderBottom: '1px solid var(--bg-control)' }, extra)}>{c}</td>;
    return (
      <CustomPopupDialog open={open} title="Universe summary" width={860} standardButtons={['Close']} onClicked={onClose} onClose={onClose}>
        <div data-uni-summary="1" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
            <CustomComboBox width={180} currValue={uni} model={(universes || []).map(x => ({ mLabel: x.name, mValue: x.id }))} onValueChanged={setUni} />
            {colToggle('manufacturer', 'Manufacturer')}{colToggle('model', 'Model')}{colToggle('weight', 'Weight')}{colToggle('power', 'Consumption')}{colToggle('dip', 'DIP switch')}
            <span style={{ flex: 1 }} />
            <IconButton faSource={G.print} size={26} tooltip="Print the universe summary" onClick={print} data-uni-print="1" />
          </div>
          <div style={{ maxHeight: '55vh', overflow: 'auto' }}>
            <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--font-roboto)', fontSize: 13, color: 'var(--fg-main)' }}>
              <thead><tr>{th('ID', 40)}{th('Name')}{cols.manufacturer ? th('Manufacturer') : null}{cols.model ? th('Model') : null}{th('Address', 80)}{th('Ch', 40)}{cols.weight ? th('Weight', 70) : null}{cols.power ? th('Power', 70) : null}{cols.dip ? th('DIP switch', 120) : null}</tr></thead>
              <tbody>{rows.map(r => (
                <tr key={r.f.id} data-uni-row={r.f.id}>
                  {td(r.f.id, { color: 'var(--fg-medium)' })}{td(r.f.name)}{cols.manufacturer ? td(r.f.manufacturer || '—') : null}{cols.model ? td(r.f.model || '—') : null}
                  {td((r.f.address + 1) + '–' + (r.f.address + r.f.channels))}{td(r.f.channels)}
                  {cols.weight ? td(r.weight ? r.weight + ' kg' : '—') : null}{cols.power ? td(r.power ? r.power + ' W' : '—') : null}{cols.dip ? td(<DipSwitch address={r.f.address} />) : null}
                </tr>
              ))}</tbody>
            </table>
            {!rows.length ? <FF.Note text="No fixtures in this universe." /> : null}
          </div>
          <div data-uni-totals="1" style={{ display: 'grid', gridTemplateColumns: '220px 1fr', rowGap: 2 }}>
            <RobotoText label="DMX channels used:" fontSize={13} labelColor="var(--fg-light)" /><RobotoText label={used + ' / 512'} fontSize={13} />
            <RobotoText label="Total weight:" fontSize={13} labelColor="var(--fg-light)" /><RobotoText label={loading ? '…' : weight.toFixed(2) + ' kg'} fontSize={13} />
            <RobotoText label="Estimated power consumption:" fontSize={13} labelColor="var(--fg-light)" /><RobotoText label={loading ? '…' : power + ' W' + (fuzzy ? ' (+ ' + fuzzy + ' fixtures without data)' : '')} fontSize={13} />
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- generic RGB panel ----------------------------------------------------------------------- */
  const COMPONENTS = ['RGB', 'BGR', 'BRG', 'GBR', 'GRB', 'RBG', 'RGBW'];
  function RgbPanelDialog({ open, qlc, universes, onClose, onCreated }) {
    const [p, setP] = React.useState({ name: 'RGB Panel', universe: 0, address: 1, components: 'RGB', columns: 10, rows: 10, physicalWidth: 1000, physicalHeight: 1000, startCorner: 'topLeft', displacement: 'snake', direction: 'horizontal' });
    const [err, setErr] = React.useState('');
    const set = (k, v) => setP(o => Object.assign({}, o, { [k]: v }));
    const perRow = (p.direction === 'vertical' ? p.rows : p.columns) * (p.components === 'RGBW' ? 4 : 3);
    const fixturesCount = p.direction === 'vertical' ? p.columns : p.rows;
    React.useEffect(() => {
      if (!open || !qlc.online) return;
      setErr('');
      qlc.call('fixtures.findAvailableAddress', { universe: p.universe, channels: perRow, quantity: 1, requestedAddress: p.address - 1 })
        .then(r => { if (r && r.available && r.address !== p.address - 1) set('address', r.address + 1); }).catch(() => {});
    }, [open, p.universe, perRow]);
    const create = () => {
      setErr('');
      FF.mutate(qlc, 'fixtures.createRgbPanel', { name: p.name, universe: p.universe, address: p.address - 1, columns: p.columns, rows: p.rows, components: p.components,
        physicalWidth: p.physicalWidth, physicalHeight: p.physicalHeight, startCorner: p.startCorner, displacement: p.displacement, direction: p.direction })
        .then(r => { onCreated && onCreated(r); onClose(); })
        .catch(e => setErr((e && e.message) || String(e)));
    };
    const combo = (k, model, w) => <CustomComboBox width={w || 170} currValue={p[k]} model={model} onValueChanged={v => set(k, v)} />;
    return (
      <CustomPopupDialog open={open} title="Add an RGB panel" width={460} standardButtons={['Cancel', 'Add']} onClicked={b => { if (b === 'Add') create(); else onClose(); }} onClose={onClose}>
        <div data-rgbpanel="1" style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          <FF.Row label="Name" width={120}><input data-rgbpanel-name="1" value={p.name} onChange={e => set('name', e.target.value)} style={Object.assign({ width: 200 }, inputStyle)} /></FF.Row>
          <FF.Row label="Universe" width={120}>{combo('universe', (universes || []).map(u => ({ mLabel: u.name, mValue: u.id })))}</FF.Row>
          <FF.Row label="Address" width={120}><CustomSpinBox value={p.address} from={1} to={512} width={90} onValueModified={v => set('address', v)} /></FF.Row>
          <FF.Row label="Components" width={120}>{combo('components', COMPONENTS.map(c => ({ mLabel: c, mValue: c })), 100)}</FF.Row>
          <FF.Row label="Columns" width={120}><span data-rgbpanel-cols="1"><CustomSpinBox value={p.columns} from={1} to={170} width={90} onValueModified={v => set('columns', v)} /></span></FF.Row>
          <FF.Row label="Rows" width={120}><span data-rgbpanel-rows="1"><CustomSpinBox value={p.rows} from={1} to={999} width={90} onValueModified={v => set('rows', v)} /></span></FF.Row>
          <FF.Row label="Physical width" width={120}><CustomSpinBox value={p.physicalWidth} from={1} to={99999} width={100} onValueModified={v => set('physicalWidth', v)} /><RobotoText label="mm" fontSize={13} /></FF.Row>
          <FF.Row label="Physical height" width={120}><CustomSpinBox value={p.physicalHeight} from={1} to={99999} width={100} onValueModified={v => set('physicalHeight', v)} /><RobotoText label="mm" fontSize={13} /></FF.Row>
          <FF.Row label="Start corner" width={120}>{combo('startCorner', [['topLeft', 'Top-Left'], ['topRight', 'Top-Right'], ['bottomLeft', 'Bottom-Left'], ['bottomRight', 'Bottom-Right']].map(x => ({ mLabel: x[1], mValue: x[0] })))}</FF.Row>
          <FF.Row label="Displacement" width={120}>{combo('displacement', [['snake', 'Snake'], ['zigzag', 'Zig Zag']].map(x => ({ mLabel: x[1], mValue: x[0] })))}</FF.Row>
          <FF.Row label="Direction" width={120}>{combo('direction', [['horizontal', 'Horizontal'], ['vertical', 'Vertical']].map(x => ({ mLabel: x[1], mValue: x[0] })))}</FF.Row>
          <FF.Note text={fixturesCount + ' fixture' + (fixturesCount === 1 ? '' : 's') + ' of ' + perRow + ' channels (' + fixturesCount * perRow + ' in total) plus a fixture group "' + p.name + '". A row that does not fit the rest of the universe continues in the next existing universe.'} />
          {err ? <span data-rgbpanel-error="1"><RobotoText label={err} fontSize={13} labelColor="var(--override-red)" /></span> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- fixture group grid editor ------------------------------------------------------------- */
  function hueFor(id) { const n = Number(id) || 0; return (n * 137.508) % 360; }
  /** FixtureGroupEditor::transformSelection() maths on a list of {x, y}: returns new positions. */
  function transformPoints(pts, kind) {
    const minX = Math.min(...pts.map(p => p.x)), maxX = Math.max(...pts.map(p => p.x));
    const minY = Math.min(...pts.map(p => p.y)), maxY = Math.max(...pts.map(p => p.y));
    const w = maxX - minX + 1, h = maxY - minY + 1;
    return pts.map(p => {
      const x = p.x - minX, y = p.y - minY;
      let nx = x, ny = y;
      if (kind === 'r90') { nx = h - 1 - y; ny = x; }
      else if (kind === 'r180') { nx = w - 1 - x; ny = h - 1 - y; }
      else if (kind === 'r270') { nx = y; ny = w - 1 - x; }
      else if (kind === 'flipH') { nx = w - 1 - x; }
      else if (kind === 'flipV') { ny = h - 1 - y; }
      return { x: nx + minX, y: ny + minY };
    });
  }
  function GroupGridEditor({ open, qlc, group, fixtures, candidateIds, onClose, reload }) {
    const [sel, setSel] = React.useState([]); /* ['x,y'] */
    const [armed, setArmed] = React.useState(null); /* {fixtureId, headIndex} to place by clicking a cell */
    const [cols, setCols] = React.useState(group ? group.size.columns : 1);
    const [rows, setRows] = React.useState(group ? group.size.rows : 1);
    const [regenRows, setRegenRows] = React.useState(0);
    const [busy, setBusy] = React.useState(false);
    const [confirmReset, setConfirmReset] = React.useState(false);
    const dragFrom = React.useRef(null);
    React.useEffect(() => { if (group) { setCols(group.size.columns); setRows(group.size.rows); } }, [group && group.size.columns, group && group.size.rows]);
    React.useEffect(() => { setSel([]); setArmed(null); }, [open, group && group.id]);
    if (!group) return null;
    const gid = String(group.id);
    const byPos = {};
    (group.heads || []).forEach(h => { byPos[h.x + ',' + h.y] = h; });
    const fx = (id) => (fixtures || []).find(f => String(f.id) === String(id));
    const run = (list) => { setBusy(true); return FF.mutateSeq(qlc, list).catch(() => {}).then(() => { setBusy(false); return reload(); }); };
    const W = Math.max(group.size.columns, ...(group.heads || []).map(h => h.x + 1));
    const H = Math.max(group.size.rows, ...(group.heads || []).map(h => h.y + 1));
    const cell = Math.max(22, Math.min(48, Math.floor(560 / Math.max(W, 1))));
    const inGroup = new Set((group.heads || []).map(h => h.fixtureId + ':' + h.headIndex));
    const candidates = Array.from(new Set((candidateIds || []).map(String).concat((group.heads || []).map(h => String(h.fixtureId))))).map(fx).filter(Boolean);

    const moveHead = (fromKey, x, y) => {
      const h = byPos[fromKey];
      if (!h) return;
      const [fx0, fy0] = fromKey.split(',').map(Number);
      if (fx0 === x && fy0 === y) return;
      run([['fixtures.group.swapHeads', { groupId: gid, ax: fx0, ay: fy0, bx: x, by: y }]]);
    };
    const place = (x, y) => {
      const list = [];
      if (byPos[x + ',' + y]) list.push(['fixtures.group.unassignHead', { groupId: gid, x, y }]);
      list.push(['fixtures.group.assignHead', { groupId: gid, fixtureId: String(armed.fixtureId), headIndex: armed.headIndex, x, y }]);
      run(list).then(() => setArmed(null));
    };
    const down = (key, e) => { dragFrom.current = byPos[key] ? key : null; e.preventDefault(); };
    const up = (key, e) => {
      const [x, y] = key.split(',').map(Number);
      const from = dragFrom.current; dragFrom.current = null;
      if (armed) { place(x, y); return; }
      if (from && from !== key) { moveHead(from, x, y); setSel([key]); return; }
      if (!byPos[key]) { setSel([]); return; }
      if (e.ctrlKey || e.metaKey || e.shiftKey) setSel(s => s.indexOf(key) === -1 ? s.concat([key]) : s.filter(k => k !== key));
      else setSel(s => (s.length === 1 && s[0] === key) ? [] : [key]);
    };
    const swap = () => { if (sel.length !== 2) return; const [a, b] = sel.map(k => k.split(',').map(Number)); run([['fixtures.group.swapHeads', { groupId: gid, ax: a[0], ay: a[1], bx: b[0], by: b[1] }]]); };
    const removeSel = () => { run(sel.map(k => { const [x, y] = k.split(',').map(Number); return ['fixtures.group.unassignHead', { groupId: gid, x, y }]; })).then(() => setSel([])); };
    /* Transform the selection (or the whole group) and apply it as a chain of swaps: each head is
       swapped into its target cell; whatever sat there moves to the head's old cell and is moved
       on by a later swap if it is part of the transformation. */
    const transform = (kind) => {
      const keys = sel.length ? sel : Object.keys(byPos);
      if (!keys.length) return;
      const pts = keys.map(k => { const [x, y] = k.split(',').map(Number); return { x, y }; });
      const targets = transformPoints(pts, kind);
      const needW = Math.max(group.size.columns, ...targets.map(t => t.x + 1)), needH = Math.max(group.size.rows, ...targets.map(t => t.y + 1));
      const list = [];
      if (needW !== group.size.columns || needH !== group.size.rows) list.push(['fixtures.group.setSize', { groupId: gid, columns: needW, rows: needH }]);
      const pos = {}; /* headKey -> 'x,y' */
      const at = {}; /* 'x,y' -> headKey */
      Object.keys(byPos).forEach(k => { const h = byPos[k]; const hk = h.fixtureId + ':' + h.headIndex; pos[hk] = k; at[k] = hk; });
      pts.forEach((p, i) => {
        const hk = at[p.x + ',' + p.y];
        const cur = pos[hk], tgt = targets[i].x + ',' + targets[i].y;
        if (!hk || cur === tgt) return;
        const [ax, ay] = cur.split(',').map(Number);
        list.push(['fixtures.group.swapHeads', { groupId: gid, ax, ay, bx: targets[i].x, by: targets[i].y }]);
        const other = at[tgt];
        at[tgt] = hk; pos[hk] = tgt;
        if (other) { at[cur] = other; pos[other] = cur; } else delete at[cur];
      });
      run(list).then(() => setSel(targets.map(t => t.x + ',' + t.y)));
    };
    /* FixtureGroupEditor::regenerateFromDmxOrder(): members sorted by address, laid out row by row. */
    const regenerate = () => {
      const ids = Array.from(new Set((group.heads || []).map(h => String(h.fixtureId)))).map(fx).filter(Boolean)
        .sort((a, b) => (a.universe * 512 + a.address) - (b.universe * 512 + b.address));
      if (!ids.length) return;
      const total = ids.reduce((a, f) => a + Math.max(1, f.heads || 1), 0);
      const c = Math.max(1, regenRows > 0 ? Math.ceil(total / regenRows) : Math.ceil(Math.sqrt(total)));
      const list = [['fixtures.group.setSize', { groupId: gid, columns: c, rows: Math.ceil(total / c) }], ['fixtures.group.reset', { groupId: gid }]];
      let i = 0;
      ids.forEach(f => { list.push(['fixtures.group.assignFixture', { groupId: gid, fixtureId: String(f.id), x: i % c, y: Math.floor(i / c) }]); i += Math.max(1, f.heads || 1); });
      run(list).then(() => setSel([]));
    };
    const cellsEls = [];
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const key = x + ',' + y;
      const h = byPos[key];
      const f = h ? fx(h.fixtureId) : null;
      const outside = x >= group.size.columns || y >= group.size.rows;
      const isSel = sel.indexOf(key) !== -1;
      cellsEls.push(
        <div key={key} data-grid-cell={key} data-head={h ? h.fixtureId + ':' + h.headIndex : ''} onMouseDown={e => down(key, e)} onMouseUp={e => up(key, e)}
          title={h ? (f ? f.name : 'Fixture ' + h.fixtureId) + '\nHead ' + (h.headIndex + 1) + (f ? '\nAddress ' + (f.address + 1) + ', universe ' + (f.universe + 1) : '') : (armed ? 'Place the armed head here' : 'Empty')}
          style={{ width: cell, height: cell, boxSizing: 'border-box', border: isSel ? '2px solid var(--highlight)' : '1px solid var(--bg-control)', cursor: armed ? 'copy' : (h ? 'grab' : 'default'),
            background: h ? 'hsl(' + hueFor(h.fixtureId) + ',45%,32%)' : (outside ? 'repeating-linear-gradient(45deg,var(--bg-stronger),var(--bg-stronger) 3px,var(--bg-medium) 3px,var(--bg-medium) 6px)' : 'var(--bg-stronger)'),
            display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', overflow: 'hidden', userSelect: 'none', fontFamily: 'var(--font-roboto)', fontSize: 10, color: 'var(--fg-main)', lineHeight: 1.1 }}>
          {h ? <><span>{String(h.fixtureId)}</span><span style={{ color: 'var(--fg-light)' }}>{'h' + (h.headIndex + 1)}</span></> : null}
        </div>
      );
    }
    return (
      <CustomPopupDialog open={open} title={'Fixture group layout — ' + group.name} width={Math.min(980, Math.max(640, W * cell + 300))} standardButtons={['Close']} onClicked={onClose} onClose={onClose}>
        <div data-grid-editor="1" style={{ display: 'flex', gap: 10 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6, minWidth: 0 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
              <RobotoText label="Size" fontSize={13} labelColor="var(--fg-light)" />
              <span data-grid-cols="1"><CustomSpinBox value={cols} from={1} to={999} width={70} onValueModified={setCols} /></span>
              <RobotoText label="×" fontSize={13} />
              <span data-grid-rows="1"><CustomSpinBox value={rows} from={1} to={999} width={70} onValueModified={setRows} /></span>
              <GenericButton label="Set size" width={70} height={24} disabled={busy || (cols === group.size.columns && rows === group.size.rows)}
                onClick={() => run([['fixtures.group.setSize', { groupId: gid, columns: cols, rows: rows }]])} data-grid-setsize="1" />
              <span style={{ width: 8 }} />
              <IconButton faSource={G.rotate} size={26} disabled={busy} tooltip="Rotate 90° clockwise (selection, or the whole group)" onClick={() => transform('r90')} data-grid-rotate="90" />
              <GenericButton label="180°" width={46} height={24} disabled={busy} onClick={() => transform('r180')} />
              <GenericButton label="270°" width={46} height={24} disabled={busy} onClick={() => transform('r270')} />
              <IconButton faSource={G.flipH} size={26} disabled={busy} tooltip="Flip horizontally" onClick={() => transform('flipH')} data-grid-flip="h" />
              <IconButton faSource={G.flipV} size={26} disabled={busy} tooltip="Flip vertically" onClick={() => transform('flipV')} data-grid-flip="v" />
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
              <GenericButton label="Swap" width={60} height={24} disabled={busy || sel.length !== 2} onClick={swap} data-grid-swap="1" />
              <GenericButton label="Remove" width={70} height={24} disabled={busy || !sel.length} onClick={removeSel} />
              <span style={{ width: 8 }} />
              <GenericButton label="Regenerate in DMX order" width={170} height={24} disabled={busy} onClick={regenerate} />
              <RobotoText label="rows" fontSize={12} labelColor="var(--fg-light)" />
              <CustomSpinBox value={regenRows} from={0} to={999} width={64} onValueModified={setRegenRows} />
              <GenericButton label="Reset" width={60} height={24} disabled={busy} onClick={() => setConfirmReset(true)} />
            </div>
            <div data-grid="1" style={{ display: 'grid', gridTemplateColumns: 'repeat(' + W + ', ' + cell + 'px)', gap: 1, overflow: 'auto', maxHeight: '55vh', maxWidth: 620, alignSelf: 'flex-start', padding: 2, background: 'var(--bg-strong)' }}>
              {cellsEls}
            </div>
            <FF.Note text="Click a head to select it (Ctrl-click for several); drag a head onto another cell to move it (heads swap places). Rotate / flip act on the selection, or on the whole group when nothing is selected." />
          </div>
          <div style={{ width: 210, flex: 'none', display: 'flex', flexDirection: 'column', gap: 4 }}>
            <RobotoText label="Assign heads" fontBold fontSize={13} />
            <FF.Note text="Pick a head, then click a cell. Fixtures selected in the tree are listed too." />
            <div style={{ maxHeight: '50vh', overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 4 }}>
              {candidates.map(f => (
                <div key={f.id}>
                  <RobotoText label={f.name} fontSize={12} height={18} />
                  <div style={{ display: 'flex', flexWrap: 'wrap', gap: 2 }}>
                    {Array.from({ length: Math.max(1, f.heads || 1) }, (_, i) => {
                      const used = inGroup.has(String(f.id) + ':' + i);
                      const isArmed = armed && String(armed.fixtureId) === String(f.id) && armed.headIndex === i;
                      return (
                        <button key={i} type="button" data-grid-head={f.id + ':' + i} onClick={() => setArmed(isArmed ? null : { fixtureId: f.id, headIndex: i })}
                          title={used ? 'Already in the group: placing it moves it' : 'Not in the group yet'}
                          style={{ minWidth: 26, height: 22, border: 'var(--border-control)', cursor: 'pointer', fontSize: 11, fontFamily: 'var(--font-roboto)', color: 'var(--fg-main)',
                            background: isArmed ? 'var(--highlight)' : (used ? 'hsl(' + hueFor(f.id) + ',30%,26%)' : 'var(--bg-control)') }}>{i + 1}</button>
                      );
                    })}
                  </div>
                </div>
              ))}
              {!candidates.length ? <RobotoText label="Select fixtures in the tree to assign their heads." fontSize={12} labelColor="var(--fg-medium)" /> : null}
            </div>
          </div>
        </div>
        <CustomPopupDialog open={confirmReset} title="Reset fixture group" width={360} message={'Remove every head from "' + group.name + '"? The group itself stays.'}
          standardButtons={['Cancel', 'Reset']} onClicked={b => { setConfirmReset(false); if (b === 'Reset') run([['fixtures.group.reset', { groupId: gid }]]); }} onClose={() => setConfirmReset(false)} />
      </CustomPopupDialog>
    );
  }

  /* ---- colour filters --------------------------------------------------------------------------- */
  let filtersCache = null;
  function ColorFiltersPicker({ qlc, onPick }) {
    const [files, setFiles] = React.useState(filtersCache);
    const [fileIndex, setFileIndex] = React.useState(0);
    const [search, setSearch] = React.useState('');
    const [picked, setPicked] = React.useState('');
    React.useEffect(() => {
      if (!qlc.online || filtersCache) return;
      qlc.call('fixtures.colorFilters.list').then(r => { filtersCache = (r && r.files) || []; setFiles(filtersCache); }).catch(() => setFiles([]));
    }, [qlc.online]);
    if (!files) return <RobotoText label="Loading colour filters…" fontSize={13} labelColor="var(--fg-medium)" />;
    if (!files.length) return <FF.Note text="No colour filter files found on the server." />;
    const file = files[Math.min(fileIndex, files.length - 1)];
    const needle = search.trim().toLowerCase();
    const list = file.filters.filter(c => !needle || c.name.toLowerCase().indexOf(needle) !== -1);
    const pick = (c) => {
      setPicked(c.name);
      const rgb = FF.parseHex(c.rgb || '#000000') || { r: 0, g: 0, b: 0 };
      const wauv = (c.white != null || c.amber != null || c.uv != null) ? { w: c.white || 0, a: c.amber || 0, uv: c.uv || 0 } : null;
      onPick(rgb, wauv);
    };
    return (
      <div data-color-filters="1" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <CustomComboBox width={160} currValue={fileIndex} model={files.map((f, i) => ({ mLabel: f.name + (f.isUser ? ' (user)' : ''), mValue: i }))} onValueChanged={setFileIndex} />
          <input value={search} onChange={e => setSearch(e.target.value)} placeholder="Filter…" data-color-filter-search="1" style={Object.assign({ width: 110 }, inputStyle)} />
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(92px, 1fr))', gap: 3, maxHeight: 220, overflow: 'auto' }}>
          {list.map(c => (
            <button key={c.name} type="button" data-color-filter={c.name} onClick={() => pick(c)} title={c.name + (c.rgb ? ' ' + c.rgb : '') + (c.white != null ? ' W' + c.white + ' A' + c.amber + ' UV' + c.uv : '')}
              style={{ display: 'flex', alignItems: 'center', gap: 4, height: 22, padding: '0 4px', border: picked === c.name ? '1px solid var(--highlight)' : 'var(--border-control)', background: 'var(--bg-control)', color: 'var(--fg-main)', cursor: 'pointer', fontFamily: 'var(--font-roboto)', fontSize: 11, overflow: 'hidden' }}>
              <span style={{ width: 12, height: 12, flex: 'none', background: c.rgb || '#000', border: '1px solid var(--bg-strong)' }} />
              <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{c.name}</span>
            </button>
          ))}
        </div>
        <RobotoText label={list.length + ' of ' + file.filters.length + ' filters' + (picked ? ' · ' + picked : '')} fontSize={11} labelColor="var(--fg-medium)" />
        <FF.Note text="Editing filter files (the Qt tool's edit mode) is not available in the web UI." />
      </div>
    );
  }

  /* ---- fixture console (bottom panel) ------------------------------------------------------------ */
  const WINDOW = 12;
  /**
   * Per-channel faders for the selected fixtures. writeFn(item, writes, key) sends to the Fixture
   * Tools target (live override or open Scene). allFixtures: every patched fixture summary, for
   * "copy to all fixtures of the same type".
   */
  function FixtureConsole({ items, allFixtures, writeFn }) {
    const [cur, setCur] = React.useState(0);
    const [page, setPage] = React.useState(0);
    const [panTilt, setPanTilt] = React.useState(false);
    const [multi, setMulti] = React.useState(false);
    const [selected, setSelected] = React.useState([]); /* channel indices of the current fixture */
    const [values, setValues] = React.useState({}); /* 'fid:ch' -> value */
    const [copied, setCopied] = React.useState('');
    const ids = items.map(it => String(it.detail.id)).join(',');
    React.useEffect(() => { setCur(0); setPage(0); setSelected([]); }, [ids]);
    /* Pan/tilt mode walks the moving fixtures one page each (SceneEditor's pan/tilt pages). */
    const moving = items.filter(it => it.channels.some(c => c.role === 'pan' || c.role === 'tilt'));
    const pool = panTilt ? moving : items;
    const idx = Math.min(panTilt ? page : cur, Math.max(0, pool.length - 1));
    const it = pool[idx];
    if (!items.length) return null;
    if (!it) return <FF.Note text="None of the selected fixtures has pan or tilt channels." />;
    const all = panTilt ? it.channels.filter(c => c.role === 'pan' || c.role === 'tilt') : it.channels;
    const pages = panTilt ? moving.length : Math.max(1, Math.ceil(all.length / WINDOW));
    const shown = panTilt ? all : all.slice(page * WINDOW, page * WINDOW + WINDOW);
    const key = (ch) => it.detail.id + ':' + ch;
    const val = (ch) => values[key(ch)] || 0;
    const move = (ch, v) => {
      const targets = multi && selected.indexOf(ch) !== -1 ? selected : [ch];
      setValues(s => { const n = Object.assign({}, s); targets.forEach(c => { n[key(c)] = v; }); return n; });
      writeFn(it, targets.map(c => ({ channel: c, value: v })), 'console');
    };
    const toggleSel = (ch) => { if (!multi) return; setSelected(s => s.indexOf(ch) === -1 ? s.concat([ch]) : s.filter(c => c !== ch)); };
    /* SceneEditor::pasteToAllFixtureSameType(): the selected channels (all when none selected) of
       the current fixture go to every patched fixture with the same definition and mode. */
    const copyToSameType = () => {
      const d = it.detail;
      const chs = (multi && selected.length ? selected : it.channels.map(c => c.index));
      const writes = chs.map(c => ({ channel: c, value: val(c) }));
      const same = (allFixtures || []).filter(f => String(f.id) !== String(d.id) && f.manufacturer === d.manufacturer && f.model === d.model && f.mode === d.mode && f.channels === d.channels);
      same.forEach(f => writeFn({ detail: f, channels: it.channels }, writes, 'console-copy'));
      setValues(s => { const n = Object.assign({}, s); same.forEach(f => writes.forEach(w => { n[f.id + ':' + w.channel] = w.value; })); return n; });
      setCopied(writes.length + ' value' + (writes.length === 1 ? '' : 's') + ' copied to ' + same.length + ' fixture' + (same.length === 1 ? '' : 's'));
    };
    return (
      <div data-fx-console="1" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexWrap: 'wrap' }}>
          {!panTilt && items.length > 1 ? <CustomComboBox width={150} currValue={cur} model={items.map((x, i) => ({ mLabel: x.detail.name, mValue: i }))} onValueChanged={v => { setCur(v); setPage(0); setSelected([]); }} /> : null}
          {panTilt ? <RobotoText label={it.detail.name} fontSize={13} /> : null}
          <IconButton imgSource={window.QLCData.icon('position')} size={24} checked={panTilt} tooltip="Pan & Tilt mode: the faders control pan, pan fine, tilt and tilt fine of one fixture; the arrows step through the moving fixtures" onClick={() => { setPanTilt(!panTilt); setPage(0); setSelected([]); }} data-console-pantilt="1" />
          <IconButton faSource={FF.GLYPH.circleLeft} size={24} disabled={page <= 0} tooltip="Shift the faders backward" onClick={() => setPage(page - 1)} data-console-prev="1" />
          <span data-console-page="1"><RobotoText label={(page + 1) + "/" + pages} fontSize={12} labelColor="var(--fg-light)" /></span>
          <IconButton faSource={FF.GLYPH.circleRight} size={24} disabled={page >= pages - 1} tooltip="Shift the faders forward" onClick={() => setPage(page + 1)} data-console-next="1" />
          <IconButton imgSource={window.QLCData.icon('multiple')} size={24} checked={multi} tooltip="Toggle multiple channel selection: click channel numbers, then any selected fader moves them all" onClick={() => { setMulti(!multi); setSelected([]); }} data-console-multi="1" />
          <IconButton faSource={G.copy} size={24} tooltip="Copy the selected channel values (all channels when none is selected) to all the fixtures of the same type" onClick={copyToSameType} data-console-copy="1" />
        </div>
        <div style={{ display: 'flex', gap: 4, overflowX: 'auto', paddingBottom: 4 }}>
          {shown.map(ch => {
            const isSel = selected.indexOf(ch.index) !== -1;
            return (
              <div key={ch.index} data-console-ch={ch.index} style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2, width: 40, flex: 'none', border: isSel ? '1px solid var(--highlight)' : '1px solid transparent' }}>
                <span onClick={() => toggleSel(ch.index)} style={{ cursor: multi ? 'pointer' : 'default' }} data-console-sel={ch.index}>
                  <RobotoText label={String(ch.index + 1)} fontSize={11} height={14} labelColor={isSel ? 'var(--highlight)' : 'var(--fg-light)'} />
                </span>
                <input type="range" min={0} max={255} value={val(ch.index)} onChange={e => move(ch.index, Number(e.target.value))} title={ch.name}
                  data-console-fader={ch.index} style={{ writingMode: 'vertical-lr', direction: 'rtl', width: 22, height: 110, accentColor: 'var(--highlight)' }} />
                <RobotoText label={String(val(ch.index))} fontSize={11} height={14} />
                <span title={ch.name}><img src={window.QLCData.icon((window.QLCIcons.CHANNEL_GROUP_ICONS || {})[ch.group] || 'other')} alt="" style={{ width: 16, height: 16 }} /></span>
              </div>
            );
          })}
        </div>
        {copied ? <span data-console-copied="1"><RobotoText label={copied} fontSize={12} labelColor="var(--fg-light)" /></span> : null}
      </div>
    );
  }

  /* ---- single-axis position (SingleAxisTool.qml) --------------------------------------------- */
  /** One slider in degrees for fixtures that only have pan OR only tilt. */
  function SingleAxis({ axis, value16, maxDegrees, onChange }) {
    const deg = Math.round((value16 / 65535) * maxDegrees);
    return (
      <div data-single-axis={axis} style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <img src={window.QLCData.icon(axis)} alt="" style={{ width: 18, height: 18 }} />
          <RobotoText label={axis === 'pan' ? 'Pan' : 'Tilt'} fontSize={13} labelColor="var(--fg-light)" style={{ width: 36 }} />
          <CustomSlider value={deg} from={0} to={maxDegrees} length={170} onMoved={d => onChange(Math.round((d / maxDegrees) * 65535))} />
          <CustomSpinBox value={deg} from={0} to={maxDegrees} width={70} onValueModified={d => onChange(Math.round((d / maxDegrees) * 65535))} />
          <RobotoText label="°" fontSize={13} />
        </div>
        <div style={{ display: 'flex', gap: 4 }}>
          {[0, 0.25, 0.5, 0.75, 1].map(fr => <GenericButton key={fr} label={Math.round(fr * maxDegrees) + '°'} width={48} height={22} onClick={() => onChange(Math.round(fr * 65535))} />)}
        </div>
      </div>
    );
  }

  Object.assign(FF, { FixtureModeRow, FixtureChannelList, ModifiersEditorDialog, FixtureSummaryDialog, UniverseSummaryDialog, RgbPanelDialog, GroupGridEditor, ColorFiltersPicker, FixtureConsole, SingleAxis, printHtml });
})();
