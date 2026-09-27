/**
 * fe-core.jsx — shared state and plumbing for the Fixture Editor screen (webui/FixtureEditor.jsx).
 *
 * Server: the fixturedefs.* domain (controlapi/src/domains/apifixturedefsdomain.cpp); read the
 * header of webui/api/domains/fixturedefs.js first. In short:
 *  - an editing *session* is a private in-memory clone of one definition; every session mutation
 *    carries baseRevision = the session's sessionRevision and answers the new sessionRevision; the
 *    full definition snapshot arrives in the 'fixturedefs.session.updated' event (own echoes too);
 *  - fixturedefs.save / fixturedefs.delete use the library's per-definition defRevision instead
 *    (null for a manufacturer/model the library does not have yet);
 *  - sessions live on the server until closed, so a reload lists them (session.list) and fetches
 *    each one again (session.get).
 *
 * The store is module-level on purpose: App.jsx unmounts a screen when the operator switches to
 * another one, and the open tabs / selected channel must survive a trip to Fixtures & Functions.
 *
 * window.FE = { store, useStore, mutate, ..., constants }
 */
(function () {
  'use strict';
  const FE = (window.FE = window.FE || {});

  /* ---- engine enum strings (QLCChannel / QLCCapability / QLCFixtureDef wire names) ------------ */
  FE.FIXTURE_TYPES = [
    ['Color Changer', 'fixture'], ['Dimmer', 'dimmer'], ['Effect', 'effect'], ['Fan', 'fan'], ['Flower', 'flower'],
    ['Hazer', 'hazer'], ['Laser', 'laser'], ['LED Bar (Beams)', 'ledbar_beams'], ['LED Bar (Pixels)', 'ledbar_pixels'],
    ['Moving Head', 'movinghead'], ['Other', 'other'], ['Scanner', 'scanner'], ['Smoke', 'smoke'], ['Strobe', 'strobe']
  ];
  FE.GROUPS = ['Intensity', 'Colour', 'Gobo', 'Speed', 'Pan', 'Tilt', 'Shutter', 'Prism', 'Beam', 'Effect', 'Maintenance', 'Nothing',
    'Position X', 'Position Y', 'Position Z', 'Rotation X', 'Rotation Y', 'Rotation Z', 'Scale X', 'Scale Y', 'Scale Z'];
  FE.COLOURS = ['Generic', 'Red', 'Green', 'Blue', 'Cyan', 'Magenta', 'Yellow', 'Amber', 'White', 'UV', 'Lime', 'Indigo'];
  FE.CHANNEL_PRESETS = ['Custom', 'IntensityMasterDimmer', 'IntensityMasterDimmerFine', 'IntensityDimmer', 'IntensityDimmerFine',
    'IntensityRed', 'IntensityRedFine', 'IntensityGreen', 'IntensityGreenFine', 'IntensityBlue', 'IntensityBlueFine',
    'IntensityCyan', 'IntensityCyanFine', 'IntensityMagenta', 'IntensityMagentaFine', 'IntensityYellow', 'IntensityYellowFine',
    'IntensityAmber', 'IntensityAmberFine', 'IntensityWhite', 'IntensityWhiteFine', 'IntensityUV', 'IntensityUVFine',
    'IntensityIndigo', 'IntensityIndigoFine', 'IntensityLime', 'IntensityLimeFine', 'IntensityHue', 'IntensityHueFine',
    'IntensitySaturation', 'IntensitySaturationFine', 'IntensityLightness', 'IntensityLightnessFine', 'IntensityValue',
    'IntensityValueFine', 'PositionPan', 'PositionPanFine', 'PositionTilt', 'PositionTiltFine', 'PositionXAxis', 'PositionYAxis',
    'SpeedPanSlowFast', 'SpeedPanFastSlow', 'SpeedTiltSlowFast', 'SpeedTiltFastSlow', 'SpeedPanTiltSlowFast', 'SpeedPanTiltFastSlow',
    'ColorMacro', 'ColorWheel', 'ColorWheelFine', 'ColorRGBMixer', 'ColorCTOMixer', 'ColorCTCMixer', 'ColorCTBMixer',
    'GoboWheel', 'GoboWheelFine', 'GoboIndex', 'GoboIndexFine', 'ShutterStrobeSlowFast', 'ShutterStrobeFastSlow',
    'ShutterIrisMinToMax', 'ShutterIrisMaxToMin', 'ShutterIrisFine', 'BeamFocusNearFar', 'BeamFocusFarNear', 'BeamFocusFine',
    'BeamZoomSmallBig', 'BeamZoomBigSmall', 'BeamZoomFine', 'PrismRotationSlowFast', 'PrismRotationFastSlow', 'NoFunction'];
  FE.CAPABILITY_PRESETS = ['Custom', 'SlowToFast', 'FastToSlow', 'NearToFar', 'FarToNear', 'BigToSmall', 'SmallToBig',
    'ShutterOpen', 'ShutterClose', 'StrobeSlowToFast', 'StrobeFastToSlow', 'StrobeRandom', 'StrobeRandomSlowToFast',
    'StrobeRandomFastToSlow', 'StrobeFrequency', 'StrobeFreqRange', 'PulseSlowToFast', 'PulseFastToSlow', 'PulseFrequency',
    'PulseFreqRange', 'RampUpSlowToFast', 'RampUpFastToSlow', 'RampDownSlowToFast', 'RampDownFastToSlow', 'RampUpFrequency',
    'RampUpFreqRange', 'RampDownFrequency', 'RampDownFreqRange', 'RotationStop', 'RotationIndexed', 'RotationClockwise',
    'RotationClockwiseSlowToFast', 'RotationClockwiseFastToSlow', 'RotationCounterClockwise',
    'RotationCounterClockwiseSlowToFast', 'RotationCounterClockwiseFastToSlow', 'ColorMacro', 'ColorDoubleMacro',
    'ColorWheelIndex', 'GoboMacro', 'GoboShakeMacro', 'GenericPicture', 'PrismEffectOn', 'PrismEffectOff', 'LampOn', 'LampOff',
    'ResetAll', 'ResetPanTilt', 'ResetPan', 'ResetTilt', 'ResetMotors', 'ResetGobo', 'ResetColor', 'ResetCMY', 'ResetCTO',
    'ResetEffects', 'ResetPrism', 'ResetBlades', 'ResetIris', 'ResetFrost', 'ResetZoom', 'SilentModeOn', 'SilentModeOff',
    'SilentModeAutomatic', 'Alias'];
  /** QLCCapability::presetType() / presetUnits() (engine/src/qlccapability.cpp). */
  FE.capabilityPresetType = function (preset) {
    switch (preset) {
      case 'StrobeFrequency': case 'PulseFrequency': case 'RampUpFrequency': case 'RampDownFrequency': case 'PrismEffectOn': return 'SingleValue';
      case 'StrobeFreqRange': case 'PulseFreqRange': case 'RampUpFreqRange': case 'RampDownFreqRange': return 'DoubleValue';
      case 'ColorMacro': return 'SingleColor';
      case 'ColorDoubleMacro': return 'DoubleColor';
      case 'GoboMacro': case 'GoboShakeMacro': case 'GenericPicture': return 'Picture';
      default: return 'None';
    }
  };
  FE.capabilityPresetUnits = (preset) => (preset === 'PrismEffectOn' ? 'Faces' : FE.capabilityPresetType(preset).indexOf('Value') !== -1 ? 'Hz' : '');
  /** PopupChannelWizard.qml's type list. */
  FE.WIZARD_TYPES = [['Red', 'red'], ['Green', 'green'], ['Blue', 'blue'], ['White', 'white'], ['Amber', 'amber'], ['UV', 'uv'],
    ['Lime', 'lime'], ['Indigo', 'indigo'], ['RGB', 'color'], ['RGBW', 'color'], ['RGBA', 'color'], ['RGBL', 'color'], ['RGBAW', 'color'],
    ['Dimmer', 'dimmer'], ['Pan', 'pan'], ['Tilt', 'tilt'], ['Color Macro', 'colorwheel'], ['Shutter', 'shutter'], ['Beam', 'beam'], ['Effect', 'star']];
  FE.WIZARD_COMPOUND = { RGB: ['Red', 'Green', 'Blue'], RGBW: ['Red', 'Green', 'Blue', 'White'], RGBA: ['Red', 'Green', 'Blue', 'Amber'],
    RGBL: ['Red', 'Green', 'Blue', 'Lime'], RGBAW: ['Red', 'Green', 'Blue', 'Amber', 'White'] };
  /** PhysicalProperties.qml's editable-combo suggestions. */
  FE.BULB_TYPES = ['LED', 'CDM 70W', 'CDM 150W', 'CP29 5000W', 'CP41 2000W', 'CP60 1000W', 'CP61 1000W', 'CP62 1000W', 'CP86 500W',
    'CP87 500W', 'CP88 500W', 'EFP 100W', 'EFP 150W', 'EFR 100W', 'EFR 150W', 'ELC 250W', 'HMI 150W', 'HMI 250W', 'HMI 400W',
    'HMI 575W', 'HMI 700W', 'HMI 1200W', 'HMI 4000W', 'HSD 150W', 'HSD 200W', 'HSD 250W', 'HSD 575W', 'HTI 150W', 'HTI 250W',
    'HTI 300W', 'HTI 400W', 'HTI 575W', 'HTI 700W', 'HTI 1200W', 'HTI 2500W', 'MSD 200W', 'MSD 250W', 'MSD 275W',
    'MSD Platinum 15 R 300W', 'MSD 575W', 'MSR 575W', 'MSR 700W', 'MSR 1200W'];
  FE.LENS_TYPES = ['Other', 'PC', 'Fresnel'];
  FE.FOCUS_TYPES = ['Fixed', 'Head', 'Mirror', 'Barrel'];
  FE.DMX_CONNECTORS = ['3-pin', '5-pin', '3-pin and 5-pin', '3.5 mm stereo jack', 'Other'];

  /** Split "IntensityRedFine" into "Intensity Red Fine" for display. */
  FE.humanize = (s) => String(s || '').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2');
  FE.channelIcon = function (ch) {
    const Icons = window.QLCIcons, D = window.QLCData;
    if (!ch) return D.icon('other');
    if (Icons && Icons.channelIcon) return Icons.channelIcon(ch);
    return D.icon('other');
  };
  FE.typeIcon = (type) => window.QLCData.icon((FE.FIXTURE_TYPES.find(t => t[0] === type) || [null, 'other'])[1]);
  FE.errorText = (e) => (e && (e.message || e.code)) || 'request failed';

  /* ---- the store -------------------------------------------------------------------------------- */
  /* sessions: sessionId -> { sessionId, definition, sessionRevision, defRev (revision the definition
     snapshot belongs to), isUser, baseRevision (library defRevision, null = never saved), isModified }.
     ui: per-session view state (tab, selected channel / capability / mode). */
  const store = FE.store = FE.store || {
    sessions: {}, order: [], active: null, ui: {}, version: 0, listeners: new Set(),
    status: { text: '', error: false }, pending: null, bound: null, validation: {}
  };
  function emit() { store.version++; store.listeners.forEach(fn => { try { fn(); } catch (e) { console.error(e); } }); }
  FE.emit = emit;
  FE.useStore = function () {
    const [, set] = React.useState(0);
    React.useEffect(() => { const fn = () => set(v => v + 1); store.listeners.add(fn); return () => store.listeners.delete(fn); }, []);
    return store;
  };
  FE.say = function (text, error) { store.status = { text: text || '', error: !!error, at: Date.now() }; emit(); };
  FE.ui = function (sid) { if (!store.ui[sid]) store.ui[sid] = { tab: 'general', channelId: null, capIndex: -1, modeId: null, chanSel: [], slotSel: [], headSel: [] }; return store.ui[sid]; };
  FE.setUi = function (sid, patch) { Object.assign(FE.ui(sid), patch); emit(); };
  FE.session = (sid) => store.sessions[sid] || null;
  FE.activeSession = () => (store.active ? store.sessions[store.active] || null : null);

  /** A session.open/create/import/get result or a session.opened event. */
  FE.putSession = function (r, activate) {
    if (!r || !r.sessionId) return;
    const prev = store.sessions[r.sessionId];
    const rev = r.sessionRevision || 0;
    if (prev && prev.defRev > rev) return;
    store.sessions[r.sessionId] = {
      sessionId: r.sessionId, definition: r.definition, sessionRevision: Math.max(rev, prev ? prev.sessionRevision : 0), defRev: rev,
      isUser: !!r.isUser, baseRevision: r.baseRevision === undefined ? null : r.baseRevision,
      isModified: r.isModified != null ? !!r.isModified : (prev ? prev.isModified : false)
    };
    if (store.order.indexOf(r.sessionId) === -1) store.order.push(r.sessionId);
    if (activate || !store.active) store.active = r.sessionId;
    emit();
  };
  function applyUpdated(d) {
    const s = store.sessions[d.sessionId];
    if (!s) return false;
    if (d.sessionRevision < s.defRev) return true;
    s.definition = d.definition; s.defRev = d.sessionRevision; s.sessionRevision = Math.max(s.sessionRevision, d.sessionRevision);
    s.isUser = d.definition && d.definition.isUser != null ? !!d.definition.isUser : s.isUser;
    s.isModified = true;
    delete store.validation[d.sessionId];
    emit();
    return true;
  }
  function dropSession(sid) {
    if (!store.sessions[sid]) return;
    delete store.sessions[sid]; delete store.ui[sid]; delete store.validation[sid];
    const i = store.order.indexOf(sid);
    if (i !== -1) store.order.splice(i, 1);
    if (store.active === sid) store.active = store.order[Math.max(0, i - 1)] || null;
    emit();
  }
  FE.dropSession = dropSession;

  /** Re-read every server session (session.list + session.get each): used on (re)connect and mount. */
  FE.resync = async function (qlc) {
    const list = await qlc.call('fixturedefs.session.list', {});
    const ids = (list.sessions || []).map(s => s.sessionId);
    Object.keys(store.sessions).forEach(sid => { if (ids.indexOf(sid) === -1) dropSession(sid); });
    for (const sid of ids) {
      try { FE.putSession(await qlc.call('fixturedefs.session.get', { sessionId: sid }), false); }
      catch (e) { if (e && e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || '')) { FE.say('This server has no fixturedefs.session.get: open sessions from before the reload cannot be resumed.', true); break; } }
    }
    store.order.sort((a, b) => ids.indexOf(a) - ids.indexOf(b));
    emit();
  };

  /** Subscribe once per client object to the domain's events. Returns an unsubscribe. */
  FE.bindEvents = function (qlc) {
    const offs = [
      qlc.subscribeTo('fixturedefs.session.opened', (d) => FE.putSession(d, false)),
      qlc.subscribeTo('fixturedefs.session.updated', (d) => { if (!applyUpdated(d)) FE.putSession(Object.assign({ isModified: true }, d), false); }),
      qlc.subscribeTo('fixturedefs.session.closed', (d) => dropSession(d.sessionId)),
      qlc.subscribeTo('fixturedefs.saved', (d) => {
        const s = store.sessions[d.sessionId];
        if (s) { s.definition = d.definition || s.definition; s.baseRevision = d.defRevision; s.isModified = false; s.isUser = true; emit(); }
        store.libraryVersion = (store.libraryVersion || 0) + 1; emit();
      }),
      qlc.subscribeTo('fixturedefs.deleted', () => { store.libraryVersion = (store.libraryVersion || 0) + 1; emit(); })
    ];
    return () => offs.forEach(f => f());
  };

  /* ---- revision-gated session mutations ----------------------------------------------------------- */
  /* One serial queue per session: baseRevision is read at send time (after the previous mutation's
     answer landed), a CONFLICT rebases from error.details {sessionRevision, definition} and is retried
     once, and items sharing a `key` that have not been sent yet are coalesced (a spin box sends only
     its latest value). */
  const queues = {};
  FE.mutate = function (qlc, method, params, opts) {
    const sid = params.sessionId;
    const q = queues[sid] || (queues[sid] = { busy: false, items: [] });
    const key = opts && opts.key;
    if (key) {
      const waiting = q.items.find(it => it.key === key);
      if (waiting) { waiting.params = params; return waiting.promise; }
    }
    const item = { method, params, key, plain: !!(opts && opts.plain) };
    item.promise = new Promise((res, rej) => { item.res = res; item.rej = rej; });
    q.items.push(item);
    pump(qlc, sid, q);
    return item.promise;
  };
  async function pump(qlc, sid, q) {
    if (q.busy) return;
    q.busy = true;
    while (q.items.length) {
      const it = q.items.shift();
      const send = () => {
        const s = store.sessions[sid];
        const p = Object.assign({}, it.params);
        /* $build: fields computed at send time from the then-current snapshot */
        if (typeof p.$build === 'function') { const extra = p.$build(); delete p.$build; Object.assign(p, extra); }
        /* plain items (save, export, validate) run in order with the session's mutations but carry
           their own revision semantics (or none) */
        if (it.plain) return qlc.call(it.method, p);
        return qlc.call(it.method, Object.assign(p, { baseRevision: s ? s.sessionRevision : 0 }));
      };
      try {
        let r;
        try { r = await send(); }
        catch (e) {
          if (it.plain || !(e && e.code === 'CONFLICT' && e.details)) throw e;
          const s = store.sessions[sid];
          if (s && e.details.definition) { s.definition = e.details.definition; s.defRev = e.details.sessionRevision; }
          if (s) s.sessionRevision = e.details.sessionRevision;
          emit();
          r = await send();
        }
        const s = store.sessions[sid];
        if (!it.plain && s && r && typeof r.sessionRevision === 'number') { s.sessionRevision = Math.max(s.sessionRevision, r.sessionRevision); s.isModified = true; }
        it.res(r);
      } catch (e) {
        if (!it.plain) FE.say(it.method.replace('fixturedefs.', '') + ': ' + FE.errorText(e), true);
        it.rej(e);
      }
    }
    q.busy = false;
  }
  /** Fire-and-report wrapper: resolves with the result or undefined (errors already reported). */
  FE.act = (qlc, method, params, opts) => FE.mutate(qlc, method, params, opts).catch(() => undefined);

  /* ---- lookups --------------------------------------------------------------------------------- */
  FE.channelById = (def, id) => (def && def.channels.find(c => c.channelId === id)) || null;
  FE.modeById = (def, id) => (def && def.modes.find(m => m.modeId === id)) || null;
  FE.channelsSorted = (def) => (def ? def.channels.slice() : []);
  FE.aliasCapabilities = function (def) {
    const out = [];
    (def ? def.channels : []).forEach(ch => ch.capabilities.forEach((cap, i) => { if (cap.preset === 'Alias') out.push({ ch, cap, capIndex: i }); }));
    return out;
  };

  /** base64 <-> bytes for import/export. */
  FE.bytesToBase64 = function (buf) {
    const bytes = new Uint8Array(buf); let bin = '';
    for (let i = 0; i < bytes.length; i += 0x8000) bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(bin);
  };
  FE.downloadBase64 = function (fileName, b64) {
    const bin = atob(b64), bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    const url = URL.createObjectURL(new Blob([bytes], { type: 'application/xml' }));
    const a = document.createElement('a'); a.href = url; a.download = fileName; document.body.appendChild(a); a.click();
    setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 1000);
  };

  /* ---- small shared UI bits -------------------------------------------------------------------- */
  const DS = window.PatchDesignSystem_5432c9;
  FE.inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-control)', color: 'var(--fg-main)', border: '1px solid var(--spin-border)',
    borderRadius: 'var(--radius-spin)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px', outline: 'none' };

  /** Label | control row (PhysicalProperties.qml / ChannelEditor.qml grid). */
  FE.Row = function Row({ label, width = 120, children, title }) {
    return (
      <div title={title} style={{ display: 'flex', alignItems: 'center', gap: 8, minHeight: 28 }}>
        <DS.RobotoText label={label} fontSize={14} height={26} labelColor="var(--fg-light)" style={{ width, minWidth: width, flex: 'none' }} />
        <div style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'center', gap: 6 }}>{children}</div>
      </div>
    );
  };

  /** Text field that commits on Enter / blur only when the value changed. */
  FE.Text = function Text({ value, onCommit, width = '100%', placeholder, disabled, list, ...rest }) {
    const [v, setV] = React.useState(value == null ? '' : String(value));
    const focused = React.useRef(false);
    React.useEffect(() => { if (!focused.current) setV(value == null ? '' : String(value)); }, [value]);
    const commit = () => { const t = v; if (t !== (value == null ? '' : String(value))) onCommit(t); };
    return (
      <input value={v} placeholder={placeholder} disabled={disabled} list={list} {...rest}
        onFocus={() => { focused.current = true; }}
        onBlur={() => { focused.current = false; commit(); }}
        onChange={e => setV(e.target.value)}
        onKeyDown={e => { if (e.key === 'Enter') { e.preventDefault(); commit(); e.target.blur(); } else if (e.key === 'Escape') { setV(value == null ? '' : String(value)); } }}
        style={Object.assign({}, FE.inputStyle, { width, opacity: disabled ? .5 : 1 })} />
    );
  };

  /** Number field with an explicit step (weights / lens degrees are doubles; the DS spin box is integer-only). */
  FE.Num = function Num({ value, onCommit, width = 110, step = 1, min = 0, max = 999999, suffix, disabled, ...rest }) {
    return (
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
        <FE.Text value={value} width={width} disabled={disabled} type="number" step={step} min={min} max={max} {...rest}
          onCommit={t => { const n = Number(t); if (t !== '' && !Number.isNaN(n)) onCommit(Math.max(min, Math.min(max, n))); }} />
        {suffix ? <DS.RobotoText label={suffix} fontSize={13} labelColor="var(--fg-light)" height={24} /> : null}
      </span>
    );
  };

  /** Small list row used by every list in the editor. */
  FE.ListRow = function ListRow({ selected, onClick, onDoubleClick, children, style, ...rest }) {
    const [hover, setHover] = React.useState(false);
    return (
      <div onClick={onClick} onDoubleClick={onDoubleClick} onMouseEnter={() => setHover(true)} onMouseLeave={() => setHover(false)} {...rest}
        style={Object.assign({ display: 'flex', alignItems: 'center', gap: 6, minHeight: 28, padding: '0 6px', cursor: 'pointer', borderBottom: '1px solid var(--bg-medium)',
          background: selected ? 'var(--highlight)' : hover ? 'var(--bg-control)' : 'transparent', borderRadius: 3 }, style)}>{children}</div>
    );
  };

  /** Sub toolbar with + / - style buttons (the QML list toolbars). */
  FE.ListToolbar = function ListToolbar({ children }) {
    return <div style={{ display: 'flex', alignItems: 'center', gap: 4, height: 34, padding: '0 4px', background: 'var(--gradient-toolbar-sub)', flex: 'none' }}>{children}</div>;
  };
})();
