/**
 * Virtual Console — shared pieces used by vc-widgets.jsx, vc-edit.jsx and VirtualConsole.jsx.
 *
 * Everything the live widgets need from the screen (connection, live value store, edit state,
 * zoom scale) travels through VCContext so a Button nested three frames deep does not need a
 * prop chain. The value store itself lives in VirtualConsole.jsx (useVCLiveStore).
 */
const { RobotoText, FaIcon, GenericButton } = window.PatchDesignSystem_5432c9;

const VCContext = React.createContext(null);
function useVC() { return React.useContext(VCContext); }

/* UISettings.qml screenPixelDensity at 96 dpi; VirtualConsole::snappingSize() = pixelDensity * 3. */
const VC_PIXEL_DENSITY = 3.7795;
const VC_SNAP = VC_PIXEL_DENSITY * 3;

const VC_METHODS = {
  PRESS: 'vc.button.press',
  SET_VALUE: 'vc.slider.setValue',
  CUE_GET: 'vc.cueList.get',
  CUE_PLAY: 'vc.cueList.play',
  XY_SET: 'vc.xyPad.setPosition',
  SPEED_SET: 'vc.speedDial.setValue',
  SPEED_TAP: 'vc.speedDial.tap',
  FRAME_GOTO: 'vc.frame.gotoPage',
  FRAME_GET: 'vc.frame.get'
};

/* WidgetsList.qml — the palette, in its order. `create` is what vc.widget.create gets as widgetType;
   Knob is a Slider with widgetStyle Knob (the engine has no separate knob widget). */
const VC_PALETTE = [
  { name: 'Frame', create: 'Frame', icon: 'frame', size: { width: 300, height: 200 } },
  { name: 'Solo Frame', create: 'SoloFrame', icon: 'soloframe', size: { width: 300, height: 200 } },
  { name: 'Button', create: 'Button', icon: 'button', size: { width: 64, height: 64 } },
  { name: 'Slider', create: 'Slider', icon: 'slider', size: { width: 57, height: 151 } },
  { name: 'Knob', create: 'Slider', icon: 'knob', size: { width: 64, height: 94 }, typeConfig: { widgetStyle: 'Knob' } },
  { name: 'Cue List', create: 'CueList', icon: 'cuelist', size: { width: 300, height: 200 } },
  { name: 'Speed', create: 'Speed', icon: 'speed', size: { width: 200, height: 175 } },
  { name: 'XY Pad', create: 'XYPad', icon: 'xypad', size: { width: 230, height: 230 } },
  { name: 'Animation', create: 'Animation', icon: 'animation', size: { width: 200, height: 200 } },
  { name: 'Label', create: 'Label', icon: 'label', size: { width: 100, height: 30 } },
  { name: 'Audio Triggers', create: 'AudioTriggers', icon: 'audiotriggers', size: { width: 200, height: 200 } },
  { name: 'Clock', create: 'Clock', icon: 'clock', size: { width: 150, height: 60 } }
];

const VC_WIDGET_ICONS = { Button: 'button', Slider: 'slider', Frame: 'frame', SoloFrame: 'soloframe', Label: 'label', CueList: 'cuelist',
  XYPad: 'xypad', Speed: 'speed', SpeedDial: 'speed', Clock: 'clock', Animation: 'animation', AudioTriggers: 'audiotriggers' };

/* Font Awesome 7 Solid codepoints missing from the bundle's FA map (FaIcon renders a raw glyph). */
const VC_GLYPH = { stop: '', previous: '', next: '', copy: '', paste: '', scissors: '', minus: '', plus: '' };

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function snapTo(v, on) { return on ? Math.round(v / VC_SNAP) * VC_SNAP : v; }
function roundGeom(g) { return { x: Math.round(g.x * 100) / 100, y: Math.round(g.y * 100) / 100, width: Math.round(g.width * 100) / 100, height: Math.round(g.height * 100) / 100 }; }

/** CSS for a VcWidgetStyle font: pointSize -> px at 96 dpi (1pt = 1.333px). */
function vcFontCss(style) {
  const f = (style && style.font) || {};
  return { fontFamily: f.family ? '"' + f.family + '", var(--font-roboto)' : 'var(--font-roboto)', fontSize: Math.round((f.pointSize || 12) * 1.333),
    fontWeight: f.bold ? 700 : 400, fontStyle: f.italic ? 'italic' : 'normal', textDecoration: f.underline ? 'underline' : 'none' };
}

/** TimeUtils::timeToQlcString for milliseconds: "1.5s" style seconds for < 60s, "m:ss.z" above. */
function vcMsToString(ms) {
  ms = Math.max(0, Math.round(Number(ms) || 0));
  if (ms < 60000) return (ms % 1000 ? (ms / 1000).toFixed(2).replace(/0+$/, '').replace(/\.$/, '') : String(ms / 1000)) + 's';
  const m = Math.floor(ms / 60000), s = Math.floor((ms % 60000) / 1000), z = Math.floor((ms % 1000) / 100);
  return m + ':' + String(s).padStart(2, '0') + (z ? '.' + z : '');
}

/**
 * Structural (§4a) call: injects the current docRevision as baseRevision and, on a CONFLICT
 * (someone else changed the document since), retries once with the revision the server handed
 * back in error.details — the transport already learned it by the time the promise rejects.
 */
function vcStructural(qlc, method, params) {
  const attempt = () => qlc.call(method, Object.assign({}, params, { baseRevision: qlc.docRevision() }));
  /* Up to three tries: two edits fired back to back (a text field committing while a checkbox is
     clicked) both conflict on the same revision, and the second retry must still find a fresh one. */
  const retry = (left) => attempt().catch(e => { if (e && e.code === 'CONFLICT' && left > 0) return retry(left - 1); throw e; });
  return retry(2);
}

/**
 * Per-key trailing-edge throttle for continuous gestures (slider drag, XY pad drag): at most one
 * send per key every `interval` ms, the last value always wins. Returns a stable sender.
 */
function useThrottledSender(interval) {
  const timers = React.useRef({});
  React.useEffect(() => () => { Object.values(timers.current).forEach(t => clearTimeout(t.handle)); }, []);
  return React.useCallback((key, fn) => {
    const ms = interval || 33;
    /* A cool-down window per key; a trailing send re-arms it so two sends are never closer than `ms`. */
    const arm = () => {
      timers.current[key] = { fn: null, handle: setTimeout(() => {
        const last = timers.current[key];
        delete timers.current[key];
        if (last && last.fn) { last.fn(); arm(); }
      }, ms) };
    };
    const t = timers.current[key];
    if (t) { t.fn = fn; return; }
    fn();
    arm();
  }, [interval]);
}

/**
 * The VC fader (QLCPlusFader.qml look: 5px track, grey fill above the handle, metal handle),
 * on pointer events so a finger works as well as a mouse. `inverted` puts 0 at the top
 * (VCSlider invertedAppearance). Calls onPressChange(true/false) around a gesture so the owner
 * can hold off incoming remote values while the operator has the handle.
 */
function VCFader({ value = 0, from = 0, to = 255, onMoved, onPressChange, width = 32, height = 100, trackColor = 'var(--fader-track)', inverted = false, disabled = false, style }) {
  const ref = React.useRef(null);
  const [press, setPress] = React.useState(false);
  const span = (to - from) || 1;
  const pos = clamp((value - from) / span, 0, 1);
  const handleTop = inverted ? pos : 1 - pos;
  const set = (clientY) => {
    const el = ref.current;
    if (!el || !onMoved) return;
    const r = el.getBoundingClientRect();
    let frac = clamp((clientY - r.top) / (r.height || 1), 0, 1);
    if (!inverted) frac = 1 - frac;
    onMoved(Math.round(from + frac * span));
  };
  const down = (e) => {
    if (disabled) return;
    e.preventDefault(); e.stopPropagation();
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
    setPress(true);
    if (onPressChange) onPressChange(true);
    set(e.clientY);
  };
  const move = (e) => { if (press) { e.preventDefault(); set(e.clientY); } };
  const up = (e) => { if (!press) return; setPress(false); if (onPressChange) onPressChange(false); };
  return (
    <div ref={ref} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} data-vc-fader=""
      style={Object.assign({ position: 'relative', width, height, flex: 'none', touchAction: 'none', cursor: disabled ? 'default' : 'ns-resize', opacity: disabled ? .5 : 1 }, style)}>
      <div style={{ position: 'absolute', left: '50%', transform: 'translateX(-50%)', top: 0, bottom: 0, width: 5, background: trackColor, borderRadius: 'var(--radius-fader)' }}>
        <div style={{ position: 'absolute', left: 0, right: 0, background: 'var(--fader-fill)', borderRadius: 'var(--radius-fader)',
          top: inverted ? 'auto' : 0, bottom: inverted ? 0 : 'auto', height: (1 - pos) * 100 + '%' }} />
      </div>
      <div style={{ position: 'absolute', left: '50%', width: 'min(var(--icon-size-default), 100%)', height: 'calc(var(--icon-size-default) * 0.75)',
        top: 'calc(' + handleTop * 100 + '% - (var(--icon-size-default) * 0.375))', transform: 'translateX(-50%)',
        background: press ? 'var(--gradient-fader-handle-hover)' : 'var(--gradient-fader-handle)', border: '1px solid var(--fader-handle-border)', borderRadius: 'var(--radius-handle)' }} />
    </div>
  );
}

/**
 * QLCPlusKnob.qml stand-in: a 270-degree arc (SVG, no gradient) with the filled part in the fader
 * track colour; a vertical pointer drag changes the value (one full travel per 200px).
 */
function VCKnob({ value = 0, from = 0, to = 255, onMoved, onPressChange, size = 60, disabled = false, style }) {
  const [press, setPress] = React.useState(false);
  const start = React.useRef({ y: 0, v: 0 });
  const span = (to - from) || 1;
  const pos = clamp((value - from) / span, 0, 1);
  const r = size / 2 - 5, c = size / 2, circ = 2 * Math.PI * r, arc = circ * 0.75;
  const down = (e) => {
    if (disabled) return;
    e.preventDefault(); e.stopPropagation();
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
    start.current = { y: e.clientY, v: value };
    setPress(true);
    if (onPressChange) onPressChange(true);
  };
  const move = (e) => {
    if (!press || !onMoved) return;
    e.preventDefault();
    onMoved(Math.round(clamp(start.current.v + (start.current.y - e.clientY) / 200 * span, from, to)));
  };
  const up = () => { if (!press) return; setPress(false); if (onPressChange) onPressChange(false); };
  const angle = -135 + pos * 270;
  return (
    <svg width={size} height={size} viewBox={'0 0 ' + size + ' ' + size} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} data-vc-knob=""
      style={Object.assign({ flex: 'none', touchAction: 'none', cursor: disabled ? 'default' : 'ns-resize', opacity: disabled ? .5 : 1 }, style)}>
      <circle cx={c} cy={c} r={r} fill="var(--bg-control)" stroke="var(--bg-stronger)" strokeWidth={5}
        strokeDasharray={arc + ' ' + circ} transform={'rotate(135 ' + c + ' ' + c + ')'} />
      <circle cx={c} cy={c} r={r} fill="none" stroke={press ? 'var(--selection)' : 'var(--fader-track)'} strokeWidth={5}
        strokeDasharray={(arc * pos) + ' ' + circ} transform={'rotate(135 ' + c + ' ' + c + ')'} />
      <line x1={c} y1={c} x2={c} y2={c - r + 8} stroke="var(--fg-main)" strokeWidth={3} strokeLinecap="round" transform={'rotate(' + angle + ' ' + c + ' ' + c + ')'} />
    </svg>
  );
}

/** One-line yellow notice strip with a Dismiss button (server has no X, value rejected, ...). */
function VCNotice({ text, onDismiss }) {
  if (!text) return null;
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '4px 10px', background: 'var(--bg-strong)', borderBottom: '2px solid var(--selection)', flex: 'none' }}>
      <RobotoText label={text} fontSize="var(--text-size-small)" labelColor="var(--selection)" wrapText height="auto" style={{ flex: 1 }} />
      <GenericButton label="Dismiss" width={80} height={22} fontSize="var(--text-size-menubar)" onClick={onDismiss} />
    </div>
  );
}

Object.assign(window, { VCContext, useVC, VC_PIXEL_DENSITY, VC_SNAP, VC_METHODS, VC_PALETTE, VC_WIDGET_ICONS, VC_GLYPH,
  vcClamp: clamp, vcSnapTo: snapTo, vcRoundGeom: roundGeom, vcFontCss, vcMsToString, vcStructural, useThrottledSender, VCFader, VCKnob, VCNotice });
