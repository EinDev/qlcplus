/**
 * Virtual Console — edit mode (Design mode only, like AC_VCEditing in the QML app).
 *
 *  - VCEditable: the selection / move / resize wrapper around one widget box. Pointer events, so
 *    it works with a finger. Move and resize snap to VirtualConsole::snappingSize() (11.34 px)
 *    while snapping is on, in page coordinates (the CSS zoom scale is divided out). A gesture is
 *    committed as ONE vc.widget.reposition per parent frame when the pointer goes up.
 *  - VCWidgetPalette: WidgetsList.qml — pick a type, then click on the page where it goes.
 *  - VCWidgetProperties: VCWidgetProperties.qml (basic style, geometry - a multi-selection is styled
 *    with one vc.widget.bulkStyle) + the full VCButtonProperties.qml / VCSliderProperties.qml
 *    (VCButtonConfigSections / VCSliderConfigSections below, incl. the Level-mode channel picker over
 *    fixtures.list/get and the Usage popup over vc.widget.usage). Other widget types plug in through
 *    window.QLCVCProperties[widgetType] (Frame/SoloFrame/Label: vc/vc-props-layout.jsx).
 */
const { RobotoText, FaIcon, IconButton, GenericButton, CustomSpinBox, CustomCheckBox, CustomComboBox, CustomTextInput, SectionBox, IconTextEntry, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

const VC_RESIZE_HANDLE = 14;
const VC_MIN_SIZE = 10;

function VCEditable({ w, box, selected, children }) {
  const vc = useVC();
  const e = vc.editApi;
  const gesture = React.useRef(null);

  const begin = (ev, kind) => {
    ev.preventDefault(); ev.stopPropagation();
    const additive = ev.ctrlKey || ev.metaKey || ev.shiftKey;
    const ids = e.selectFor(w.id, additive, kind === 'resize');
    try { ev.currentTarget.setPointerCapture(ev.pointerId); } catch (x) {}
    gesture.current = { kind, x0: ev.clientX, y0: ev.clientY, moved: false, ids,
      start: Object.fromEntries(ids.map(id => [id, Object.assign({}, e.geometryOf(id))])) };
  };
  const move = (ev) => {
    const g = gesture.current;
    if (!g) return;
    const dx = (ev.clientX - g.x0) / vc.scale, dy = (ev.clientY - g.y0) / vc.scale;
    if (!g.moved && Math.abs(dx) < 3 && Math.abs(dy) < 3) return;
    g.moved = true;
    ev.preventDefault();
    const next = {};
    if (g.kind === 'move') {
      g.ids.forEach(id => {
        const s = g.start[id];
        next[id] = { x: Math.max(0, vcSnapTo(s.x + dx, e.snap)), y: Math.max(0, vcSnapTo(s.y + dy, e.snap)), width: s.width, height: s.height };
      });
    } else {
      const s = g.start[w.id];
      next[w.id] = { x: s.x, y: s.y, width: Math.max(VC_MIN_SIZE, vcSnapTo(s.width + dx, e.snap)), height: Math.max(VC_MIN_SIZE, vcSnapTo(s.height + dy, e.snap)) };
    }
    e.setDragGeom(next);
  };
  const end = (ev) => {
    const g = gesture.current;
    gesture.current = null;
    if (!g || !g.moved) return;
    ev.preventDefault();
    e.commitDrag();
  };
  return (
    <div style={Object.assign({}, box, { pointerEvents: 'auto', zIndex: selected ? 99 : (box.zIndex || 0), cursor: 'move', touchAction: 'none', outline: 'none' })}
      data-vc-widget={w.id} data-vc-type={w.widgetType} data-vc-selected={selected ? 'true' : undefined}
      onPointerDown={(ev) => begin(ev, 'move')} onPointerMove={move} onPointerUp={end} onPointerCancel={end}
      title={(w.style && w.style.caption ? w.style.caption + ' · ' : '') + w.widgetType + ' #' + w.id}>
      {children}
      {/* VCWidgetItem.qml resizeLayer: 2px bgLight border, 3px yellow while selected */}
      <div style={{ position: 'absolute', inset: 0, border: selected ? '3px solid var(--selection)' : '2px solid var(--bg-light)', pointerEvents: 'none', zIndex: 100 }} />
      {selected && w.allowResize !== false ? (
        <div onPointerDown={(ev) => begin(ev, 'resize')} onPointerMove={move} onPointerUp={end} onPointerCancel={end} title="Resize" data-vc-resize={w.id}
          style={{ position: 'absolute', right: 0, bottom: 0, width: VC_RESIZE_HANDLE, height: VC_RESIZE_HANDLE, background: 'var(--selection)', border: '1px solid var(--border-color-dark)', cursor: 'nwse-resize', zIndex: 101, touchAction: 'none' }} />
      ) : null}
    </div>
  );
}

/* ---------------------------------------------------------------- palette */
function VCWidgetPalette({ placing, onPick }) {
  const D = window.QLCData;
  return (
    <div style={{ display: 'flex', flexDirection: 'column' }}>
      <RobotoText label={placing ? 'Click on the page to place the ' + placing.name.toLowerCase() : 'Pick a widget, then click on the page'} fontSize="var(--text-size-menubar)" labelColor={placing ? 'var(--selection)' : 'var(--fg-medium)'} wrapText height="auto" style={{ padding: '4px 6px' }} />
      {VC_PALETTE.map(p => (
        <div key={p.name} role="button" onClick={() => onPick(placing && placing.name === p.name ? null : p)}
          style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--icon-size-medium)', padding: '0 6px', cursor: 'pointer',
            background: placing && placing.name === p.name ? 'var(--highlight)' : 'transparent', borderBottom: 'var(--border-dark)' }}>
          <img src={D.icon(p.icon)} alt="" style={{ width: 22, height: 22, flex: 'none' }} />
          <RobotoText label={p.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
        </div>
      ))}
    </div>
  );
}

/* ---------------------------------------------------------------- properties */
function PropRow({ label, children }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6, minHeight: 'var(--icon-size-medium)', padding: '2px 6px' }}>
      <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 84px' }} wrapText />
      <div style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'center', gap: 4 }}>{children}</div>
    </div>
  );
}

function ColorField({ value, onChange }) {
  return (
    <>
      <input type="color" value={value || '#555555'} onChange={(e) => onChange(e.target.value)}
        style={{ width: 38, height: 26, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)', cursor: 'pointer' }} />
      <RobotoText label={value || 'default'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" style={{ flex: 1 }} />
      {value ? <GenericButton label="Default" width={60} height={24} fontSize="var(--text-size-menubar)" onClick={() => onChange(null)} /> : null}
    </>
  );
}

function CheckRow({ label, checked, onToggle, disabled }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px' }}>
      <CustomCheckBox checked={checked} onToggled={onToggle} size={22} disabled={disabled} />
      <RobotoText label={label} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
    </div>
  );
}

/** Geometry fields commit on Enter / Apply, never per keystroke (CustomSpinBox fires on every digit). */
function GeometryEditor({ geometry, onCommit }) {
  const [g, setG] = React.useState(geometry);
  React.useEffect(() => { setG(geometry); }, [geometry.x, geometry.y, geometry.width, geometry.height]);
  const dirty = ['x', 'y', 'width', 'height'].some(k => Math.round(g[k]) !== Math.round(geometry[k]));
  const field = (k, label) => (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 3 }}>
      <RobotoText label={label} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" />
      <CustomSpinBox value={Math.round(g[k])} from={0} to={20000} showControls={false} width={58} height={24}
        onValueModified={(v) => setG(Object.assign({}, g, { [k]: v }))} onKeyDown={(e) => { if (e.key === 'Enter') onCommit(g); }} />
    </span>
  );
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '4px 6px' }}>
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>{field('x', 'X')}{field('y', 'Y')}</div>
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>{field('width', 'W')}{field('height', 'H')}</div>
      <GenericButton label="Apply geometry" width="100%" height={24} fontSize="var(--text-size-menubar)" disabled={!dirty} onClick={() => onCommit(g)} />
    </div>
  );
}

/** Attached-function picker: text filter over functions.list, one row per match (capped). */
function FunctionPicker({ functions, currentId, onPick, onDetach }) {
  const [needle, setNeedle] = React.useState('');
  const D = window.QLCData;
  const current = functions.find(f => String(f.id) === String(currentId));
  const n = needle.trim().toLowerCase();
  const matches = n ? functions.filter(f => !f.hidden && (f.name.toLowerCase().indexOf(n) !== -1 || String(f.id) === n)).slice(0, 40) : [];
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '4px 6px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
        <RobotoText label={current ? current.name + ' (' + current.type + ')' : (currentId && currentId !== '4294967295' ? 'Function #' + currentId : 'No function')} fontSize="var(--text-size-small)" height="auto" wrapText style={{ flex: 1 }} labelColor={current ? 'var(--fg-main)' : 'var(--fg-medium)'} />
        <IconButton faSource="fa_xmark" size={24} tooltip="Detach the current function" disabled={!current} onClick={onDetach} />
      </div>
      <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', gap: 4 }}>
        <img src={D.icon('search')} alt="" style={{ width: 14, height: 14 }} />
        <CustomTextInput text={needle} editing placeholder="Search functions" width="100%" height={22} onTextConfirmed={setNeedle} onChange={(e) => setNeedle(e.target.value)} style={{ fontSize: 'var(--text-size-small)' }} />
      </span>
      {n ? (
        <div style={{ maxHeight: 190, overflow: 'auto', border: 'var(--border-dark)' }}>
          {matches.length ? matches.map(f => (
            <div key={f.id} role="button" onClick={() => { onPick(f); setNeedle(''); }}
              style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 4px', cursor: 'pointer', background: String(f.id) === String(currentId) ? 'var(--highlight)' : 'transparent' }}>
              <RobotoText label={f.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
              <RobotoText label={f.type} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
            </div>
          )) : <RobotoText label="No match" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={4} />}
        </div>
      ) : null}
    </div>
  );
}

/** Small styled text box (the Label field's look) committing on Enter / blur - once per change:
    CustomTextInput confirms on Enter AND on the blur that follows, and a second identical structural
    call would only race the next edit for the docRevision. */
function TextField({ value, onCommit, placeholder, width = '100%' }) {
  const committed = React.useRef(value || '');
  React.useEffect(() => { committed.current = value || ''; }, [value]);
  return (
    <span style={{ flex: 1, display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', width }}>
      <CustomTextInput key={value || ''} text={value || ''} editing width="100%" height={22} placeholder={placeholder}
        onTextConfirmed={(t) => { if (t !== committed.current) { committed.current = t; onCommit(t); } }} style={{ fontSize: 'var(--text-size-small)' }} />
    </span>
  );
}

/**
 * "Usage" popup: every VC widget referencing a Function (vc.widget.usage - VirtualConsole::usageList()),
 * the same list the desktop app's Function Manager shows under a function's Usage tab. Clicking a row
 * selects that widget (and switches to its page).
 */
function VCUsageDialog({ functionId, functionName, onClose }) {
  const vc = useVC();
  const [rows, setRows] = React.useState(null);
  const [err, setErr] = React.useState('');
  React.useEffect(() => {
    if (functionId == null) return;
    setRows(null); setErr('');
    vc.qlc.call('vc.widget.usage', { functionId: String(functionId) }).then(r => setRows(r.widgets || [])).catch(e => setErr((e && e.message) || 'vc.widget.usage failed'));
  }, [functionId]);
  const D = window.QLCData;
  return (
    <CustomPopupDialog open={functionId != null} title={'Usage of ' + (functionName || ('function #' + functionId))} width={420} standardButtons={['Close']} onClose={onClose} onClicked={onClose}>
      <div data-vc-usage="" style={{ maxHeight: 300, overflow: 'auto', border: 'var(--border-dark)' }}>
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="var(--list-item-height)" leftMargin={6} /> : null}
        {rows == null && !err ? <RobotoText label="Loading…" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} /> : null}
        {rows && !rows.length ? <RobotoText label="No Virtual Console widget uses this function" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} /> : null}
        {(rows || []).map(r => (
          <div key={r.id} role="button" data-vc-usage-row={r.id} onClick={() => { if (vc.selectWidget) vc.selectWidget(r); onClose(); }}
            style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', borderBottom: 'var(--border-dark)' }}>
            <img src={D.icon(VC_WIDGET_ICONS[r.widgetType] || 'frame')} alt="" style={{ width: 18, height: 18 }} />
            <RobotoText label={(r.style && r.style.caption) || r.widgetType} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
            <RobotoText label={r.widgetType + ' #' + r.id + ' · page ' + (Number(r.page) + 1)} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
          </div>
        ))}
      </div>
    </CustomPopupDialog>
  );
}

function UsageButton({ functionId, functions }) {
  const [open, setOpen] = React.useState(false);
  const f = functions.find(x => String(x.id) === String(functionId));
  const has = functionId != null && String(functionId) !== '4294967295';
  return (
    <>
      <IconButton faSource="fa_circle_info" size={24} tooltip="Show which VC widgets use this function" disabled={!has} onClick={() => setOpen(true)} data-vc-usage-btn="" />
      {open ? <VCUsageDialog functionId={functionId} functionName={f && f.name} onClose={() => setOpen(false)} /> : null}
    </>
  );
}

/** CustomSpinBox that commits on Enter / blur / arrow buttons instead of every keystroke: a
    structural (§4a) call per typed digit is a CONFLICT storm ("1500" = four edits in flight). */
function SpinField({ value, onCommit, ...rest }) {
  const [draft, setDraft] = React.useState(value);
  const pending = React.useRef(null);   // { value, timer } while an edit waits to be committed
  const committed = React.useRef(value);
  React.useEffect(() => { committed.current = value; if (!pending.current) setDraft(value); }, [value]);
  React.useEffect(() => () => { if (pending.current) clearTimeout(pending.current.timer); }, []);
  const flush = () => {
    const p = pending.current;
    if (!p) return;
    clearTimeout(p.timer);
    pending.current = null;
    if (p.value !== committed.current) { committed.current = p.value; onCommit(p.value); }
  };
  /* Arrow buttons and typed digits both land here; the commit waits for a pause, Enter/blur force it. */
  const modified = (v) => {
    setDraft(v);
    if (pending.current) clearTimeout(pending.current.timer);
    pending.current = { value: v, timer: setTimeout(flush, 500) };
  };
  return <CustomSpinBox {...rest} value={draft} onValueModified={modified}
    onKeyDown={(e) => { if (e.key === 'Enter') flush(); }} onBlur={flush} />;
}

/** Percent spin box for a 0..1 fraction (VCButtonProperties.qml's startup intensity). */
function PercentField({ value, onCommit, disabled }) {
  return <SpinField value={Math.round((Number.isFinite(value) ? value : 1) * 100)} from={0} to={100} width={70} height={24} suffix="%" disabled={disabled} onCommit={(v) => onCommit(v / 100)} />;
}

/**
 * VCButtonProperties.qml: attached function, pressure behaviour, startup intensity (Toggle/Flash),
 * stop-all fade out (StopAll), flash priority flags (Flash). Every field is VcButtonConfig.
 */
function VCButtonConfigSections({ w, cfg, functions, setConfig, section }) {
  const action = cfg.actionType || 'Toggle';
  return (
    <>
      {section('fn', 'Attached Function', (
        <div>
          <FunctionPicker functions={functions} currentId={cfg.functionID} onPick={(f) => setConfig({ functionID: String(f.id) })} onDetach={() => setConfig({ functionID: '4294967295' })} />
          <PropRow label="Usage"><UsageButton functionId={cfg.functionID} functions={functions} /></PropRow>
        </div>
      ))}
      {section('action', 'Pressure behaviour', (
        <div>
          {[['Toggle', 'Toggle Function on/off'], ['Flash', 'Flash Function (only for Scenes)'], ['Blackout', 'Toggle Blackout'], ['StopAll', 'Stop all Functions']].map(([v, l]) => (
            <CheckRow key={v} label={l} checked={action === v} onToggle={() => setConfig({ actionType: v })} />
          ))}
        </div>
      ))}
      {action === 'Toggle' || action === 'Flash' ? section('intensity', 'Adjust Function intensity', (
        <div>
          <CheckRow label="Enable" checked={!!cfg.startupIntensityEnabled} onToggle={(b) => setConfig({ startupIntensityEnabled: b })} />
          <PropRow label="Intensity"><PercentField value={cfg.startupIntensity} disabled={!cfg.startupIntensityEnabled} onCommit={(v) => setConfig({ startupIntensity: v })} /></PropRow>
        </div>
      )) : null}
      {action === 'StopAll' ? section('stopall', 'Stop all Functions', (
        <PropRow label="Fade out"><SpinField value={Number(cfg.stopAllFadeOutTime) || 0} from={0} to={3600000} stepSize={100} width={90} height={24} suffix=" ms" onCommit={(v) => setConfig({ stopAllFadeOutTime: v })} /></PropRow>
      )) : null}
      {action === 'Flash' ? section('flash', 'Flash properties', (
        <div>
          <CheckRow label="Override priority" checked={!!cfg.flashOverrides} onToggle={(b) => setConfig({ flashOverrides: b })} />
          <CheckRow label="Force LTP" checked={!!cfg.flashForceLTP} onToggle={(b) => setConfig({ flashForceLTP: b })} />
        </div>
      )) : null}
    </>
  );
}

/**
 * Level-mode channel picker (VCSliderProperties.qml's fixture/channel tree): fixtures.list, then
 * fixtures.get per expanded fixture for its channelList; every tick commits the whole list through
 * vc.slider.setLevelChannels (a bulk replace).
 */
function LevelChannelsPicker({ channels, onCommit }) {
  const vc = useVC();
  const [fixtures, setFixtures] = React.useState(null);
  const [details, setDetails] = React.useState({});
  const [openFx, setOpenFx] = React.useState({});
  const [needle, setNeedle] = React.useState('');
  const [expanded, setExpanded] = React.useState(false);
  React.useEffect(() => {
    if (!expanded || fixtures) return;
    vc.qlc.call('fixtures.list').then(r => setFixtures(r.fixtures || [])).catch(() => setFixtures([]));
  }, [expanded]);
  const toggleFx = (id) => {
    const next = !openFx[id];
    setOpenFx(o => Object.assign({}, o, { [id]: next }));
    if (next && !details[id]) vc.qlc.call('fixtures.get', { fixtureId: String(id) }).then(d => setDetails(x => Object.assign({}, x, { [id]: d }))).catch(() => {});
  };
  /* Two quick ticks must not both start from the same stale prop (the committed list only comes
     back after the server round trip): every change builds on the last list this picker sent. */
  const latest = React.useRef(channels);
  React.useEffect(() => { latest.current = channels; }, [channels]);
  const commit = (next) => { latest.current = next; onCommit(next); };
  const has = (fid, ch) => channels.some(c => String(c.fixtureId) === String(fid) && Number(c.channel) === ch);
  const flip = (fid, ch) => {
    const cur = latest.current;
    const on = cur.some(c => String(c.fixtureId) === String(fid) && Number(c.channel) === ch);
    commit(on ? cur.filter(c => !(String(c.fixtureId) === String(fid) && Number(c.channel) === ch)) : cur.concat([{ fixtureId: String(fid), channel: ch }]));
  };
  const flipAll = (fx, on) => {
    const d = details[fx.id];
    const count = d && d.channelList ? d.channelList.length : Number(fx.channels) || 0;
    const rest = latest.current.filter(c => String(c.fixtureId) !== String(fx.id));
    commit(on ? rest.concat(Array.from({ length: count }, (_, i) => ({ fixtureId: String(fx.id), channel: i }))) : rest);
  };
  /* The row and the checkbox inside it both toggle: ignore the row click that bubbled up from the button. */
  const rowClick = (fn) => (e) => { if (e.target.closest && e.target.closest('button')) return; fn(); };
  const n = needle.trim().toLowerCase();
  const list = (fixtures || []).filter(f => !n || f.name.toLowerCase().indexOf(n) !== -1);
  const D = window.QLCData;
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '2px 6px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <RobotoText label={(channels.length ? channels.length : 'None') + ' selected'} fontSize="var(--text-size-small)" height="auto" style={{ flex: 1 }} />
        <GenericButton label={expanded ? 'Hide channels' : 'Pick channels'} width={110} height={24} fontSize="var(--text-size-menubar)" onClick={() => setExpanded(!expanded)} data-vc-levelpick="" />
        {channels.length ? <GenericButton label="Clear" width={50} height={24} fontSize="var(--text-size-menubar)" onClick={() => onCommit([])} /> : null}
      </div>
      {expanded ? (
        <>
          <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', gap: 4 }}>
            <img src={D.icon('search')} alt="" style={{ width: 14, height: 14 }} />
            <CustomTextInput text={needle} editing placeholder="Search fixtures" width="100%" height={22} onTextConfirmed={setNeedle} onChange={(e) => setNeedle(e.target.value)} style={{ fontSize: 'var(--text-size-small)' }} />
          </span>
          <div style={{ maxHeight: 260, overflow: 'auto', border: 'var(--border-dark)' }} data-vc-levellist="">
            {fixtures == null ? <RobotoText label="Loading fixtures…" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={4} /> : null}
            {list.map(fx => {
              const mine = channels.filter(c => String(c.fixtureId) === String(fx.id)).length;
              const d = details[fx.id];
              return (
                <div key={fx.id} data-vc-levelfx={fx.id}>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 4px', background: 'var(--bg-medium)' }}>
                    <IconButton faSource={openFx[fx.id] ? 'fa_chevron_down' : 'fa_chevron_right'} size={20} onClick={() => toggleFx(fx.id)} tooltip="Channels" />
                    <CustomCheckBox checked={mine > 0} size={18} onToggled={(on) => flipAll(fx, on)} tooltip="Every channel of this fixture" />
                    <RobotoText label={fx.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                    <RobotoText label={'U' + (Number(fx.universe) + 1) + ' · ' + (Number(fx.address) + 1) + (mine ? ' · ' + mine + ' ch' : '')} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
                  </div>
                  {openFx[fx.id] ? (d && d.channelList ? d.channelList.map(ch => (
                    <div key={ch.index} role="button" data-vc-levelch={fx.id + ':' + ch.index} onClick={rowClick(() => flip(fx.id, ch.index))}
                      style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 4px 0 28px', cursor: 'pointer', background: has(fx.id, ch.index) ? 'var(--highlight)' : 'transparent' }}>
                      <CustomCheckBox checked={has(fx.id, ch.index)} size={18} onToggled={() => flip(fx.id, ch.index)} />
                      <RobotoText label={(ch.index + 1) + ': ' + ch.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                      <RobotoText label={ch.group} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
                    </div>
                  )) : <RobotoText label="Loading…" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={28} />) : null}
                </div>
              );
            })}
          </div>
        </>
      ) : null}
    </div>
  );
}

/**
 * VCSliderProperties.qml, section by section: display style, slider mode, function control
 * (Adjust), level mode (Level: channels, click & go, monitor), values range (Level/Adjust),
 * external input, grand master mode. Every field is VcSliderConfig; the channel list goes through
 * its own vc.slider.setLevelChannels.
 */
function VCSliderConfigSections({ w, cfg, functions, setConfig, setLevelChannels, section }) {
  const vc = useVC();
  const mode = cfg.sliderMode || 'Level';
  const [attrs, setAttrs] = React.useState(null);
  const fnId = cfg.controlledFunction;
  const hasFn = fnId != null && String(fnId) !== '4294967295';
  React.useEffect(() => {
    if (mode !== 'Adjust' || !hasFn) { setAttrs(null); return; }
    vc.qlc.call('functions.get', { functionId: String(fnId) }).then(d => setAttrs(d.attributes || [])).catch(() => setAttrs([]));
  }, [mode, fnId, vc.qlc.online]);
  const attr = attrs && attrs[Number(cfg.controlledAttribute) || 0];
  /* Adjust mode's range follows the attribute (Intensity is 0..255 like a DMX value, others their own units). */
  const rangeLo = mode === 'Adjust' && attr && Number(cfg.controlledAttribute) !== 0 ? attr.min : 0;
  const rangeHi = mode === 'Adjust' && attr && Number(cfg.controlledAttribute) !== 0 ? attr.max : 255;
  const channels = cfg.levelChannels || [];
  return (
    <>
      {section('display', 'Display Style', (
        <div>
          <CheckRow label="DMX Value" checked={cfg.valueDisplayStyle !== 'PercentageValue'} onToggle={() => setConfig({ valueDisplayStyle: 'DMXValue' })} />
          <CheckRow label="Percentage" checked={cfg.valueDisplayStyle === 'PercentageValue'} onToggle={() => setConfig({ valueDisplayStyle: 'PercentageValue' })} />
          <CheckRow label="Normal" checked={!cfg.invertedAppearance} onToggle={() => setConfig({ invertedAppearance: false })} />
          <CheckRow label="Inverted" checked={!!cfg.invertedAppearance} onToggle={() => setConfig({ invertedAppearance: true })} />
          <PropRow label="Widget style">
            <CustomComboBox width="100%" height={24} currValue={cfg.widgetStyle || 'Slider'} onValueChanged={(v) => setConfig({ widgetStyle: v })}
              model={[{ mLabel: 'Slider', mValue: 'Slider' }, { mLabel: 'Knob', mValue: 'Knob' }]} />
          </PropRow>
        </div>
      ))}
      {section('mode', 'Slider Mode', (
        <div>
          {[['Level', 'Level'], ['Adjust', 'Adjust'], ['Submaster', 'Submaster'], ['GrandMaster', 'Grand Master']].map(([v, l]) => (
            <CheckRow key={v} label={l} checked={mode === v} onToggle={() => setConfig({ sliderMode: v })} />
          ))}
        </div>
      ))}
      {mode === 'Adjust' ? section('control', 'Function Control', (
        <div>
          <FunctionPicker functions={functions} currentId={fnId} onPick={(f) => setConfig({ controlledFunction: String(f.id) })} onDetach={() => setConfig({ controlledFunction: '4294967295' })} />
          <PropRow label="Attribute">
            <CustomComboBox width="100%" height={24} currValue={String(Number(cfg.controlledAttribute) || 0)} onValueChanged={(v) => setConfig({ controlledAttribute: Number(v) })}
              model={(attrs && attrs.length ? attrs : [{ index: 0, name: 'Intensity' }]).map(a => ({ mLabel: a.name, mValue: String(a.index) }))} />
          </PropRow>
          <CheckRow label="Show flash button" checked={!!cfg.adjustFlashEnabled} onToggle={(b) => setConfig({ adjustFlashEnabled: b })} />
          <PropRow label="Usage"><UsageButton functionId={fnId} functions={functions} /></PropRow>
        </div>
      )) : null}
      {mode === 'Level' ? section('level', 'Level mode', (
        <div>
          <PropRow label="Channels"><span style={{ flex: 1 }} /></PropRow>
          <LevelChannelsPicker channels={channels} onCommit={setLevelChannels} />
          <PropRow label="Click & Go">
            <CustomComboBox width="100%" height={24} currValue={cfg.clickAndGoType || 'None'} onValueChanged={(v) => setConfig({ clickAndGoType: v })}
              model={[{ mLabel: 'None', mValue: 'None' }, { mLabel: 'Colors', mValue: 'Colors' }, { mLabel: 'Preset', mValue: 'Preset' }]} />
          </PropRow>
          <CheckRow label="Monitor channel levels" checked={cfg.monitorEnabled !== false} onToggle={(b) => setConfig({ monitorEnabled: b })} />
        </div>
      )) : null}
      {mode === 'Level' || mode === 'Adjust' ? section('range', 'Values range', (
        <div>
          <PropRow label="Upper limit"><SpinField value={Number.isFinite(cfg.rangeHighLimit) ? Math.round(cfg.rangeHighLimit) : rangeHi} from={rangeLo} to={rangeHi} width={70} height={24} onCommit={(v) => setConfig({ rangeHighLimit: v })} /></PropRow>
          <PropRow label="Lower limit"><SpinField value={Number.isFinite(cfg.rangeLowLimit) ? Math.round(cfg.rangeLowLimit) : rangeLo} from={rangeLo} to={rangeHi} width={70} height={24} onCommit={(v) => setConfig({ rangeLowLimit: v })} /></PropRow>
        </div>
      )) : null}
      {section('input', 'External input', (
        <CheckRow label="Catch up with the external controller input value" checked={!!cfg.catchValues} onToggle={(b) => setConfig({ catchValues: b })} />
      ))}
      {mode === 'GrandMaster' ? section('gm', 'Grand Master mode', (
        <div>
          <CheckRow label="Reduce values" checked={cfg.grandMasterValueMode !== 'Limit'} onToggle={() => setConfig({ grandMasterValueMode: 'Reduce' })} />
          <CheckRow label="Limit values" checked={cfg.grandMasterValueMode === 'Limit'} onToggle={() => setConfig({ grandMasterValueMode: 'Limit' })} />
          <CheckRow label="Intensity channels" checked={cfg.grandMasterChannelMode !== 'AllChannels'} onToggle={() => setConfig({ grandMasterChannelMode: 'Intensity' })} />
          <CheckRow label="All channels" checked={cfg.grandMasterChannelMode === 'AllChannels'} onToggle={() => setConfig({ grandMasterChannelMode: 'AllChannels' })} />
        </div>
      )) : null}
    </>
  );
}

function VCWidgetProperties({ widgets, functions }) {
  const vc = useVC();
  const e = vc.editApi;
  /* Every section starts expanded unless listed here as closed (registered panels add their own keys). */
  const [open, setOpen] = React.useState({ range: false, input: false });
  const isOpen = (k) => open[k] !== false;
  const toggle = (k) => setOpen(o => Object.assign({}, o, { [k]: !isOpen(k) }));
  /* nothing selected: the shown page's own properties (VCPageProperties.qml, vc/vc-props-style.jsx) */
  if (!widgets.length) return window.VCPagePropertiesPanel ? <window.VCPagePropertiesPanel />
    : <RobotoText label="Select a widget first" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--icon-size-default)" textHAlign="center" style={{ width: '100%' }} />;
  const w = widgets[0];
  const many = widgets.length > 1;
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const font = style.font || {};
  /* A font patch as the server takes it: pixelSize is read-only and a pointSize of -1 (pixel-sized
     default font) must not be written back (QFont::setPointSize refuses it). */
  const fontPatch = (patch) => { const f = Object.assign({}, font, patch); delete f.pixelSize; if (!(f.pointSize > 0)) delete f.pointSize; return f; };
  /* A multi-selection is styled with ONE vc.widget.bulkStyle (VirtualConsole::setWidgetsCaption/Font/...). */
  const setStyle = (patch) => many && e.bulkStyle ? e.bulkStyle(widgets.map(x => x.id), patch) : widgets.forEach(x => e.updateWidget(x.id, { style: patch }));
  const setConfig = (patch) => e.setConfig(w.id, patch);
  const setLevelChannels = (channels) => e.setLevelChannels ? e.setLevelChannels(w.id, channels) : null;
  const section = (key, label, body) => (
    <SectionBox key={key} sectionLabel={label} isExpanded={isOpen(key)} onToggle={() => toggle(key)}>{isOpen(key) ? body : null}</SectionBox>
  );
  /* Registry: window.QLCVCProperties[widgetType] (registered from a file loaded after this one) adds
     the type-specific property sections below the shared Basic/Geometry ones. Props: { w, widgets,
     functions, cfg, setConfig, setStyle, section, PropRow, CheckRow, ColorField, FunctionPicker }.
     Use `section(key, label, body)` for each collapsible block so it matches the built-in look. */
  const Extra = !many ? (window.QLCVCProperties || {})[w.widgetType] : null;
  return (
    <div style={{ display: 'flex', flexDirection: 'column' }}>
      <RobotoText label={many ? widgets.length + ' widgets selected' : w.widgetType + ' #' + w.id} fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="var(--list-item-height)" leftMargin={6} />
      {section('basic', 'Basic properties', (
        <div>
          <PropRow label="Label">
            <span style={{ flex: 1, display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
              <CustomTextInput key={w.id + ':' + (style.caption || '')} text={style.caption || ''} editing width="100%" height={22} onTextConfirmed={(t) => { if (t !== (style.caption || '')) setStyle({ caption: t }); }} style={{ fontSize: 'var(--text-size-small)' }} />
            </span>
          </PropRow>
          <PropRow label="Background color"><span data-vc-color="background" style={{ display: 'contents' }}><ColorField value={style.backgroundColor} onChange={(c) => setStyle({ backgroundColor: c })} /></span></PropRow>
          {window.VCBackgroundImageRow ? <window.VCBackgroundImageRow widgets={widgets} setStyle={setStyle} PropRow={PropRow} /> : null}
          {!many && window.VCZIndexRow ? <window.VCZIndexRow w={w} PropRow={PropRow} /> : null}
          <PropRow label="Foreground color"><span data-vc-color="foreground" style={{ display: 'contents' }}><ColorField value={style.foregroundColor} onChange={(c) => setStyle({ foregroundColor: c })} /></span></PropRow>
          {/* VCWidgetProperties.qml's font dialog: family, size, bold / italic */}
          <PropRow label="Font">
            <span data-vc-font-family="" style={{ flex: 1, display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', minWidth: 0 }}
              title="Font family (a font installed on the machine running QLC+)">
              <CustomTextInput key={w.id + ':f:' + (font.family || '')} text={font.family || ''} editing width="100%" height={22} placeholder="Roboto Condensed"
                onTextConfirmed={(t) => { if (t.trim() && t.trim() !== (font.family || '')) setStyle({ font: fontPatch({ family: t.trim() }) }); }}
                style={{ fontSize: 'var(--text-size-small)', fontFamily: font.family ? '"' + font.family + '", var(--font-roboto)' : undefined }} />
            </span>
          </PropRow>
          <PropRow label="">
            <span data-vc-font-size=""><CustomSpinBox value={font.pointSize > 0 ? font.pointSize : font.pixelSize > 0 ? Math.round(font.pixelSize * 0.75) : 12} from={6} to={72} width={64} height={24} suffix="pt" onValueModified={(v) => setStyle({ font: fontPatch({ pointSize: v }) })} /></span>
            <span data-vc-font-bold=""><CustomCheckBox checked={!!font.bold} size={22} onToggled={(b) => setStyle({ font: fontPatch({ bold: b }) })} tooltip="Bold" /></span>
            <RobotoText label="Bold" fontSize="var(--text-size-small)" height="auto" style={{ flex: 'none' }} />
            <span data-vc-font-italic=""><CustomCheckBox checked={!!font.italic} size={22} onToggled={(b) => setStyle({ font: fontPatch({ italic: b }) })} tooltip="Italic" /></span>
            <RobotoText label="Italic" fontSize="var(--text-size-small)" height="auto" style={{ flex: 'none', fontStyle: 'italic' }} />
          </PropRow>
        </div>
      ))}
      {!many ? section('geometry', 'Geometry', <GeometryEditor geometry={w.geometry} onCommit={(g) => e.reposition([{ widgetId: w.id, geometry: g }])} />) : null}
      {/* Registry: window.QLCVCPropertiesCommon = [Component, ...] - sections every widget type gets (external controls: vc/vc-external.jsx). */}
      {!many ? (window.QLCVCPropertiesCommon || []).map((C, i) => <C key={'common' + i} w={w} section={section} PropRow={PropRow} CheckRow={CheckRow} />) : null}
      {!many && w.widgetType === 'Button' ? <VCButtonConfigSections w={w} cfg={cfg} functions={functions} setConfig={setConfig} section={section} /> : null}
      {!many && w.widgetType === 'Slider' ? <VCSliderConfigSections w={w} cfg={cfg} functions={functions} setConfig={setConfig} setLevelChannels={setLevelChannels} section={section} /> : null}
      {Extra ? <Extra w={w} widgets={widgets} functions={functions} cfg={cfg} setConfig={setConfig} setStyle={setStyle} section={section}
        PropRow={PropRow} CheckRow={CheckRow} ColorField={ColorField} FunctionPicker={FunctionPicker} TextField={TextField} /> : null}
      {!many && !Extra && w.widgetType !== 'Button' && w.widgetType !== 'Slider' && w.widgetType !== 'Frame' && w.widgetType !== 'SoloFrame' && w.widgetType !== 'Label'
        ? <RobotoText label={w.widgetType + ' settings cannot be edited through the Control API yet'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} /> : null}
    </div>
  );
}

Object.assign(window, { VCEditable, VCWidgetPalette, VCWidgetProperties, VCUsageDialog, VCPropRow: PropRow, VCCheckRow: CheckRow, VCTextField: TextField, VCSpinField: SpinField, VCFunctionPicker: FunctionPicker, VCLevelChannelsPicker: LevelChannelsPicker });
