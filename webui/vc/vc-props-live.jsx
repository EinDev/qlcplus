/**
 * Virtual Console — XY Pad, Clock, Animation and Audio Triggers: the live bodies and the type-specific
 * property panels. Registers into the extension points of vc-widgets.jsx / vc-edit.jsx:
 *   window.QLCVCBodies.XYPad / .Clock / .Animation / .AudioTriggers          (props { w, header, children })
 *   window.QLCVCProperties.XYPad / .Clock / .Animation / .AudioTriggers      (props from VCWidgetProperties)
 *
 * Mirrors qmlui/qml/virtualconsole/VCXYPadItem.qml + VCXYPadProperties.qml + VCXYPadPresets.qml,
 * VCClockItem.qml + VCClockProperties.qml (+ DayTimeTool.qml), VCAnimationItem.qml +
 * VCAnimationProperties.qml + VCAnimationPresets.qml (+ popup/PopupAnimationPreset.qml) and
 * VCAudioTriggersItem.qml + VCAudioTriggersProperties.qml.
 *
 * Server side: vc.widget.get's typeConfig carries the document config of each type PLUS its read-only
 * sub-resource lists (fixtures / presets / schedules / bars) - every structural change re-broadcasts the
 * widget on vc.widget.configChanged, which the screen already refreshes on, so `w.typeConfig` is always
 * current here. Live state the shared store does not carry (floor position, active preset, clock time,
 * fader level, capture / levels, animation colours) is seeded from the snapshot's additive fields and
 * followed through the vc.xyPad.* / vc.clock.* / vc.animation.* / vc.audioTriggers.* events.
 * vc.clock.timeChanged and vc.audioTriggers.levelsChanged are subscribe-gated: the bodies subscribe
 * when they mount (and again after a reconnect).
 */
const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomCheckBox, CustomComboBox, CustomPopupDialog, SectionBox } = window.PatchDesignSystem_5432c9;

const NO_FUNCTION_ID = '4294967295';
const LIVE_METHODS = {
  FLOOR: 'vc.xyPad.setFloorPosition',
  CLOCK_PLAY: 'vc.clock.playPause', CLOCK_RESET: 'vc.clock.reset',
  FADER: 'vc.animation.setFaderLevel', KNOB: 'vc.animation.setPresetKnobValue',
  CAPTURE: 'vc.audioTriggers.setCaptureEnabled',
  PRESET_APPLY: 'vc.widget.preset.apply', PRESET_ADD: 'vc.widget.preset.add', PRESET_REMOVE: 'vc.widget.preset.remove'
};
const XY_POS_MAX = 255 + 255 / 256; /* VCXYPad's native position domain (kPosMax) */
const DAYS = ['M', 'T', 'W', 'T', 'F', 'S', 'S'];
const CLOCK_TYPES = ['Clock', 'Stopwatch', 'Countdown'];
const ANIM_VISIBILITY = [['Fader', 'Level Fader'], ['Label', 'Label'], ['Color1', 'Color 1 Button'], ['Color2', 'Color 2 Button'], ['Color3', 'Color 3 Button'], ['Color4', 'Color 4 Button'], ['Color5', 'Color 5 Button'], ['PresetCombo', 'Preset List']];
const BAR_TYPES = [{ mLabel: 'None', mValue: 'None' }, { mLabel: 'DMX', mValue: 'DMXBar' }, { mLabel: 'Function', mValue: 'FunctionBar' }, { mLabel: 'Widget', mValue: 'VCWidgetBar' }];
const XY_PRESET_ICON = { function: 'efx', fixtureGroup: 'group', fixtureGroupHead: 'group', position: 'position' };

/* ---------------------------------------------------------------- shared helpers */
/** Subscribe to one vc.* event for one widget; `handler` gets the event data. */
function useWidgetEvent(vc, topic, widgetId, handler) {
  const ref = React.useRef(handler);
  ref.current = handler;
  React.useEffect(() => vc.qlc.subscribeTo(topic, (d) => { if (d && String(d.widgetId) === String(widgetId)) ref.current(d); }), [vc.qlc, topic, widgetId, vc.qlc.online]);
}

/** Server-side subscription for a gated topic, re-sent after every reconnect (the transport only re-sends
    the DMX universe subscriptions itself). Never unsubscribed: several widgets share one topic. */
function useGatedTopic(vc, topic, active) {
  React.useEffect(() => {
    if (!active || !vc.qlc.online) return;
    const c = vc.qlc.client();
    if (c && c.subscribe) c.subscribe([topic]).catch(() => {});
  }, [vc.qlc, topic, active, vc.qlc.online]);
}

/** Collapsible sections with this file's own expanded-by-default state (same SectionBox look). */
function useLiveSections(collapsedKeys) {
  const [open, setOpen] = React.useState(() => { const o = {}; (collapsedKeys || []).forEach(k => { o[k] = false; }); return o; });
  return (key, label, body) => (
    <SectionBox key={key} sectionLabel={label} isExpanded={open[key] !== false} onToggle={() => setOpen(o => Object.assign({}, o, { [key]: o[key] === false }))}>
      {open[key] !== false ? <div data-e2e-section={key}>{body}</div> : null}
    </SectionBox>
  );
}

function liveMethodError(vc, method) {
  return (e) => {
    if (!e || e.code === 'NOT_CONNECTED') return;
    if (e.code === 'NOT_FOUND' && /^Unknown method/.test(e.message || '')) vc.notice('This server has no ' + method + ' yet');
    else vc.notice(method + ': ' + (e.message || e.code || 'failed'));
  };
}

/** A structural (§4a) call for one widget: baseRevision + CONFLICT retry via vcStructural, errors to the notice strip. */
function useStructural(vc, w) {
  return React.useCallback((method, params) => vcStructural(vc.qlc, method, Object.assign({ widgetId: String(w.id) }, params || {}))
    .catch(e => { vc.notice(method + ': ' + ((e && e.message) || 'failed')); throw e; }), [vc.qlc, w.id]);
}

function LiveTextField({ value, onChange, onConfirm, placeholder, tag, width }) {
  return (
    <span style={{ flex: 1, display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', gap: 4, width }}>
      <input value={value} placeholder={placeholder} data-e2e={tag} onChange={(e) => onChange(e.target.value)}
        onBlur={() => { if (onConfirm) onConfirm(value); }} onKeyDown={(e) => { if (e.key === 'Enter' && onConfirm) onConfirm(value); }}
        style={{ width: '100%', height: 22, padding: 0, background: 'transparent', border: 'none', outline: 'none', color: 'var(--fg-main)', fontFamily: 'var(--font-roboto)', fontSize: 'var(--text-size-small)' }} />
    </span>
  );
}

/** CustomSpinBox that commits once, on Enter / blur (a structural call per keystroke would be a CONFLICT storm). */
function LiveSpin({ value, from, to, suffix, width, onCommit, tag, disabled }) {
  const [draft, setDraft] = React.useState(value);
  React.useEffect(() => { setDraft(value); }, [value]);
  const commit = () => { if (Math.round(draft) !== Math.round(value)) onCommit(Math.round(draft)); };
  return <CustomSpinBox value={draft} from={from} to={to} suffix={suffix} showControls={false} width={width || 70} height={24} disabled={disabled} onValueModified={setDraft}
    onKeyDown={(e) => { if (e.key === 'Enter') commit(); }} onBlur={commit} data-e2e={tag} />;
}

/** Text filter over functions.list (optionally by type), one row per match; picking one calls onPick(f). */
function LiveFunctionSearch({ functions, types, exclude, onPick, placeholder, tag }) {
  const [needle, setNeedle] = React.useState('');
  const D = window.QLCData;
  const n = needle.trim().toLowerCase();
  const matches = n ? functions.filter(f => !f.hidden && (!types || types.indexOf(f.type) !== -1) && (!exclude || exclude.indexOf(String(f.id)) === -1)
    && (f.name.toLowerCase().indexOf(n) !== -1 || String(f.id) === n)).slice(0, 40) : [];
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '4px 6px' }}>
      <span style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
        <img src={D.icon('search')} alt="" style={{ width: 14, height: 14 }} />
        <LiveTextField value={needle} onChange={setNeedle} placeholder={placeholder || 'Search functions'} tag={tag} />
      </span>
      {n ? (
        <div style={{ maxHeight: 190, overflow: 'auto', border: 'var(--border-dark)' }} data-e2e={tag ? tag + '-matches' : undefined}>
          {matches.length ? matches.map(f => (
            <div key={f.id} role="button" onClick={() => { onPick(f); setNeedle(''); }} data-e2e-fn={f.id}
              style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 4px', cursor: 'pointer' }}>
              <RobotoText label={f.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
              <RobotoText label={f.type} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
            </div>
          )) : <RobotoText label="No match" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={4} />}
        </div>
      ) : null}
    </div>
  );
}

/** DayTimeTool.qml: hours / minutes / seconds spin boxes over one seconds-since-midnight value; commits on Enter/blur. */
function DayTimeTool({ value, onCommit, disabled, tag }) {
  const v = Math.max(0, Number(value) || 0);
  const h = Math.floor(v / 3600), m = Math.floor((v % 3600) / 60), s = v % 60;
  const set = (nh, nm, ns) => onCommit(nh * 3600 + nm * 60 + ns);
  return (
    <span style={{ display: 'inline-flex', gap: 3, opacity: disabled ? .5 : 1 }} data-e2e={tag}>
      <LiveSpin value={h} from={0} to={23} suffix="h" width={54} disabled={disabled} onCommit={(x) => set(x, m, s)} tag={tag ? tag + '-h' : undefined} />
      <LiveSpin value={m} from={0} to={59} suffix="m" width={54} disabled={disabled} onCommit={(x) => set(h, x, s)} tag={tag ? tag + '-m' : undefined} />
      <LiveSpin value={s} from={0} to={59} suffix="s" width={54} disabled={disabled} onCommit={(x) => set(h, m, x)} tag={tag ? tag + '-s' : undefined} />
    </span>
  );
}

/** TimeUtils.js msToStringWithPrecision(ms, 1): hh:mm:ss.z */
function msToClockString(ms) {
  ms = Math.max(0, Math.round(Number(ms) || 0));
  const h = Math.floor(ms / 3600000), m = Math.floor((ms % 3600000) / 60000), s = Math.floor((ms % 60000) / 1000), z = Math.floor((ms % 1000) / 100);
  const two = (n) => (n < 10 ? '0' : '') + n;
  return two(h) + ':' + two(m) + ':' + two(s) + '.' + z;
}

function toolButton(faSource, tooltip, onClick, disabled, tag, faColor) {
  return <IconButton faSource={faSource} faColor={faColor} size={24} tooltip={tooltip} disabled={disabled} onClick={onClick} data-e2e={tag} />;
}

/* ================================================================ XY Pad body */
function VCXYPadBodyEx({ w }) {
  const vc = useVC();
  const D = window.QLCData;
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const pos = vc.live.xy[w.id] || { x: 0.5, y: 0.5 };
  const canSet = !vc.unsupported(VC_METHODS.XY_SET);
  const canFloor = !vc.unsupported(LIVE_METHODS.FLOOR);
  const floor = !!cfg.floorControl;
  const floorSize = cfg.floorSize || { x: 10, y: 10, z: 10 };
  const heightMax = Number(cfg.floorHeightMax) || 20, heightStep = Number(cfg.floorHeightStep) || 0.5;
  const [floorPos, setFloorPos] = React.useState(w.floorPosition || cfg.floorPosition || { x: floorSize.x / 2, y: 0, z: floorSize.z / 2 });
  const [activePreset, setActivePreset] = React.useState(w.activePresetId != null ? Number(w.activePresetId) : (cfg.activePresetId != null ? Number(cfg.activePresetId) : -1));
  React.useEffect(() => { if (w.floorPosition) setFloorPos(w.floorPosition); }, [w.floorPosition && w.floorPosition.x, w.floorPosition && w.floorPosition.y, w.floorPosition && w.floorPosition.z]);
  React.useEffect(() => { if (w.activePresetId != null) setActivePreset(Number(w.activePresetId)); }, [w.activePresetId]);
  useWidgetEvent(vc, 'vc.xyPad.floorPositionChanged', w.id, (d) => setFloorPos({ x: Number(d.x), y: Number(d.y), z: Number(d.z) }));
  useWidgetEvent(vc, 'vc.xyPad.activePresetChanged', w.id, (d) => setActivePreset(Number(d.activePresetId)));
  const throttled = useThrottledSender(33);
  const ref = React.useRef(null);
  const [press, setPress] = React.useState(false);
  const stop = (e) => e.stopPropagation();
  const sendFloor = (p) => { setFloorPos(p); throttled('floor:' + w.id, () => vc.qlc.call(LIVE_METHODS.FLOOR, { widgetId: String(w.id), x: Math.round(p.x * 100) / 100, y: Math.round(p.y * 100) / 100, z: Math.round(p.z * 100) / 100 }).catch(liveMethodError(vc, LIVE_METHODS.FLOOR))); };
  const set = (e) => {
    const r = ref.current.getBoundingClientRect();
    const fx = vcClamp((e.clientX - r.left) / (r.width || 1), 0, 1), fy = vcClamp((e.clientY - r.top) / (r.height || 1), 0, 1);
    if (floor) sendFloor({ x: fx * floorSize.x, y: floorPos.y, z: fy * floorSize.z });
    else vc.act.xySet(w.id, fx, fy);
  };
  const down = (e) => {
    if (vc.edit) return;
    e.preventDefault(); stop(e);
    if (floor ? !canFloor : !canSet) { vc.notice('This server has no ' + (floor ? LIVE_METHODS.FLOOR : VC_METHODS.XY_SET) + ' yet'); return; }
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
    setPress(true); set(e);
  };
  const move = (e) => { if (press) { e.preventDefault(); set(e); } };
  const up = () => { if (press) { setPress(false); if (!floor) vc.act.xyRelease(w.id); } };
  const cx = (floor ? floorPos.x / (floorSize.x || 1) : pos.x) * 100 + '%', cy = (floor ? floorPos.z / (floorSize.z || 1) : pos.y) * 100 + '%';
  const presets = Array.isArray(cfg.presets) ? cfg.presets : [];
  const apply = (p) => vc.qlc.call(LIVE_METHODS.PRESET_APPLY, { widgetId: String(w.id), presetId: p.presetId }).catch(liveMethodError(vc, LIVE_METHODS.PRESET_APPLY));
  /* Range window (turquoise sliders in the QML item) drawn as a rectangle over the pad. */
  const hr = cfg.horizontalRange || { min: 0, max: 255 }, vr = cfg.verticalRange || { min: 0, max: 255 };
  const win = { left: hr.min / 256 * 100 + '%', top: vr.min / 256 * 100 + '%', width: (hr.max - hr.min) / 256 * 100 + '%', height: (vr.max - vr.min) / 256 * 100 + '%' };
  const limited = hr.min > 0 || hr.max < 255 || vr.min > 0 || vr.max < 255;
  const gridLines = floor ? Array.from({ length: Math.max(1, Math.round(floorSize.x)) - 1 }, (_, i) => (i + 1) / Math.round(floorSize.x)) : [];
  const gridRows = floor ? Array.from({ length: Math.max(1, Math.round(floorSize.z)) - 1 }, (_, i) => (i + 1) / Math.round(floorSize.z)) : [];
  return (
    <div data-e2e="xypad-body" data-e2e-floor={floor ? 'on' : 'off'} style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', flexDirection: 'column', overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      <div style={{ flex: 1, minHeight: 0, display: 'flex', gap: 4, margin: 4 }}>
        <div ref={ref} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} data-vc-pad=""
          style={{ flex: 1, minWidth: 0, position: 'relative', background: floor ? 'var(--bg-medium)' : 'var(--bg-stronger)', border: 'var(--border-dark)', touchAction: 'none', cursor: vc.edit ? 'default' : 'crosshair' }}>
          {floor ? gridLines.map((f, i) => <div key={'gx' + i} style={{ position: 'absolute', left: f * 100 + '%', top: 0, bottom: 0, width: 1, background: 'var(--bg-light)', opacity: .5 }} />) : null}
          {floor ? gridRows.map((f, i) => <div key={'gz' + i} style={{ position: 'absolute', top: f * 100 + '%', left: 0, right: 0, height: 1, background: 'var(--bg-light)', opacity: .5 }} />) : null}
          {!floor ? <div style={{ position: 'absolute', left: '50%', top: 0, bottom: 0, width: 1, background: 'var(--bg-control)' }} /> : null}
          {!floor ? <div style={{ position: 'absolute', top: '50%', left: 0, right: 0, height: 1, background: 'var(--bg-control)' }} /> : null}
          {limited ? <div style={Object.assign({ position: 'absolute', border: '1px solid turquoise', pointerEvents: 'none' }, win)} /> : null}
          <div style={{ position: 'absolute', left: cx, top: 0, bottom: 0, width: 1, background: press ? 'var(--selection)' : 'var(--fader-track)' }} />
          <div style={{ position: 'absolute', top: cy, left: 0, right: 0, height: 1, background: press ? 'var(--selection)' : 'var(--fader-track)' }} />
          <div style={{ position: 'absolute', left: cx, top: cy, width: 14, height: 14, marginLeft: -7, marginTop: -7, borderRadius: 7, background: press ? 'var(--selection)' : (floor ? 'var(--check-lime)' : 'var(--fader-track)'), border: '2px solid var(--fg-main)' }} />
          {floor && floorPos.y > 0 ? <div style={{ position: 'absolute', left: cx, top: cy, width: 14 + floorPos.y * 2, height: 14 + floorPos.y * 2, marginLeft: -(7 + floorPos.y), marginTop: -(7 + floorPos.y), borderRadius: '50%', border: '1px dashed var(--check-lime)', opacity: .8, pointerEvents: 'none' }} /> : null}
        </div>
        {floor ? (
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', flex: 'none', width: 44 }} onPointerDown={stop}>
            <span style={{ fontSize: 11, fontFamily: 'var(--font-mono)', color: 'var(--fg-light)' }}>{floorPos.y.toFixed(1) + 'm'}</span>
            <VCFader value={Math.round(floorPos.y / heightStep)} from={0} to={Math.round(heightMax / heightStep)} height={Math.max(30, (w.geometry ? w.geometry.height : 200) - 90)} width={38}
              onMoved={(v) => sendFloor({ x: floorPos.x, y: v * heightStep, z: floorPos.z })} disabled={vc.edit || !canFloor} />
          </div>
        ) : null}
      </div>
      {presets.length ? (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3, padding: '0 4px', flex: 'none' }} data-e2e="xypad-presets" onPointerDown={stop}>
          {presets.map(p => (
            <button key={p.presetId} type="button" title={p.name} data-e2e-preset={p.presetId} disabled={vc.edit} onClick={(e) => { stop(e); apply(p); }}
              style={{ display: 'inline-flex', alignItems: 'center', gap: 4, height: 24, padding: '0 6px', borderRadius: 3, cursor: 'pointer', background: p.color || 'var(--bg-control)', color: '#222',
                border: Number(p.presetId) === activePreset ? '2px solid white' : '1px solid var(--bg-light)', fontSize: 'var(--text-size-menubar)', maxWidth: 120, overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }}>
              <img src={D.icon(p.functionType === 'Scene' ? 'scene' : (XY_PRESET_ICON[p.presetType] || 'position'))} alt="" style={{ width: 14, height: 14 }} />
              {p.name}
            </button>
          ))}
        </div>
      ) : null}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '0 6px 4px', flex: 'none', fontSize: 'var(--text-size-small)' }}>
        <span style={{ flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{style.caption || 'XY Pad'}</span>
        <span data-e2e="xypad-readout" style={{ fontFamily: 'var(--font-mono)', fontSize: 12, color: 'var(--fg-light)' }}>
          {floor ? ('X ' + floorPos.x.toFixed(1) + 'm  Z ' + floorPos.z.toFixed(1) + 'm  H ' + floorPos.y.toFixed(1) + 'm') : ('X ' + Math.round(pos.x * 255) + '  Y ' + Math.round(pos.y * 255))}
        </span>
      </div>
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ================================================================ XY Pad properties */
/** "Add a fixture/head or a group" picker: fixtures.list (movers first), fixture groups, universes. */
function XyPadAddDialog({ open, onClose, onAdd }) {
  const vc = useVC();
  const [tab, setTab] = React.useState('fixtures');
  const [fixtures, setFixtures] = React.useState(null);
  const [groups, setGroups] = React.useState(null);
  const [needle, setNeedle] = React.useState('');
  const [head, setHead] = React.useState(-1);
  const [err, setErr] = React.useState('');
  React.useEffect(() => {
    if (!open) return;
    setErr(''); setNeedle('');
    vc.qlc.call('fixtures.list').then(r => setFixtures(r.fixtures || [])).catch(() => setFixtures([]));
    vc.qlc.call('fixtures.group.list').then(r => setGroups(r.groups || [])).catch(() => setGroups([]));
  }, [open]);
  const n = needle.trim().toLowerCase();
  const isMover = (f) => /moving|scanner|head/i.test(f.fixtureType || '');
  const list = (fixtures || []).filter(f => !n || f.name.toLowerCase().indexOf(n) !== -1).sort((a, b) => (isMover(b) - isMover(a)) || a.name.localeCompare(b.name)).slice(0, 300);
  const universes = Array.from(new Set((fixtures || []).map(f => Number(f.universe)))).sort((a, b) => a - b);
  const add = (params, label) => { setErr(''); onAdd(params).then(() => vc.notice(label + ' added to the pad')).catch(e => setErr((e && e.message) || 'failed')); };
  const tabBtn = (id, label) => <GenericButton label={label} width={90} height={24} fontSize="var(--text-size-menubar)" bgColor={tab === id ? 'var(--highlight)' : undefined} onClick={() => setTab(id)} data-e2e={'xypad-add-tab-' + id} />;
  return (
    <CustomPopupDialog open={open} title="Add fixtures to the XY pad" width={460} standardButtons={['Close']} onClose={onClose} onClicked={onClose}>
      <div data-e2e="xypad-add-dialog" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', gap: 4 }}>{tabBtn('fixtures', 'Fixtures')}{tabBtn('groups', 'Groups')}{tabBtn('universes', 'Universes')}</div>
        {tab === 'fixtures' ? (
          <>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <LiveTextField value={needle} onChange={setNeedle} placeholder="Search fixtures" tag="xypad-add-search" />
              <RobotoText label="Head" fontSize="var(--text-size-menubar)" height="auto" />
              <CustomSpinBox value={head} from={-1} to={63} width={56} height={24} showControls={false} onValueModified={setHead} data-e2e="xypad-add-head" />
            </div>
            <RobotoText label="Head -1 = every Pan/Tilt head of the fixture; a fixture without Pan/Tilt channels is refused by the server." fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" />
            <div style={{ maxHeight: 280, overflow: 'auto', border: 'var(--border-dark)' }} data-e2e="xypad-add-fixtures">
              {fixtures == null ? <RobotoText label="Loading fixtures…" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} /> : null}
              {list.map(f => (
                <div key={f.id} role="button" data-e2e-fixture={f.id} onClick={() => add(head >= 0 ? { fixtureId: String(f.id), headIndex: head } : { fixtureId: String(f.id) }, f.name)}
                  style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', borderBottom: 'var(--border-dark)', opacity: isMover(f) ? 1 : .6 }}>
                  <RobotoText label={f.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                  <RobotoText label={(f.fixtureType || '') + ' · U' + (Number(f.universe) + 1) + '.' + (Number(f.address) + 1)} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
                </div>
              ))}
            </div>
          </>
        ) : null}
        {tab === 'groups' ? (
          <div style={{ maxHeight: 300, overflow: 'auto', border: 'var(--border-dark)' }} data-e2e="xypad-add-groups">
            {groups == null ? <RobotoText label="Loading…" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} /> : null}
            {groups && !groups.length ? <RobotoText label="No fixture groups in this project" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} /> : null}
            {(groups || []).map(g => (
              <div key={g.id} role="button" data-e2e-group={g.id} onClick={() => add({ fixtureGroupId: String(g.id) }, g.name)}
                style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', borderBottom: 'var(--border-dark)' }}>
                <img src={window.QLCData.icon('group')} alt="" style={{ width: 16, height: 16 }} />
                <RobotoText label={g.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
              </div>
            ))}
            <RobotoText label="A dropped group is kept as one entry (its members are resolved live) and also gets its own preset." fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} />
          </div>
        ) : null}
        {tab === 'universes' ? (
          <div style={{ border: 'var(--border-dark)' }} data-e2e="xypad-add-universes">
            {universes.map(u => (
              <div key={u} role="button" data-e2e-universe={u} onClick={() => add({ universe: u }, 'Universe ' + (u + 1))}
                style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', borderBottom: 'var(--border-dark)' }}>
                <RobotoText label={'Universe ' + (u + 1)} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                <RobotoText label={(fixtures || []).filter(f => Number(f.universe) === u).length + ' fixtures'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
              </div>
            ))}
          </div>
        ) : null}
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" wrapText /> : null}
      </div>
    </CustomPopupDialog>
  );
}

/** "Set Pan/Tilt range" (rangePopup in VCXYPadProperties.qml), in the pad's current display units. */
function XyPadRangeDialog({ open, entry, onClose, onApply }) {
  const xr = (entry && entry.xRange) || { min: 0, max: 100, reverse: false, maxValue: 100 };
  const yr = (entry && entry.yRange) || { min: 0, max: 100, reverse: false, maxValue: 100 };
  const [v, setV] = React.useState({ xMin: xr.min, xMax: xr.max, xReverse: !!xr.reverse, yMin: yr.min, yMax: yr.max, yReverse: !!yr.reverse });
  const [err, setErr] = React.useState('');
  React.useEffect(() => { if (open) { setV({ xMin: xr.min, xMax: xr.max, xReverse: !!xr.reverse, yMin: yr.min, yMax: yr.max, yReverse: !!yr.reverse }); setErr(''); } }, [open, entry]);
  const units = (entry && entry.units) || '%';
  const row = (label, lo, hi, rev, max) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
      <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 40px' }} />
      <CustomSpinBox value={v[lo]} from={0} to={max} suffix={units} width={90} height={24} onValueModified={(x) => setV(s => Object.assign({}, s, { [lo]: x }))} data-e2e={'xypad-range-' + lo} />
      <CustomSpinBox value={v[hi]} from={0} to={max} suffix={units} width={90} height={24} onValueModified={(x) => setV(s => Object.assign({}, s, { [hi]: x }))} data-e2e={'xypad-range-' + hi} />
      <CustomCheckBox checked={v[rev]} size={20} onToggled={(b) => setV(s => Object.assign({}, s, { [rev]: b }))} tooltip="Reverse" data-e2e={'xypad-range-' + rev} />
      <RobotoText label="Reverse" fontSize="var(--text-size-menubar)" height="auto" />
    </div>
  );
  const apply = () => {
    if (v.xMin >= v.xMax || v.yMin >= v.yMax) { setErr('Minimum must be below maximum'); return; }
    onApply(v).then(onClose).catch(e => setErr((e && e.message) || 'failed'));
  };
  return (
    <CustomPopupDialog open={open} title="Set Pan/Tilt range" width={420} standardButtons={['Cancel', 'Apply']} onClose={onClose} onClicked={(b) => { if (b === 'Apply') apply(); else onClose(); }}>
      <div data-e2e="xypad-range-dialog" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', gap: 6 }}><span style={{ flex: '0 0 40px' }} /><RobotoText label="Minimum" fontSize="var(--text-size-menubar)" height="auto" style={{ width: 90 }} /><RobotoText label="Maximum" fontSize="var(--text-size-menubar)" height="auto" style={{ width: 90 }} /></div>
        {row('Pan', 'xMin', 'xMax', 'xReverse', xr.maxValue || 100)}
        {row('Tilt', 'yMin', 'yMax', 'yReverse', yr.maxValue || 100)}
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" /> : null}
      </div>
    </CustomPopupDialog>
  );
}

function headOf(entry) {
  return entry.fixtureGroupId != null ? { fixtureGroupId: String(entry.fixtureGroupId) } : { fixtureId: String(entry.fixtureId), headIndex: Number(entry.headIndex) || 0 };
}
function entryKey(entry) { return entry.fixtureGroupId != null ? 'g' + entry.fixtureGroupId : 'f' + entry.fixtureId + ':' + entry.headIndex; }

function VCXYPadProps({ w, cfg, setConfig, functions, CheckRow, PropRow }) {
  const vc = useVC();
  const D = window.QLCData;
  const section = useLiveSections([]);
  const structural = useStructural(vc, w);
  const fixtures = Array.isArray(cfg.fixtures) ? cfg.fixtures : [];
  const presets = Array.isArray(cfg.presets) ? cfg.presets : [];
  const [sel, setSel] = React.useState({});
  const [dialog, setDialog] = React.useState(null);
  const [presetSel, setPresetSel] = React.useState(-1);
  const [presetTool, setPresetTool] = React.useState(null);
  const [groups, setGroups] = React.useState(null);
  const [pName, setPName] = React.useState('');
  const selected = fixtures.filter(f => sel[entryKey(f)]);
  React.useEffect(() => { setSel({}); }, [cfg.displayMode]);
  const selectedPreset = presets.find(p => Number(p.presetId) === presetSel) || null;
  React.useEffect(() => { if (presetSel >= 0 && !selectedPreset) setPresetSel(-1); if (selectedPreset) setPName(selectedPreset.name || ''); }, [presets, presetSel]);
  React.useEffect(() => { if (presetTool === 'group' && groups == null) vc.qlc.call('fixtures.group.list').then(r => setGroups(r.groups || [])).catch(() => setGroups([])); }, [presetTool]);
  const hr = cfg.horizontalRange || { min: 0, max: 255 }, vr = cfg.verticalRange || { min: 0, max: 255 };
  const cycleUnits = () => setConfig({ displayMode: cfg.displayMode === 'Degrees' ? 'Percentage' : cfg.displayMode === 'Percentage' ? 'DMX' : 'Degrees' });
  const unitsLabel = cfg.displayMode === 'Percentage' ? '%' : cfg.displayMode === 'DMX' ? 'DMX' : '°';
  const remove = () => structural('vc.xyPad.fixture.remove', { heads: selected.map(headOf) }).then(() => setSel({})).catch(() => {});
  const addPreset = (preset) => structural(LIVE_METHODS.PRESET_ADD, { preset }).then(r => { if (r && r.presetId != null) setPresetSel(Number(r.presetId)); }).catch(() => {});
  const removePreset = () => { if (presetSel >= 0) structural(LIVE_METHODS.PRESET_REMOVE, { presetId: presetSel }).then(() => setPresetSel(-1)).catch(() => {}); };
  const movePreset = (dir) => { if (presetSel >= 0) structural('vc.xyPad.preset.move', { presetId: presetSel, direction: dir }).then(r => { if (r && r.presetId != null) setPresetSel(Number(r.presetId)); }).catch(() => {}); };
  const renamePreset = (t) => { if (selectedPreset && t.trim() && t.trim() !== selectedPreset.name) structural('vc.xyPad.preset.rename', { presetId: presetSel, name: t.trim() }).catch(() => {}); };
  const presetLabel = (p) => p.presetType === 'function' ? (p.functionType || 'Function') : p.presetType === 'position' ? 'Position' : ((p.headsCount != null ? p.headsCount : 0) + ' heads');
  return (
    <>
      {section('xyDisplay', 'Display Properties', (
        <div>
          <CheckRow label="Inverted Y-Axis" checked={!!cfg.invertedAppearance} onToggle={(b) => setConfig({ invertedAppearance: b })} />
          <CheckRow label="Floor control (point the fixtures at a stage floor position)" checked={!!cfg.floorControl} onToggle={(b) => setConfig({ floorControl: b })} />
          <PropRow label="Units">
            <CustomComboBox width="100%" height={24} currValue={cfg.displayMode || 'Degrees'} onValueChanged={(v) => setConfig({ displayMode: v })} data-e2e="xypad-units"
              model={[{ mLabel: 'Degrees (°)', mValue: 'Degrees' }, { mLabel: 'Percentage (%)', mValue: 'Percentage' }, { mLabel: 'DMX values', mValue: 'DMX' }]} />
          </PropRow>
          <PropRow label="Pan window"><LiveSpin value={Math.round(hr.min)} from={0} to={255} width={62} onCommit={(v) => setConfig({ horizontalRange: { min: v, max: Math.max(v + 1, hr.max) } })} tag="xypad-hmin" /><LiveSpin value={Math.round(hr.max)} from={1} to={256} width={62} onCommit={(v) => setConfig({ horizontalRange: { min: Math.min(hr.min, v - 1), max: v } })} tag="xypad-hmax" /></PropRow>
          <PropRow label="Tilt window"><LiveSpin value={Math.round(vr.min)} from={0} to={255} width={62} onCommit={(v) => setConfig({ verticalRange: { min: v, max: Math.max(v + 1, vr.max) } })} tag="xypad-vmin" /><LiveSpin value={Math.round(vr.max)} from={1} to={256} width={62} onCommit={(v) => setConfig({ verticalRange: { min: Math.min(vr.min, v - 1), max: v } })} tag="xypad-vmax" /></PropRow>
        </div>
      ))}
      {section('xyFixtures', 'Fixtures', (
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '2px 6px', background: 'var(--bg-medium)' }}>
            <GenericButton label={unitsLabel} width={44} height={24} fontSize="var(--text-size-small)" onClick={cycleUnits} data-e2e="xypad-units-cycle" />
            <RobotoText label={fixtures.length + ' entries'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" style={{ flex: 1 }} />
            {toolButton('fa_plus', 'Add a fixture/head or a group. A dropped group also gets its own preset', () => setDialog('add'), false, 'xypad-add', 'limegreen')}
            <GenericButton label="Range" width={54} height={24} fontSize="var(--text-size-menubar)" disabled={!selected.length} onClick={() => setDialog('range')} data-e2e="xypad-range" />
            {toolButton('fa_trash_can', 'Remove the selected fixture head(s)', remove, !selected.length, 'xypad-remove', 'crimson')}
          </div>
          <div style={{ display: 'flex', fontSize: 'var(--text-size-menubar)', color: 'var(--fg-light)', padding: '0 6px', gap: 4 }}>
            <span style={{ flex: 1 }}>Fixture</span><span style={{ width: 92 }}>X-Axis Range</span><span style={{ width: 92 }}>Y-Axis Range</span>
          </div>
          <div style={{ maxHeight: 220, overflow: 'auto' }} data-e2e="xypad-fixtures">
            {fixtures.length ? fixtures.map(f => (
              <div key={entryKey(f)} role="button" data-e2e-entry={entryKey(f)} onClick={(e) => setSel(s => e.ctrlKey || e.metaKey ? Object.assign({}, s, { [entryKey(f)]: !s[entryKey(f)] }) : { [entryKey(f)]: true })}
                style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', background: sel[entryKey(f)] ? 'var(--highlight)' : 'transparent' }}>
                <img src={D.icon(f.fixtureGroupId != null ? 'group' : 'position')} alt="" style={{ width: 14, height: 14, flex: 'none' }} />
                <RobotoText label={f.name || (f.fixtureGroupId != null ? 'Group ' + f.fixtureGroupId : 'Fixture ' + f.fixtureId)} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1, minWidth: 0 }} />
                <RobotoText label={f.xRangeLabel || ''} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" style={{ width: 92 }} />
                <RobotoText label={f.yRangeLabel || ''} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" style={{ width: 92 }} />
              </div>
            )) : <RobotoText label="No fixture on this pad yet - add one with +" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
          </div>
          <XyPadAddDialog open={dialog === 'add'} onClose={() => setDialog(null)} onAdd={(params) => structural('vc.xyPad.fixture.add', params)} />
          <XyPadRangeDialog open={dialog === 'range'} entry={selected[0]} onClose={() => setDialog(null)}
            onApply={(v) => structural('vc.xyPad.setHeadsRange', Object.assign({ heads: selected.map(headOf) }, v))} />
        </div>
      ))}
      {section('xyPresets', 'Presets', (
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '2px 6px', background: 'var(--bg-medium)' }}>
            <IconButton imgSource={D.icon('functions')} size={24} tooltip="Add a Scene/EFX function as a preset" checked={presetTool === 'function'} onClick={() => setPresetTool(presetTool === 'function' ? null : 'function')} data-e2e="xypad-preset-fn" />
            <IconButton imgSource={D.icon('group')} size={24} tooltip="Add a fixture group (or every head of one fixture on the pad) as a preset" checked={presetTool === 'group'} onClick={() => setPresetTool(presetTool === 'group' ? null : 'group')} data-e2e="xypad-preset-group" />
            <IconButton imgSource={D.icon('position')} size={24} tooltip="Create a position preset from the current XY position" onClick={() => addPreset({ presetType: 'position' })} data-e2e="xypad-preset-position" />
            <span style={{ flex: 1 }} />
            {toolButton('fa_trash_can', 'Remove selected preset', removePreset, presetSel < 0, 'xypad-preset-remove', 'crimson')}
            {toolButton('fa_chevron_down', 'Move selected preset down', () => movePreset('down'), presetSel < 0, 'xypad-preset-down')}
            {toolButton('fa_chevron_up', 'Move selected preset up', () => movePreset('up'), presetSel < 0, 'xypad-preset-up')}
          </div>
          {presetTool === 'function' ? <LiveFunctionSearch functions={functions} types={['Scene', 'EFX']} onPick={(f) => addPreset({ presetType: 'function', functionID: String(f.id) })} placeholder="Search Scene / EFX functions" tag="xypad-preset-fn-search" /> : null}
          {presetTool === 'group' ? (
            <div style={{ maxHeight: 160, overflow: 'auto', border: 'var(--border-dark)', margin: '4px 6px' }} data-e2e="xypad-preset-groups">
              {(groups || []).map(g => (
                <div key={g.id} role="button" data-e2e-group={g.id} onClick={() => addPreset({ presetType: 'fixtureGroup', fixtureGroupId: String(g.id) })}
                  style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 4px', cursor: 'pointer' }}>
                  <img src={D.icon('group')} alt="" style={{ width: 14, height: 14 }} /><RobotoText label={g.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                </div>
              ))}
              {fixtures.filter(f => f.fixtureGroupId == null).map(f => (
                <div key={entryKey(f)} role="button" data-e2e-head={entryKey(f)} onClick={() => addPreset({ presetType: 'fixtureGroupHead', fixtureId: String(f.fixtureId), headIndex: Number(f.headIndex) || 0 })}
                  style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 4px', cursor: 'pointer' }}>
                  <img src={D.icon('position')} alt="" style={{ width: 14, height: 14 }} /><RobotoText label={f.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} /><RobotoText label="head" fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
                </div>
              ))}
              {groups && !groups.length && !fixtures.length ? <RobotoText label="No groups, and no head on the pad" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={4} /> : null}
            </div>
          ) : null}
          <PropRow label="Preset name"><LiveTextField value={pName} onChange={setPName} onConfirm={renamePreset} placeholder={selectedPreset ? '' : 'Select a preset'} tag="xypad-preset-name" /></PropRow>
          <div style={{ maxHeight: 220, overflow: 'auto' }} data-e2e="xypad-preset-list">
            {presets.length ? presets.map(p => (
              <div key={p.presetId} role="button" data-e2e-preset-row={p.presetId} onClick={() => setPresetSel(Number(p.presetId))}
                style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', background: Number(p.presetId) === presetSel ? 'var(--highlight)' : 'transparent' }}>
                <img src={D.icon(p.functionType === 'Scene' ? 'scene' : (XY_PRESET_ICON[p.presetType] || 'position'))} alt="" style={{ width: 14, height: 14 }} />
                <RobotoText label={p.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                <RobotoText label={presetLabel(p)} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
              </div>
            )) : <RobotoText label="No presets" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
          </div>
        </div>
      ))}
    </>
  );
}

/* ================================================================ Clock body */
function VCClockBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const type = cfg.clockType || 'Clock';
  const [time, setTime] = React.useState(Number(w.currentTime) || 0);
  const [running, setRunning] = React.useState(!!w.running);
  const [wall, setWall] = React.useState(new Date());
  React.useEffect(() => { if (w.currentTime != null) setTime(Number(w.currentTime)); if (w.running != null) setRunning(!!w.running); }, [w.currentTime, w.running]);
  useGatedTopic(vc, 'vc.clock.timeChanged', type !== 'Clock');
  useWidgetEvent(vc, 'vc.clock.timeChanged', w.id, (d) => { setTime(Number(d.currentTime) || 0); setRunning(!!d.running); });
  /* A plain Clock shows the browser's own wall clock, ticking locally like VCClockItem.qml. */
  React.useEffect(() => { if (type !== 'Clock') return; const t = setInterval(() => setWall(new Date()), 1000); return () => clearInterval(t); }, [type]);
  const stop = (e) => e.stopPropagation();
  const call = (method) => vc.qlc.call(method, { widgetId: String(w.id) }).catch(liveMethodError(vc, method));
  const timer = type !== 'Clock';
  const text = timer ? msToClockString(time) : wall.toLocaleTimeString([], { hour12: false });
  const schedules = Array.isArray(cfg.schedules) ? cfg.schedules : [];
  let daysMask = 0;
  if (type === 'Clock') schedules.forEach(s => { const f = Number(s.weekFlags) & 0x7F; if (f !== 0 && f !== 0x7F) daysMask |= f; });
  const structural = useStructural(vc, w);
  const g = w.geometry || { width: 150, height: 60 };
  const fontPx = Math.max(12, Math.min(g.height * 0.55, (g.width - 40) / 6.5));
  return (
    <div data-e2e="clock-body" data-e2e-running={running ? 'true' : 'false'} style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', alignItems: 'stretch', overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', cursor: timer && !vc.edit ? 'pointer' : 'default', userSelect: 'none' }}
        title={timer ? 'Click: play / pause · right-click: reset' : (style.caption || 'Clock')}
        onClick={(e) => { if (vc.edit || !timer) return; stop(e); if (type === 'Countdown' && time <= 0) return; call(LIVE_METHODS.CLOCK_PLAY); }}
        onContextMenu={(e) => { e.preventDefault(); if (vc.edit || !timer) return; stop(e); call(LIVE_METHODS.CLOCK_RESET); }}>
        <span data-e2e="clock-time" style={Object.assign({ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', fontFamily: 'var(--font-mono)', fontWeight: 700, lineHeight: 1 }, vcFontCss(style), { fontSize: fontPx, color: running ? 'var(--check-lime)' : (style.foregroundColor || 'var(--fg-main)') })}>{text}</span>
        {type === 'Clock' && daysMask ? (
          <div style={{ display: 'flex', gap: 2, padding: '0 4px 2px', fontSize: 11 }}>
            {DAYS.map((d, i) => <span key={i} style={{ flex: 1, textAlign: 'center', fontWeight: (daysMask >> i) & 1 ? 700 : 400, color: (daysMask >> i) & 1 ? 'var(--highlight)' : 'inherit', opacity: (daysMask >> i) & 1 ? 1 : .45 }}>{d}</span>)}
          </div>
        ) : null}
        {timer ? (
          <div style={{ display: 'flex', gap: 2, padding: '0 4px 3px', justifyContent: 'center' }} onPointerDown={stop}>
            <IconButton faSource={running ? 'fa_pause' : 'fa_play'} size={22} tooltip={running ? 'Pause' : 'Start'} disabled={vc.edit || (type === 'Countdown' && time <= 0 && !running)} onClick={(e) => { stop(e); call(LIVE_METHODS.CLOCK_PLAY); }} data-e2e="clock-play" />
            <GenericButton label="Reset" width={50} height={22} fontSize="var(--text-size-menubar)" disabled={vc.edit} onClick={(e) => { stop(e); call(LIVE_METHODS.CLOCK_RESET); }} data-e2e="clock-reset" />
          </div>
        ) : null}
      </div>
      <div style={{ display: 'flex', alignItems: 'center', padding: '0 3px', flex: 'none' }} onPointerDown={stop}>
        <IconButton faSource="fa_check" faColor="lime" size={Math.min(g.height - 8, 30)} checked={!!cfg.enableSchedule} tooltip="Enable/Disable this scheduler" disabled={vc.edit}
          onClick={(e) => { stop(e); structural('vc.widget.setConfig', { config: { enableSchedule: !cfg.enableSchedule } }).catch(() => {}); }} data-e2e="clock-enable" />
      </div>
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ================================================================ Clock properties */
function VCClockProps({ w, cfg, setConfig, functions, CheckRow, PropRow }) {
  const vc = useVC();
  const D = window.QLCData;
  const section = useLiveSections([]);
  const structural = useStructural(vc, w);
  const type = cfg.clockType || 'Clock';
  const schedules = Array.isArray(cfg.schedules) ? cfg.schedules : [];
  const [adding, setAdding] = React.useState(false);
  const fnName = (id) => { const f = functions.find(x => String(x.id) === String(id)); return f ? f.name : 'Function #' + id; };
  const update = (index, patch) => structural('vc.clock.schedule.update', Object.assign({ index }, patch)).catch(() => {});
  const flag = (s, bit) => (Number(s.weekFlags) & bit) !== 0;
  const setFlag = (s, bit, on) => update(s.index, { weekFlags: on ? (Number(s.weekFlags) | bit) : (Number(s.weekFlags) & ~bit) });
  return (
    <>
      {section('clockType', 'Clock type', (
        <div>
          {CLOCK_TYPES.map(t => <CheckRow key={t} label={t} checked={type === t} onToggle={() => setConfig({ clockType: t })} />)}
          {type === 'Countdown' ? <PropRow label="Countdown"><DayTimeTool value={Math.round((Number(cfg.targetTime) || 0) / 1000)} onCommit={(secs) => setConfig({ targetTime: secs * 1000 })} tag="clock-target" /></PropRow> : null}
          <CheckRow label="Enable the scheduler" checked={!!cfg.enableSchedule} onToggle={(b) => setConfig({ enableSchedule: b })} />
        </div>
      ))}
      {type !== 'Stopwatch' ? section('clockSchedule', 'Schedule', (
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '2px 6px', background: 'var(--bg-medium)' }}>
            <RobotoText label={schedules.length + ' schedule' + (schedules.length === 1 ? '' : 's') + (type === 'Countdown' ? ' - every one starts when the countdown reaches 0' : '')} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" wrapText style={{ flex: 1 }} />
            {toolButton('fa_plus', 'Add a function schedule', () => setAdding(!adding), false, 'clock-schedule-add', 'limegreen')}
          </div>
          {adding ? <LiveFunctionSearch functions={functions} onPick={(f) => structural('vc.clock.schedule.add', { functionIds: [String(f.id)] }).then(() => setAdding(false)).catch(() => {})} tag="clock-schedule-fn" /> : null}
          <div data-e2e="clock-schedules">
            {schedules.map(s => (
              <div key={s.index} data-e2e-schedule={s.index} style={{ borderBottom: '1px solid var(--fg-medium)', padding: '2px 0' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 6px' }}>
                  <img src={D.icon('functions')} alt="" style={{ width: 14, height: 14 }} />
                  <RobotoText label={s.functionName || fnName(s.functionID)} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                  {toolButton('fa_trash_can', 'Remove this schedule', () => structural('vc.clock.schedule.remove', { index: s.index }).catch(() => {}), false, 'clock-schedule-remove-' + s.index, 'darkred')}
                </div>
                {type === 'Clock' ? (
                  <>
                    <PropRow label="Start time"><DayTimeTool value={s.startTime} onCommit={(v) => update(s.index, { startTime: v })} tag={'clock-start-' + s.index} /></PropRow>
                    <PropRow label="Stop time">
                      <DayTimeTool value={s.stopTime >= 0 ? s.stopTime : 0} disabled={s.stopTime < 0} onCommit={(v) => update(s.index, { stopTime: v })} tag={'clock-stop-' + s.index} />
                      <CustomCheckBox checked={s.stopTime >= 0} size={20} tooltip="Enable the stop time" onToggled={(b) => update(s.index, { stopTime: b ? Math.min(86399, Number(s.startTime) + 3600) : -1 })} data-e2e={'clock-stop-enable-' + s.index} />
                    </PropRow>
                    <PropRow label="Days">
                      <span style={{ display: 'inline-flex', gap: 3, alignItems: 'center' }}>
                        {DAYS.map((d, i) => <span key={i} style={{ display: 'inline-flex', flexDirection: 'column', alignItems: 'center', fontSize: 10 }}>{d}<CustomCheckBox checked={flag(s, 1 << i)} size={18} onToggled={(b) => setFlag(s, 1 << i, b)} data-e2e={'clock-day-' + s.index + '-' + i} /></span>)}
                        <GenericButton label="Repeat" width={54} height={22} fontSize="var(--text-size-menubar)" bgColor={flag(s, 0x80) ? 'var(--highlight)' : undefined} onClick={() => setFlag(s, 0x80, !flag(s, 0x80))} data-e2e={'clock-repeat-' + s.index} />
                      </span>
                    </PropRow>
                  </>
                ) : null}
              </div>
            ))}
            {!schedules.length ? <RobotoText label="No schedule - add a function with +" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} /> : null}
          </div>
        </div>
      )) : null}
    </>
  );
}

/* ================================================================ Animation body */
function useAnimationLive(vc, w) {
  const cfg = w.typeConfig || {};
  const [level, setLevel] = React.useState(Number(w.faderLevel) || 0);
  const [active, setActive] = React.useState(w.activePresetId != null ? Number(w.activePresetId) : -1);
  const [colors, setColors] = React.useState(Array.isArray(cfg.colors) ? cfg.colors : []);
  const [algo, setAlgo] = React.useState(Number(cfg.algorithmIndex) || 0);
  const [knobs, setKnobs] = React.useState({});
  React.useEffect(() => { if (w.faderLevel != null) setLevel(Number(w.faderLevel)); }, [w.faderLevel]);
  React.useEffect(() => { if (w.activePresetId != null) setActive(Number(w.activePresetId)); }, [w.activePresetId]);
  React.useEffect(() => { if (Array.isArray(cfg.colors)) setColors(cfg.colors); if (cfg.algorithmIndex != null) setAlgo(Number(cfg.algorithmIndex)); }, [JSON.stringify(cfg.colors), cfg.algorithmIndex]);
  useWidgetEvent(vc, 'vc.animation.faderLevelChanged', w.id, (d) => setLevel(Number(d.level) || 0));
  useWidgetEvent(vc, 'vc.animation.activePresetChanged', w.id, (d) => { setActive(Number(d.activePresetId)); if (d.knobPresetId != null) setKnobs(k => Object.assign({}, k, { [d.knobPresetId]: Number(d.knobValue) })); });
  useWidgetEvent(vc, 'vc.animation.styleChanged', w.id, (d) => { if (Array.isArray(d.colors)) setColors(d.colors); if (d.algorithmIndex != null) setAlgo(Number(d.algorithmIndex)); });
  return { level, setLevel, active, colors, algo, knobs, setKnobs };
}

function VCAnimationBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const mask = Array.isArray(cfg.visibilityMask) ? cfg.visibilityMask : ['Fader', 'Label', 'Color1', 'Color2', 'PresetCombo'];
  const has = (f) => mask.indexOf(f) !== -1;
  const live = useAnimationLive(vc, w);
  const structural = useStructural(vc, w);
  const throttled = useThrottledSender(33);
  const hasFn = cfg.functionID != null && String(cfg.functionID) !== NO_FUNCTION_ID;
  const canFade = !vc.unsupported(LIVE_METHODS.FADER);
  const stop = (e) => e.stopPropagation();
  const setFader = (v) => { live.setLevel(v); throttled('anim:' + w.id, () => vc.qlc.call(LIVE_METHODS.FADER, { widgetId: String(w.id), level: Math.round(v) }).catch(liveMethodError(vc, LIVE_METHODS.FADER))); };
  const colorCount = Number(cfg.colorCount) || 0;
  const algorithms = Array.isArray(cfg.algorithms) ? cfg.algorithms : [];
  const presets = Array.isArray(cfg.presets) ? cfg.presets : [];
  /* Colour swatches: the native colour input commits a structural setConfig per change (debounced). */
  const colorTimer = React.useRef({});
  const setColor = (i, hex) => { clearTimeout(colorTimer.current[i]); colorTimer.current[i] = setTimeout(() => { const arr = live.colors.slice(0, Math.max(live.colors.length, i + 1)); while (arr.length <= i) arr.push(''); arr[i] = hex; structural('vc.widget.setConfig', { config: { colors: arr } }).catch(() => {}); }, 250); };
  const apply = (p) => vc.qlc.call(LIVE_METHODS.PRESET_APPLY, { widgetId: String(w.id), presetId: p.presetId }).catch(liveMethodError(vc, LIVE_METHODS.PRESET_APPLY));
  const knobValue = (p) => live.knobs[p.presetId] != null ? live.knobs[p.presetId] : (Number(p.knobValue) || 0);
  const turnKnob = (p, v) => { live.setKnobs(k => Object.assign({}, k, { [p.presetId]: v })); throttled('knob:' + w.id + ':' + p.presetId, () => vc.qlc.call(LIVE_METHODS.KNOB, { widgetId: String(w.id), presetId: p.presetId, value: Math.round(v) }).catch(liveMethodError(vc, LIVE_METHODS.KNOB))); };
  const g = w.geometry || { width: 200, height: 200 };
  const presetLabel = (p) => p.presetType === 'algorithm' ? p.algorithmName : p.presetType === 'text' ? p.text : '';
  return (
    <div data-e2e="animation-body" style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', gap: 4, padding: 4, overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      {has('Fader') ? (
        <div style={{ flex: 'none', display: 'flex', flexDirection: 'column', alignItems: 'center' }} onPointerDown={stop} title={hasFn ? 'Level' : 'Attach an RGB Matrix first'}>
          <VCFader value={live.level} from={0} to={255} height={Math.max(30, g.height - 16)} width={34} onMoved={setFader} disabled={vc.edit || !canFade || !hasFn} />
        </div>
      ) : null}
      <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', gap: 4 }}>
        {has('Label') ? <span style={Object.assign({ textAlign: 'center', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }, vcFontCss(style), { fontSize: 13 })}>{style.caption || ''}</span> : null}
        {colorCount > 0 ? (
          <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap' }} data-e2e="animation-colors" onPointerDown={stop}>
            {Array.from({ length: colorCount }, (_, i) => (
              <label key={i} title={'Color ' + (i + 1)} style={{ width: 28, height: 28, borderRadius: 5, border: '2px solid var(--bg-light)', background: live.colors[i] || 'transparent', cursor: vc.edit ? 'default' : 'pointer', position: 'relative', overflow: 'hidden' }} data-e2e-color={i}>
                <input type="color" value={live.colors[i] || '#000000'} disabled={vc.edit} onChange={(e) => setColor(i, e.target.value)} style={{ position: 'absolute', inset: 0, opacity: 0, width: '100%', height: '100%', cursor: 'pointer' }} />
              </label>
            ))}
          </div>
        ) : null}
        {has('PresetCombo') && algorithms.length ? (
          <div onPointerDown={stop}>
            <CustomComboBox width="100%" height={24} currValue={live.algo} disabled={vc.edit} onValueChanged={(v) => structural('vc.widget.setConfig', { config: { algorithmIndex: Number(v) } }).catch(() => {})} data-e2e="animation-algorithm"
              model={algorithms.map((a, i) => ({ mLabel: a, mValue: i }))} />
          </div>
        ) : null}
        {presets.length ? (
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3, overflow: 'auto', flex: 1, minHeight: 0, alignContent: 'flex-start' }} data-e2e="animation-presets" onPointerDown={stop}>
            {presets.map(p => p.isKnob ? (
              <span key={p.presetId} title={'Color ' + (Number(p.colorIndex) + 1) + ' ' + (p.knobChannel || '') + ' component'} data-e2e-knob={p.presetId} style={{ display: 'inline-flex' }}>
                <VCKnob value={knobValue(p)} from={0} to={255} size={30} disabled={vc.edit} onMoved={(v) => turnKnob(p, v)} style={{ filter: p.knobColor ? 'drop-shadow(0 0 2px ' + p.knobColor + ')' : undefined }} />
              </span>
            ) : (
              <button key={p.presetId} type="button" data-e2e-preset={p.presetId} disabled={vc.edit} onClick={(e) => { stop(e); apply(p); }} title={presetLabel(p) || p.presetType}
                style={{ height: 28, minWidth: p.colorIndex != null && p.colorIndex >= 0 ? 28 : 56, padding: '0 6px', borderRadius: 4, cursor: 'pointer', fontSize: 'var(--text-size-menubar)', color: 'var(--fg-main)',
                  background: p.presetType === 'color' && p.color ? p.color : 'var(--bg-control)', border: Number(p.presetId) === live.active ? '2px solid white' : '1px solid var(--bg-light)', maxWidth: 110, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {presetLabel(p)}
              </button>
            ))}
          </div>
        ) : null}
        {!hasFn ? <RobotoText label="No RGB Matrix attached" fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" height="auto" textHAlign="center" /> : null}
      </div>
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ================================================================ Animation properties */
/** PopupAnimationPreset.qml: pick an RGB script and edit its properties, then add an algorithm preset. */
function AnimationAlgorithmDialog({ open, onClose, onAdd }) {
  const vc = useVC();
  const [scripts, setScripts] = React.useState(null);
  const [name, setName] = React.useState('');
  const [props, setProps] = React.useState([]);
  const [values, setValues] = React.useState({});
  const [err, setErr] = React.useState('');
  React.useEffect(() => {
    if (!open) return;
    setErr('');
    vc.qlc.call('functions.rgbmatrix.listAlgorithms').then(r => { const s = (r.algorithms || []).filter(a => a.type === 'script').map(a => a.name); setScripts(s); if (!name && s.length) setName(s[0]); }).catch(() => setScripts([]));
  }, [open]);
  React.useEffect(() => {
    if (!open || !name) return;
    vc.qlc.call('functions.rgbmatrix.getScriptProperties', { scriptName: name }).then(r => { const p = r.properties || []; setProps(p); const v = {}; p.forEach(x => { if (x.type === 'List' && x.listValues && x.listValues.length) v[x.name] = x.listValues[0]; else if (x.type === 'Range') v[x.name] = String(x.rangeMin != null ? x.rangeMin : 0); }); setValues(v); }).catch(() => { setProps([]); setValues({}); });
  }, [open, name]);
  const editor = (p) => {
    const v = values[p.name] != null ? values[p.name] : '';
    const set = (x) => setValues(s => Object.assign({}, s, { [p.name]: String(x) }));
    if (p.type === 'List') return <CustomComboBox width="100%" height={24} currValue={v} onValueChanged={set} model={(p.listValues || []).map(x => ({ mLabel: x, mValue: x }))} data-e2e={'anim-prop-' + p.name} />;
    if (p.type === 'Range') return <CustomSpinBox value={Number(v) || 0} from={Number(p.rangeMin) || 0} to={Number(p.rangeMax) || 255} width={90} height={24} onValueModified={set} data-e2e={'anim-prop-' + p.name} />;
    return <LiveTextField value={v} onChange={set} tag={'anim-prop-' + p.name} />;
  };
  return (
    <CustomPopupDialog open={open} title="Add algorithm preset" width={460} standardButtons={['Cancel', 'Add']} onClose={onClose}
      onClicked={(b) => { if (b !== 'Add') { onClose(); return; } onAdd({ presetType: 'algorithm', algorithmName: name, algorithmProperties: values }).then(onClose).catch(e => setErr((e && e.message) || 'failed')); }}>
      <div data-e2e="anim-algo-dialog" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <RobotoText label="Algorithm" fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 90px' }} />
          <CustomComboBox width="100%" height={24} currValue={name} onValueChanged={setName} model={(scripts || []).map(s => ({ mLabel: s, mValue: s }))} data-e2e="anim-algo-name" />
        </div>
        {props.length ? <RobotoText label="Parameters" fontSize="var(--text-size-small)" fontBold height="auto" /> : null}
        <div style={{ maxHeight: 260, overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 4 }}>
          {props.map(p => (
            <div key={p.name} style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <RobotoText label={p.displayName || p.name} fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 45%' }} wrapText />
              <div style={{ flex: 1 }}>{editor(p)}</div>
            </div>
          ))}
        </div>
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" wrapText /> : null}
      </div>
    </CustomPopupDialog>
  );
}

function VCAnimationProps({ w, cfg, setConfig, functions, CheckRow, PropRow }) {
  const vc = useVC();
  const section = useLiveSections([]);
  const structural = useStructural(vc, w);
  const mask = Array.isArray(cfg.visibilityMask) ? cfg.visibilityMask : [];
  const presets = Array.isArray(cfg.presets) ? cfg.presets : [];
  const current = functions.find(f => String(f.id) === String(cfg.functionID));
  const [slot, setSlot] = React.useState(0);
  const [color, setColor] = React.useState('#ff0000');
  const [text, setText] = React.useState('');
  const [sel, setSel] = React.useState(-1);
  const [algoDialog, setAlgoDialog] = React.useState(false);
  React.useEffect(() => { if (sel >= 0 && !presets.some(p => Number(p.presetId) === sel)) setSel(-1); }, [presets]);
  const toggleMask = (flag, on) => setConfig({ visibilityMask: on ? mask.concat([flag]) : mask.filter(x => x !== flag) });
  const addPreset = (preset) => structural(LIVE_METHODS.PRESET_ADD, { preset }).then(r => { if (r && r.presetId != null) setSel(Number(r.presetId)); return r; });
  const removePreset = () => { if (sel >= 0) structural(LIVE_METHODS.PRESET_REMOVE, { presetId: sel }).then(() => setSel(-1)).catch(() => {}); };
  const movePreset = (dir) => { if (sel >= 0) structural('vc.animation.preset.move', { presetId: sel, direction: dir }).then(r => { if (r && r.presetId != null) setSel(Number(r.presetId)); }).catch(() => {}); };
  const label = (p) => p.presetType === 'algorithm' ? p.algorithmName : p.presetType === 'text' ? 'Text: ' + p.text : p.presetType === 'colorKnobs' ? 'Color ' + (Number(p.colorIndex) + 1) + ' knob (' + (p.knobChannel || '') + ')' : p.presetType === 'colorReset' ? 'Color ' + (Number(p.colorIndex) + 1) + ' reset' : 'Color ' + (Number(p.colorIndex) + 1);
  return (
    <>
      {section('animFunction', 'Attached Function', (
        <div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '2px 6px' }}>
            <RobotoText label={current ? current.name + ' (' + current.type + ')' : 'No RGB Matrix attached'} fontSize="var(--text-size-small)" height="auto" wrapText style={{ flex: 1 }} labelColor={current ? 'var(--fg-main)' : 'var(--fg-medium)'} data-e2e="anim-function" />
            <IconButton faSource="fa_xmark" size={24} tooltip="Detach the current function" disabled={!current} onClick={() => setConfig({ functionID: NO_FUNCTION_ID })} data-e2e="anim-detach" />
          </div>
          <LiveFunctionSearch functions={functions} types={['RGBMatrix']} onPick={(f) => setConfig({ functionID: String(f.id) })} placeholder="Search RGB Matrix functions" tag="anim-fn-search" />
          <CheckRow label="Apply color and preset changes immediately" checked={!!cfg.instantChanges} onToggle={(b) => setConfig({ instantChanges: b })} />
        </div>
      ))}
      {section('animAppearance', 'Appearance', (
        <div>{ANIM_VISIBILITY.map(([flag, l]) => <CheckRow key={flag} label={l} checked={mask.indexOf(flag) !== -1} onToggle={(b) => toggleMask(flag, b)} />)}</div>
      ))}
      {section('animPresets', 'Presets', (
        <div>
          <PropRow label="Color slot">
            <CustomComboBox width="100%" height={24} currValue={slot} onValueChanged={(v) => setSlot(Number(v))} model={[0, 1, 2, 3, 4].map(i => ({ mLabel: 'Color ' + (i + 1), mValue: i }))} data-e2e="anim-slot" />
          </PropRow>
          <div style={{ display: 'flex', alignItems: 'center', gap: 3, padding: '2px 6px', background: 'var(--bg-medium)', flexWrap: 'wrap' }}>
            <label title="Add a fixed-color preset for the selected color slot" style={{ display: 'inline-flex', alignItems: 'center', gap: 2 }}>
              <input type="color" value={color} onChange={(e) => setColor(e.target.value)} data-e2e="anim-color-pick" style={{ width: 28, height: 24, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)', cursor: 'pointer' }} />
              <IconButton imgSource={window.QLCData.icon('palette')} size={24} tooltip="Add a fixed-color preset for the selected color slot" onClick={() => addPreset({ presetType: 'color', colorIndex: slot, color }).catch(() => {})} data-e2e="anim-add-color" />
            </label>
            <IconButton imgSource={window.QLCData.icon('sliders')} size={24} tooltip="Add R/G/B knobs for the selected color slot" onClick={() => addPreset({ presetType: 'colorKnobs', colorIndex: slot }).catch(() => {})} data-e2e="anim-add-knobs" />
            <GenericButton label="Algorithm…" width={80} height={24} fontSize="var(--text-size-menubar)" onClick={() => setAlgoDialog(true)} data-e2e="anim-add-algo" />
            <LiveTextField value={text} onChange={setText} placeholder="Text preset" tag="anim-text" width={80} />
            <GenericButton label="Add text" width={60} height={24} fontSize="var(--text-size-menubar)" disabled={!text.trim()} onClick={() => addPreset({ presetType: 'text', text: text.trim() }).then(() => setText('')).catch(() => {})} data-e2e="anim-add-text" />
            <span style={{ flex: 1 }} />
            {toolButton('fa_trash_can', 'Remove selected preset', removePreset, sel < 0, 'anim-preset-remove', 'crimson')}
            {toolButton('fa_chevron_down', 'Move selected preset down', () => movePreset('down'), sel < 0, 'anim-preset-down')}
            {toolButton('fa_chevron_up', 'Move selected preset up', () => movePreset('up'), sel < 0, 'anim-preset-up')}
          </div>
          <div style={{ maxHeight: 220, overflow: 'auto' }} data-e2e="anim-preset-list">
            {presets.length ? presets.map(p => (
              <div key={p.presetId} role="button" data-e2e-preset-row={p.presetId} onClick={() => setSel(Number(p.presetId))}
                style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', background: Number(p.presetId) === sel ? 'var(--highlight)' : 'transparent' }}>
                {p.colorIndex != null && p.colorIndex >= 0 ? <span style={{ width: 14, height: 14, borderRadius: 3, border: '1px solid var(--bg-light)', background: p.color || p.knobColor || 'transparent', flex: 'none' }} /> : <span style={{ width: 14 }} />}
                <RobotoText label={label(p)} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                <RobotoText label={p.isKnob ? 'Knob' : 'Button'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
              </div>
            )) : <RobotoText label="No presets" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
          </div>
          <AnimationAlgorithmDialog open={algoDialog} onClose={() => setAlgoDialog(false)} onAdd={addPreset} />
        </div>
      ))}
    </>
  );
}

/* ================================================================ Audio triggers body */
function VCAudioTriggersBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const bars = Math.max(1, Number(cfg.barsNumber) || (Array.isArray(cfg.bars) ? cfg.bars.length : 1));
  const [capture, setCapture] = React.useState(!!w.captureEnabled);
  const [levels, setLevels] = React.useState(Array.isArray(w.levels) ? w.levels : []);
  const [volume, setVolume] = React.useState(Number(cfg.volumeLevel) || 0);
  React.useEffect(() => { if (w.captureEnabled != null) setCapture(!!w.captureEnabled); }, [w.captureEnabled]);
  React.useEffect(() => { if (cfg.volumeLevel != null) setVolume(Number(cfg.volumeLevel)); }, [cfg.volumeLevel]);
  useGatedTopic(vc, 'vc.audioTriggers.levelsChanged', true);
  useWidgetEvent(vc, 'vc.audioTriggers.captureEnabledChanged', w.id, (d) => setCapture(!!d.enabled));
  useWidgetEvent(vc, 'vc.audioTriggers.levelsChanged', w.id, (d) => setLevels(Array.isArray(d.levels) ? d.levels : []));
  const structural = useStructural(vc, w);
  const stop = (e) => e.stopPropagation();
  const toggle = (e) => { stop(e); if (vc.edit) return; setCapture(!capture); vc.qlc.call(LIVE_METHODS.CAPTURE, { widgetId: String(w.id), enabled: !capture }).catch(liveMethodError(vc, LIVE_METHODS.CAPTURE)); };
  const g = w.geometry || { width: 200, height: 200 };
  return (
    <div data-e2e="audio-body" data-e2e-capture={capture ? 'on' : 'off'} style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', gap: 4, padding: 4, overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      <div style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'stretch' }} data-e2e="audio-bars">
        {Array.from({ length: bars }, (_, i) => (
          <div key={i} style={{ flex: 1, position: 'relative', background: 'var(--bg-strong)', border: '1px solid var(--bg-light)' }} title={i === 0 ? 'Volume' : 'Band ' + i}>
            <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: vcClamp(Number(levels[i]) || 0, 0, 255) / 255 * 100 + '%', background: i === 0 ? '#00FF00' : 'var(--selection)', borderRadius: 3 }} />
          </div>
        ))}
      </div>
      <div style={{ flex: 'none', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }} onPointerDown={stop}>
        <IconButton faSource="fa_check" faColor="lime" size={26} checked={capture} tooltip="Enable/Disable the audio capture" disabled={vc.edit} onClick={toggle} data-e2e="audio-capture" />
        <VCFader value={volume} from={0} to={100} height={Math.max(30, g.height - 50)} width={34} disabled={vc.edit || w.isDisabled} onMoved={setVolume}
          onPressChange={(on) => { if (!on && volume !== Number(cfg.volumeLevel)) structural('vc.widget.setConfig', { config: { volumeLevel: Math.round(volume) } }).catch(() => {}); }} />
      </div>
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ================================================================ Audio triggers properties */
function VCAudioTriggersProps({ w, cfg, setConfig, functions, PropRow }) {
  const vc = useVC();
  const section = useLiveSections([]);
  const structural = useStructural(vc, w);
  const bars = Array.isArray(cfg.bars) ? cfg.bars : [];
  const [editing, setEditing] = React.useState(-1);
  const Picker = window.VCLevelChannelsPicker;
  const set = (index, patch) => structural('vc.audioTriggers.setBarConfig', Object.assign({ index }, patch)).catch(() => {});
  const pct = (v) => Math.round((Number(v) || 0) * 100 / 255);
  const from255 = (p) => Math.round(vcClamp(p, 0, 100) * 255 / 100);
  const widgets = vc.widgets.filter(x => String(x.id) !== String(w.id) && ['Button', 'Slider', 'Speed', 'CueList'].indexOf(x.widgetType) !== -1);
  const fnName = (id) => { const f = functions.find(x => String(x.id) === String(id)); return f ? f.name : (id ? 'Function #' + id : ''); };
  return (
    <>
      {section('audioBars', 'Spectrum Bars', (
        <div>
          <PropRow label="Number of bars"><LiveSpin value={Math.max(0, (Number(cfg.barsNumber) || 1) - 1)} from={1} to={32} width={70} onCommit={(v) => setConfig({ barsNumber: v + 1 })} tag="audio-bars-number" /></PropRow>
          <PropRow label="Volume"><LiveSpin value={Number(cfg.volumeLevel) || 0} from={0} to={100} suffix="%" width={70} onCommit={(v) => setConfig({ volumeLevel: v })} tag="audio-volume" /></PropRow>
          <div style={{ display: 'flex', fontSize: 'var(--text-size-menubar)', color: 'var(--section-header)', padding: '0 6px', gap: 4, fontWeight: 700 }}>
            <span style={{ flex: 1 }}>Name</span><span style={{ width: 92 }}>Type</span><span style={{ width: 24 }} />
          </div>
          <div data-e2e="audio-bar-list">
            {bars.map(b => (
              <div key={b.index} data-e2e-bar={b.index} style={{ borderBottom: 'var(--border-dark)', padding: '2px 0' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 4, height: 'var(--list-item-height)', padding: '0 6px' }}>
                  <RobotoText label={b.label || (b.index === 0 ? 'Volume Bar' : '#' + b.index)} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1, minWidth: 0 }} />
                  <CustomComboBox width={92} height={22} currValue={b.type || 'None'} onValueChanged={(v) => set(b.index, { type: v })} model={BAR_TYPES} data-e2e={'audio-bar-type-' + b.index} />
                  <IconButton faSource="fa_gear" size={22} tooltip="Edit this bar" disabled={(b.type || 'None') === 'None'} checked={editing === b.index} onClick={() => setEditing(editing === b.index ? -1 : b.index)} data-e2e={'audio-bar-edit-' + b.index} />
                </div>
                {(b.type || 'None') !== 'None' ? (
                  <div style={{ padding: '0 6px 2px 12px', fontSize: 'var(--text-size-menubar)', color: 'var(--fg-light)' }} data-e2e={'audio-bar-info-' + b.index}>
                    {b.type === 'DMXBar' ? ((b.dmxChannels || []).length + ' channels') : ('Thresholds: ' + pct(b.minThreshold) + '% - ' + pct(b.maxThreshold) + '%')}
                    {b.type === 'FunctionBar' ? ' · ' + (b.functionName || fnName(b.functionId) || 'no function') : ''}
                    {b.type === 'VCWidgetBar' ? ' · ' + (b.triggeredWidgetCaption || (b.triggeredWidgetId && b.triggeredWidgetId !== NO_FUNCTION_ID ? 'Widget #' + b.triggeredWidgetId : 'no widget')) : ''}
                  </div>
                ) : null}
                {editing === b.index && (b.type || 'None') !== 'None' ? (
                  <div style={{ padding: '2px 0 4px' }} data-e2e={'audio-bar-editor-' + b.index}>
                    {b.type !== 'DMXBar' ? (
                      <>
                        <PropRow label="Activation"><LiveSpin value={pct(b.maxThreshold)} from={5} to={95} suffix="%" width={70} onCommit={(v) => set(b.index, { maxThreshold: from255(v) })} tag={'audio-max-' + b.index} /></PropRow>
                        <PropRow label="Deactivation"><LiveSpin value={pct(b.minThreshold)} from={5} to={95} suffix="%" width={70} onCommit={(v) => set(b.index, { minThreshold: from255(v) })} tag={'audio-min-' + b.index} /></PropRow>
                      </>
                    ) : null}
                    {b.type === 'FunctionBar' ? <LiveFunctionSearch functions={functions} onPick={(f) => set(b.index, { functionId: String(f.id) })} tag={'audio-fn-' + b.index} /> : null}
                    {b.type === 'VCWidgetBar' ? (
                      <PropRow label="Widget">
                        <CustomComboBox width="100%" height={24} currValue={String(b.triggeredWidgetId || '')} onValueChanged={(v) => set(b.index, { triggeredWidgetId: String(v) })} data-e2e={'audio-widget-' + b.index}
                          model={[{ mLabel: 'Pick a widget on this page…', mValue: String(b.triggeredWidgetId || '') }].concat(widgets.map(x => ({ mLabel: ((x.style && x.style.caption) || x.widgetType) + ' #' + x.id, mValue: String(x.id) })))} />
                      </PropRow>
                    ) : null}
                    {b.type === 'DMXBar' ? (Picker ? <Picker channels={(b.dmxChannels || []).map(c => ({ fixtureId: String(c.fixtureId), channel: Number(c.channel) }))} onCommit={(list) => set(b.index, { dmxChannels: list.map(c => ({ fixtureId: String(c.fixtureId), channel: Number(c.channel) })) })} />
                      : <RobotoText label="Channel picker unavailable (vc-edit.jsx did not export it)" fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" />) : null}
                  </div>
                ) : null}
              </div>
            ))}
          </div>
          <RobotoText label="Capture runs on the machine that hosts QLC+; the bars show what it hears." fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} />
        </div>
      ))}
    </>
  );
}

/* ================================================================ registrations */
window.QLCVCBodies = Object.assign(window.QLCVCBodies || {}, { XYPad: VCXYPadBodyEx, Clock: VCClockBody, Animation: VCAnimationBody, AudioTriggers: VCAudioTriggersBody });
window.QLCVCProperties = Object.assign(window.QLCVCProperties || {}, { XYPad: VCXYPadProps, Clock: VCClockProps, Animation: VCAnimationProps, AudioTriggers: VCAudioTriggersProps });
Object.assign(window, { VCXYPadBodyEx, VCClockBody, VCAnimationBody, VCAudioTriggersBody, vcMsToClockString: msToClockString });
