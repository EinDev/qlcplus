/**
 * Virtual Console — Cue List and Speed Dial: the live bodies with their extras (side fader,
 * playback layouts; factor buttons, tap reset, presets) and the type-specific property panels.
 *
 * Registers into the extension points of vc-widgets.jsx / vc-edit.jsx instead of editing them:
 *   window.QLCVCBodies.CueList / .Speed / .SpeedDial      (props { w, header, children })
 *   window.QLCVCProperties.CueList / .Speed / .SpeedDial  (props from VCWidgetProperties)
 *
 * Mirrors qmlui/qml/virtualconsole/VCCueListItem.qml, VCCueListProperties.qml,
 * VCSpeedDialItem.qml, VCSpeedDialProperties.qml and VCSpeedDialPresets.qml. Live state that the
 * shared store does not carry (side fader level / crossfade step labels, multiplier factor, tap
 * interval, preset list) is seeded from the widget snapshot's additive fields
 * (sideFaderLevel/nextStepIndex/primaryTop, factor/tapTimeValue, typeConfig.presets) and followed
 * through vc.cueList.sideFaderChanged / vc.speedDial.factorChanged / tapChanged / presetsChanged.
 */
const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomCheckBox, CustomComboBox, CustomTextInput } = window.PatchDesignSystem_5432c9;

const VC_CUE_METHODS = {
  SIDE_FADER: 'vc.cueList.setSideFaderLevel',
  FACTOR: 'vc.speedDial.setFactor',
  APPLY: 'vc.speedDial.apply',
  RESET_TAP: 'vc.speedDial.resetTap',
  PRESET_APPLY: 'vc.widget.preset.apply',
  PRESET_ADD: 'vc.widget.preset.add',
  PRESET_REMOVE: 'vc.widget.preset.remove',
  PRESET_UPDATE: 'vc.speedDial.preset.update'
};
const NO_FUNCTION = '4294967295';

/* VcSpeedDialMultiplier, in VCSpeedDial::SpeedMultiplier order; the dial's own factor only uses 1/16..16. */
const SPEED_FACTORS = ['None', 'Zero', 'OneSixteenth', 'OneEighth', 'OneFourth', 'Half', 'One', 'Two', 'Four', 'Eight', 'Sixteen'];
const SPEED_FACTOR_LABEL = { None: '(Not sent)', Zero: '0', OneSixteenth: '1/16', OneEighth: '1/8', OneFourth: '1/4', Half: '1/2', One: '1', Two: '2', Four: '4', Eight: '8', Sixteen: '16' };
const SPEED_FACTOR_MODEL = SPEED_FACTORS.map(f => ({ mLabel: SPEED_FACTOR_LABEL[f], mValue: f }));
const SPEED_DIAL_FACTORS = SPEED_FACTORS.slice(2);
/* VCSpeedDialProperties.qml's Appearance checkboxes (PlusMinus is commented out there, XPad never shown). */
const SPEED_VISIBILITY = [['Dial', 'Dial'], ['Tap', 'Tap'], ['Multipliers', 'Multipliers'], ['Apply', 'Apply'], ['Hours', 'Hours'], ['Minutes', 'Minutes'], ['Seconds', 'Seconds'], ['Milliseconds', 'Milliseconds'], ['Beats', 'Beats']];
const SPEED_ACTIVE = 'green'; /* VCSpeedDialItem.qml activeColor */

const CUE_LIST_COLS = [['#', 34], ['Name', 0], ['Fade In', 62], ['Fade Out', 62], ['Hold', 62], ['Notes', 90]];

/** Subscribe to one vc.* event for one widget; `handler` gets the event data. */
function useVCWidgetEvent(vc, topic, widgetId, handler) {
  const ref = React.useRef(handler);
  ref.current = handler;
  React.useEffect(() => vc.qlc.subscribeTo(topic, (d) => { if (d && String(d.widgetId) === String(widgetId)) ref.current(d); }), [vc.qlc, topic, widgetId, vc.qlc.online]);
}

/** The speed dial's preset list: seeded from typeConfig.presets, kept current by vc.speedDial.presetsChanged. */
function useSpeedDialPresets(vc, w) {
  const seed = (w.typeConfig && w.typeConfig.presets) || [];
  const key = JSON.stringify(seed);
  const [presets, setPresets] = React.useState(seed);
  React.useEffect(() => { setPresets(seed); }, [key]);
  useVCWidgetEvent(vc, 'vc.speedDial.presetsChanged', w.id, (d) => setPresets(d.presets || []));
  return presets;
}

function vcCueMask(cfg) { return Array.isArray(cfg.visibilityMask) ? cfg.visibilityMask : []; }

function vcCueMethodError(vc, method) {
  return (e) => {
    if (!e || e.code === 'NOT_CONNECTED') return;
    if (e.code === 'NOT_FOUND' && /^Unknown method/.test(e.message || '')) vc.notice('This server has no ' + method + ' yet');
    else vc.notice(method + ': ' + (e.message || e.code || 'failed'));
  };
}

/* ================================================================ Cue list body */
function VCCueListBodyEx({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const cl = vc.live.cueLists[w.id] || {};
  const steps = cl.steps;
  const idx = cl.playbackIndex != null ? Number(cl.playbackIndex) : -1;
  const running = !!cl.running, paused = !!cl.paused;
  const canGet = !vc.unsupported(VC_METHODS.CUE_GET);
  const canPlay = !vc.unsupported(VC_METHODS.CUE_PLAY);
  const canFade = !vc.unsupported(VC_CUE_METHODS.SIDE_FADER);
  const layoutPlayStopPause = cfg.playbackLayout === 'PlayStopPause';
  const mode = cfg.sideFaderMode || 'None';
  const faderMax = mode === 'Crossfade' ? 100 : 255;
  const suffix = mode === 'Crossfade' ? '%' : '';

  /* Side fader state: seeded from the snapshot, followed via sideFaderChanged (which also carries
     the crossfade bookkeeping) and playbackChanged; the operator's own drag wins while pressed. */
  const [sf, setSf] = React.useState({ level: w.sideFaderLevel != null ? Number(w.sideFaderLevel) : faderMax, next: w.nextStepIndex != null ? Number(w.nextStepIndex) : -1, primaryTop: w.primaryTop !== false });
  const pressing = React.useRef(false);
  React.useEffect(() => { setSf(s => ({ level: w.sideFaderLevel != null ? Number(w.sideFaderLevel) : s.level, next: w.nextStepIndex != null ? Number(w.nextStepIndex) : s.next, primaryTop: w.primaryTop != null ? w.primaryTop !== false : s.primaryTop })); }, [w.sideFaderLevel, w.nextStepIndex, w.primaryTop]);
  useVCWidgetEvent(vc, 'vc.cueList.sideFaderChanged', w.id, (d) => setSf(s => ({ level: pressing.current || d.level == null ? s.level : Number(d.level), next: d.nextStepIndex != null ? Number(d.nextStepIndex) : s.next, primaryTop: d.primaryTop != null ? !!d.primaryTop : s.primaryTop })));
  useVCWidgetEvent(vc, 'vc.cueList.playbackChanged', w.id, (d) => { if (d.nextStepIndex != null || d.primaryTop != null) setSf(s => ({ level: s.level, next: d.nextStepIndex != null ? Number(d.nextStepIndex) : s.next, primaryTop: d.primaryTop != null ? !!d.primaryTop : s.primaryTop })); });
  const throttled = useThrottledSender(33);
  const moveFader = (v) => {
    if (!canFade) { vc.notice('This server has no ' + VC_CUE_METHODS.SIDE_FADER + ' yet'); return; }
    const level = vcClamp(Math.round(v), 0, faderMax);
    setSf(s => Object.assign({}, s, { level }));
    throttled('sf:' + w.id, () => vc.qlc.call(VC_CUE_METHODS.SIDE_FADER, { widgetId: String(w.id), level }).catch(vcCueMethodError(vc, VC_CUE_METHODS.SIDE_FADER)));
  };
  React.useEffect(() => { vc.act.cueGet(w.id); }, [w.id, vc.qlc.online]);
  const stop = (e) => e.stopPropagation();
  const th = (label, width, i) => (
    <span key={i} style={{ flex: width ? 'none' : 1, width: width || 'auto', minWidth: 0, padding: '0 4px', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', textAlign: i === 0 ? 'right' : 'left' }}>{label}</span>
  );
  const nextIdx = mode === 'Crossfade' ? sf.next : -1;
  const row = (s, i) => {
    const infinite = s.hold == null || s.hold === -1 || Number(s.hold) >= 4294967295;
    const cells = [(s.index != null ? s.index : i) + 1, s.name || '', vcMsToString(s.fadeIn), vcMsToString(s.fadeOut), infinite ? '∞' : vcMsToString(s.hold), s.notes || ''];
    const stepIndex = s.index != null ? s.index : i;
    const isCurrent = stepIndex === idx, isNext = stepIndex === nextIdx && idx >= 0;
    /* ChaserWidget.qml: the current step in the highlight colour, the crossfade's next step in orange. */
    return (
      <div key={stepIndex} role="row" onClick={(e) => { stop(e); if (!vc.edit) vc.act.cueJump(w.id, stepIndex); }} title={'Jump to step ' + (stepIndex + 1)}
        style={{ display: 'flex', alignItems: 'center', height: 'var(--list-item-height)', flex: 'none', cursor: vc.edit ? 'default' : 'pointer', fontSize: 'var(--text-size-small)',
          background: isCurrent ? (paused ? 'var(--highlight-pressed)' : 'var(--highlight)') : isNext ? 'orange' : (stepIndex % 2 ? 'var(--bg-medium)' : 'transparent'), color: isNext && !isCurrent ? '#111' : 'var(--fg-main)' }}>
        {cells.map((c, j) => th(c, CUE_LIST_COLS[j][1], j))}
      </div>
    );
  };
  const total = steps ? steps.length : 0;
  /* VCCueListItem.qml transport colours per layout. */
  const playBg = layoutPlayStopPause ? (running ? 'var(--override-red)' : 'var(--bg-light)') : (running && !paused ? 'darkorange' : paused ? 'green' : 'var(--bg-light)');
  const stopBg = layoutPlayStopPause ? (paused ? 'darkorange' : 'var(--bg-light)') : (running ? 'var(--override-red)' : 'var(--bg-light)');
  const playGlyph = layoutPlayStopPause ? (running ? VC_GLYPH.stop : 'fa_play') : (running && !paused ? 'fa_pause' : 'fa_play');
  const stopGlyph = layoutPlayStopPause ? 'fa_pause' : VC_GLYPH.stop;
  const transport = (glyph, tip, bg, disabled, fn, tag) => (
    <span data-e2e={tag} style={{ display: 'inline-flex', background: bg, borderRadius: 3 }}>
      <IconButton faSource={glyph} size={30} tooltip={tip} disabled={disabled || vc.edit} onClick={(e) => { stop(e); fn(); }} onPointerDown={stop} />
    </span>
  );
  /* Side fader labels (VCCueListItem.qml updateLabels()). */
  let topLabel = '', bottomLabel = '', topColor = 'transparent', bottomColor = 'transparent';
  if (idx >= 0) {
    if (mode === 'Steps') { bottomLabel = '#' + (idx + 1); bottomColor = 'var(--highlight)'; }
    else if (mode === 'Crossfade') {
      topColor = sf.primaryTop ? 'var(--highlight)' : 'orange'; topLabel = '#' + ((sf.primaryTop ? idx : sf.next) + 1);
      bottomColor = sf.primaryTop ? 'orange' : 'var(--highlight)'; bottomLabel = '#' + ((sf.primaryTop ? sf.next : idx) + 1);
    }
  }
  const labelBox = (label, color, hidden) => (
    <div style={{ width: 26, height: 20, flex: 'none', border: '1px solid var(--fg-main)', background: color, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 11, visibility: hidden ? 'hidden' : 'visible', color: color === 'orange' ? '#111' : 'var(--fg-main)' }}>{label}</div>
  );
  return (
    <div style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-stronger)', border: '2px solid var(--border-color-dark)', display: 'flex', overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      {mode !== 'None' ? (
        <div data-e2e="cue-side-fader" data-mode={mode} data-level={sf.level} style={{ width: 44, flex: 'none', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3, padding: '3px 0', background: 'var(--bg-strong)', borderRight: 'var(--border-dark)' }} onPointerDown={stop}>
          <span style={{ fontSize: 12, fontFamily: 'var(--font-mono)', height: 16, lineHeight: '16px' }}>{sf.level + suffix}</span>
          {labelBox(topLabel, topColor, mode !== 'Crossfade')}
          <div style={{ flex: 1, minHeight: 30, display: 'flex', alignItems: 'stretch', width: '100%', justifyContent: 'center' }}>
            <VCFader value={sf.level} from={0} to={faderMax} width={36} height="100%" disabled={vc.edit} onMoved={moveFader}
              onPressChange={(on) => { pressing.current = on; }} style={{ height: '100%' }} />
          </div>
          {labelBox(bottomLabel, bottomColor, false)}
          <span style={{ fontSize: 12, fontFamily: 'var(--font-mono)', height: 16, lineHeight: '16px', visibility: mode === 'Crossfade' ? 'visible' : 'hidden' }}>{(faderMax - sf.level) + suffix}</span>
        </div>
      ) : null}
      <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
        <div style={{ display: 'flex', alignItems: 'center', height: 'var(--list-item-height)', flex: 'none', background: 'var(--section-header)', fontSize: 'var(--text-size-small)', fontWeight: 700 }}>
          {CUE_LIST_COLS.map((c, i) => th(c[0], c[1], i))}
        </div>
        <div style={{ flex: 1, minHeight: 0, overflow: 'auto' }} data-e2e="cue-steps">
          {steps == null
            ? <RobotoText label={canGet ? 'Loading steps…' : 'Steps not available on this server'} fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />
            : steps.length ? steps.map(row) : <RobotoText label={cfg.chaserID && cfg.chaserID !== NO_FUNCTION ? 'No steps' : 'No Chaser attached'} fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: 3, flex: 'none', background: 'var(--bg-strong)', borderTop: 'var(--border-dark)' }}>
          {transport(playGlyph, layoutPlayStopPause ? 'Play/Stop' : 'Play/Pause', playBg, !canPlay, () => vc.act.cuePlay(w.id), 'cue-play')}
          {transport(stopGlyph, layoutPlayStopPause ? 'Pause' : 'Stop', stopBg, !canPlay, () => vc.act.cueStop(w.id), 'cue-stop')}
          {transport(VC_GLYPH.previous, 'Previous cue', 'transparent', !canPlay, () => vc.act.cuePrev(w.id), 'cue-prev')}
          {transport(VC_GLYPH.next, 'Next cue', 'transparent', !canPlay, () => vc.act.cueNext(w.id), 'cue-next')}
          <span style={{ flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', fontSize: 'var(--text-size-small)', textAlign: 'right', color: running ? (paused ? 'var(--selection)' : 'var(--check-lime)') : 'var(--fg-light)' }}>
            {(style.caption ? style.caption + ' · ' : '') + (running ? (paused ? 'Paused' : 'Playing') : 'Stopped') + (idx >= 0 ? ' · ' + (idx + 1) + (total ? '/' + total : '') : '')}
          </span>
        </div>
      </div>
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ================================================================ Speed dial body */
function VCSpeedDialBodyEx({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const mask = vcCueMask(cfg);
  const has = (flag) => mask.indexOf(flag) !== -1;
  const ms = Number(vc.live.speed[w.id]) || 0;
  const canSet = !vc.unsupported(VC_METHODS.SPEED_SET);
  const canTap = !vc.unsupported(VC_METHODS.SPEED_TAP);
  const canFactor = !vc.unsupported(VC_CUE_METHODS.FACTOR);
  const [factor, setFactor] = React.useState(w.factor || 'One');
  const [tapTime, setTapTime] = React.useState(Number(w.tapTimeValue) || 0);
  const [blink, setBlink] = React.useState(false);
  React.useEffect(() => { if (w.factor) setFactor(w.factor); }, [w.factor]);
  React.useEffect(() => { if (w.tapTimeValue != null) setTapTime(Number(w.tapTimeValue) || 0); }, [w.tapTimeValue]);
  useVCWidgetEvent(vc, 'vc.speedDial.factorChanged', w.id, (d) => { if (d.factor) setFactor(d.factor); });
  useVCWidgetEvent(vc, 'vc.speedDial.tapChanged', w.id, (d) => setTapTime(Number(d.tapTimeValue) || 0));
  const presets = useSpeedDialPresets(vc, w);
  /* VCSpeedDialItem.qml tapTimer: the TAP border blinks at the tapped interval while a series is set. */
  React.useEffect(() => {
    if (!(tapTime > 0)) { setBlink(false); return; }
    const t = setInterval(() => setBlink(b => !b), Math.max(80, tapTime));
    return () => clearInterval(t);
  }, [tapTime]);
  const throttled = useThrottledSender(33);
  const stop = (e) => e.stopPropagation();
  const call = (method, params) => vc.qlc.call(method, Object.assign({ widgetId: String(w.id) }, params || {})).catch(vcCueMethodError(vc, method));
  const setMs = (v) => { if (!canSet) { vc.notice('This server has no ' + VC_METHODS.SPEED_SET + ' yet'); return; } vc.act.speedSet(w.id, Math.max(0, Math.round(v))); };
  const setDialMs = (v) => { throttled('sd:' + w.id, () => setMs(v)); };
  const pickFactor = (f) => { if (!canFactor) { vc.notice('This server has no ' + VC_CUE_METHODS.FACTOR + ' yet'); return; } setFactor(f); call(VC_CUE_METHODS.FACTOR, { factor: f }); };
  const stepFactor = (dir) => { const i = SPEED_DIAL_FACTORS.indexOf(factor); const n = vcClamp((i < 0 ? SPEED_DIAL_FACTORS.indexOf('One') : i) + dir, 0, SPEED_DIAL_FACTORS.length - 1); if (SPEED_DIAL_FACTORS[n] !== factor) pickFactor(SPEED_DIAL_FACTORS[n]); };
  const tap = () => { if (!canTap) { vc.notice('This server has no ' + VC_METHODS.SPEED_TAP + ' yet'); return; } vc.act.speedTap(w.id); };
  const resetTap = (e) => { e.preventDefault(); stop(e); call(VC_CUE_METHODS.RESET_TAP); };
  const h = Math.floor(ms / 3600000), m = Math.floor((ms % 3600000) / 60000), s = Math.floor((ms % 60000) / 1000), z = ms % 1000;
  const fromParts = (nh, nm, ns, nz) => (has('Hours') ? nh * 3600000 : 0) + (has('Minutes') ? nm * 60000 : 0) + (has('Seconds') ? ns * 1000 : 0) + (has('Milliseconds') ? nz : 0);
  const timeRow = has('Hours') || has('Minutes') || has('Seconds') || has('Milliseconds');
  const dialMin = Number(cfg.timeMinimumValue) || 0, dialMaxRaw = Number(cfg.timeMaximumValue) || 0;
  const dialMax = dialMaxRaw > dialMin ? dialMaxRaw : dialMin + 10000;
  const g = w.geometry || { width: 200, height: 175 };
  const factorBtn = (f) => (
    <GenericButton key={f} label={SPEED_FACTOR_LABEL[f]} width="100%" height={26} fontSize="var(--text-size-small)" bgColor={factor === f ? SPEED_ACTIVE : 'var(--bg-control)'}
      disabled={!canFactor} onClick={(e) => { stop(e); pickFactor(f); }} onPointerDown={stop} data-e2e={'speed-factor-' + f} />
  );
  const spin = (flag, value, max, suffix, mk) => has(flag) ? (
    <CustomSpinBox key={flag} value={value} from={0} to={max} suffix={suffix} showControls={false} width="100%" height={24} disabled={!canSet}
      onValueModified={(v) => setMs(mk(v))} data-e2e={'speed-' + flag.toLowerCase()} />
  ) : null;
  const nothing = !mask.length && !presets.length;
  return (
    <div data-e2e="speed-body" data-factor={factor} data-tap={tapTime} style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', flexDirection: 'column', gap: 3, padding: 4, overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)', pointerEvents: vc.edit ? 'none' : 'auto' }}>
      {style.caption ? <span style={Object.assign({ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', flex: 'none', textAlign: 'center' }, vcFontCss(style), { fontSize: 13 })}>{style.caption}</span> : null}
      {has('Dial') || has('Beats') || has('Tap') ? (
        <div style={{ display: 'flex', gap: 4, flex: has('Dial') ? 1 : 'none', minHeight: 0 }}>
          {has('Dial') ? (
            <div style={{ flex: 2, minWidth: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }} onPointerDown={stop} data-e2e="speed-dial">
              <VCKnob value={vcClamp(ms, dialMin, dialMax)} from={dialMin} to={dialMax} size={Math.max(36, Math.min(g.width / 2 - 12, g.height - 90))} disabled={!canSet} onMoved={setDialMs} />
            </div>
          ) : null}
          {has('Beats') ? (
            <div style={{ flex: 2, minWidth: 0, display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 3, alignContent: 'center' }}>
              {['OneSixteenth', 'OneEighth', 'OneFourth', 'Half', 'Two', 'Four', 'Eight', 'Sixteen'].map(factorBtn)}
            </div>
          ) : null}
          {has('Tap') ? (
            <div style={{ flex: 1, minWidth: 48, display: 'flex' }}>
              <GenericButton label="TAP" width="100%" height="100%" fontSize="var(--text-size-default)" disabled={!canTap} data-e2e="speed-tap"
                bgColor={blink ? 'var(--keypad-enter-hover)' : 'var(--keypad-enter)'} hoverColor="var(--keypad-enter-hover)" pressedColor="var(--keypad-enter-pressed)"
                onPointerDown={(e) => { if (e.button === 0) { stop(e); tap(); } }} onClick={stop} onContextMenu={resetTap} style={{ minHeight: 34, border: '2px solid ' + (blink ? '#00FF00' : 'var(--bg-medium)') }} />
            </div>
          ) : null}
        </div>
      ) : null}
      {timeRow ? (
        <div style={{ display: 'flex', gap: 3, flex: 'none' }} onPointerDown={stop}>
          {spin('Hours', h, 999, 'h', (v) => fromParts(v, m, s, z))}
          {spin('Minutes', m, 59, 'm', (v) => fromParts(h, v, s, z))}
          {spin('Seconds', s, 59, 's', (v) => fromParts(h, m, v, z))}
          {spin('Milliseconds', z, 999, 'ms', (v) => fromParts(h, m, s, v))}
        </div>
      ) : null}
      {has('Multipliers') ? (
        <div style={{ display: 'flex', gap: 3, alignItems: 'stretch', flex: 'none', height: 34 }}>
          <GenericButton label="-" width={30} height="100%" fontSize="var(--text-size-large)" disabled={!canFactor} onClick={(e) => { stop(e); stepFactor(-1); }} onPointerDown={stop} data-e2e="speed-minus" />
          <span data-e2e="speed-mult-label" style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', fontFamily: 'var(--font-mono)', fontSize: 12, lineHeight: 1.15 }}>
            <span>{(SPEED_FACTOR_LABEL[factor] || '1') + 'x'}</span><span>{vcMsToString(ms)}</span>
          </span>
          <GenericButton label="+" width={30} height="100%" fontSize="var(--text-size-large)" disabled={!canFactor} onClick={(e) => { stop(e); stepFactor(1); }} onPointerDown={stop} data-e2e="speed-plus" />
          <IconButton faSource="fa_xmark" size={30} tooltip="Reset the multiplier to 1x" disabled={!canFactor} onClick={(e) => { stop(e); pickFactor('One'); }} onPointerDown={stop} data-e2e="speed-factor-reset" />
        </div>
      ) : null}
      {has('Apply') ? (
        <GenericButton label="Apply" width="100%" height={26} fontSize="var(--text-size-small)" disabled={vc.unsupported(VC_CUE_METHODS.APPLY)} onClick={(e) => { stop(e); call(VC_CUE_METHODS.APPLY); }} onPointerDown={stop} data-e2e="speed-apply" style={{ flex: 'none' }} />
      ) : null}
      {presets.length ? (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3, flex: 'none' }} data-e2e="speed-presets">
          {presets.map(p => (
            <GenericButton key={p.presetId} label={p.name} height={24} fontSize="var(--text-size-small)" bgColor={ms === Number(p.valueMs) ? SPEED_ACTIVE : 'var(--bg-control)'} style={{ minWidth: 54, padding: '0 6px' }}
              disabled={vc.unsupported(VC_CUE_METHODS.PRESET_APPLY)} onClick={(e) => { stop(e); call(VC_CUE_METHODS.PRESET_APPLY, { presetId: p.presetId }); }} onPointerDown={stop} data-e2e-preset={p.presetId} />
          ))}
        </div>
      ) : null}
      {nothing ? <RobotoText label="No controls visible — enable some in the widget properties" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" wrapText height="auto" textHAlign="center" style={{ padding: 6 }} /> : null}
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ================================================================ Cue list properties */
function VCCueListProps({ w, cfg, setConfig, functions, section, PropRow, CheckRow, FunctionPicker }) {
  const chasers = React.useMemo(() => functions.filter(f => f.type === 'Chaser' || f.type === 'Sequence'), [functions]);
  const mode = cfg.sideFaderMode || 'None';
  return (
    <>
      {section('cueChaser', 'Attached Chaser', (
        <FunctionPicker functions={chasers} currentId={cfg.chaserID} onPick={(f) => setConfig({ chaserID: String(f.id) })} onDetach={() => setConfig({ chaserID: NO_FUNCTION })} />
      ))}
      {section('cueButtons', 'Buttons behavior', (
        <div>
          <PropRow label="Play/Stop layout">
            <CustomComboBox width="100%" height={24} currValue={cfg.playbackLayout || 'PlayPauseStop'} onValueChanged={(v) => { if (v !== (cfg.playbackLayout || 'PlayPauseStop')) setConfig({ playbackLayout: v }); }}
              model={[{ mLabel: 'Play/Pause + Stop', mValue: 'PlayPauseStop' }, { mLabel: 'Play/Stop + Pause', mValue: 'PlayStopPause' }]} data-e2e="cue-layout" />
          </PropRow>
          <PropRow label="Next/Previous (when chaser is not running)">
            <CustomComboBox width="100%" height={24} currValue={cfg.nextPrevBehavior || 'DefaultRunFirst'} onValueChanged={(v) => { if (v !== (cfg.nextPrevBehavior || 'DefaultRunFirst')) setConfig({ nextPrevBehavior: v }); }}
              model={[{ mLabel: 'Run from first/last cue', mValue: 'DefaultRunFirst' }, { mLabel: 'Run from next/previous cue', mValue: 'RunNext' }, { mLabel: 'Select next/previous cue', mValue: 'Select' }, { mLabel: 'Do nothing', mValue: 'Nothing' }]} data-e2e="cue-nextprev" />
          </PropRow>
        </div>
      ))}
      {section('cueFader', 'Side fader', (
        <div data-e2e="cue-fader-mode">
          {[['None', 'None'], ['Crossfade', 'Crossfade'], ['Steps', 'Steps']].map(([v, l]) => (
            <CheckRow key={v} label={l} checked={mode === v} onToggle={() => { if (mode !== v) setConfig({ sideFaderMode: v }); }} />
          ))}
        </div>
      ))}
    </>
  );
}

/* ================================================================ Speed dial properties */
/** Text filter over functions.list, one row per match; picking one calls onPick(f). */
function FunctionSearch({ functions, exclude, onPick, placeholder }) {
  const [needle, setNeedle] = React.useState('');
  const D = window.QLCData;
  const n = needle.trim().toLowerCase();
  const matches = n ? functions.filter(f => !f.hidden && exclude.indexOf(String(f.id)) === -1 && (f.name.toLowerCase().indexOf(n) !== -1 || String(f.id) === n)).slice(0, 40) : [];
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: '4px 6px' }}>
      <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', gap: 4 }}>
        <img src={D.icon('search')} alt="" style={{ width: 14, height: 14 }} />
        <CustomTextInput text={needle} editing placeholder={placeholder || 'Search functions'} width="100%" height={22} onTextConfirmed={setNeedle} onChange={(e) => setNeedle(e.target.value)} style={{ fontSize: 'var(--text-size-small)' }} data-e2e="speed-fn-search" />
      </span>
      {n ? (
        <div style={{ maxHeight: 190, overflow: 'auto', border: 'var(--border-dark)' }} data-e2e="speed-fn-matches">
          {matches.length ? matches.map(f => (
            <div key={f.id} role="button" onClick={() => { onPick(f); setNeedle(''); }}
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

function VCSpeedDialProps({ w, cfg, setConfig, functions, section, PropRow, CheckRow }) {
  const vc = useVC();
  const mask = vcCueMask(cfg);
  const has = (flag) => mask.indexOf(flag) !== -1;
  const list = Array.isArray(cfg.functions) ? cfg.functions : [];
  const byId = React.useMemo(() => { const m = {}; functions.forEach(f => { m[String(f.id)] = f; }); return m; }, [functions]);
  const setList = (next) => setConfig({ functions: next });
  const setFactor = (fid, key, value) => setList(list.map(f => String(f.functionID) === String(fid) ? Object.assign({}, f, { [key]: value }) : f));
  const toggleFlag = (flag, on) => setConfig({ visibilityMask: on ? mask.concat(mask.indexOf(flag) === -1 ? [flag] : []) : mask.filter(x => x !== flag) });
  const [inMs, setInMs] = React.useState(false);
  const unit = inMs ? 1 : 1000;
  const presets = useSpeedDialPresets(vc, w);
  const [sel, setSel] = React.useState(-1);
  const [pName, setPName] = React.useState('');
  const [pTime, setPTime] = React.useState(0);
  const selected = presets.find(p => p.presetId === sel) || null;
  React.useEffect(() => { if (sel >= 0 && !selected) { setSel(-1); setPName(''); setPTime(0); } }, [presets]);
  const select = (p) => { setSel(p.presetId); setPName(p.name); setPTime(Number(p.valueMs) || 0); };
  const structural = (method, params) => vcStructural(vc.qlc, method, Object.assign({ widgetId: String(w.id) }, params)).catch(vcCueMethodError(vc, method));
  const addPreset = () => structural(VC_CUE_METHODS.PRESET_ADD, { preset: { name: pName.trim(), valueMs: Math.round(pTime) } }).then(r => { if (r && r.presetId != null) setSel(r.presetId); });
  const removePreset = () => { if (sel >= 0) structural(VC_CUE_METHODS.PRESET_REMOVE, { presetId: sel }).then(() => { setSel(-1); setPName(''); setPTime(0); }); };
  const updateName = (t) => { setPName(t); if (selected && t.trim() && t.trim() !== selected.name) structural(VC_CUE_METHODS.PRESET_UPDATE, { presetId: sel, name: t.trim() }); };
  const updateTime = (v) => { setPTime(v); if (selected && Math.round(v) !== Number(selected.valueMs)) structural(VC_CUE_METHODS.PRESET_UPDATE, { presetId: sel, valueMs: Math.round(v) }); };
  const factorCombo = (f, key, tag) => (
    <CustomComboBox width={72} height={22} currValue={f[key] || (key === 'durationFactor' ? 'One' : 'None')} onValueChanged={(v) => { if (v !== f[key]) setFactor(f.functionID, key, v); }} model={SPEED_FACTOR_MODEL} data-e2e={tag} />
  );
  return (
    <>
      {section('speedFunctions', 'Functions', (
        <div data-e2e="speed-functions">
          {list.length ? (
            <div style={{ display: 'flex', flexDirection: 'column' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 3, padding: '0 6px', height: 'var(--list-item-height)', fontSize: 'var(--text-size-menubar)', color: 'var(--fg-light)' }}>
                <span style={{ flex: 1 }}>Function</span><span style={{ width: 72 }}>Fade In</span><span style={{ width: 72 }}>Fade Out</span><span style={{ width: 72 }}>Duration</span><span style={{ width: 24 }} />
              </div>
              {list.map(f => {
                const fn = byId[String(f.functionID)];
                return (
                  <div key={f.functionID} data-e2e="speed-fn-row" data-fid={f.functionID} style={{ display: 'flex', alignItems: 'center', gap: 3, padding: '1px 6px', minHeight: 'var(--list-item-height)' }}>
                    <RobotoText label={fn ? fn.name : 'Function #' + f.functionID} fontSize="var(--text-size-small)" height="auto" wrapText style={{ flex: 1, minWidth: 0 }} />
                    {factorCombo(f, 'fadeInFactor', 'speed-fn-fadein')}{factorCombo(f, 'fadeOutFactor', 'speed-fn-fadeout')}{factorCombo(f, 'durationFactor', 'speed-fn-duration')}
                    <IconButton faSource="fa_minus" size={22} tooltip="Remove this function" onClick={() => setList(list.filter(x => String(x.functionID) !== String(f.functionID)))} data-e2e="speed-fn-remove" />
                  </div>
                );
              })}
            </div>
          ) : <RobotoText label="No functions yet — search below to add the Functions this dial controls" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: '4px 6px' }} />}
          <FunctionSearch functions={functions} exclude={list.map(f => String(f.functionID))} placeholder="Add a function…"
            onPick={(f) => setList(list.concat([{ functionID: String(f.id), fadeInFactor: 'None', fadeOutFactor: 'None', durationFactor: 'One' }]))} />
        </div>
      ))}
      {section('speedControl', 'Control Properties', (
        <div>
          {has('Tap') ? <CheckRow label="Tap button controls the global BPM rate" checked={!!cfg.controlBPM} onToggle={(b) => setConfig({ controlBPM: typeof b === 'boolean' ? b : !cfg.controlBPM })} /> : null}
          {has('Dial') ? <CheckRow label="Reset multiplier factor when the dial value changes" checked={!!cfg.resetOnDialChange} onToggle={(b) => setConfig({ resetOnDialChange: typeof b === 'boolean' ? b : !cfg.resetOnDialChange })} /> : null}
          <PropRow label="Dial time range">
            <CustomSpinBox value={Math.floor((Number(cfg.timeMinimumValue) || 0) / unit)} from={0} to={100000} width={70} height={24} suffix={inMs ? 'ms' : 's'} onValueModified={(v) => setConfig({ timeMinimumValue: v * unit })} data-e2e="speed-range-min" />
            <RobotoText label="to" fontSize="var(--text-size-small)" height="auto" />
            <CustomSpinBox value={Math.floor((Number(cfg.timeMaximumValue) || 0) / unit)} from={0} to={100000} width={70} height={24} suffix={inMs ? 'ms' : 's'} onValueModified={(v) => setConfig({ timeMaximumValue: v * unit })} data-e2e="speed-range-max" />
            <GenericButton label={inMs ? 'ms' : 'S'} width={30} height={24} fontSize="var(--text-size-menubar)" onClick={() => setInMs(!inMs)} />
          </PropRow>
          {!has('Tap') && !has('Dial') ? <RobotoText label="Enable the Tap or Dial control below for their options" fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: '0 6px 4px' }} /> : null}
        </div>
      ))}
      {section('speedAppearance', 'Appearance', (
        <div data-e2e="speed-visibility">
          {SPEED_VISIBILITY.map(([flag, label]) => (
            <CheckRow key={flag} label={label} checked={has(flag)} onToggle={(b) => toggleFlag(flag, typeof b === 'boolean' ? b : !has(flag))} />
          ))}
        </div>
      ))}
      {section('speedPresets', 'Presets', (
        <div data-e2e="speed-preset-editor">
          <PropRow label="Preset name">
            <span style={{ flex: 1, display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
              <CustomTextInput key={w.id + ':' + sel} text={pName} editing width="100%" height={22} onChange={(e) => setPName(e.target.value)} onTextConfirmed={updateName} style={{ fontSize: 'var(--text-size-small)' }} data-e2e="speed-preset-name" />
            </span>
          </PropRow>
          <PropRow label="Preset time">
            <CustomSpinBox value={Math.round(pTime)} from={0} to={3600000} suffix="ms" showControls={false} width={100} height={24} onValueModified={updateTime} data-e2e="speed-preset-time" />
          </PropRow>
          <div style={{ display: 'flex', gap: 4, padding: '2px 6px', justifyContent: 'flex-end' }}>
            <IconButton faSource="fa_plus" size={26} tooltip="Add a preset" disabled={!pName.trim() || !(pTime > 0)} onClick={addPreset} data-e2e="speed-preset-add" />
            <IconButton faSource="fa_minus" size={26} tooltip="Remove the selected preset" disabled={sel < 0} onClick={removePreset} data-e2e="speed-preset-remove" />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 3, padding: '0 6px', height: 'var(--list-item-height)', fontSize: 'var(--text-size-menubar)', color: 'var(--fg-light)' }}>
            <span style={{ flex: 1 }}>Name</span><span style={{ width: 80 }}>Time</span>
          </div>
          {presets.length ? presets.map(p => (
            <div key={p.presetId} role="button" data-e2e="speed-preset-row" data-preset={p.presetId} onClick={() => select(p)}
              style={{ display: 'flex', alignItems: 'center', gap: 3, padding: '0 6px', height: 'var(--list-item-height)', cursor: 'pointer', background: p.presetId === sel ? 'var(--highlight)' : 'transparent' }}>
              <RobotoText label={p.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1, minWidth: 0 }} />
              <RobotoText label={vcMsToString(p.valueMs)} fontSize="var(--text-size-small)" height="100%" style={{ width: 80 }} />
            </div>
          )) : <RobotoText label="No presets" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
        </div>
      ))}
    </>
  );
}

window.QLCVCBodies = Object.assign(window.QLCVCBodies || {}, { CueList: VCCueListBodyEx, Speed: VCSpeedDialBodyEx, SpeedDial: VCSpeedDialBodyEx });
window.QLCVCProperties = Object.assign(window.QLCVCProperties || {}, { CueList: VCCueListProps, Speed: VCSpeedDialProps, SpeedDial: VCSpeedDialProps });
Object.assign(window, { VC_CUE_METHODS, VCCueListBodyEx, VCSpeedDialBodyEx, VCCueListProps, VCSpeedDialProps });
