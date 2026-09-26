/**
 * Virtual Console — edit mode (Design mode only, like AC_VCEditing in the QML app).
 *
 *  - VCEditable: the selection / move / resize wrapper around one widget box. Pointer events, so
 *    it works with a finger. Move and resize snap to VirtualConsole::snappingSize() (11.34 px)
 *    while snapping is on, in page coordinates (the CSS zoom scale is divided out). A gesture is
 *    committed as ONE vc.widget.reposition per parent frame when the pointer goes up.
 *  - VCWidgetPalette: WidgetsList.qml — pick a type, then click on the page where it goes.
 *  - VCWidgetProperties: VCWidgetProperties.qml + VCButtonProperties.qml + VCSliderProperties.qml,
 *    limited to what vc.widget.update / vc.widget.setConfig accept on this server (Button and
 *    Slider type config; basic style for every type).
 */
const { RobotoText, FaIcon, IconButton, GenericButton, CustomSpinBox, CustomCheckBox, CustomComboBox, CustomTextInput, SectionBox, IconTextEntry } = window.PatchDesignSystem_5432c9;

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

function VCWidgetProperties({ widgets, functions }) {
  const vc = useVC();
  const e = vc.editApi;
  const [open, setOpen] = React.useState({ basic: true, geometry: true, fn: true, action: true, display: true, mode: true, range: false });
  const toggle = (k) => setOpen(o => Object.assign({}, o, { [k]: !o[k] }));
  if (!widgets.length) return <RobotoText label="Select a widget first" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--icon-size-default)" textHAlign="center" style={{ width: '100%' }} />;
  const w = widgets[0];
  const many = widgets.length > 1;
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const font = style.font || {};
  const setStyle = (patch) => widgets.forEach(x => e.updateWidget(x.id, { style: patch }));
  const setConfig = (patch) => e.setConfig(w.id, patch);
  const section = (key, label, body) => (
    <SectionBox key={key} sectionLabel={label} isExpanded={!!open[key]} onToggle={() => toggle(key)}>{open[key] ? body : null}</SectionBox>
  );
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
          <PropRow label="Background color"><ColorField value={style.backgroundColor} onChange={(c) => setStyle({ backgroundColor: c })} /></PropRow>
          <PropRow label="Foreground color"><ColorField value={style.foregroundColor} onChange={(c) => setStyle({ foregroundColor: c })} /></PropRow>
          <PropRow label="Font">
            <CustomSpinBox value={font.pointSize || 12} from={6} to={72} width={64} height={24} suffix="pt" onValueModified={(v) => setStyle({ font: Object.assign({}, font, { pointSize: v }) })} />
            <CustomCheckBox checked={!!font.bold} size={22} onToggled={(b) => setStyle({ font: Object.assign({}, font, { bold: b }) })} tooltip="Bold" />
            <RobotoText label="Bold" fontSize="var(--text-size-small)" height="auto" />
          </PropRow>
        </div>
      ))}
      {!many ? section('geometry', 'Geometry', <GeometryEditor geometry={w.geometry} onCommit={(g) => e.reposition([{ widgetId: w.id, geometry: g }])} />) : null}
      {!many && w.widgetType === 'Button' ? section('fn', 'Attached Function', (
        <FunctionPicker functions={functions} currentId={cfg.functionID} onPick={(f) => setConfig({ functionID: String(f.id) })} onDetach={() => setConfig({ functionID: '4294967295' })} />
      )) : null}
      {!many && w.widgetType === 'Button' ? section('action', 'Pressure behaviour', (
        <div>
          {[['Toggle', 'Toggle Function on/off'], ['Flash', 'Flash Function (only for Scenes)'], ['Blackout', 'Toggle Blackout'], ['StopAll', 'Stop all Functions']].map(([v, l]) => (
            <CheckRow key={v} label={l} checked={(cfg.actionType || 'Toggle') === v} onToggle={() => setConfig({ actionType: v })} />
          ))}
        </div>
      )) : null}
      {!many && w.widgetType === 'Slider' ? section('display', 'Display Style', (
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
      )) : null}
      {!many && w.widgetType === 'Slider' ? section('mode', 'Slider Mode', (
        <div>
          {[['Level', 'Level'], ['Adjust', 'Adjust'], ['Submaster', 'Submaster'], ['GrandMaster', 'Grand Master']].map(([v, l]) => (
            <CheckRow key={v} label={l} checked={(cfg.sliderMode || 'Level') === v} onToggle={() => setConfig({ sliderMode: v })} />
          ))}
        </div>
      )) : null}
      {!many && w.widgetType === 'Slider' ? section('range', 'Values range', (
        <div>
          <PropRow label="Upper limit"><CustomSpinBox value={Number.isFinite(cfg.rangeHighLimit) ? cfg.rangeHighLimit : 255} from={0} to={255} width={70} height={24} onValueModified={(v) => setConfig({ rangeHighLimit: v })} /></PropRow>
          <PropRow label="Lower limit"><CustomSpinBox value={Number.isFinite(cfg.rangeLowLimit) ? cfg.rangeLowLimit : 0} from={0} to={255} width={70} height={24} onValueModified={(v) => setConfig({ rangeLowLimit: v })} /></PropRow>
        </div>
      )) : null}
      {!many && w.widgetType !== 'Button' && w.widgetType !== 'Slider' && w.widgetType !== 'Frame' && w.widgetType !== 'SoloFrame' && w.widgetType !== 'Label'
        ? <RobotoText label={w.widgetType + ' settings cannot be edited through the Control API yet'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} /> : null}
    </div>
  );
}

Object.assign(window, { VCEditable, VCWidgetPalette, VCWidgetProperties });
