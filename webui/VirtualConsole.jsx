/**
 * Virtual Console screen: pages, widgets at their real geometry, live interaction (buttons,
 * sliders, cue lists, XY pads, speed dials, multipage frames) and — in Design mode — layout editing
 * (add / move / resize / configure / copy / paste / delete, page add / rename / delete).
 *
 * Structure: this file owns the data (pages, widgets, the live value store, edit state) and the
 * layout; vc/vc-widgets.jsx draws each widget type; vc/vc-edit.jsx is the selection / drag / resize
 * wrapper plus the palette and the properties panel; vc/vc-shared.jsx holds the context and the
 * pointer-event fader. Everything reaches the screen state through VCContext.
 *
 * Wire notes (verified against this fork's server, see docs/agent-reports/2026-09-26-*):
 *  - vc.widget.list returns full snapshots (typeConfig included) on this server.
 *  - `page` on a widget is the top-level page only; children of a multipage frame are told apart
 *    by isVisible, so a frame page change re-fetches the list.
 *  - The live methods/events (press, setValue, cueList.*, xyPad, speedDial, frame.gotoPage,
 *    *Changed) are a contract still being implemented server-side: every call degrades to a
 *    notice + view-only via the client's NOT_FOUND tracking when the running server lacks it.
 */
const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, QLCPlusFader, GenericButton, ShortcutHint, CustomComboBox, SectionBox, SidePanel, MenuBarEntry, CustomPopupDialog, CustomTextInput, FaIcon } = window.PatchDesignSystem_5432c9;

/* core.history.changed is in both lists: undo/redo emit no domain events, so the screen re-reads itself. */
const WIDGET_REFRESH_TOPICS = ['vc.widget.created', 'vc.widget.deleted', 'vc.widget.updated', 'vc.widget.configChanged', 'vc.widget.bulkUpdated', 'vc.page.deleted', 'core.project.loaded', 'core.history.changed'];
const PAGE_REFRESH_TOPICS = ['vc.page.created', 'vc.page.deleted', 'vc.page.renamed', 'vc.page.updated', 'core.project.loaded', 'core.history.changed'];
const NO_FUNCTION_ID = '4294967295';

/* --- mock widgets (offline preview) -------------------------------------------------------- */
function VCSlider({ w, onChange }) {
  return (
    <div style={{ width: 74, background: 'var(--bg-strong)', border: 'var(--border-control)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4, padding: '6px 0' }}>
      <VCFader value={w.value} onMoved={onChange} height={180} />
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

/* --- live value store ---------------------------------------------------------------------- */
function normalizeXY(v) { const n = Number(v) || 0; return vcClamp(n > 1 ? n / 256 : n, 0, 1); }
function normalizePlayback(d) {
  const out = {};
  if (d.playbackIndex != null) out.playbackIndex = Number(d.playbackIndex);
  if (d.running != null || d.paused != null) { out.running = !!d.running; out.paused = !!d.paused; }
  else if (d.playbackStatus != null) { out.running = d.playbackStatus !== 'Stopped'; out.paused = d.playbackStatus === 'Paused'; }
  if (d.steps) out.steps = d.steps;
  return out;
}

/** Seed the store from the fields vc.widget.list items carry (state, value, playbackIndex, x/y, ms, currentPage/pages). */
function seedFromWidgets(store, widgets) {
  const next = { buttons: Object.assign({}, store.buttons), sliders: Object.assign({}, store.sliders), cueLists: Object.assign({}, store.cueLists),
    xy: Object.assign({}, store.xy), speed: Object.assign({}, store.speed), frames: Object.assign({}, store.frames) };
  widgets.forEach(w => {
    switch (w.widgetType) {
      case 'Button': if (w.state != null) next.buttons[w.id] = w.state; break;
      case 'Slider': if (w.value != null) next.sliders[w.id] = Number(w.value); break;
      case 'CueList': next.cueLists[w.id] = Object.assign({}, next.cueLists[w.id], normalizePlayback(w)); break;
      case 'XYPad': if (w.x != null && w.y != null) next.xy[w.id] = { x: normalizeXY(w.x), y: normalizeXY(w.y) }; break;
      case 'Speed': case 'SpeedDial': if (w.ms != null) next.speed[w.id] = Number(w.ms); break;
      case 'Frame': case 'SoloFrame':
        if (w.currentPage != null || w.pages != null || w.multipage != null)
          next.frames[w.id] = Object.assign({}, next.frames[w.id], { currentPage: w.currentPage, pages: w.pages, multipage: w.multipage });
        break;
      default: break;
    }
  });
  return next;
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
      <VCFader value={gm ? gm.value : 255} onMoved={move} height={160} disabled={!gm} trackColor="var(--override-red)" />
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
  const [mode, setMode] = React.useState(null);
  const [edit, setEdit] = React.useState(false);
  const [snap, setSnap] = React.useState(true);
  const [panel, setPanel] = React.useState(null);
  const [pages, setPages] = React.useState(null);
  const [page, setPage] = React.useState(0);
  const [widgets, setWidgets] = React.useState(null);
  const [store, setStore] = React.useState({ buttons: {}, sliders: {}, cueLists: {}, xy: {}, speed: {}, frames: {} });
  const [zoom, setZoom] = React.useState('fit');
  const [notice, setNotice] = React.useState('');
  const [selection, setSelection] = React.useState([]);
  const [dragGeom, setDragGeom] = React.useState({});
  const [placing, setPlacing] = React.useState(null);
  const [clipboard, setClipboard] = React.useState([]);
  const [functions, setFunctions] = React.useState([]);
  const [dialog, setDialog] = React.useState(null);
  /* PIN unlocks are per browser session (vc.page/frame.validatePin are stateless server-side): 'page:<index>' / 'frame:<id>'. */
  const [unlocked, setUnlocked] = React.useState({});
  const areaRef = React.useRef(null);
  const canvasRef = React.useRef(null);
  const [areaSize, setAreaSize] = React.useState({ w: 800, h: 600 });
  const throttled = useThrottledSender(33);
  const dragging = React.useRef({});
  const selectionRef = React.useRef([]);
  const dragGeomRef = React.useRef({});
  const widgetsRef = React.useRef([]);
  const refreshRef = React.useRef(() => {});
  selectionRef.current = selection;
  dragGeomRef.current = dragGeom;
  widgetsRef.current = widgets || [];

  const patchStore = React.useCallback((section, id, value) => {
    setStore(s => Object.assign({}, s, { [section]: Object.assign({}, s[section], { [id]: typeof value === 'function' ? value(s[section][id]) : value }) }));
  }, []);
  const say = React.useCallback((text) => setNotice(text), []);
  /* NOT_FOUND "Unknown method" = server predates the method (view-only). Everything else — NOT_FOUND
     for an unknown widget, INVALID_PARAMS (wrong widget type), INVALID_STATE (no Function attached,
     disabled widget, cue list without a Chaser) — is a real answer and is shown as such. */
  const notFound = (method) => (e) => {
    if (!e || e.code === 'NOT_CONNECTED') return;
    if (e.code === 'NOT_FOUND' && /^Unknown method/.test(e.message || '')) say('This server has no ' + method + ' yet');
    else say(method + ': ' + (e.message || e.code || 'failed') + (e.details && e.details.widgetType ? ' (' + e.details.widgetType + ')' : ''));
  };

  /* Engine mode gates editing (AC_VCEditing in the QML app = Design mode only). */
  React.useEffect(() => {
    if (!live) { setMode(null); setEdit(false); return; }
    qlc.call('core.mode.get').then(r => setMode(r.mode)).catch(() => {});
    return qlc.subscribeTo('core.mode.changed', (d) => setMode(d.mode));
  }, [live]);
  React.useEffect(() => { if (mode === 'operate' && edit) { setEdit(false); say('Engine switched to Operate mode — editing locked'); } }, [mode]);
  React.useEffect(() => {
    if (!edit) { setPlacing(null); setSelection([]); if (panel === 'palette' || panel === 'props') setPanel(null); return; }
    if (panel == null) setPanel('palette');
    if (live && !functions.length) qlc.call('functions.list').then(r => setFunctions(r.functions || [])).catch(() => {});
  }, [edit]);

  /* Pages: list once per connection, follow page events. The page shown here is local — a
     remote should not flip the operator's screen, so vc.page.select is deliberately not called. */
  React.useEffect(() => {
    if (!live) { setPages(null); setWidgets(null); setUnlocked({}); return; }
    let alive = true;
    const load = () => qlc.call('vc.page.list').then(r => { if (!alive) return; setPages(r.pages || []); setPage(p => (r.pages || []).some(x => x.index === p) ? p : (r.selectedPage || 0)); }).catch(() => {});
    load();
    const offs = PAGE_REFRESH_TOPICS.map(t => qlc.subscribeTo(t, load));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [live]);

  /* Widgets of the shown page, refreshed (debounced) on every structural VC event. */
  React.useEffect(() => {
    if (!live) return;
    let alive = true, timer = null;
    const load = () => qlc.call('vc.widget.list', { page }).then(r => {
      if (!alive) return;
      const list = r.widgets || [];
      setWidgets(list);
      setDragGeom({});
      setStore(s => seedFromWidgets(s, list));
      setSelection(sel => sel.filter(id => list.some(w => w.id === id)));
    }).catch(() => {});
    const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
    refreshRef.current = debounced;
    setWidgets(null);
    load();
    const offs = WIDGET_REFRESH_TOPICS.map(t => qlc.subscribeTo(t, debounced));
    offs.push(qlc.subscribeTo('vc.widget.repositioned', (d) => {
      const moved = {};
      ((d && d.widgets) || []).forEach(x => { moved[x.widgetId] = x.geometry; });
      setWidgets(ws => ws ? ws.map(w => moved[w.id] ? Object.assign({}, w, { geometry: moved[w.id] }) : w) : ws);
    }));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [live, page]);

  /* Live value events, once per connection (whatever page is shown). */
  React.useEffect(() => {
    if (!live) return;
    const offs = [
      qlc.subscribeTo('vc.button.stateChanged', d => { if (d && d.widgetId != null) patchStore('buttons', d.widgetId, d.state != null ? d.state : (d.active || d.pressed ? 'active' : 'inactive')); }),
      qlc.subscribeTo('vc.slider.valueChanged', d => { if (d && d.widgetId != null && !dragging.current[d.widgetId]) patchStore('sliders', d.widgetId, Number(d.value)); }),
      qlc.subscribeTo('vc.cueList.playbackChanged', d => { if (d && d.widgetId != null) patchStore('cueLists', d.widgetId, (c) => Object.assign({}, c, normalizePlayback(d))); }),
      qlc.subscribeTo('vc.xyPad.positionChanged', d => { if (d && d.widgetId != null && !dragging.current['xy:' + d.widgetId]) patchStore('xy', d.widgetId, { x: normalizeXY(d.x), y: normalizeXY(d.y) }); }),
      qlc.subscribeTo('vc.speedDial.valueChanged', d => { if (d && d.widgetId != null) patchStore('speed', d.widgetId, Number(d.ms)); }),
      qlc.subscribeTo('vc.speedDial.currentTimeChanged', d => { if (d && d.widgetId != null) patchStore('speed', d.widgetId, Number(d.currentTimeMs != null ? d.currentTimeMs : d.ms)); }),
      qlc.subscribeTo('functions.chaser.stepsChanged', () => { widgetsRef.current.filter(w => w.widgetType === 'CueList').forEach(w => act.cueGet(w.id)); })
    ];
    const framePage = d => {
      if (!d || d.widgetId == null) return;
      const p = d.page != null ? d.page : (d.pageIndex != null ? d.pageIndex : d.currentPage);
      patchStore('frames', d.widgetId, (f) => Object.assign({}, f, { currentPage: Number(p) }));
      refreshRef.current();
    };
    offs.push(qlc.subscribeTo('vc.frame.pageChanged', framePage), qlc.subscribeTo('vc.frame.currentPageChanged', framePage));
    return () => offs.forEach(f => f());
  }, [live]);

  React.useEffect(() => {
    const el = areaRef.current;
    if (!el) return;
    const measure = () => setAreaSize({ w: el.clientWidth, h: el.clientHeight });
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    return () => ro.disconnect();
  }, [live]);

  /* --- live actions ------------------------------------------------------------------------ */
  const act = React.useMemo(() => ({
    press: (id, down) => qlc.call(VC_METHODS.PRESS, { widgetId: String(id), pressed: !!down }).catch(notFound(VC_METHODS.PRESS)),
    slide: (id, v) => { patchStore('sliders', id, v); throttled('s:' + id, () => qlc.call(VC_METHODS.SET_VALUE, { widgetId: String(id), value: Math.round(v) }).catch(notFound(VC_METHODS.SET_VALUE))); },
    sliderPress: (id, on) => { if (on) dragging.current[id] = true; else delete dragging.current[id]; },
    cueGet: (id) => { if (qlc.isUnsupported(VC_METHODS.CUE_GET)) return; qlc.call(VC_METHODS.CUE_GET, { widgetId: String(id) }).then(r => patchStore('cueLists', id, (c) => Object.assign({}, c, normalizePlayback(r || {}), { steps: (r && r.steps) || [] }))).catch(() => {}); },
    cuePlay: (id) => qlc.call('vc.cueList.play', { widgetId: String(id) }).catch(notFound('vc.cueList.play')),
    cueStop: (id) => qlc.call('vc.cueList.stop', { widgetId: String(id) }).catch(notFound('vc.cueList.stop')),
    cueNext: (id) => qlc.call('vc.cueList.next', { widgetId: String(id) }).catch(notFound('vc.cueList.next')),
    cuePrev: (id) => qlc.call('vc.cueList.previous', { widgetId: String(id) }).catch(notFound('vc.cueList.previous')),
    cueJump: (id, index) => qlc.call('vc.cueList.setPlaybackIndex', { widgetId: String(id), index: index, playbackIndex: index }).catch(notFound('vc.cueList.setPlaybackIndex')),
    xySet: (id, x, y) => { dragging.current['xy:' + id] = true; patchStore('xy', id, { x, y }); throttled('xy:' + id, () => qlc.call(VC_METHODS.XY_SET, { widgetId: String(id), x: Math.round(x * 10000) / 10000, y: Math.round(y * 10000) / 10000 }).catch(notFound(VC_METHODS.XY_SET))); },
    xyRelease: (id) => { delete dragging.current['xy:' + id]; },
    speedSet: (id, ms) => { patchStore('speed', id, ms); qlc.call(VC_METHODS.SPEED_SET, { widgetId: String(id), ms: ms }).catch(notFound(VC_METHODS.SPEED_SET)); },
    speedTap: (id) => qlc.call(VC_METHODS.SPEED_TAP, { widgetId: String(id) }).catch(notFound(VC_METHODS.SPEED_TAP)),
    /* A frame page flip is persisted by the engine and bumps docRevision, but neither the ack nor
       vc.frame.pageChanged carries the new value — re-read it via core.project.get so the next
       structural edit does not start with a CONFLICT (vcStructural would retry once anyway). */
    frameGoto: (id, p) => { patchStore('frames', id, (f) => Object.assign({}, f, { currentPage: p })); qlc.client().vc.frame.gotoPage(String(id), p).then(() => { refreshRef.current(); qlc.call('core.project.get').catch(() => {}); }).catch(notFound(VC_METHODS.FRAME_GOTO)); }
  }), [qlc, throttled]);

  /* --- edit actions ------------------------------------------------------------------------ */
  const byId = React.useMemo(() => { const m = {}; (widgets || []).forEach(w => { m[w.id] = w; }); return m; }, [widgets]);
  const geometryOf = (id) => dragGeomRef.current[id] || (byId[id] && byId[id].geometry) || { x: 0, y: 0, width: 50, height: 50 };
  const structural = (method, params) => vcStructural(qlc, method, params).catch(e => { say(method + ': ' + ((e && e.message) || 'failed')); throw e; });
  const editApi = React.useMemo(() => ({
    snap,
    geometryOf,
    selectFor: (id, additive, forResize) => {
      let sel = selectionRef.current;
      if (forResize) sel = [id];
      else if (additive) sel = sel.indexOf(id) !== -1 ? sel.filter(x => x !== id) : sel.concat([id]);
      else if (sel.indexOf(id) === -1) sel = [id];
      selectionRef.current = sel;
      setSelection(sel);
      setPlacing(null);
      if (sel.length && panel !== 'props') setPanel('props');
      return sel.indexOf(id) !== -1 ? sel : [];
    },
    setDragGeom: (map) => { dragGeomRef.current = Object.assign({}, dragGeomRef.current, map); setDragGeom(dragGeomRef.current); },
    commitDrag: () => {
      const moved = dragGeomRef.current;
      const ids = Object.keys(moved);
      if (!ids.length) return;
      editApi.reposition(ids.map(id => ({ widgetId: id, geometry: vcRoundGeom(moved[id]) })));
    },
    reposition: (list) => {
      const groups = {};
      list.forEach(x => { const p = (byId[x.widgetId] && byId[x.widgetId].parentId) || 'root'; (groups[p] = groups[p] || []).push(x); });
      return Promise.all(Object.values(groups).map(g => structural('vc.widget.reposition', { widgets: g })))
        .then(() => {
          const map = {}; list.forEach(x => { map[x.widgetId] = x.geometry; });
          setWidgets(ws => ws ? ws.map(w => map[w.id] ? Object.assign({}, w, { geometry: map[w.id] }) : w) : ws);
          dragGeomRef.current = {}; setDragGeom({});
        })
        .catch(() => { dragGeomRef.current = {}; setDragGeom({}); });
    },
    updateWidget: (id, patch) => structural('vc.widget.update', Object.assign({ widgetId: String(id) }, patch)).then(() => {
      if (patch.style) setWidgets(ws => ws ? ws.map(w => w.id === id ? Object.assign({}, w, { style: Object.assign({}, w.style, patch.style) }) : w) : ws);
    }).catch(() => {}),
    setConfig: (id, config) => structural('vc.widget.setConfig', { widgetId: String(id), config }).then(() => {
      setWidgets(ws => ws ? ws.map(w => w.id === id ? Object.assign({}, w, { typeConfig: Object.assign({}, w.typeConfig, config) }) : w) : ws);
    }).catch(() => {}),
    deleteWidgets: (ids) => structural('vc.widget.delete', { widgetIds: ids.map(String) }).then(() => { setSelection([]); refreshRef.current(); }).catch(() => {}),
    create: (params) => structural('vc.widget.create', Object.assign({ page }, params)).then(r => { refreshRef.current(); if (r && r.widgetId != null) { setSelection([String(r.widgetId)]); setPanel('props'); } return r; }),
    /* Layout / configuration slice (vc/vc-props-layout.jsx drives these from the toolbar and the frame properties). */
    bulkStyle: (ids, style) => structural('vc.widget.bulkStyle', Object.assign({ widgetIds: ids.map(String) }, style)).then(() => {
      setWidgets(ws => ws ? ws.map(w => ids.indexOf(w.id) !== -1 ? Object.assign({}, w, { style: Object.assign({}, w.style, style) }) : w) : ws);
    }).catch(() => {}),
    setLevelChannels: (id, channels) => structural('vc.slider.setLevelChannels', { widgetId: String(id), channels }).then(() => {
      setWidgets(ws => ws ? ws.map(w => w.id === id ? Object.assign({}, w, { typeConfig: Object.assign({}, w.typeConfig, { levelChannels: channels }) }) : w) : ws);
    }).catch(() => {}),
    align: (ids, referenceWidgetId, alignment) => structural('vc.widget.align', { widgetIds: ids.map(String), referenceWidgetId: String(referenceWidgetId), alignment }).then(() => refreshRef.current()).catch(() => {}),
    distribute: (ids, direction) => structural('vc.widget.distribute', { widgetIds: ids.map(String), direction }).then(() => refreshRef.current()).catch(() => {}),
    createFromFunctions: (params) => structural('vc.widget.createFromFunctions', Object.assign({ page }, params)).then(r => { refreshRef.current(); if (r && r.widgetIds) setSelection(r.widgetIds.map(String)); return r; }),
    createMatrix: (params) => structural('vc.widget.createMatrix', Object.assign({ page }, params)).then(r => { refreshRef.current(); if (r && r.widgetIds && r.widgetIds.length) setSelection([String(r.widgetIds[0])]); return r; }),
    frameSetPin: (id, currentPIN, newPIN) => structural('vc.frame.setPin', { widgetId: String(id), currentPIN: currentPIN || '', newPIN: newPIN || '' }).then(() => refreshRef.current()),
    cloneFirstPage: (id) => structural('vc.frame.cloneFirstPage', { widgetId: String(id) }).then(() => refreshRef.current()).catch(() => {})
  }), [snap, byId, page, panel, qlc]);

  /* PIN prompts (page and frame): a correct PIN unlocks for this browser session only. */
  const pin = React.useMemo(() => ({
    isUnlocked: (kind, key) => !!unlocked[kind + ':' + key],
    unlock: (kind, key) => setUnlocked(u => Object.assign({}, u, { [kind + ':' + key]: true })),
    validatePage: (index, value) => qlc.call('vc.page.validatePin', { index, pin: String(value) }).then(r => !!(r && r.valid)),
    validateFrame: (id, value) => qlc.call('vc.frame.validatePin', { widgetId: String(id), pin: String(value) }).then(r => !!(r && r.valid))
  }), [unlocked, qlc]);
  const openPage = (index) => {
    const p = (pages || []).find(x => x.index === index);
    if (p && p.hasPin && !pin.isUnlocked('page', index)) { setDialog({ kind: 'pagePin', index }); return; }
    setPage(index); setSelection([]);
  };

  /** Absolute page position of a widget (geometry is parent-relative). */
  const absoluteOf = (w) => { let x = 0, y = 0, cur = w; while (cur) { const g = dragGeom[cur.id] || cur.geometry || {}; x += g.x || 0; y += g.y || 0; cur = cur.parentId ? byId[cur.parentId] : null; } return { x, y }; };
  const scale = React.useRef(1);

  const placeAt = (e) => {
    if (!placing || !canvasRef.current) return;
    const r = canvasRef.current.getBoundingClientRect();
    const px = (e.clientX - r.left) / scale.current, py = (e.clientY - r.top) / scale.current;
    /* Deepest visible frame under the pointer becomes the parent. */
    let parent = null, depth = -1;
    (widgets || []).forEach(w => {
      if ((w.widgetType !== 'Frame' && w.widgetType !== 'SoloFrame') || w.isVisible === false) return;
      const a = absoluteOf(w), g = dragGeom[w.id] || w.geometry;
      if (px >= a.x && py >= a.y && px <= a.x + g.width && py <= a.y + g.height) {
        let d = 0, cur = w; while (cur && cur.parentId) { d++; cur = byId[cur.parentId]; }
        if (d > depth) { depth = d; parent = w; }
      }
    });
    const origin = parent ? absoluteOf(parent) : { x: 0, y: 0 };
    const geometry = vcRoundGeom({ x: Math.max(0, vcSnapTo(px - origin.x, snap)), y: Math.max(0, vcSnapTo(py - origin.y, snap)), width: placing.size.width, height: placing.size.height });
    const params = { widgetType: placing.create, geometry, style: { caption: placing.name } };
    if (parent) params.parentId = String(parent.id);
    if (placing.typeConfig) params.typeConfig = placing.typeConfig;
    const item = placing;
    setPlacing(null);
    editApi.create(params).then(() => say(item.name + ' added')).catch(() => {});
  };

  const copySelection = () => { const items = selection.map(id => byId[id]).filter(Boolean); if (items.length) { setClipboard(items.map(w => JSON.parse(JSON.stringify(w)))); say(items.length + ' widget' + (items.length > 1 ? 's' : '') + ' copied'); } };
  const paste = () => {
    if (!clipboard.length) return;
    const offset = VC_SNAP * 2;
    Promise.all(clipboard.map(w => {
      const params = { widgetType: w.widgetType, geometry: vcRoundGeom({ x: (w.geometry.x || 0) + offset, y: (w.geometry.y || 0) + offset, width: w.geometry.width, height: w.geometry.height }),
        style: { caption: w.style && w.style.caption, backgroundColor: w.style && w.style.backgroundColor, foregroundColor: w.style && w.style.foregroundColor, font: w.style && w.style.font } };
      if (w.parentId && byId[w.parentId]) params.parentId = String(w.parentId);
      if ((w.widgetType === 'Button' || w.widgetType === 'Slider') && w.typeConfig) params.typeConfig = w.typeConfig;
      return editApi.create(params);
    })).then(rs => { setSelection(rs.map(r => String(r.widgetId))); say('Pasted ' + rs.length + ' widget' + (rs.length > 1 ? 's' : '') + (clipboard.some(w => w.widgetType === 'Frame' || w.widgetType === 'SoloFrame') ? ' (frame contents are not copied)' : '')); }).catch(() => {});
  };

  /* Keyboard: Delete removes, Ctrl+C / Ctrl+V copy / paste, Escape clears — edit mode only, never while typing. */
  React.useEffect(() => {
    const typing = () => { const el = document.activeElement; return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable); };
    const k = (e) => {
      if (e.ctrlKey && e.key.toLowerCase() === 'l' && live) { e.preventDefault(); if (mode === 'design') setEdit(v => !v); else say('Switch the engine to Design mode to edit the Virtual Console'); return; }
      if (!edit || typing()) return;
      if (e.key === 'Escape') { setPlacing(null); setSelection([]); }
      else if (e.key === 'Delete' && selection.length) { setDialog({ kind: 'deleteWidgets' }); }
      else if (e.ctrlKey && e.key.toLowerCase() === 'c') { e.preventDefault(); copySelection(); }
      else if (e.ctrlKey && e.key.toLowerCase() === 'v') { e.preventDefault(); paste(); }
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, [edit, live, mode, selection, clipboard, byId]);

  /* --- render ------------------------------------------------------------------------------ */
  const setMock = (id, patch) => setMockWidgets(p => p.map(w => w.id === id ? Object.assign({}, w, patch) : w));
  const canEdit = live && mode === 'design';
  const currentPage = pages ? pages.find(p => p.index === page) : null;

  let content = null, bounds = { width: 400, height: 300 };
  if (live && widgets) {
    const byParent = {};
    widgets.forEach(w => { const p = w.parentId || 'root'; (byParent[p] = byParent[p] || []).push(w); });
    Object.keys(byParent).forEach(k => byParent[k].sort((a, b) => (a.zIndex || 0) - (b.zIndex || 0)));
    const render = (parentKey) => (byParent[parentKey] || []).filter(w => w.isVisible !== false).map(w => {
      const g = dragGeom[w.id] || w.geometry || { x: 0, y: 0, width: 100, height: 40 };
      const box = { position: 'absolute', left: g.x, top: g.y, width: g.width, height: g.height, boxSizing: 'border-box', zIndex: w.zIndex || 0 };
      /* VcFrameConfig.showHeader when the server exposes it; otherwise a frame whose children start
         inside the 26px header band runs headerless in the desktop app. A collapsed frame shows only
         its header (VCFrameItem.qml hides the whole body). */
      const fcfg = w.typeConfig || {};
      const header = typeof fcfg.showHeader === 'boolean' ? fcfg.showHeader : !(byParent[w.id] || []).some(c => c.geometry && c.geometry.y < 26);
      const body = <VCWidgetBody w={w} header={header}>{fcfg.isCollapsed ? null : render(w.id)}</VCWidgetBody>;
      return edit
        ? <VCEditable key={w.id} w={w} box={box} selected={selection.indexOf(w.id) !== -1}>{body}</VCEditable>
        : <div key={w.id} style={box} data-vc-widget={w.id} data-vc-type={w.widgetType}>{body}</div>;
    });
    content = render('root');
    let bw = 400, bh = 300;
    (byParent.root || []).forEach(x => { const g = dragGeom[x.id] || x.geometry || {}; bw = Math.max(bw, (g.x || 0) + (g.width || 0)); bh = Math.max(bh, (g.y || 0) + (g.height || 0)); });
    bounds = { width: bw + 20, height: bh + 20 };
  }
  scale.current = live && widgets ? (zoom === 'fit' ? Math.min(1, (areaSize.w - 24) / bounds.width, (areaSize.h - 24) / bounds.height) : Number(zoom)) : 1;
  const canvas = { width: Math.max(bounds.width, (areaSize.w - 24) / scale.current), height: Math.max(bounds.height, (areaSize.h - 24) / scale.current) };
  const selected = selection.map(id => byId[id]).filter(Boolean);

  /* Shared with every widget body / property panel / toolbar tool (vc/vc-props-layout.jsx registers
     into window.QLCVCEditTools and reads selection, widgets, page, functions and pin from here). */
  const ctx = { qlc, live: store, act, edit, scale: scale.current, unsupported: (m) => qlc.isUnsupported(m), notice: say, editApi,
    selection, widgets: widgets || [], byId, page, pages: pages || [], functions, mode, pin, refresh: () => refreshRef.current(),
    setSelection: (ids) => { setSelection(ids); if (ids.length) setPanel('props'); },
    selectWidget: (w) => { if (w.page != null && Number(w.page) !== page) openPage(Number(w.page)); setSelection([String(w.id)]); if (edit) setPanel('props'); } };

  const pageEntries = live && pages ? pages.map(p => (
    <MenuBarEntry key={p.index} entryText={p.name || 'Page ' + (p.index + 1)} checked={p.index === page} checkedColor="var(--toolbar-selection-sub)" height="100%"
      faSource={p.hasPin ? (pin.isUnlocked('page', p.index) ? (window.VC_FA_LOCK_OPEN || 'fa_lock') : 'fa_lock') : undefined} onClick={() => openPage(p.index)} style={{ padding: '0 10px', fontSize: 'var(--text-size-small)', flex: 'none' }}
      onDoubleClick={() => { if (edit) setDialog({ kind: 'renamePage', name: p.name || '' }); }}
      title={'Page ' + (p.index + 1) + (p.hasPin ? ' (PIN protected)' : '') + (edit ? ' — double-click to rename' : '')} />
  )) : null;

  const glyphButton = (glyph, tooltip, disabled, onClick) => (
    <IconButton faSource={glyph} size={26} tooltip={tooltip} disabled={disabled} onClick={onClick} />
  );

  return (
    <VCContext.Provider value={ctx}>
      <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0, position: 'relative' }}>
        <ViewToolbar variant="sub">
          <div style={{ display: 'flex', alignItems: 'stretch', height: '100%', flex: '1 1 auto', minWidth: 0, overflowX: 'auto', overflowY: 'hidden', scrollbarWidth: 'none' }}>
            {pageEntries}
            {live && pages && !pages.length ? <RobotoText label="No pages" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" leftMargin={6} /> : null}
          </div>
          {edit ? (
            <>
              {glyphButton('fa_plus', 'Add page', !live, () => structural('vc.page.create', { index: pages ? pages.length : 0 }).catch(() => {}))}
              <IconButton imgSource={D.icon('rename')} size={26} tooltip="Rename page" disabled={!currentPage} onClick={() => setDialog({ kind: 'renamePage', name: currentPage ? currentPage.name || '' : '' })} />
              {glyphButton('fa_trash_can', 'Delete page', !currentPage || (pages && pages.length < 2), () => setDialog({ kind: 'deletePage' }))}
              <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
              {glyphButton(VC_GLYPH.copy, 'Copy the selected widgets to clipboard', !selection.length, copySelection)}
              {glyphButton(VC_GLYPH.paste, 'Paste widgets from clipboard', !clipboard.length, paste)}
              {glyphButton('fa_trash_can', 'Remove the selected widgets', !selection.length, () => setDialog({ kind: 'deleteWidgets' }))}
              <IconButton imgSource={D.icon('grid')} size={26} checked={snap} tooltip="Enable/Disable widgets snapping" onClick={() => setSnap(!snap)} />
              {/* Registry: window.QLCVCEditTools = [Component, ...] - extra edit-mode toolbar tools (align / distribute / matrix / ...), each reading useVC(). */}
              {(window.QLCVCEditTools || []).map((Tool, i) => <Tool key={i} />)}
            </>
          ) : null}
          <ShortcutHint keys="Ctrl L" placement="corner">
            <IconButton imgSource={D.icon('edit')} size={26} checked={edit} disabled={!canEdit}
              onClick={() => setEdit(!edit)} tooltip={!live ? 'Enable/Disable the widgets edit mode — connect first' : mode !== 'design' ? 'Enable/Disable the widgets edit mode — Design mode only' : 'Enable/Disable the widgets edit mode'} />
          </ShortcutHint>
          {live ? (
            <CustomComboBox width={84} height={26} currValue={zoom} onValueChanged={setZoom}
              model={[{ mLabel: 'Fit', mValue: 'fit' }, { mLabel: '50%', mValue: '0.5' }, { mLabel: '75%', mValue: '0.75' }, { mLabel: '100%', mValue: '1' }, { mLabel: '150%', mValue: '1.5' }]} />
          ) : null}
          <IconButton imgSource={D.icon('network')} size={26} disabled={!live} tooltip={live ? 'Reload pages and widgets from the desk' : 'Not connected'}
            onClick={() => { qlc.call('vc.page.list').then(r => setPages(r.pages || [])).catch(() => {}); refreshRef.current(); }} />
        </ViewToolbar>

        <VCNotice text={notice} onDismiss={() => setNotice('')} />

        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div ref={areaRef} style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 12, background: 'var(--bg-medium)', position: 'relative' }}>
            {live ? (
              widgets ? (
                <div style={{ width: canvas.width * scale.current, height: canvas.height * scale.current, position: 'relative' }}>
                  <div ref={canvasRef} onClick={placing ? placeAt : undefined}
                    onPointerDown={(e) => { if (edit && !placing && e.target === e.currentTarget) setSelection([]); }}
                    style={{ position: 'absolute', left: 0, top: 0, width: canvas.width, height: canvas.height, transform: 'scale(' + scale.current + ')', transformOrigin: '0 0',
                      background: 'var(--bg-stronger)', border: edit ? '2px dashed var(--bg-light)' : 'var(--border-control)', cursor: placing ? 'crosshair' : 'default',
                      backgroundImage: edit && snap ? 'linear-gradient(to right, var(--bg-strong) 1px, transparent 1px), linear-gradient(to bottom, var(--bg-strong) 1px, transparent 1px)' : 'none',
                      backgroundSize: VC_SNAP * 4 + 'px ' + VC_SNAP * 4 + 'px' }}>
                    {content}
                    {!widgets.length && !placing ? <div style={{ padding: 20, pointerEvents: 'none' }}><RobotoText label={edit ? 'This page has no widgets — pick one from the palette' : 'This page has no widgets'} fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
                  </div>
                </div>
              ) : <RobotoText label="Loading widgets…" fontSize={14} labelColor="var(--fg-medium)" />
            ) : (
              <div style={{ display: 'inline-flex', flexDirection: 'column', gap: 10, padding: 10, border: 'var(--border-control)', background: 'var(--bg-stronger)' }}>
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

          <SidePanel isOpen={panel != null} alignment="right" expandedWidth={panel === 'props' ? 300 : 'var(--side-panel-width)'} rail={
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
              <IconButton imgSource={D.icon('sliders')} checked={panel === 'gm'} onClick={() => setPanel(panel === 'gm' ? null : 'gm')} tooltip="Grand Master" />
              <IconButton faSource="fa_plus" checked={panel === 'palette'} disabled={!edit} onClick={() => setPanel(panel === 'palette' ? null : 'palette')} tooltip={edit ? 'Add a new widget to the console' : 'Add a new widget to the console — enable edit mode first'} />
              <IconButton imgSource={D.icon('configure')} checked={panel === 'props'} disabled={!edit} onClick={() => setPanel(panel === 'props' ? null : 'props')} tooltip={edit ? 'Widget properties' : 'Widget properties — enable edit mode first'} />
            </div>}>
            <div>
              {panel === 'gm' ? <SectionBox sectionLabel="Grand Master" isExpanded><GrandMaster qlc={qlc} /></SectionBox> : null}
              {panel === 'palette' ? <SectionBox sectionLabel="Widgets" isExpanded><VCWidgetPalette placing={placing} onPick={(p) => { setPlacing(p); if (p) setSelection([]); }} /></SectionBox> : null}
              {panel === 'props' ? <SectionBox sectionLabel="Widget properties" isExpanded><VCWidgetProperties widgets={selected} functions={functions} /></SectionBox> : null}
            </div>
          </SidePanel>
        </div>

        <CustomPopupDialog open={!!dialog && dialog.kind === 'renamePage'} title="Rename page" width={360} standardButtons={['Cancel', 'Rename']}
          onClose={() => setDialog(null)}
          onClicked={(b) => { if (b === 'Rename' && currentPage && dialog.name.trim()) structural('vc.page.rename', { index: currentPage.index, name: dialog.name.trim() }).catch(() => {}); setDialog(null); }}>
          <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
            <CustomTextInput text={dialog ? dialog.name : ''} editing autoFocus width="100%" height={22} onChange={(e) => setDialog(d => Object.assign({}, d, { name: e.target.value }))}
              onTextConfirmed={(t) => setDialog(d => d ? Object.assign({}, d, { name: t }) : d)}
              onKeyDown={(e) => { if (e.key === 'Enter' && currentPage && e.target.value.trim()) { structural('vc.page.rename', { index: currentPage.index, name: e.target.value.trim() }).catch(() => {}); setDialog(null); } }} />
          </span>
        </CustomPopupDialog>
        <CustomPopupDialog open={!!dialog && dialog.kind === 'deletePage'} title="Delete page" width={380} standardButtons={['Cancel', 'Delete']}
          message={currentPage ? 'Are you sure you want to delete the page "' + (currentPage.name || 'Page ' + (currentPage.index + 1)) + '" and every widget on it?' : ''}
          onClose={() => setDialog(null)}
          onClicked={(b) => { if (b === 'Delete' && currentPage) structural('vc.page.delete', { index: currentPage.index }).then(() => setPage(0)).catch(() => {}); setDialog(null); }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'deleteWidgets'} title="Remove widgets" width={380} standardButtons={['Cancel', 'Remove']}
          message={'Are you sure you want to remove the selected widget' + (selection.length > 1 ? 's' : '') + '? Frames are removed with their contents.'}
          onClose={() => setDialog(null)}
          onClicked={(b) => { if (b === 'Remove' && selection.length) editApi.deleteWidgets(selection); setDialog(null); }} />
        {window.VCPinDialog ? (
          <VCPinDialog open={!!dialog && dialog.kind === 'pagePin'} title={dialog && dialog.kind === 'pagePin' && pages ? 'Page "' + ((pages.find(p => p.index === dialog.index) || {}).name || 'Page ' + (dialog.index + 1)) + '" is PIN protected' : 'PIN'}
            onClose={() => setDialog(null)}
            onSubmit={(value) => pin.validatePage(dialog.index, value).then(ok => { if (ok) { pin.unlock('page', dialog.index); setPage(dialog.index); setSelection([]); setDialog(null); } return ok; })} />
        ) : null}
      </div>
    </VCContext.Provider>
  );
}
Object.assign(window, { VirtualConsole, VCSlider, VCButton });
