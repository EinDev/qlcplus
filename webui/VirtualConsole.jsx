const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, QLCPlusFader, GenericButton, ShortcutHint, CustomSpinBox, SectionBox, SidePanel, CustomComboBox, CustomCheckBox } = window.PatchDesignSystem_5432c9;

const WIDGET_ICONS = { Button: 'button', Slider: 'slider', Frame: 'frame', SoloFrame: 'soloframe', Label: 'label', CueList: 'cuelist',
  XYPad: 'xypad', Speed: 'knob', SpeedDial: 'knob', Clock: 'clock', Animation: 'animation', AudioTriggers: 'audiotriggers' };
const PRESS = 'vc.button.press', SET_VALUE = 'vc.slider.setValue';

function fontCss(style) {
  const f = (style && style.font) || {};
  return { fontFamily: f.family ? '"' + f.family + '", var(--font-roboto)' : 'var(--font-roboto)', fontSize: (f.pointSize || 12) * 1.33,
    fontWeight: f.bold ? 700 : 400, fontStyle: f.italic ? 'italic' : 'normal', textDecoration: f.underline ? 'underline' : 'none' };
}

/* --- mock widgets (offline) ---------------------------------------------------------------- */
function VCSlider({ w, onChange }) {
  return (
    <div style={{ width: 74, background: 'var(--bg-strong)', border: 'var(--border-control)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4, padding: '6px 0' }}>
      <QLCPlusFader value={w.value} onMoved={onChange} height={180} />
      <RobotoText label={String(w.value)} fontSize={14} labelColor="var(--fg-light)" height={18} textHAlign="center" style={{ width: '100%' }} />
      <RobotoText label={w.label} fontSize={14} height={20} textHAlign="center" style={{ width: '100%' }} />
    </div>
  );
}
function VCButton({ w, onToggle }) {
  return (
    <button type="button" onClick={onToggle}
      style={{ width: 108, height: 60, cursor: 'pointer', padding: 4, background: w.on ? 'var(--highlight)' : 'var(--bg-control)',
        border: 'var(--border-control)', color: 'var(--fg-main)', font: '400 var(--text-size-small)/1.2 var(--font-roboto)' }}>{w.label}</button>
  );
}

/* --- live widgets (server geometry) -------------------------------------------------------- */

/** A widget as the server describes it, at its own geometry inside its parent. */
function LiveWidget({ w, children, qlc, sliderValues, setSliderValue, pressedMap, onPress, canPress, canSlide, onUnsupported }) {
  const D = window.QLCData;
  const g = w.geometry || { x: 0, y: 0, width: 100, height: 40 };
  const base = { position: 'absolute', left: g.x, top: g.y, width: g.width, height: g.height, boxSizing: 'border-box',
    opacity: w.isDisabled ? 0.45 : 1, zIndex: w.zIndex || 0 };
  const style = w.style || {};
  const caption = style.caption || '';
  const fg = style.foregroundColor || 'var(--fg-main)';
  const bg = style.backgroundColor || 'var(--bg-control)';
  const font = fontCss(style);

  if (w.widgetType === 'Frame' || w.widgetType === 'SoloFrame') {
    return (
      <div style={Object.assign({}, base, { background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid ' + (w.widgetType === 'SoloFrame' ? 'var(--override-red)' : 'var(--border-color-dark)'), overflow: 'hidden' })}>
        <div style={{ position: 'absolute', left: 0, top: 0, right: 0, height: 24, background: 'var(--section-header)', display: 'flex', alignItems: 'center', gap: 6, padding: '0 6px', pointerEvents: 'none' }}>
          <img src={D.icon(w.widgetType === 'SoloFrame' ? 'soloframe' : 'frame')} alt="" style={{ width: 16, height: 16 }} />
          <span style={Object.assign({ color: fg, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }, font, { fontSize: 14 })}>{caption}</span>
        </div>
        {children}
      </div>
    );
  }
  if (w.widgetType === 'Button') {
    const isOn = !!(pressedMap && pressedMap[w.id]);
    const down = (e) => { e.preventDefault(); if (!canPress) { onUnsupported(PRESS); return; } onPress(w.id, true); };
    const up = () => { if (canPress) onPress(w.id, false); };
    return (
      <div role="button" tabIndex={0} title={caption + (canPress ? '' : ' — press not supported by this server')}
        onPointerDown={down} onPointerUp={up} onPointerLeave={up} onPointerCancel={up}
        style={Object.assign({}, base, { background: isOn ? 'var(--highlight)' : bg, border: '2px solid ' + (isOn ? 'var(--check-lime)' : 'var(--border-color-dark)'),
          borderRadius: 4, display: 'grid', placeItems: 'center', textAlign: 'center', padding: 4, cursor: canPress ? 'pointer' : 'not-allowed', userSelect: 'none', color: fg, overflow: 'hidden' }, font)}>
        <span style={{ overflow: 'hidden', wordBreak: 'break-word', maxHeight: '100%' }}>{caption}</span>
      </div>
    );
  }
  if (w.widgetType === 'Slider') {
    const cfg = w.typeConfig || {};
    const v = sliderValues[w.id] != null ? sliderValues[w.id] : 0;
    const knob = cfg.widgetStyle === 'Knob';
    return (
      <div title={caption + (canSlide ? '' : ' — setValue not supported by this server')}
        style={Object.assign({}, base, { background: style.backgroundColor || 'var(--bg-strong)', border: '2px solid var(--border-color-dark)', borderRadius: 4, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4, padding: '6px 2px', color: fg, overflow: 'hidden' })}>
        <span style={{ fontSize: 12, fontFamily: 'var(--font-mono)' }}>{cfg.valueDisplayStyle === 'Percentage' ? Math.round(v / 2.55) + '%' : v}</span>
        <div style={{ flex: 1, minHeight: 20, display: 'flex', alignItems: 'center', justifyContent: 'center', width: '100%' }} onPointerDown={() => { if (!canSlide) onUnsupported(SET_VALUE); }}>
          <QLCPlusFader value={v} from={cfg.rangeLowLimit || 0} to={cfg.rangeHighLimit || 255} height={Math.max(40, g.height - 60)} width={knob ? 40 : 32}
            disabled={!canSlide} onMoved={(nv) => setSliderValue(w.id, nv)} />
        </div>
        <span style={Object.assign({ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', maxWidth: '100%' }, font, { fontSize: 13 })}>{caption}</span>
      </div>
    );
  }
  if (w.widgetType === 'Label') {
    return (
      <div style={Object.assign({}, base, { background: style.backgroundColor || 'transparent', color: fg, display: 'flex', alignItems: 'center', justifyContent: 'center', textAlign: 'center', overflow: 'hidden', border: style.backgroundColor ? 'none' : '1px dashed var(--border-color-dark)' }, font)}>
        {caption}
      </div>
    );
  }
  return (
    <div title={w.widgetType + ' — not interactive in the web UI yet (no ' + w.widgetType + ' interaction methods on the server)'}
      style={Object.assign({}, base, { background: bg, border: '2px dashed var(--fg-medium)', borderRadius: 4, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 4, color: fg, overflow: 'hidden', padding: 4 })}>
      <img src={D.icon(WIDGET_ICONS[w.widgetType] || 'frame')} alt="" style={{ width: 24, height: 24, opacity: .7 }} />
      <span style={Object.assign({ textAlign: 'center' }, font, { fontSize: 13 })}>{caption || w.widgetType}</span>
      <span style={{ fontSize: 11, color: 'var(--fg-medium)' }}>{w.widgetType} · view only</span>
    </div>
  );
}

/** Plain helper (no hooks — it is called conditionally): nests widgets by parentId and measures the page. */
function buildLivePage(widgets, common) {
  const byParent = {};
  widgets.forEach(w => { const p = w.parentId || 'root'; (byParent[p] = byParent[p] || []).push(w); });
  Object.keys(byParent).forEach(k => byParent[k].sort((a, b) => (a.zIndex || 0) - (b.zIndex || 0)));
  const render = (parentKey) => (byParent[parentKey] || []).filter(w => w.isVisible !== false).map(w => (
    <LiveWidget key={w.id} w={w} {...common}>{render(w.id)}</LiveWidget>
  ));
  let bw = 400, bh = 300;
  (byParent.root || []).forEach(x => { const g = x.geometry || {}; bw = Math.max(bw, (g.x || 0) + (g.width || 0)); bh = Math.max(bh, (g.y || 0) + (g.height || 0)); });
  return { content: render('root'), bounds: { width: bw + 20, height: bh + 20 } };
}

/** Grand Master fader: io.grandMaster.get / setValue + io.grandMaster.changed. */
function GrandMaster({ qlc }) {
  const [gm, setGm] = React.useState(null);
  React.useEffect(() => {
    if (!qlc.online) { setGm(null); return; }
    qlc.call('io.grandMaster.get').then(setGm).catch(() => {});
    return qlc.subscribeTo('io.grandMaster.changed', setGm);
  }, [qlc.online]);
  const move = (v) => { setGm(g => Object.assign({}, g, { value: v })); qlc.client().setGrandMaster(v); };
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4, padding: 8 }}>
      <QLCPlusFader value={gm ? gm.value : 255} onMoved={move} height={160} disabled={!gm} trackColor="var(--override-red)" />
      <RobotoText label={gm ? String(gm.value) : '—'} fontSize={14} height={18} textHAlign="center" style={{ width: '100%' }} />
      <RobotoText label={gm ? gm.channelMode + ' · ' + gm.valueMode : 'connect first'} fontSize={12} labelColor="var(--fg-medium)" height="auto" wrapText textHAlign="center" style={{ width: '100%' }} />
    </div>
  );
}

function VirtualConsole() {
  const D = window.QLCData;
  const qlc = useQLC();
  const live = qlc.online;
  const [mockWidgets, setMockWidgets] = React.useState(D.vcWidgets);
  const [edit, setEdit] = React.useState(false);
  const [panel, setPanel] = React.useState(false);
  const [pages, setPages] = React.useState(null);
  const [page, setPage] = React.useState(0);
  const [widgets, setWidgets] = React.useState(null);
  const [sliderValues, setSliderValues] = React.useState({});
  const [pressed, setPressed] = React.useState({});
  const [zoom, setZoom] = React.useState('fit');
  const [notice, setNotice] = React.useState('');
  const areaRef = React.useRef(null);
  const [areaSize, setAreaSize] = React.useState({ w: 800, h: 600 });
  const sendTimer = React.useRef({});

  /* Pages: list once per connection, follow page events. The page shown here is local — a
     remote should not flip the operator's screen, so vc.page.select is deliberately not called. */
  React.useEffect(() => {
    if (!live) { setPages(null); setWidgets(null); return; }
    let alive = true;
    const load = () => qlc.call('vc.page.list').then(r => { if (!alive) return; setPages(r.pages || []); setPage(p => (r.pages || []).some(x => x.index === p) ? p : (r.selectedPage || 0)); }).catch(() => {});
    load();
    const offs = ['vc.page.created', 'vc.page.deleted', 'vc.page.renamed', 'core.project.loaded'].map(t => qlc.subscribeTo(t, load));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [live]);

  /* Widgets of the shown page, refreshed (debounced) on every structural VC event. */
  React.useEffect(() => {
    if (!live) return;
    let alive = true, timer = null;
    const load = () => qlc.call('vc.widget.list', { page }).then(r => { if (alive) setWidgets(r.widgets || []); }).catch(() => {});
    const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
    setWidgets(null);
    load();
    const offs = ['vc.widget.created', 'vc.widget.deleted', 'vc.widget.updated', 'vc.widget.configChanged', 'vc.widget.repositioned', 'vc.page.deleted', 'core.project.loaded']
      .map(t => qlc.subscribeTo(t, debounced));
    /* Live value/state events per the spec — the server does not emit them yet, but if it starts, the UI follows. */
    offs.push(qlc.subscribeTo('vc.slider.valueChanged', d => { if (d && d.widgetId != null) setSliderValues(s => Object.assign({}, s, { [d.widgetId]: d.value })); }));
    offs.push(qlc.subscribeTo('vc.button.stateChanged', d => { if (d && d.widgetId != null) setPressed(s => Object.assign({}, s, { [d.widgetId]: d.state === 'Active' || d.active === true || d.pressed === true })); }));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [live, page]);

  React.useEffect(() => {
    const el = areaRef.current;
    if (!el) return;
    const measure = () => setAreaSize({ w: el.clientWidth, h: el.clientHeight });
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    return () => ro.disconnect();
  }, [live]);

  const canPress = live && !qlc.isUnsupported(PRESS);
  const canSlide = live && !qlc.isUnsupported(SET_VALUE);
  const onUnsupported = (m) => setNotice('This server has no ' + m + ' yet — Virtual Console widgets are view-only until the Control API grows live interaction.');
  const onPress = (id, down) => {
    setPressed(p => Object.assign({}, p, { [id]: down }));
    qlc.client().pressButton(id, down).catch(e => { if (e.code === 'NOT_FOUND') onUnsupported(PRESS); });
  };
  const setSliderValue = (id, v) => {
    setSliderValues(s => Object.assign({}, s, { [id]: v }));
    /* Throttle a drag to ~30 updates/s per slider; last value always wins. */
    const t = sendTimer.current[id];
    if (t) { t.value = v; return; }
    sendTimer.current[id] = { value: v };
    qlc.client().setSliderValue(id, v).catch(e => { if (e.code === 'NOT_FOUND') onUnsupported(SET_VALUE); });
    setTimeout(() => {
      const last = sendTimer.current[id];
      delete sendTimer.current[id];
      if (last && last.value !== v) qlc.client().setSliderValue(id, last.value).catch(() => {});
    }, 33);
  };
  const common = { qlc, sliderValues, setSliderValue, pressedMap: pressed, onPress, canPress, canSlide, onUnsupported };

  const setMock = (id, patch) => setMockWidgets(p => p.map(w => w.id === id ? Object.assign({}, w, patch) : w));
  const livePage = live && widgets ? buildLivePage(widgets, common) : null;
  const scale = livePage ? (zoom === 'fit' ? Math.min(1, (areaSize.w - 24) / livePage.bounds.width, (areaSize.h - 24) / livePage.bounds.height) : Number(zoom)) : 1;
  const interaction = live ? (qlc.isUnsupported(PRESS) || qlc.isUnsupported(SET_VALUE) ? 'view-only: server lacks ' + [qlc.isUnsupported(PRESS) ? PRESS : null, qlc.isUnsupported(SET_VALUE) ? SET_VALUE : null].filter(Boolean).join(', ') : 'buttons/sliders send vc.button.press / vc.slider.setValue') : '';

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <ShortcutHint keys="Ctrl L" placement="corner">
          <IconButton imgSource={edit ? D.icon('unlock') : D.icon('lock')} size={26} checked={edit}
            onClick={() => setEdit(!edit)} tooltip={edit ? 'Lock editing' : 'Unlock editing (layout editing is not available in the web UI yet)'} />
        </ShortcutHint>
        <IconButton imgSource={D.icon('frame')} size={26} tooltip="Add frame — not available in the web UI yet" disabled />
        <IconButton imgSource={D.icon('button')} size={26} tooltip="Add button — not available in the web UI yet" disabled />
        <IconButton imgSource={D.icon('slider')} size={26} tooltip="Add slider — not available in the web UI yet" disabled />
        <IconButton imgSource={D.icon('xypad')} size={26} tooltip="Add XY pad — not available in the web UI yet" disabled />
        <IconButton imgSource={D.icon('network')} size={26} disabled={!live}
          tooltip={live ? 'Reload pages and widgets from the desk' : 'Not connected'}
          onClick={() => { qlc.call('vc.page.list').then(r => setPages(r.pages || [])).catch(() => {}); qlc.call('vc.widget.list', { page }).then(r => setWidgets(r.widgets || [])).catch(() => {}); }} />
        {live ? (
          <CustomComboBox width={90} height={26} currValue={zoom} onValueChanged={setZoom}
            model={[{ mLabel: 'Fit', mValue: 'fit' }, { mLabel: '50%', mValue: '0.5' }, { mLabel: '75%', mValue: '0.75' }, { mLabel: '100%', mValue: '1' }]} />
        ) : null}
        <ToolbarSpacer />
        <RobotoText label={live ? (widgets ? widgets.length + ' widgets on this page · ' + interaction : 'Loading…') : 'Offline — local preview'} fontSize={14}
          labelColor={live ? (qlc.isUnsupported(PRESS) ? 'var(--selection)' : 'var(--check-lime)') : 'var(--fg-medium)'} />
      </ViewToolbar>

      {live && pages ? (
        <div style={{ display: 'flex', gap: 2, padding: '4px 6px 0', background: 'var(--bg-strong)', borderBottom: 'var(--border-dark)', overflowX: 'auto', flex: 'none' }}>
          {pages.map(p => (
            <button key={p.index} type="button" onClick={() => setPage(p.index)} title={'Page ' + (p.index + 1) + (p.hasPin ? ' (PIN protected in the desktop app)' : '')}
              style={{ flex: 'none', height: 26, padding: '0 10px', cursor: 'pointer', border: 'var(--border-control)', borderBottom: 'none',
                background: p.index === page ? 'var(--highlight)' : 'var(--bg-control)', color: 'var(--fg-main)', font: '400 13px var(--font-roboto)', whiteSpace: 'nowrap' }}>
              {p.hasPin ? '🔒 ' : ''}{p.name || 'Page ' + (p.index + 1)}
            </button>
          ))}
        </div>
      ) : null}
      {notice ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '4px 10px', background: 'var(--bg-strong)', borderBottom: '2px solid var(--selection)', flex: 'none' }}>
          <RobotoText label={notice} fontSize={13} labelColor="var(--selection)" wrapText height="auto" style={{ flex: 1 }} />
          <GenericButton label="Dismiss" width={80} height={22} fontSize={12} onClick={() => setNotice('')} />
        </div>
      ) : null}

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div ref={areaRef} style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 12, background: 'var(--bg-medium)', position: 'relative' }}>
          {live ? (
            livePage ? (
              <div style={{ width: livePage.bounds.width * scale, height: livePage.bounds.height * scale, position: 'relative' }}>
                <div style={{ position: 'absolute', left: 0, top: 0, width: livePage.bounds.width, height: livePage.bounds.height, transform: 'scale(' + scale + ')', transformOrigin: '0 0',
                  background: 'var(--bg-stronger)', border: edit ? '2px dashed var(--bg-light)' : 'var(--border-control)' }}>
                  {livePage.content}
                  {!widgets.length ? <div style={{ padding: 20 }}><RobotoText label="This page has no widgets." fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
                </div>
              </div>
            ) : <RobotoText label="Loading widgets…" fontSize={14} labelColor="var(--fg-medium)" />
          ) : (
            <div style={{ display: 'inline-flex', flexDirection: 'column', gap: 10, padding: 10, border: edit ? '2px dashed var(--bg-light)' : 'var(--border-control)', background: 'var(--bg-stronger)' }}>
              <RobotoText label="Main frame (mock)" fontSize={14} labelColor="var(--fg-medium)" height={20} />
              <div style={{ display: 'flex', gap: 8, alignItems: 'flex-start' }}>
                {mockWidgets.filter(w => w.kind === 'slider').map(w => (
                  <VCSlider key={w.id} w={w} onChange={v => setMock(w.id, { value: v })} />
                ))}
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2,auto)', gap: 6, alignContent: 'start' }}>
                  {mockWidgets.filter(w => w.kind === 'button').map(w => (
                    <VCButton key={w.id} w={w} onToggle={() => setMock(w.id, { on: !w.on })} />
                  ))}
                </div>
              </div>
            </div>
          )}
        </div>

        <SidePanel isOpen={panel} alignment="right" rail={
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
            <IconButton imgSource={D.icon('sliders')} checked={panel} onClick={() => setPanel(!panel)} tooltip="Grand Master" />
            <IconButton imgSource={D.icon('configure')} disabled tooltip="Widget properties — not available in the web UI yet" />
            <IconButton imgSource={D.icon('keybinding')} disabled tooltip="Key bindings — not available in the web UI yet" />
          </div>}>
          <div>
            <SectionBox sectionLabel="Grand Master" isExpanded>
              <GrandMaster qlc={qlc} />
            </SectionBox>
          </div>
        </SidePanel>
      </div>
    </div>
  );
}
Object.assign(window, { VirtualConsole, VCSlider, VCButton });
