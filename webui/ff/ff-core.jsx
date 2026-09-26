/**
 * ff-core.jsx — shared plumbing for the Fixtures & Functions screen (webui/ff/*).
 *
 * Everything here hangs off window.FF so the screen files can resolve it at render time (Babel
 * runs the text/babel scripts in document order; FixturesFunctions.jsx loads *before* this file,
 * so nothing may capture window.FF members at file scope).
 *
 *  - FF.mutate(qlc, method, params, {key})   revision-gated mutation through one serial queue per
 *    client: baseRevision is read at send time, a CONFLICT is retried once with the fresh revision
 *    the client learnt from error.details, and calls sharing a `key` are coalesced so a slider drag
 *    sends only its latest value instead of a stale-revision storm.
 *  - FF.live(qlc, key, fn)                    throttle for live (§4b) actions, ~30 Hz per key.
 *  - FF.fixtureDetail(qlc, id) / FF.useFixtureDetail  cached fixtures.get, invalidated by events.
 *  - FF.modeChannels(qlc, summary)            capabilities per channel via fixtures.defs.getMode →
 *    getModel → none (each level degrades gracefully when the server lacks the method).
 *  - channel classification + colour/position → DMX value maths, mirroring
 *    qmlui/contextmanager.cpp (setColorValue / setPositionValue) and Fixture::positionToValues.
 *  - FF.useForeignEvents                       server events not echoed from this client.
 *  - small UI bits: Row, Note (the standard "not available" notice), PopupMenu, FA glyphs.
 */
(function () {
  'use strict';
  const FF = (window.FF = window.FF || {});
  const DS = window.PatchDesignSystem_5432c9;

  /* ---- constants ------------------------------------------------------------------------- */
  FF.FUNCTION_TYPES = [
    { type: 'Scene', icon: 'scene', label: 'New Scene' },
    { type: 'Chaser', icon: 'chaser', label: 'New Chaser' },
    { type: 'Sequence', icon: 'sequence', label: 'New Sequence' },
    { type: 'EFX', icon: 'efx', label: 'New EFX' },
    { type: 'Collection', icon: 'collection', label: 'New Collection' },
    { type: 'RGBMatrix', icon: 'rgbmatrix', label: 'New RGB Matrix' },
    { type: 'Show', icon: 'showmanager', label: 'New Show' },
    { type: 'Script', icon: 'script', label: 'New Script' },
    { type: 'Audio', icon: 'audio', label: 'New Audio' },
    { type: 'Video', icon: 'video', label: 'New Video' }
  ];
  FF.RUN_ORDERS = ['Loop', 'SingleShot', 'PingPong', 'Random'];
  FF.RUN_ORDER_LABELS = { Loop: 'Loop', SingleShot: 'Single Shot', PingPong: 'Ping Pong', Random: 'Random' };
  FF.DIRECTIONS = ['Forward', 'Backward'];
  FF.TEMPO_TYPES = ['Time', 'Beats'];
  FF.SPEED_MODES = ['Default', 'Common', 'PerStep'];
  FF.SPEED_MODE_LABELS = { Default: 'Default', Common: 'Common', PerStep: 'Per Step' };
  FF.INFINITE = 4294967294; /* Function::infiniteSpeed() */
  /* Font Awesome 7 Solid codepoints the compiled FA map lacks; FaIcon renders an unknown name as
     the literal glyph, so these pass straight through IconButton faSource. */
  FF.GLYPH = {
    stop: '', minus: '', arrowUp: '', arrowDown: '', clone: '',
    folder: '', clock: '', crosshairs: '', circleLeft: '', circleRight: '',
    rotateLeft: '', check: '', xmark: '', pen: '', eye: '', eyeSlash: '',
    anglesRight: '', anglesLeft: '', retweet: '', rightLong: '', rightLeft: '',
    shuffle: '', bolt: '', lightbulb: '', palette: '', layerGroup: ''
  };

  /* ---- formatting ------------------------------------------------------------------------ */
  FF.ms = function (v) {
    if (v == null) return '—';
    if (v >= FF.INFINITE) return '∞';
    if (v === 0) return '0 ms';
    if (v >= 60000) { const m = Math.floor(v / 60000), s = (v % 60000) / 1000; return m + 'm ' + (s ? s.toFixed(1).replace(/\.0$/, '') + 's' : ''); }
    if (v >= 1000) return (v / 1000).toFixed(2).replace(/\.?0+$/, '') + ' s';
    return Math.round(v) + ' ms';
  };
  /** Parse "1.5s", "500", "2m", "1:30", "inf" into milliseconds; null if unparseable. */
  FF.parseMs = function (text) {
    const t = String(text || '').trim().toLowerCase();
    if (!t) return null;
    if (/^(inf|∞|infinite)$/.test(t)) return FF.INFINITE;
    let m;
    if ((m = /^(\d+):(\d+(?:\.\d+)?)$/.exec(t))) return Math.round(Number(m[1]) * 60000 + Number(m[2]) * 1000);
    if ((m = /^(\d+(?:\.\d+)?)\s*(ms|s|m)?$/.exec(t))) {
      const n = Number(m[1]);
      return Math.round(m[2] === 's' ? n * 1000 : m[2] === 'm' ? n * 60000 : n);
    }
    return null;
  };

  /* ---- error reporting --------------------------------------------------------------------- */
  const errorListeners = [];
  FF.reportError = function (err, method) {
    const msg = (err && err.message) || String(err);
    errorListeners.slice().forEach(fn => { try { fn({ method: method || (err && err.method), message: msg, code: err && err.code }); } catch (e) { /* ignore */ } });
  };
  /** Latest reported error, cleared after a few seconds; shown by the screen's toolbar. */
  FF.useLastError = function () {
    const [err, setErr] = React.useState(null);
    React.useEffect(() => {
      let timer = null;
      const fn = (e) => { setErr(e); clearTimeout(timer); timer = setTimeout(() => setErr(null), 7000); };
      errorListeners.push(fn);
      return () => { clearTimeout(timer); const i = errorListeners.indexOf(fn); if (i !== -1) errorListeners.splice(i, 1); };
    }, []);
    return err;
  };

  /* ---- revision-gated mutation queue -------------------------------------------------------- */
  const queues = new WeakMap();
  function queueFor(client) {
    let q = queues.get(client);
    if (!q) { q = { busy: false, items: [], byKey: new Map() }; queues.set(client, q); }
    return q;
  }
  function withRevision(client, params) {
    return Object.assign({}, params, { baseRevision: client.docRevision == null ? 0 : client.docRevision });
  }
  async function pump(q, client) {
    if (q.busy) return;
    q.busy = true;
    while (q.items.length) {
      const it = q.items.shift();
      if (it.key) q.byKey.delete(it.key);
      try {
        let result;
        try { result = await client.call(it.method, withRevision(client, it.params)); }
        catch (e) {
          /* A stale-revision CONFLICT carries the fresh docRevision (learnt by the client) and is
             retried once; an address-overlap CONFLICT from fixtures.patch/update carries the
             offending address and would fail again, so it is reported instead. */
          const stale = e && e.code === 'CONFLICT' && e.details && e.details.docRevision != null && e.details.address == null;
          if (stale) result = await client.call(it.method, withRevision(client, it.params));
          else throw e;
        }
        it.resolvers.forEach(r => r[0](result));
      } catch (e) {
        if (!e || e.code !== 'LOCAL_CLOSED') FF.reportError(e, it.method);
        it.resolvers.forEach(r => r[1](e));
      }
    }
    q.busy = false;
  }
  /**
   * Queue one structural mutation. `params` must NOT contain baseRevision — it is added when the
   * call is actually sent. With opts.key, a still-pending call with the same key is replaced by
   * this one (both promises settle with the final result).
   */
  FF.mutate = function (qlc, method, params, opts) {
    opts = opts || {};
    const client = qlc.client && qlc.client();
    if (!client || !client.connected()) { const e = new Error('not connected'); e.code = 'NOT_CONNECTED'; return Promise.reject(e); }
    const q = queueFor(client);
    return new Promise((resolve, reject) => {
      if (opts.key && q.byKey.has(opts.key)) {
        const old = q.byKey.get(opts.key);
        old.params = params; old.resolvers.push([resolve, reject]);
        return;
      }
      const item = { method, params, key: opts.key, resolvers: [[resolve, reject]] };
      q.items.push(item);
      if (opts.key) q.byKey.set(opts.key, item);
      pump(q, client);
    });
  };
  /** Run several mutations strictly in order; stops at the first failure. */
  FF.mutateSeq = async function (qlc, list) {
    const out = [];
    for (const [method, params] of list) out.push(await FF.mutate(qlc, method, params));
    return out;
  };

  /* ---- live action throttle ------------------------------------------------------------------ */
  const liveTimers = new Map();
  /** Run fn at most every 33 ms per key; the last call within the window always wins. */
  FF.live = function (key, fn) {
    const now = Date.now();
    const t = liveTimers.get(key);
    if (t && t.timer) { t.fn = fn; return; }
    if (t && now - t.last < 33) {
      t.fn = fn;
      t.timer = setTimeout(() => { t.timer = null; t.last = Date.now(); const f = t.fn; t.fn = null; if (f) f(); }, 33 - (now - t.last));
      return;
    }
    liveTimers.set(key, { last: now, timer: null, fn: null });
    fn();
  };

  /* ---- local (same-page) change bus ------------------------------------------------------------ */
  /* Own-origin server echoes are filtered below, so when one component edits a document object
     another component is showing (Fixture Tools writing into the open Scene), it announces the
     edit here and the other side patches its optimistic copy. */
  const localListeners = new Map();
  FF.notifyLocal = function (topic, data) {
    (localListeners.get(topic) || new Set()).forEach(fn => { try { fn(data); } catch (e) { /* ignore */ } });
  };
  FF.useLocalEvents = function (topic, handler, deps) {
    const ref = React.useRef(handler);
    ref.current = handler;
    React.useEffect(() => {
      const fn = (d) => ref.current(d);
      if (!localListeners.has(topic)) localListeners.set(topic, new Set());
      localListeners.get(topic).add(fn);
      return () => localListeners.get(topic).delete(fn);
    }, [topic].concat(deps || []));
  };

  /* ---- own-origin echo filtering -------------------------------------------------------------- */
  /**
   * Subscribe to server event topics, skipping events this very client caused (originClientId ==
   * our hello clientId) so optimistic local state is not overwritten mid-edit. Handler gets
   * (topic, data, frame).
   */
  FF.useForeignEvents = function (qlc, topics, handler, deps) {
    const ref = React.useRef(handler);
    ref.current = handler;
    React.useEffect(() => {
      if (!qlc.online) return undefined;
      const client = qlc.client();
      if (!client) return undefined;
      const fn = (frame) => {
        if (!frame || topics.indexOf(frame.topic) === -1) return;
        if (frame.originClientId != null && client.clientId != null && String(frame.originClientId) === String(client.clientId)) return;
        ref.current(frame.topic, frame.data, frame);
      };
      client.on('event', fn);
      return () => client.off('event', fn);
    }, [qlc.online].concat(deps || []));
  };

  /* ---- fixture detail cache ---------------------------------------------------------------- */
  const caches = new WeakMap();
  function cacheFor(client) {
    let c = caches.get(client);
    if (c) return c;
    c = { fixtures: new Map(), modes: new Map(), listeners: new Set() };
    caches.set(client, c);
    const drop = (id) => { c.fixtures.delete(String(id)); c.listeners.forEach(fn => fn(String(id))); };
    client.on('fixtures.updated', d => { if (d && d.fixture) drop(d.fixture.id); });
    client.on('fixtures.unpatched', d => ((d && d.fixtureIds) || []).forEach(drop));
    const flush = () => { const ids = Array.from(c.fixtures.keys()); c.fixtures.clear(); c.modes.clear(); ids.forEach(id => c.listeners.forEach(fn => fn(id))); };
    client.on('core.project.loaded', flush);
    /* Undo/redo emits no domain events, only core.history.changed — anything cached may be stale. */
    client.on('core.history.changed', flush);
    return c;
  }
  /** fixtures.get, cached per client until that fixture changes. */
  FF.fixtureDetail = function (qlc, fixtureId) {
    const client = qlc.client && qlc.client();
    if (!client || !client.connected()) return Promise.reject(new Error('not connected'));
    const c = cacheFor(client);
    const key = String(fixtureId);
    if (!c.fixtures.has(key)) {
      const p = client.call('fixtures.get', { fixtureId: key }).catch(e => { c.fixtures.delete(key); throw e; });
      c.fixtures.set(key, p);
    }
    return c.fixtures.get(key);
  };
  FF.useFixtureDetail = function (qlc, fixtureId) {
    const [detail, setDetail] = React.useState(null);
    React.useEffect(() => {
      setDetail(null);
      if (!qlc.online || fixtureId == null) return undefined;
      let alive = true;
      const load = () => FF.fixtureDetail(qlc, fixtureId).then(d => { if (alive) setDetail(d); }).catch(() => {});
      load();
      const c = cacheFor(qlc.client());
      const fn = (id) => { if (id === String(fixtureId)) load(); };
      c.listeners.add(fn);
      return () => { alive = false; c.listeners.delete(fn); };
    }, [qlc.online, fixtureId]);
    return detail;
  };
  /** Several fixtures at once → {id: detail}; re-renders as they arrive. */
  FF.useFixtureDetails = function (qlc, ids) {
    const [map, setMap] = React.useState({});
    const key = (ids || []).join(',');
    React.useEffect(() => {
      if (!qlc.online) { setMap({}); return undefined; }
      let alive = true;
      const load = (id) => FF.fixtureDetail(qlc, id).then(d => { if (alive) setMap(m => Object.assign({}, m, { [String(id)]: d })); }).catch(() => {});
      (ids || []).forEach(load);
      const c = cacheFor(qlc.client());
      const fn = (id) => { if ((ids || []).some(x => String(x) === id)) load(id); };
      c.listeners.add(fn);
      return () => { alive = false; c.listeners.delete(fn); };
    }, [qlc.online, key]);
    return map;
  };

  /**
   * Channels with capabilities for a fixture summary/detail ({manufacturer, model, mode}).
   * fixtures.get's channelList carries no capabilities, so they come from the definition library:
   * fixtures.defs.getModel first (one call covers every mode of that model — modes[{name,
   * channelCount, channels[{index,name,group,preset,colour,controlByte,defaultValue,
   * capabilities[{min,max,name,preset,presetType,color1,color2}]}]}], cached per model), then
   * fixtures.defs.getMode for servers whose getModel lists modes without channels. Resolves null
   * when neither is available — callers then fall back to plain 0-255 controls.
   */
  FF.modeChannels = function (qlc, f) {
    const client = qlc.client && qlc.client();
    if (!client || !client.connected() || !f || !f.manufacturer || !f.model || !f.mode) return Promise.resolve(null);
    const c = cacheFor(client);
    const modelKey = f.manufacturer + ' ' + f.model;
    if (!c.modes.has(modelKey)) {
      const p = client.isUnsupported('fixtures.defs.getModel') ? Promise.resolve(null)
        : client.call('fixtures.defs.getModel', { manufacturer: f.manufacturer, model: f.model }).then(r => (r && r.modes) || null).catch(() => null);
      c.modes.set(modelKey, p);
    }
    return c.modes.get(modelKey).then(modes => {
      const mode = (modes || []).find(m => m.name === f.mode);
      if (mode && mode.channels && mode.channels.length) return mode.channels;
      const key = modelKey + ' ' + f.mode;
      if (!c.modes.has(key)) {
        c.modes.set(key, client.isUnsupported('fixtures.defs.getMode') ? Promise.resolve(null)
          : client.call('fixtures.defs.getMode', { manufacturer: f.manufacturer, model: f.model, mode: f.mode }).then(r => (r && r.channels) || null).catch(() => null));
      }
      return c.modes.get(key);
    });
  };
  FF.useModeChannels = function (qlc, f) {
    const [chs, setChs] = React.useState(undefined);
    const key = f ? [f.manufacturer, f.model, f.mode].join('|') : '';
    React.useEffect(() => {
      setChs(undefined);
      if (!qlc.online || !f) return undefined;
      let alive = true;
      FF.modeChannels(qlc, f).then(r => { if (alive) setChs(r); });
      return () => { alive = false; };
    }, [qlc.online, key]);
    return chs; /* undefined = loading, null = unavailable */
  };

  /* ---- channel classification --------------------------------------------------------------- */
  const COLOUR_ROLE = { Red: 'red', Green: 'green', Blue: 'blue', White: 'white', Amber: 'amber', UV: 'uv',
    Cyan: 'cyan', Magenta: 'magenta', Yellow: 'yellow', Lime: 'lime', Indigo: 'indigo' };
  /**
   * Role of one channel of fixtures.get's channelList (optionally enriched with a defs channel):
   * dimmer | red|green|blue|white|amber|uv|cyan|magenta|yellow|lime|indigo | pan | tilt | gobo |
   * colorwheel | shutter | beam | speed | prism | effect | maintenance | other, plus `fine` for a
   * 16-bit LSB channel (defs controlByte, or a "fine" name).
   */
  FF.classify = function (ch) {
    const name = (ch.name || '').toLowerCase();
    const fine = ch.controlByte === 'LSB' || /\bfine\b/.test(name) || /\blsb\b/.test(name);
    let role = 'other';
    switch (ch.group) {
      case 'Intensity': role = ch.colour && COLOUR_ROLE[ch.colour] ? COLOUR_ROLE[ch.colour] : 'dimmer'; break;
      case 'Pan': role = 'pan'; break;
      case 'Tilt': role = 'tilt'; break;
      case 'Gobo': role = 'gobo'; break;
      case 'Colour': role = 'colorwheel'; break;
      case 'Shutter': role = 'shutter'; break;
      case 'Beam': role = 'beam'; break;
      case 'Speed': role = 'speed'; break;
      case 'Prism': role = 'prism'; break;
      case 'Effect': role = 'effect'; break;
      case 'Maintenance': role = 'maintenance'; break;
      default: role = 'other';
    }
    return { role, fine };
  };
  /** Merge fixtures.get channelList with defs channels (capabilities) by index. */
  FF.mergeChannels = function (channelList, defsChannels) {
    const byIndex = {};
    (defsChannels || []).forEach(c => { byIndex[c.index] = c; });
    return (channelList || []).map(ch => {
      const d = byIndex[ch.index];
      const merged = Object.assign({}, d || {}, ch, { capabilities: (d && d.capabilities) || null });
      return Object.assign(merged, FF.classify(merged));
    });
  };
  /** Which tool groups a channel set offers. */
  FF.toolMask = function (channels) {
    const m = { intensity: false, colour: false, position: false, gobo: false, colorwheel: false, shutter: false, beam: false, speed: false, prism: false, effect: false, maintenance: false };
    channels.forEach(c => {
      if (c.role === 'dimmer') m.intensity = true;
      else if (COLOUR_ROLE[c.colour]) m.colour = true;
      else if (c.role === 'pan' || c.role === 'tilt') m.position = true;
      else if (m.hasOwnProperty(c.role)) m[c.role] = true;
    });
    return m;
  };

  /* ---- colour / position → values (per qmlui/contextmanager.cpp) ------------------------------ */
  /** Qt QColor::toCmyk() on 0-255 components. */
  FF.rgbToCmy = function (r, g, b) {
    const c = 1 - r / 255, m = 1 - g / 255, y = 1 - b / 255;
    const k = Math.min(c, m, y);
    if (k >= 1) return { c: 0, m: 0, y: 0 };
    return { c: Math.round((c - k) / (1 - k) * 255), m: Math.round((m - k) / (1 - k) * 255), y: Math.round((y - k) / (1 - k) * 255) };
  };
  /**
   * DMX writes for a colour on one fixture's channels: {r,g,b} (0-255) and optional {w,a,uv};
   * RGB, WAUV and CMY channels are all set, like ContextManager::setColorValue.
   */
  FF.colourValues = function (channels, rgb, wauv) {
    const cmy = FF.rgbToCmy(rgb.r, rgb.g, rgb.b);
    const table = { red: rgb.r, green: rgb.g, blue: rgb.b, cyan: cmy.c, magenta: cmy.m, yellow: cmy.y,
      white: wauv ? wauv.w : undefined, amber: wauv ? wauv.a : undefined, uv: wauv ? wauv.uv : undefined };
    const out = [];
    channels.forEach(c => { if (!c.fine && table[c.role] != null) out.push({ channel: c.index, value: table[c.role] }); });
    return out;
  };
  /** 16-bit position (0-65535) → coarse/fine writes for role 'pan' or 'tilt'. */
  FF.positionValues = function (channels, role, value16) {
    const v = Math.max(0, Math.min(65535, Math.round(value16)));
    const out = [];
    channels.forEach(c => {
      if (c.role !== role) return;
      out.push({ channel: c.index, value: c.fine ? (v & 255) : (v >> 8) });
    });
    return out;
  };
  /** Same value on every non-fine channel with `role`. */
  FF.roleValues = function (channels, role, value) {
    return channels.filter(c => c.role === role && !c.fine).map(c => ({ channel: c.index, value }));
  };
  /** "#rrggbb[wwaauv]" (QLCPalette::colorToString) → {rgb, wauv}. */
  FF.parsePaletteColour = function (s) {
    const m = /^#?([0-9a-f]{6})([0-9a-f]{6})?$/i.exec(String(s || '').trim());
    if (!m) return null;
    const h = (str, i) => parseInt(str.substr(i, 2), 16);
    const rgb = { r: h(m[1], 0), g: h(m[1], 2), b: h(m[1], 4) };
    const wauv = m[2] ? { w: h(m[2], 0), a: h(m[2], 2), uv: h(m[2], 4) } : null;
    return { rgb, wauv };
  };
  FF.hex = function (rgb) { const p = (n) => ('0' + Math.max(0, Math.min(255, Math.round(n))).toString(16)).slice(-2); return '#' + p(rgb.r) + p(rgb.g) + p(rgb.b); };
  FF.parseHex = function (s) { const m = /^#?([0-9a-f]{6})$/i.exec(String(s || '').trim()); if (!m) return null; return { r: parseInt(m[1].substr(0, 2), 16), g: parseInt(m[1].substr(2, 2), 16), b: parseInt(m[1].substr(4, 2), 16) }; };

  /** Flat Simple Desk address for a fixture channel: fixture.universe*512 + fixture.address + index. */
  FF.flatAddress = function (fixture, channelIndex) {
    return fixture.universe * 512 + fixture.address + channelIndex;
  };
  /** Write [{channel, value}] for one fixture detail to live output (io.simpleDesk.setChannels). */
  FF.writeLive = function (qlc, fixture, writes, key) {
    const client = qlc.client && qlc.client();
    if (!client || !writes.length) return;
    const items = writes.map(w => ({ address: FF.flatAddress(fixture, w.channel), value: w.value }));
    FF.live(key || ('live:' + fixture.id), () => client.setChannels(items));
  };

  /* ---- palettes --------------------------------------------------------------------------- */
  /** Palette detail → [{channel,value}] writes for one fixture's classified channels; null when
      that palette type has no client-side mapping (Shutter, Gobo, Zoom, Position3D). */
  FF.paletteValues = function (palette, channels) {
    const v = palette.values || [];
    switch (palette.type) {
      case 'Dimmer': return FF.roleValues(channels, 'dimmer', Math.round(Math.max(0, Math.min(100, Number(v[0]) || 0)) * 2.55));
      case 'Color': { const c = FF.parsePaletteColour(v[0]); return c ? FF.colourValues(channels, c.rgb, c.wauv) : []; }
      case 'Pan': return FF.positionValues(channels, 'pan', (Number(v[0]) || 0) * 257);
      case 'Tilt': return FF.positionValues(channels, 'tilt', (Number(v[0]) || 0) * 257);
      case 'PanTilt': return FF.positionValues(channels, 'pan', (Number(v[0]) || 0) * 257).concat(FF.positionValues(channels, 'tilt', (Number(v[1]) || 0) * 257));
      default: return null;
    }
  };
  FF.PALETTE_ICON = { Color: 'color', Dimmer: 'dimmer', Pan: 'pan', Tilt: 'tilt', PanTilt: 'position', Position3D: '3dpoint', Shutter: 'shutter', Gobo: 'gobo', Zoom: 'beam' };

  /* ---- small UI bits ------------------------------------------------------------------------- */
  const { RobotoText, ContextMenuEntry, FaIcon } = DS;

  FF.Row = function Row({ label, children, width = 90, height = 26 }) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, minHeight: height }}>
        <RobotoText label={label} fontSize={14} style={{ width, flex: 'none' }} labelColor="var(--fg-light)" />
        {typeof children === 'string' || typeof children === 'number' ? <RobotoText label={String(children)} fontSize={14} /> : children}
      </div>
    );
  };

  /** The one way this screen says "not available yet": small, --fg-medium, names the missing piece. */
  FF.Note = function Note({ text, style }) {
    return <RobotoText label={text} fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" style={Object.assign({ lineHeight: 1.3 }, style || {})} />;
  };

  FF.Heading = function Heading({ text, style }) {
    return <RobotoText label={text} fontBold fontSize={14} height={22} style={style} />;
  };

  /**
   * Context-menu style popup anchored at page coordinates (or under an element rect). Closes on
   * outside click / Escape. Children are ContextMenuEntry rows.
   */
  FF.PopupMenu = function PopupMenu({ open, x, y, onClose, children, width = 200 }) {
    React.useEffect(() => {
      if (!open) return undefined;
      const onKey = (e) => { if (e.key === 'Escape') onClose(); };
      window.addEventListener('keydown', onKey);
      return () => window.removeEventListener('keydown', onKey);
    }, [open]);
    if (!open) return null;
    const left = Math.min(x, window.innerWidth - width - 8), top = Math.min(y, window.innerHeight - 40);
    return (
      <div style={{ position: 'fixed', inset: 0, zIndex: 95 }} onClick={onClose} onContextMenu={(e) => { e.preventDefault(); onClose(); }}>
        <div role="menu" onClick={(e) => e.stopPropagation()}
          style={{ position: 'absolute', left, top, width, background: 'var(--bg-control)', border: 'var(--border-menu)', padding: 2, display: 'flex', flexDirection: 'column' }}>
          {children}
        </div>
      </div>
    );
  };
  FF.MenuItem = function MenuItem({ icon, glyph, text, onClick, disabled }) {
    const D = window.QLCData;
    return <ContextMenuEntry imgSource={icon ? D.icon(icon) : undefined} faSource={glyph} entryText={text} disabled={disabled} onClick={onClick} iconHeight={26} />;
  };

  /** Tiny inline value editor: click the number, type, Enter/blur commits, Escape cancels. */
  FF.InlineNumber = function InlineNumber({ value, format, parse, onCommit, width = 70, disabled, title }) {
    const [editing, setEditing] = React.useState(false);
    const [text, setText] = React.useState('');
    const start = () => { if (disabled) return; setText(format ? format(value) : String(value)); setEditing(true); };
    const commit = () => { setEditing(false); const v = parse ? parse(text) : Number(text); if (v != null && !Number.isNaN(v) && v !== value) onCommit(v); };
    if (!editing) return (
      <div onClick={start} title={title} style={{ width, height: 22, display: 'flex', alignItems: 'center', justifyContent: 'flex-end', padding: '0 4px', cursor: disabled ? 'default' : 'text', background: 'var(--bg-stronger)', borderRadius: 'var(--radius-spin)', boxSizing: 'border-box' }}>
        <RobotoText label={format ? format(value) : String(value)} fontSize={13} height={22} labelColor={disabled ? 'var(--fg-medium)' : 'var(--fg-main)'} />
      </div>
    );
    return <input autoFocus value={text} onChange={e => setText(e.target.value)} onBlur={commit}
      onKeyDown={e => { if (e.key === 'Enter') commit(); else if (e.key === 'Escape') setEditing(false); }}
      style={{ width, height: 22, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--focus-border)', borderRadius: 'var(--radius-spin)', fontFamily: 'var(--font-roboto)', fontSize: 13, textAlign: 'right', padding: '0 4px', outline: 'none' }} />;
  };

  /** Icon + text selectable chip used for run order / direction / tempo pickers. */
  FF.Choice = function Choice({ options, value, onChange, disabled, labels }) {
    return (
      <div style={{ display: 'flex', gap: 2 }}>
        {options.map(o => (
          <button key={o} type="button" disabled={disabled} onClick={() => onChange(o)} title={(labels && labels[o]) || o}
            style={{ height: 24, padding: '0 8px', border: 'var(--border-control)', borderRadius: 0, cursor: disabled ? 'default' : 'pointer',
              background: value === o ? 'var(--highlight)' : 'var(--bg-control)', color: 'var(--fg-main)', fontFamily: 'var(--font-roboto)', fontSize: 13 }}>
            {(labels && labels[o]) || o}
          </button>
        ))}
      </div>
    );
  };

  FF.Glyph = function Glyph({ g, size = 14, color }) { return <FaIcon name={g} size={size} color={color} />; };
})();
