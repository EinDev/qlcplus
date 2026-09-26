/**
 * Virtual Console — live widget bodies. One component per VcWidgetType, each filling the box its
 * owner positions it in (VirtualConsole.jsx does the geometry; vc-edit.jsx wraps the box in edit
 * mode). Looks follow qmlui/qml/virtualconsole/VC*Item.qml; live values come from the store in
 * VCContext (seeded from vc.widget.list, updated by the vc.*Changed events) and every gesture goes
 * through the store's actions so the throttling / unsupported-method handling lives in one place.
 */
const { RobotoText, FaIcon, IconButton, GenericButton, CustomSpinBox } = window.PatchDesignSystem_5432c9;

/* VCButtonItem.qml: active border is lime (red when the flash overrides/forces LTP), monitoring
   is "orange" (a QML literal there, not a UISettings colour), inactive a light grey. */
const VC_BUTTON_BORDER = { active: 'var(--check-lime)', activeOverride: 'var(--override-red)', monitoring: 'orange', inactive: 'var(--bg-lighter)' };
const VC_BUTTON_ACTION_ICON = { Flash: 'flash', StopAll: 'stopall', Blackout: 'blackout' };

function vcButtonState(raw) {
  const s = String(raw == null ? 'inactive' : raw).toLowerCase();
  return s === 'active' || s === 'monitoring' ? s : 'inactive';
}

/** Black veil QLC+ paints over a disabled control (--disabled-veil-soft, never a recolour). */
function VCDisabledVeil({ on }) {
  return on ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null;
}

/* ---------------------------------------------------------------- Frame / Solo frame */
/* VCFrameItem.qml draws a header only when showHeader is set. The API does not expose that flag for
   frames (their typeConfig is empty), so the owner passes `header=false` when a child sits in the
   header band — in the desktop app such a frame must be running headerless or the child would be
   covered. The multipage controls then float at the top-right corner instead. */
function VCFrameBody({ w, header = true, children }) {
  const vc = useVC();
  const D = window.QLCData;
  const solo = w.widgetType === 'SoloFrame';
  const style = w.style || {};
  const fg = style.foregroundColor || 'var(--fg-main)';
  const fr = vc.live.frames[w.id] || {};
  const pages = Number(fr.pages) || 0;
  const current = Number(fr.currentPage) || 0;
  const multipage = pages > 1 || fr.multipage === true;
  const canGoto = !vc.unsupported(VC_METHODS.FRAME_GOTO);
  const goto = (p) => { if (p >= 0 && (!pages || p < pages)) vc.act.frameGoto(w.id, p); };
  const pager = multipage ? (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 2, flex: 'none', pointerEvents: vc.edit ? 'none' : 'auto', background: header ? 'transparent' : 'var(--section-header)', borderRadius: header ? 0 : 'var(--radius-spin)' }}>
      <IconButton faSource="fa_chevron_left" size={22} tooltip="Previous page" disabled={!canGoto || current <= 0} onClick={() => goto(current - 1)} onPointerDown={e => e.stopPropagation()} />
      <RobotoText label={'Page ' + (current + 1) + (pages ? '/' + pages : '')} fontSize="var(--text-size-menubar)" height={22} style={{ minWidth: 52, textAlign: 'center' }} textHAlign="center" />
      <IconButton faSource="fa_chevron_right" size={22} tooltip="Next page" disabled={!canGoto || (pages > 0 && current >= pages - 1)} onClick={() => goto(current + 1)} onPointerDown={e => e.stopPropagation()} />
    </span>
  ) : null;
  return (
    <div style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)',
      border: '2px solid ' + (solo ? 'var(--override-red)' : 'var(--border-color-dark)'), overflow: 'hidden' }}>
      {header ? (
        <div style={{ position: 'absolute', left: 0, top: 0, right: 0, height: 'var(--list-item-height)', background: 'var(--section-header)', display: 'flex', alignItems: 'center', gap: 6, padding: '0 6px', zIndex: 2 }}>
          <img src={D.icon(solo ? 'soloframe' : 'frame')} alt="" style={{ width: 16, height: 16, flex: 'none' }} />
          <span style={Object.assign({ color: fg, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', flex: 1, minWidth: 0 }, vcFontCss(style), { fontSize: 14 })}>{style.caption || ''}</span>
          {pager}
        </div>
      ) : (pager ? <div style={{ position: 'absolute', right: 2, top: 2, zIndex: 3 }}>{pager}</div> : null)}
      {children}
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/* ---------------------------------------------------------------- Button */
function VCButtonBody({ w }) {
  const vc = useVC();
  const D = window.QLCData;
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const state = vcButtonState(vc.live.buttons[w.id]);
  const [pressing, setPressing] = React.useState(false);
  const canPress = !vc.unsupported(VC_METHODS.PRESS);
  const border = state === 'active' ? (cfg.flashOverrides || cfg.flashForceLTP ? VC_BUTTON_BORDER.activeOverride : VC_BUTTON_BORDER.active)
    : state === 'monitoring' ? VC_BUTTON_BORDER.monitoring : VC_BUTTON_BORDER.inactive;
  const down = (e) => {
    if (vc.edit) return;
    e.preventDefault(); e.stopPropagation();
    if (!canPress) { vc.notice('This server has no ' + VC_METHODS.PRESS + ' yet — buttons are view-only'); return; }
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
    setPressing(true);
    vc.act.press(w.id, true);
  };
  const up = (e) => { if (!pressing) return; setPressing(false); vc.act.press(w.id, false); };
  const icon = VC_BUTTON_ACTION_ICON[cfg.actionType];
  return (
    <div role="button" tabIndex={vc.edit ? -1 : 0} title={(style.caption || 'Button') + (canPress ? '' : ' — press not supported by this server')}
      onPointerDown={down} onPointerUp={up} onPointerCancel={up}
      onKeyDown={(e) => { if (!vc.edit && canPress && (e.key === ' ' || e.key === 'Enter') && !pressing) { e.preventDefault(); setPressing(true); vc.act.press(w.id, true); } }}
      onKeyUp={(e) => { if (pressing && (e.key === ' ' || e.key === 'Enter')) { setPressing(false); vc.act.press(w.id, false); } }}
      style={{ position: 'absolute', inset: 0, borderRadius: 4, background: style.backgroundColor || 'var(--bg-control)', touchAction: 'none',
        cursor: vc.edit ? 'default' : canPress ? 'pointer' : 'not-allowed', userSelect: 'none', outline: 'none' }}>
      <div style={{ position: 'absolute', inset: 1, borderRadius: 3, border: '3px solid ' + (pressing ? 'var(--highlight)' : border) }}>
        <div style={Object.assign({ position: 'absolute', inset: 3, borderRadius: 2, display: 'flex', alignItems: 'center', justifyContent: 'center', textAlign: 'center', overflow: 'hidden',
          color: style.foregroundColor || 'var(--fg-main)', lineHeight: 1.1, padding: '0 2px', wordBreak: 'break-word', background: pressing ? 'var(--disabled-veil-soft)' : 'transparent' }, vcFontCss(style))}>
          {style.caption || ''}
        </div>
        {icon ? <img src={D.icon(icon)} alt="" style={{ position: 'absolute', right: 3, top: 3, width: 20, height: 20, zIndex: 1 }} /> : null}
      </div>
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/* ---------------------------------------------------------------- Slider / Knob */
function VCSliderBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const g = w.geometry || { width: 60, height: 150 };
  const lo = Number.isFinite(cfg.rangeLowLimit) ? cfg.rangeLowLimit : 0;
  const hi = Number.isFinite(cfg.rangeHighLimit) ? cfg.rangeHighLimit : 255;
  const raw = vc.live.sliders[w.id];
  const v = vcClamp(raw != null ? Number(raw) : lo, lo, hi);
  const knob = cfg.widgetStyle === 'Knob';
  const canSlide = !vc.unsupported(VC_METHODS.SET_VALUE);
  const percent = cfg.valueDisplayStyle === 'PercentageValue' || cfg.valueDisplayStyle === 'Percentage';
  /* VCSliderItem.qml: submaster faders get a green track, grand master the red one. */
  const track = cfg.sliderMode === 'Submaster' ? '#77DD73' : cfg.sliderMode === 'GrandMaster' ? 'var(--override-red)' : 'var(--fader-track)';
  const faderH = Math.max(30, g.height - 60);
  const knobSize = Math.max(30, Math.min(g.width - 10, g.height - 56));
  const move = (nv) => { if (!canSlide) { vc.notice('This server has no ' + VC_METHODS.SET_VALUE + ' yet — sliders are view-only'); return; } vc.act.slide(w.id, nv); };
  return (
    <div title={(style.caption || 'Slider') + (canSlide ? '' : ' — setValue not supported by this server')}
      style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4,
        display: 'flex', flexDirection: 'column', alignItems: 'center', padding: '4px 2px', color: style.foregroundColor || 'var(--fg-main)', overflow: 'hidden', pointerEvents: vc.edit ? 'none' : 'auto' }}>
      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 13, height: 18, lineHeight: '18px', flex: 'none' }}>{percent ? Math.round(v / 2.55) + '%' : v}</span>
      <div style={{ flex: 1, minHeight: 20, display: 'flex', alignItems: 'center', justifyContent: 'center', width: '100%' }}>
        {knob
          ? <VCKnob value={v} from={lo} to={hi} size={knobSize} disabled={!canSlide && false} onMoved={move} onPressChange={(on) => vc.act.sliderPress(w.id, on)} />
          : <VCFader value={v} from={lo} to={hi} height={faderH} width={Math.min(38, g.width - 8)} trackColor={track} inverted={!!cfg.invertedAppearance}
              onMoved={move} onPressChange={(on) => vc.act.sliderPress(w.id, on)} />}
      </div>
      <span style={Object.assign({ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', maxWidth: '100%', flex: 'none' }, vcFontCss(style), { fontSize: 13 })}>{style.caption || ''}</span>
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/* ---------------------------------------------------------------- Cue list */
const CUE_COLS = [['#', 34], ['Name', 0], ['Fade In', 62], ['Fade Out', 62], ['Hold', 62], ['Notes', 90]];

function VCCueListBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const cl = vc.live.cueLists[w.id] || {};
  const steps = cl.steps;
  const idx = cl.playbackIndex != null ? Number(cl.playbackIndex) : -1;
  const running = !!cl.running, paused = !!cl.paused;
  const canGet = !vc.unsupported(VC_METHODS.CUE_GET);
  const canPlay = !vc.unsupported(VC_METHODS.CUE_PLAY);
  React.useEffect(() => { vc.act.cueGet(w.id); }, [w.id, vc.qlc.online]);
  const stop = (e) => e.stopPropagation();
  const th = (label, width, i) => (
    <span key={i} style={{ flex: width ? 'none' : 1, width: width || 'auto', minWidth: 0, padding: '0 4px', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
      textAlign: i === 0 ? 'right' : 'left' }}>{label}</span>
  );
  const row = (s, i) => {
    const cells = [(s.index != null ? s.index : i) + 1, s.name || '', vcMsToString(s.fadeIn), vcMsToString(s.fadeOut), s.hold === -1 || s.hold == null ? '∞' : vcMsToString(s.hold), s.notes || ''];
    const stepIndex = s.index != null ? s.index : i;
    const isCurrent = stepIndex === idx;
    return (
      <div key={stepIndex} role="row" onClick={(e) => { stop(e); if (!vc.edit) vc.act.cueJump(w.id, stepIndex); }}
        title={'Jump to step ' + (stepIndex + 1)}
        style={{ display: 'flex', alignItems: 'center', height: 'var(--list-item-height)', flex: 'none', cursor: vc.edit ? 'default' : 'pointer', fontSize: 'var(--text-size-small)',
          background: isCurrent ? (paused ? 'var(--highlight-pressed)' : 'var(--highlight)') : (stepIndex % 2 ? 'var(--bg-medium)' : 'transparent'), color: 'var(--fg-main)' }}>
        {cells.map((c, j) => th(c, CUE_COLS[j][1], j))}
      </div>
    );
  };
  const transport = (glyph, tip, disabled, fn) => (
    <IconButton faSource={glyph} size={30} tooltip={tip} disabled={disabled || vc.edit} onClick={(e) => { stop(e); fn(); }} onPointerDown={stop} />
  );
  const total = steps ? steps.length : 0;
  return (
    <div style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-stronger)', border: '2px solid var(--border-color-dark)', display: 'flex', flexDirection: 'column', overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      <div style={{ display: 'flex', alignItems: 'center', height: 'var(--list-item-height)', flex: 'none', background: 'var(--section-header)', fontSize: 'var(--text-size-small)', fontWeight: 700 }}>
        {CUE_COLS.map((c, i) => th(c[0], c[1], i))}
      </div>
      <div style={{ flex: 1, minHeight: 0, overflow: 'auto' }}>
        {steps == null
          ? <RobotoText label={canGet ? 'Loading steps…' : 'Steps not available on this server'} fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />
          : steps.length ? steps.map(row) : <RobotoText label="No steps" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: 3, flex: 'none', background: 'var(--bg-strong)', borderTop: 'var(--border-dark)' }}>
        {transport(paused ? 'fa_play' : 'fa_play', paused ? 'Resume' : 'Play', !canPlay || (running && !paused), () => vc.act.cuePlay(w.id))}
        {transport(VC_GLYPH.stop, 'Stop', !canPlay || !running, () => vc.act.cueStop(w.id))}
        {transport(VC_GLYPH.previous, 'Previous cue', !canPlay, () => vc.act.cuePrev(w.id))}
        {transport(VC_GLYPH.next, 'Next cue', !canPlay, () => vc.act.cueNext(w.id))}
        <span style={{ flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', fontSize: 'var(--text-size-small)', textAlign: 'right', color: running ? (paused ? 'var(--selection)' : 'var(--check-lime)') : 'var(--fg-light)' }}>
          {(style.caption ? style.caption + ' · ' : '') + (running ? (paused ? 'Paused' : 'Playing') : 'Stopped') + (idx >= 0 ? ' · ' + (idx + 1) + (total ? '/' + total : '') : '')}
        </span>
      </div>
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/* ---------------------------------------------------------------- XY pad */
function VCXYPadBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const pos = vc.live.xy[w.id] || { x: 0.5, y: 0.5 };
  const canSet = !vc.unsupported(VC_METHODS.XY_SET);
  const ref = React.useRef(null);
  const [press, setPress] = React.useState(false);
  const set = (e) => {
    const r = ref.current.getBoundingClientRect();
    vc.act.xySet(w.id, vcClamp((e.clientX - r.left) / (r.width || 1), 0, 1), vcClamp((e.clientY - r.top) / (r.height || 1), 0, 1));
  };
  const down = (e) => {
    if (vc.edit) return;
    e.preventDefault(); e.stopPropagation();
    if (!canSet) { vc.notice('This server has no ' + VC_METHODS.XY_SET + ' yet — XY pads are view-only'); return; }
    try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
    setPress(true); set(e);
  };
  const move = (e) => { if (press) { e.preventDefault(); set(e); } };
  const up = () => { if (press) { setPress(false); vc.act.xyRelease(w.id); } };
  const cx = pos.x * 100 + '%', cy = pos.y * 100 + '%';
  return (
    <div style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', flexDirection: 'column', overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)' }}>
      <div ref={ref} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} data-vc-pad=""
        style={{ flex: 1, minHeight: 0, margin: 4, position: 'relative', background: 'var(--bg-stronger)', border: 'var(--border-dark)', touchAction: 'none', cursor: vc.edit ? 'default' : canSet ? 'crosshair' : 'not-allowed' }}>
        <div style={{ position: 'absolute', left: '50%', top: 0, bottom: 0, width: 1, background: 'var(--bg-control)' }} />
        <div style={{ position: 'absolute', top: '50%', left: 0, right: 0, height: 1, background: 'var(--bg-control)' }} />
        <div style={{ position: 'absolute', left: cx, top: 0, bottom: 0, width: 1, background: press ? 'var(--selection)' : 'var(--fader-track)' }} />
        <div style={{ position: 'absolute', top: cy, left: 0, right: 0, height: 1, background: press ? 'var(--selection)' : 'var(--fader-track)' }} />
        <div style={{ position: 'absolute', left: cx, top: cy, width: 14, height: 14, marginLeft: -7, marginTop: -7, borderRadius: 7, background: press ? 'var(--selection)' : 'var(--fader-track)', border: '2px solid var(--fg-main)' }} />
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '0 6px 4px', flex: 'none', fontSize: 'var(--text-size-small)' }}>
        <span style={{ flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{style.caption || 'XY Pad'}</span>
        <span style={{ fontFamily: 'var(--font-mono)', fontSize: 12, color: 'var(--fg-light)' }}>{'X ' + Math.round(pos.x * 255) + '  Y ' + Math.round(pos.y * 255)}</span>
      </div>
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/* ---------------------------------------------------------------- Speed dial */
function VCSpeedBody({ w }) {
  const vc = useVC();
  const style = w.style || {};
  const ms = Number(vc.live.speed[w.id]) || 0;
  const canSet = !vc.unsupported(VC_METHODS.SPEED_SET);
  const canTap = !vc.unsupported(VC_METHODS.SPEED_TAP);
  const [draft, setDraft] = React.useState(ms);
  const [blink, setBlink] = React.useState(false);
  React.useEffect(() => { setDraft(ms); }, [ms]);
  const stop = (e) => e.stopPropagation();
  const commit = (v) => { if (canSet) vc.act.speedSet(w.id, Math.max(0, Math.round(v))); else vc.notice('This server has no ' + VC_METHODS.SPEED_SET + ' yet'); };
  const tap = () => { if (!canTap) { vc.notice('This server has no ' + VC_METHODS.SPEED_TAP + ' yet'); return; } setBlink(true); setTimeout(() => setBlink(false), 120); vc.act.speedTap(w.id); };
  const g = w.geometry || { width: 200, height: 175 };
  const compact = g.height < 120;
  return (
    <div style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', flexDirection: 'column', gap: 4, padding: 4, overflow: 'hidden', color: style.foregroundColor || 'var(--fg-main)', pointerEvents: vc.edit ? 'none' : 'auto' }}>
      <span style={Object.assign({ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', flex: 'none' }, vcFontCss(style), { fontSize: 13 })}>{style.caption || 'Speed'}</span>
      <div style={{ display: 'flex', alignItems: 'center', gap: 4, flex: 'none' }}>
        <GenericButton label="-" width={30} height={30} fontSize="var(--text-size-large)" disabled={!canSet} onClick={(e) => { stop(e); commit(ms - 100); }} onPointerDown={stop} />
        <span style={{ flex: 1, textAlign: 'center', fontFamily: 'var(--font-mono)', fontSize: 'var(--text-size-large)' }}>{vcMsToString(ms)}</span>
        <GenericButton label="+" width={30} height={30} fontSize="var(--text-size-large)" disabled={!canSet} onClick={(e) => { stop(e); commit(ms + 100); }} onPointerDown={stop} />
      </div>
      {!compact ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, flex: 'none' }} onPointerDown={stop}>
          <CustomSpinBox value={draft} from={0} to={3600000} stepSize={10} suffix=" ms" showControls={false} width="100%" height={26} disabled={!canSet}
            onValueModified={setDraft} onKeyDown={(e) => { if (e.key === 'Enter') commit(draft); }} onBlur={() => { if (draft !== ms) commit(draft); }} />
        </div>
      ) : null}
      <GenericButton label="TAP" width="100%" height={Math.max(26, compact ? 26 : 38)} fontSize="var(--text-size-default)" disabled={!canTap}
        bgColor={blink ? 'var(--keypad-enter-hover)' : 'var(--keypad-enter)'} hoverColor="var(--keypad-enter-hover)" pressedColor="var(--keypad-enter-pressed)"
        onPointerDown={(e) => { stop(e); tap(); }} onClick={stop} style={{ flex: 'none', marginTop: 'auto' }} />
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/* ---------------------------------------------------------------- Label / everything else */
function VCLabelBody({ w }) {
  const style = w.style || {};
  return (
    <div style={Object.assign({ position: 'absolute', inset: 0, background: style.backgroundColor || 'transparent', color: style.foregroundColor || 'var(--fg-main)', display: 'flex', alignItems: 'center', justifyContent: 'center', textAlign: 'center', overflow: 'hidden',
      border: style.backgroundColor ? 'none' : '1px dashed var(--border-color-dark)', padding: '0 2px' }, vcFontCss(style))}>
      {style.caption || ''}
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

function VCViewOnlyBody({ w }) {
  const D = window.QLCData;
  const style = w.style || {};
  return (
    <div title={w.widgetType + ' — view only in the web UI (no ' + w.widgetType + ' interaction methods on the server)'}
      style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-control)', border: '2px dashed var(--fg-medium)', borderRadius: 4, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 4, color: style.foregroundColor || 'var(--fg-main)', overflow: 'hidden', padding: 4 }}>
      <img src={D.icon(VC_WIDGET_ICONS[w.widgetType] || 'frame')} alt="" style={{ width: 24, height: 24, opacity: .7 }} />
      <span style={Object.assign({ textAlign: 'center' }, vcFontCss(style), { fontSize: 13 })}>{style.caption || w.widgetType}</span>
      <span style={{ fontSize: 11, color: 'var(--fg-medium)' }}>{w.widgetType + ' · view only'}</span>
      <VCDisabledVeil on={w.isDisabled} />
    </div>
  );
}

/** Dispatch on widgetType. `children` are the nested widgets of a frame. In edit mode the body is
    inert (pointer-events none) so the edit wrapper around it gets every gesture; nested widgets
    opt back in with their own pointer-events. */
function VCWidgetBody({ w, header, children }) {
  const vc = useVC();
  let body;
  switch (w.widgetType) {
    case 'Frame': case 'SoloFrame': body = <VCFrameBody w={w} header={header}>{children}</VCFrameBody>; break;
    case 'Button': body = <VCButtonBody w={w} />; break;
    case 'Slider': body = <VCSliderBody w={w} />; break;
    case 'CueList': body = <VCCueListBody w={w} />; break;
    case 'XYPad': body = <VCXYPadBody w={w} />; break;
    case 'Speed': case 'SpeedDial': body = <VCSpeedBody w={w} />; break;
    case 'Label': body = <VCLabelBody w={w} />; break;
    default: body = <VCViewOnlyBody w={w} />;
  }
  return <div style={{ position: 'absolute', inset: 0, pointerEvents: vc.edit ? 'none' : 'auto' }}>{body}</div>;
}

Object.assign(window, { VCWidgetBody, vcButtonState });
