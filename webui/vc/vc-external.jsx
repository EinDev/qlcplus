/**
 * Virtual Console — external controls: input sources (MIDI / OSC / DMX-in / ... controller mapping),
 * auto-detection, manual input source selection, custom feedback, keyboard combinations, and the
 * browser-side key bindings in Operate mode.
 *
 *  - window.QLCVCPropertiesCommon: VCExternalControls, the "External controls" section every widget's
 *    property panel gets (ExternalControls.qml + ExternalControlDelegate.qml + KeyboardSequenceDelegate.qml
 *    + PopupManualInputSource.qml + PopupCustomFeedback.qml). One row per bound input source / key
 *    combination; the toolbar adds a source by auto-detection (vc.widget.inputDetect.start), a key
 *    combination by pressing it in the browser, or a source picked by hand (universe / channel, or a
 *    channel of the input profile patched on that universe).
 *  - window.QLCVCScreenAddons: VCKeyBindings, mounted by VirtualConsole.jsx inside its context. The
 *    server cannot see the browser's keyboard, so the web UI honours the widgets' key sequences
 *    itself: while the VC screen is shown, edit mode is off and no text field has focus, a key
 *    combination bound on a widget of the current page triggers that widget through the regular live
 *    methods (vc.button.press on down + up, cue list next / previous / play / stop, frame page next /
 *    previous / shortcut / enable / collapse, speed dial tap / factor / apply / presets, slider flash).
 *    It listens in the capture phase and swallows a matched key, so a widget binding wins over the
 *    App-level shortcuts (Ctrl+1..5, Space tap, Ctrl+B/S/Z/Y) exactly like the desktop VC does; an
 *    unbound key falls through to them untouched.
 *
 * Key text is Qt's portable spelling ("Ctrl+Shift+K", "F", "Space", "Return", "PgUp", "F5"), which is
 * what the server stores (QKeySequence::PortableText) and what the .qxw carries. Matching parses both
 * sides into modifiers + key, so modifier order never matters.
 *
 * Loaded after vc-props-cue.jsx and before VirtualConsole.jsx (which reads the registries).
 */
const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

/* Font Awesome 7 Solid codepoints missing from the bundle's FA map (FaIcon renders a raw glyph). */
const VCX_GLYPH = { handPointUp: '', wand: '', minus: '' };

/* ---------------------------------------------------------------- key text */
const VCX_MOD_KEYS = { Control: 1, Shift: 1, Alt: 1, Meta: 1, AltGraph: 1, CapsLock: 1, NumLock: 1, ScrollLock: 1, OS: 1, Fn: 1 };
/* KeyboardEvent.key -> Qt portable key name. Letters / digits come from KeyboardEvent.code (layout
   independent, Qt reports Key_K for Shift+K too); other printable characters are taken as typed. */
const VCX_KEY_NAMES = { ' ': 'Space', Enter: 'Return', Escape: 'Esc', Delete: 'Del', Insert: 'Ins', PageUp: 'PgUp', PageDown: 'PgDown',
  ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down', Backspace: 'Backspace', Tab: 'Tab', Home: 'Home', End: 'End',
  Pause: 'Pause', PrintScreen: 'Print', ContextMenu: 'Menu' };
const VCX_ALIASES = { ESCAPE: 'ESC', DELETE: 'DEL', INSERT: 'INS', PAGEUP: 'PGUP', PAGEDOWN: 'PGDOWN', ENTER: 'RETURN', SPACEBAR: 'SPACE', CONTROL: 'CTRL' };

/** KeyboardEvent -> "Ctrl+Shift+K" (null for a bare modifier press). */
function vcxKeyText(e) {
  if (!e || VCX_MOD_KEYS[e.key]) return null;
  let key;
  const code = e.code || '';
  if (/^Key[A-Z]$/.test(code)) key = code.slice(3);
  else if (/^Digit[0-9]$/.test(code) && !e.shiftKey) key = code.slice(5);
  else if (/^Numpad[0-9]$/.test(code)) key = code.slice(6);
  else if (VCX_KEY_NAMES[e.key]) key = VCX_KEY_NAMES[e.key];
  else if (/^F([1-9]|[12][0-9]|3[0-5])$/.test(e.key)) key = e.key;
  else if (e.key && e.key.length === 1) key = e.key.toUpperCase();
  else return null;
  const mods = [];
  if (e.ctrlKey) mods.push('Ctrl');
  if (e.altKey) mods.push('Alt');
  if (e.shiftKey) mods.push('Shift');
  if (e.metaKey) mods.push('Meta');
  return mods.concat([key]).join('+');
}

/** "Ctrl+Shift+K" / "shift+ctrl+k" / "Ctrl++" -> canonical "CTRL|SHIFT|K" for comparisons. */
function vcxNormalize(text) {
  if (!text) return '';
  let s = String(text).trim();
  let key = null;
  if (s === '+') return '+';
  if (s.endsWith('++')) { key = '+'; s = s.slice(0, -2); }
  const parts = s.split('+').map(p => p.trim().toUpperCase()).filter(Boolean).map(p => VCX_ALIASES[p] || p);
  if (key == null) key = parts.pop() || '';
  const mods = ['CTRL', 'ALT', 'SHIFT', 'META'].filter(m => parts.indexOf(m) !== -1);
  return mods.concat([VCX_ALIASES[key.toUpperCase()] || key.toUpperCase()]).join('|');
}

function vcxTyping() {
  const el = document.activeElement;
  return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT' || el.isContentEditable);
}

/* ---------------------------------------------------------------- small pieces */
const vcxRowStyle = { display: 'flex', alignItems: 'center', gap: 4, minHeight: 'var(--list-item-height)', padding: '1px 6px' };
const vcxBox = (active) => ({ flex: 1, minWidth: 0, height: 24, display: 'flex', alignItems: 'center', padding: '0 6px', borderRadius: 'var(--radius-spin)',
  background: active ? 'var(--selection)' : 'var(--bg-light)', color: active ? 'var(--bg-strong)' : 'var(--fg-main)',
  font: '400 var(--text-size-small)/1 var(--font-roboto)', overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis',
  animation: active ? 'vcx-blink 1s ease-in-out infinite alternate' : 'none' });

function VCXLabel({ text, width = 62 }) {
  return <RobotoText label={text} fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 ' + width + 'px' }} />;
}

/** A full-width combo that may shrink inside a flex row (CustomComboBox at width 100% next to a fixed
    label would otherwise overflow the 300px property panel). */
function VCXCombo(props) {
  return <div style={{ flex: 1, minWidth: 0 }}><CustomComboBox width="100%" {...props} /></div>;
}

function controlModel(controls, keyboardOnly) {
  return (controls || []).filter(c => !keyboardOnly || c.allowKeyboard).map(c => ({ mLabel: c.name, mValue: String(c.controlId) }));
}

/* ---------------------------------------------------------------- manual input source */
/**
 * PopupManualInputSource.qml: pick a channel of the input profile patched on a universe, or type a
 * universe / channel by hand. The QML popup always binds control 0; here the control is chosen too.
 */
function VCManualSourceDialog({ open, controls, defaultControl, onClose, onApply }) {
  const vc = useVC();
  const [universes, setUniverses] = React.useState([]);
  const [profiles, setProfiles] = React.useState([]); // [{ universe, universeName, profileName, channels }]
  const [mode, setMode] = React.useState('manual');
  const [uni, setUni] = React.useState(0);
  const [ch, setCh] = React.useState(1);
  const [pick, setPick] = React.useState(null); // { universe, channel }
  const [control, setControl] = React.useState(String(defaultControl));
  const [err, setErr] = React.useState('');
  React.useEffect(() => {
    if (!open) return;
    setErr(''); setPick(null); setControl(String(defaultControl));
    vc.qlc.call('io.universe.list').then(r => {
      const list = r.universes || [];
      setUniverses(list);
      if (list.length && !list.some(u => u.id === uni)) setUni(list[0].id);
      /* Input profile channels: io.universe.get -> inputPatch.profileName -> io.inputProfile.get. */
      return Promise.all(list.filter(u => u.inputPatched).map(u => vc.qlc.call('io.universe.get', { universeId: u.id }).then(d => {
        const patch = (d && (d.inputPatch || (d.universe && d.universe.inputPatch))) || null;
        if (!patch || !patch.profileName) return null;
        return vc.qlc.call('io.inputProfile.get', { name: patch.profileName }).then(p => ({ universe: u.id, universeName: u.name, profileName: patch.profileName,
          channels: ((p.profile || p).channels || []) }));
      }).catch(() => null))).then(rows => {
        const found = rows.filter(Boolean);
        setProfiles(found);
        setMode(found.length ? 'profile' : 'manual');
      });
    }).catch(e => setErr((e && e.message) || 'io.universe.list failed'));
  }, [open]);
  const apply = () => {
    const target = mode === 'profile' ? pick : { universe: Number(uni), channel: Number(ch) - 1 };
    if (!target) { setErr('Pick a channel of an input profile first'); return; }
    Promise.resolve(onApply(Number(control), target.universe, target.channel)).then(onClose).catch(e => setErr((e && e.message) || 'failed'));
  };
  const radio = (value, label) => (
    <div role="button" onClick={() => setMode(value)} data-vcx-manual-mode={value}
      style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', cursor: 'pointer' }}>
      <span style={{ width: 16, height: 16, borderRadius: 8, border: '2px solid var(--fg-medium)', background: mode === value ? 'var(--selection)' : 'transparent', flex: 'none' }} />
      <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" />
    </div>
  );
  return (
    <CustomPopupDialog open={open} title="Manual input source selection" width={440} standardButtons={['Cancel', 'Ok']} onClose={onClose}
      onClicked={(b) => { if (b === 'Ok') apply(); else onClose(); }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }} data-vcx-manual="">
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <VCXLabel text="Control" width={80} />
          <VCXCombo height={24} currValue={control} onValueChanged={setControl} model={controlModel(controls)} data-vcx-manual-control="" />
        </div>
        {radio('profile', 'Input profiles')}
        <div style={{ maxHeight: 220, overflow: 'auto', border: 'var(--border-dark)', opacity: mode === 'profile' ? 1 : .45, pointerEvents: mode === 'profile' ? 'auto' : 'none' }}>
          {!profiles.length ? <RobotoText label="No universe has an input line with an input profile patched" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} /> : null}
          {profiles.map(p => (
            <div key={p.universe}>
              <RobotoText label={(p.universeName || 'Universe ' + (p.universe + 1)) + ' — ' + p.profileName} fontSize="var(--text-size-small)" height="var(--list-item-height)" leftMargin={6} style={{ background: 'var(--bg-medium)' }} />
              {p.channels.map(c => {
                const sel = pick && pick.universe === p.universe && pick.channel === c.number;
                return (
                  <div key={c.number} role="button" onClick={() => setPick({ universe: p.universe, channel: c.number })} data-vcx-profile-ch={p.universe + ':' + c.number}
                    style={{ display: 'flex', gap: 6, alignItems: 'center', height: 'var(--list-item-height)', padding: '0 6px 0 20px', cursor: 'pointer', background: sel ? 'var(--highlight)' : 'transparent' }}>
                    <RobotoText label={(c.number + 1) + ': ' + c.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                    <RobotoText label={c.type} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
                  </div>
                );
              })}
            </div>
          ))}
        </div>
        {radio('manual', 'Manual selection')}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4, opacity: mode === 'manual' ? 1 : .45, pointerEvents: mode === 'manual' ? 'auto' : 'none' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <VCXLabel text="Universe" width={80} />
            <VCXCombo height={24} currValue={Number(uni)} onValueChanged={(v) => setUni(Number(v))}
              model={(universes.length ? universes : [{ id: 0, name: 'Universe 1' }]).map(u => ({ mLabel: u.name || 'Universe ' + (u.id + 1), mValue: u.id }))} />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <VCXLabel text="Channel" width={80} />
            <CustomSpinBox value={ch} from={1} to={65535} width={100} height={24} onValueModified={setCh} data-vcx-manual-channel="" />
          </div>
        </div>
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" wrapText height="auto" /> : null}
      </div>
    </CustomPopupDialog>
  );
}

/* ---------------------------------------------------------------- custom feedback */
/**
 * PopupCustomFeedback.qml: lower / upper / monitor feedback values sent back to the controller
 * (0..255), picked from the input profile's colour table when it has one, plus the per-value MIDI
 * channel routing when the profile is a MIDI one with a channel table.
 */
function VCCustomFeedbackDialog({ source, onClose, onApply }) {
  const vc = useVC();
  const [vals, setVals] = React.useState({ lowerValue: 0, upperValue: 255, monitorValue: 255 });
  const [routes, setRoutes] = React.useState({ lowerChannel: 0, upperChannel: 0, monitorChannel: 0 });
  const [profile, setProfile] = React.useState(null);
  const [selected, setSelected] = React.useState(null);
  const [err, setErr] = React.useState('');
  React.useEffect(() => {
    if (!source) return;
    setErr(''); setSelected(null); setProfile(null);
    setVals({ lowerValue: source.lowerValue != null ? source.lowerValue : 0, upperValue: source.upperValue != null ? source.upperValue : 255, monitorValue: source.monitorValue != null ? source.monitorValue : 255 });
    setRoutes({ lowerChannel: source.lowerChannel || 0, upperChannel: source.upperChannel || 0, monitorChannel: source.monitorChannel || 0 });
    vc.qlc.call('io.universe.get', { universeId: Number(source.universe) }).then(d => {
      const patch = (d && (d.inputPatch || (d.universe && d.universe.inputPatch))) || null;
      if (!patch || !patch.profileName) return;
      return vc.qlc.call('io.inputProfile.get', { name: patch.profileName }).then(p => setProfile(p.profile || p));
    }).catch(() => {});
  }, [source]);
  if (!source) return null;
  const colors = (profile && profile.colorTable) || [];
  const midi = profile && profile.type === 'MIDI' ? (profile.midiChannelTable || []) : [];
  const colorOf = (v) => { const c = colors.find(x => Number(x.value) === Number(v)); return c ? c.color : null; };
  const row = (key, label) => (
    <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
      <VCXLabel text={label} width={100} />
      <CustomSpinBox value={vals[key]} from={0} to={255} width={80} height={24} onValueModified={(v) => setVals(s => Object.assign({}, s, { [key]: v }))} data-vcx-fb={key} />
      {colors.length ? (
        <div role="button" onClick={() => setSelected(key)} title="Pick from the profile colour table"
          style={{ width: 48, height: 22, cursor: 'pointer', background: colorOf(vals[key]) || 'black', border: '2px solid ' + (selected === key ? 'var(--selection)' : 'var(--fg-main)') }} />
      ) : null}
    </div>
  );
  const routeRow = (key, label) => (
    <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
      <VCXLabel text={label} width={100} />
      <VCXCombo height={24} currValue={routes[key]} onValueChanged={(v) => setRoutes(s => Object.assign({}, s, { [key]: Number(v) }))}
        model={[{ mLabel: 'From plugin settings', mValue: 0 }].concat(midi.map((m, i) => ({ mLabel: m.label, mValue: i + 1 })))} />
    </div>
  );
  const apply = () => {
    const patch = Object.assign({}, vals, midi.length ? routes : {});
    Promise.resolve(onApply(patch)).then(onClose).catch(e => setErr((e && e.message) || 'failed'));
  };
  return (
    <CustomPopupDialog open title="Custom Feedback" width={colors.length ? 560 : 380} standardButtons={['Cancel', 'Ok']} onClose={onClose}
      onClicked={(b) => { if (b === 'Ok') apply(); else onClose(); }}>
      <div style={{ display: 'flex', gap: 10 }} data-vcx-feedback="">
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6, flex: 1 }}>
          <RobotoText label={'Values' + (source.channelName ? ' — ' + source.universeName + ' / ' + source.channelName : '')} fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="auto" />
          {row('lowerValue', 'Lower Value')}
          {row('upperValue', 'Upper Value')}
          {row('monitorValue', 'Monitor Value')}
          {midi.length ? <RobotoText label="MIDI Channel" fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="auto" style={{ marginTop: 6 }} /> : null}
          {midi.length ? [routeRow('lowerChannel', 'Lower Channel'), routeRow('upperChannel', 'Upper Channel'), routeRow('monitorChannel', 'Monitor Channel')] : null}
          {!profile ? <RobotoText label="No input profile on this universe: the values are sent as they are." fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" /> : null}
          {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" wrapText height="auto" /> : null}
        </div>
        {colors.length && selected ? (
          <div style={{ flex: 1, maxHeight: 240, overflow: 'auto', border: 'var(--border-dark)' }}>
            {colors.map(c => (
              <div key={c.value} role="button" onClick={() => setVals(s => Object.assign({}, s, { [selected]: Number(c.value) }))}
                style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer' }}>
                <RobotoText label={String(c.value)} fontSize="var(--text-size-small)" height="100%" style={{ width: 32 }} />
                <RobotoText label={c.label} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
                <span style={{ width: 40, height: 18, background: c.color }} />
              </div>
            ))}
          </div>
        ) : null}
      </div>
    </CustomPopupDialog>
  );
}

/* ---------------------------------------------------------------- the section */
/**
 * ExternalControls.qml for one widget. Its data comes from vc.widget.get and follows
 * vc.widget.inputSourcesChanged / keySequencesChanged, so another client's (or the desktop app's
 * auto-detected) change shows up immediately.
 */
function VCExternalControls({ w, section }) {
  const vc = useVC();
  const qlc = vc.qlc;
  const [detail, setDetail] = React.useState({ externalControls: w.externalControls || [], inputSources: w.inputSources || [], keySequences: w.keySequences || [] });
  const [detect, setDetect] = React.useState(null);   // { controlId, replace: source|null, before: [...] }
  const [capture, setCapture] = React.useState(null); // { controlId, replace: keySequence|null }
  const [manual, setManual] = React.useState(false);
  const [feedbackFor, setFeedbackFor] = React.useState(null);
  const [msg, setMsg] = React.useState('');
  const detectRef = React.useRef(null);
  detectRef.current = detect;
  const myId = qlc.serverInfo && qlc.serverInfo.clientId;

  React.useEffect(() => {
    let alive = true;
    setDetail({ externalControls: w.externalControls || [], inputSources: w.inputSources || [], keySequences: w.keySequences || [] });
    setDetect(null); setCapture(null); setMsg('');
    qlc.call('vc.widget.get', { widgetId: String(w.id) }).then(d => { if (alive) setDetail({ externalControls: d.externalControls || [], inputSources: d.inputSources || [], keySequences: d.keySequences || [] }); }).catch(() => {});
    const offs = [
      qlc.subscribeTo('vc.widget.inputSourcesChanged', (d, origin) => {
        if (!d || String(d.widgetId) !== String(w.id)) return;
        const next = d.inputSources || [];
        const pending = detectRef.current;
        setDetail(x => Object.assign({}, x, { inputSources: next }));
        /* Our own auto-detection landed: a re-learn of an existing row replaces the old source. */
        if (pending) {
          const added = next.filter(s => !pending.before.some(b => b.universe === s.universe && b.channel === s.channel));
          if (added.length) {
            setDetect(null);
            const r = pending.replace;
            if (r && !added.some(s => s.universe === r.universe && s.channel === r.channel))
              vcStructural(qlc, 'vc.widget.inputSource.remove', { widgetId: String(w.id), controlId: r.controlId, universe: r.universe, channel: r.channel }).catch(() => {});
          }
        }
      }),
      qlc.subscribeTo('vc.widget.keySequencesChanged', (d) => { if (d && String(d.widgetId) === String(w.id)) setDetail(x => Object.assign({}, x, { keySequences: d.keySequences || [] })); })
    ];
    return () => { alive = false; offs.forEach(f => f()); };
  }, [w.id]);

  /* Leaving the widget (or the panel) with a detection armed releases the server's single slot. */
  React.useEffect(() => () => { if (detectRef.current) qlc.call('vc.widget.inputDetect.stop', {}).catch(() => {}); }, [w.id]);

  const fail = (what) => (e) => setMsg(what + ': ' + ((e && e.message) || 'failed'));
  const setSource = (params) => vcStructural(qlc, 'vc.widget.inputSource.set', Object.assign({ widgetId: String(w.id) }, params));
  const removeSource = (s) => vcStructural(qlc, 'vc.widget.inputSource.remove', { widgetId: String(w.id), controlId: s.controlId, universe: s.universe, channel: s.channel }).catch(fail('Remove input source'));
  const setKey = (controlId, keySequence) => vcStructural(qlc, 'vc.widget.keySequence.set', { widgetId: String(w.id), controlId: Number(controlId), keySequence });
  const removeKey = (keySequence) => vcStructural(qlc, 'vc.widget.keySequence.remove', { widgetId: String(w.id), keySequence });

  const startDetect = (controlId, replace) => {
    setMsg('');
    qlc.call('vc.widget.inputDetect.start', { widgetId: String(w.id), controlId: Number(controlId) })
      .then(() => setDetect({ controlId: Number(controlId), replace: replace || null, before: detail.inputSources.slice() }))
      .catch(e => { setDetect(null); setMsg(e && e.code === 'INVALID_STATE' ? 'Another client is auto-detecting an input right now' : 'Auto-detection: ' + ((e && e.message) || 'failed')); });
  };
  const stopDetect = () => { setDetect(null); qlc.call('vc.widget.inputDetect.stop', {}).catch(() => {}); };

  /* Key capture: the next non-modifier key pressed anywhere in the page is the combination. Swallowed
     in the capture phase so it neither triggers an App shortcut nor lands in a text field. */
  React.useEffect(() => {
    if (!capture) return;
    const onKey = (e) => {
      if (VCX_MOD_KEYS[e.key]) return;
      e.preventDefault(); e.stopImmediatePropagation();
      if (e.type !== 'keydown' || e.repeat) return;
      const text = vcxKeyText(e);
      if (!text) return;
      const c = capture;
      setCapture(null);
      if (c.replace && vcxNormalize(c.replace) === vcxNormalize(text)) return;
      setKey(c.controlId, text).then(() => c.replace ? removeKey(c.replace) : null).catch(fail('Key combination'));
    };
    const swallow = (e) => { if (!VCX_MOD_KEYS[e.key]) { e.preventDefault(); e.stopImmediatePropagation(); } };
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('keyup', swallow, true);
    return () => { window.removeEventListener('keydown', onKey, true); window.removeEventListener('keyup', swallow, true); };
  }, [capture]);

  const controls = detail.externalControls || [];
  const kbControls = controls.filter(c => c.allowKeyboard);
  if (!controls.length) return null; // Label / Clock: nothing to bind (the QML panel is empty too)
  const nameOf = (id) => { const c = controls.find(x => Number(x.controlId) === Number(id)); return c ? c.name : 'Control ' + id; };
  const D = window.QLCData;

  const body = (
    <div data-vcx-panel={w.id}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 6px', background: 'var(--bg-medium)' }}>
        <span style={{ flex: 1 }} />
        <IconButton imgSource={D.icon('inputoutput')} size={28} checked={!!(detect && !detect.replace)} tooltip="Add an external controller input (auto-detect: move a control on the controller)"
          onClick={() => detect && !detect.replace ? stopDetect() : startDetect(controls[0].controlId)} data-vcx-add-detect="" />
        <IconButton imgSource={D.icon('keybinding')} size={28} checked={!!(capture && !capture.replace)} disabled={!kbControls.length} tooltip="Add a keyboard combination (press it next)"
          onClick={() => setCapture(capture && !capture.replace ? null : { controlId: kbControls[0].controlId, replace: null })} data-vcx-add-key="" />
        <IconButton faSource={VCX_GLYPH.handPointUp} size={28} tooltip="Manually select an input source" onClick={() => setManual(true)} data-vcx-add-manual="" />
      </div>
      {detect && !detect.replace ? (
        <div data-vcx-pending-detect="" style={{ borderBottom: '2px solid var(--fg-medium)', padding: '2px 0' }}>
          <div style={vcxRowStyle}>
            <VCXLabel text="Control" />
            <VCXCombo height={24} currValue={String(detect.controlId)} model={controlModel(controls)}
              onValueChanged={(v) => { if (Number(v) !== detect.controlId) startDetect(v); }} />
          </div>
          <div style={vcxRowStyle}>
            <VCXLabel text="Input" />
            <span style={vcxBox(true)}>Waiting for input…</span>
            <IconButton faSource="fa_xmark" size={24} tooltip="Cancel the auto detection" onClick={stopDetect} data-vcx-detect-cancel="" />
          </div>
        </div>
      ) : null}
      {capture && !capture.replace ? (
        <div data-vcx-pending-key="" style={{ borderBottom: '2px solid var(--fg-medium)', padding: '2px 0' }}>
          <div style={vcxRowStyle}>
            <VCXLabel text="Control" />
            <VCXCombo height={24} currValue={String(capture.controlId)} model={controlModel(controls, true)}
              onValueChanged={(v) => setCapture({ controlId: Number(v), replace: null })} />
          </div>
          <div style={vcxRowStyle}>
            <VCXLabel text="Keys" />
            <span style={vcxBox(true)}>Press a key combination…</span>
            <IconButton faSource="fa_xmark" size={24} tooltip="Cancel" onClick={() => setCapture(null)} data-vcx-key-cancel="" />
          </div>
        </div>
      ) : null}
      {(detail.inputSources || []).map((s, i) => {
        const relearning = detect && detect.replace && detect.replace.universe === s.universe && detect.replace.channel === s.channel;
        return (
          <div key={'s' + i + ':' + s.universe + ':' + s.channel} data-vcx-source={s.controlId + ':' + s.universe + ':' + s.channel} style={{ borderBottom: '2px solid var(--fg-medium)', padding: '2px 0' }}>
            <div style={vcxRowStyle}>
              <VCXLabel text="Control" />
              <VCXCombo height={24} currValue={String(s.controlId)} model={controlModel(controls)} data-vcx-source-control=""
                onValueChanged={(v) => { if (Number(v) !== Number(s.controlId)) setSource({ controlId: Number(v), universe: s.universe, channel: s.channel }).catch(fail('Change control')); }} />
              <IconButton faSource={VCX_GLYPH.wand} size={24} checked={!!relearning} tooltip="Activate auto detection (the next controller signal replaces this source)"
                onClick={() => relearning ? stopDetect() : startDetect(s.controlId, s)} />
            </div>
            <div style={vcxRowStyle}>
              <VCXLabel text="Universe" />
              <span style={vcxBox(relearning)} title={'Universe ' + (Number(s.universe) + 1)}>{s.invalid ? 'None' : (s.universeName || 'Universe ' + (Number(s.universe) + 1))}</span>
              <IconButton faSource={VCX_GLYPH.minus} faColor="crimson" size={24} tooltip="Remove this input source" onClick={() => removeSource(s)} data-vcx-source-remove="" />
            </div>
            <div style={vcxRowStyle}>
              <VCXLabel text="Channel" />
              <span style={vcxBox(relearning)} title={'Channel ' + ((Number(s.channel) & 0xFFFF) + 1) + (Number(s.channel) >> 16 ? ' (frame page ' + ((Number(s.channel) >>> 16) + 1) + ')' : '')}>
                {s.invalid ? 'None' : (s.channelName || 'Channel ' + ((Number(s.channel) & 0xFFFF) + 1))}
              </span>
              <IconButton imgSource={D.icon('inputoutput')} size={24} tooltip={'Custom feedback selection (lower ' + s.lowerValue + ', upper ' + s.upperValue + ', monitor ' + s.monitorValue + ')'}
                onClick={() => setFeedbackFor(s)} data-vcx-source-feedback="" />
            </div>
          </div>
        );
      })}
      {(detail.keySequences || []).map(k => {
        const capturing = capture && capture.replace === k.keySequence;
        return (
          <div key={'k' + k.keySequence} data-vcx-key={k.keySequence} style={{ borderBottom: '2px solid var(--fg-medium)', padding: '2px 0' }}>
            <div style={vcxRowStyle}>
              <VCXLabel text="Control" />
              <VCXCombo height={24} currValue={String(k.controlId)} model={controlModel(controls, true)}
                onValueChanged={(v) => { if (Number(v) !== Number(k.controlId)) setKey(v, k.keySequence).catch(fail('Change control')); }} />
            </div>
            <div style={vcxRowStyle}>
              <VCXLabel text="Keys" />
              <span style={Object.assign(vcxBox(capturing), { fontFamily: 'var(--font-mono)' })} data-vcx-key-text="">{capturing ? 'Press a key combination…' : k.keySequence}</span>
              <IconButton imgSource={D.icon('keybinding')} size={24} checked={!!capturing} tooltip="Activate auto detection (press the new combination next)"
                onClick={() => setCapture(capturing ? null : { controlId: Number(k.controlId), replace: k.keySequence })} />
              <IconButton faSource={VCX_GLYPH.minus} faColor="crimson" size={24} tooltip="Remove this keyboard combination" onClick={() => removeKey(k.keySequence).catch(fail('Remove key'))} data-vcx-key-remove="" />
            </div>
          </div>
        );
      })}
      {!(detail.inputSources || []).length && !(detail.keySequences || []).length && !detect && !capture
        ? <RobotoText label={'No external controls yet. Controls: ' + controls.map(c => c.name).join(', ')} fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} /> : null}
      {msg ? <RobotoText label={msg} fontSize="var(--text-size-small)" labelColor="var(--selection)" wrapText height="auto" style={{ padding: 6 }} /> : null}
      <VCManualSourceDialog open={manual} controls={controls} defaultControl={controls[0].controlId} onClose={() => setManual(false)}
        onApply={(controlId, universe, channel) => setSource({ controlId, universe, channel })} />
      <VCCustomFeedbackDialog source={feedbackFor} onClose={() => setFeedbackFor(null)}
        onApply={(patch) => setSource(Object.assign({ controlId: feedbackFor.controlId, universe: feedbackFor.universe, channel: feedbackFor.channel }, patch))} />
    </div>
  );
  return section('external', 'External controls', body);
}

/* ---------------------------------------------------------------- Operate-mode key bindings */
const VCX_FACTORS = ['OneSixteenth', 'OneEighth', 'OneFourth', 'Half', 'One', 'Two', 'Four', 'Eight', 'Sixteen'];

/**
 * VCPage::handleKeyEvent() in the browser: every widget of the shown page bound to the pressed
 * combination gets the control's action (value 255 on key down, 0 on key up, like the engine feeds
 * slotInputValueChanged()). Hidden widgets (another frame page) are skipped, disabled ones too except
 * frames (their Enable control must still work), exactly like the desktop VC.
 */
function VCKeyBindings() {
  const vc = useVC();
  const ref = React.useRef(vc);
  ref.current = vc;
  const held = React.useRef({}); // normalized key -> [{w, controlId}] fired on key down, released on key up
  const [cast, setCast] = React.useState(null);
  const castTimer = React.useRef(null);

  /* Bindings change on vc.widget.keySequencesChanged: re-read the page's widget list. */
  React.useEffect(() => {
    if (!vc.qlc.online) return;
    const offs = [vc.qlc.subscribeTo('vc.widget.keySequencesChanged', () => ref.current.refresh()),
      vc.qlc.subscribeTo('vc.widget.inputSourcesChanged', () => ref.current.refresh())];
    return () => offs.forEach(f => f());
  }, [vc.qlc.online]);

  React.useEffect(() => {
    const trigger = (w, controlId, down) => {
      const c = ref.current, q = c.qlc, id = String(w.id), cfg = w.typeConfig || {};
      const ack = (m) => (e) => { if (e && e.code !== 'NOT_CONNECTED') c.notice(m + ': ' + ((e && e.message) || e.code || 'failed')); };
      switch (w.widgetType) {
        case 'Button':
          if (controlId === 0) c.act.press(id, down);
          return;
        case 'Slider':
          if (controlId === 2) q.call('vc.slider.flash', { widgetId: id, on: down }).catch(ack('vc.slider.flash'));
          return;
        default: break;
      }
      if (!down) return; // every other control acts on the key press only (the engine ignores value 0)
      switch (w.widgetType) {
        case 'CueList':
          if (controlId === 0) c.act.cueNext(id);
          else if (controlId === 1) c.act.cuePrev(id);
          else if (controlId === 2) c.act.cuePlay(id);
          else if (controlId === 3) c.act.cueStop(id);
          return;
        case 'Frame': case 'SoloFrame': {
          const live = (c.live.frames || {})[id] || {};
          const pages = Number(live.pages != null ? live.pages : (w.pages != null ? w.pages : cfg.totalPagesNumber)) || 1;
          const cur = Number(live.currentPage != null ? live.currentPage : (w.currentPage || 0));
          const loop = !!cfg.pagesLoop;
          let to = null;
          if (controlId === 0) to = cur + 1 < pages ? cur + 1 : (loop ? 0 : null);
          else if (controlId === 1) to = cur > 0 ? cur - 1 : (loop ? pages - 1 : null);
          else if (controlId >= 20) to = controlId - 20 < pages ? controlId - 20 : null;
          else if (controlId === 2) { vcStructural(q, 'vc.widget.update', { widgetId: id, isDisabled: !w.isDisabled }).then(() => c.refresh()).catch(ack('Frame enable')); return; }
          else if (controlId === 3) { c.editApi.setConfig(id, { isCollapsed: !cfg.isCollapsed }).then(() => c.refresh()); return; }
          if (to != null && to !== cur) c.act.frameGoto(id, to);
          return;
        }
        case 'Speed': case 'SpeedDial': {
          const f = VCX_FACTORS.indexOf(w.factor || cfg.factor || 'One');
          if (controlId === 1) c.act.speedTap(id);
          else if (controlId === 2 || controlId === 3) {
            const next = VCX_FACTORS[Math.max(0, Math.min(VCX_FACTORS.length - 1, (f < 0 ? 4 : f) + (controlId === 2 ? 1 : -1)))];
            q.call('vc.speedDial.setFactor', { widgetId: id, factor: next }).then(() => c.refresh()).catch(ack('vc.speedDial.setFactor'));
          } else if (controlId === 4) q.call('vc.speedDial.setFactor', { widgetId: id, factor: 'One' }).then(() => c.refresh()).catch(ack('vc.speedDial.setFactor'));
          else if (controlId === 5) q.call('vc.speedDial.apply', { widgetId: id }).catch(ack('vc.speedDial.apply'));
          else if (controlId >= 6 && controlId <= 13) q.call('vc.speedDial.setFactor', { widgetId: id, factor: VCX_FACTORS[controlId - 6 + (controlId >= 10 ? 1 : 0)] }).then(() => c.refresh()).catch(ack('vc.speedDial.setFactor'));
          else if (controlId >= 30) q.call('vc.widget.preset.apply', { widgetId: id, presetId: controlId - 30 }).catch(ack('vc.widget.preset.apply'));
          return;
        }
        case 'XYPad': case 'Animation':
          if (controlId >= 30) q.call('vc.widget.preset.apply', { widgetId: id, presetId: controlId - 30 }).catch(ack('vc.widget.preset.apply'));
          else c.notice(w.widgetType + ' key bindings for "' + controlId + '" are not supported in the web UI yet');
          return;
        default:
          c.notice(w.widgetType + ' key bindings are not supported in the web UI yet');
      }
    };
    const matches = (norm) => {
      const c = ref.current, out = [];
      (c.widgets || []).forEach(w => {
        if (!w.keySequences || !w.keySequences.length || w.isVisible === false) return;
        const frame = w.widgetType === 'Frame' || w.widgetType === 'SoloFrame';
        if (w.isDisabled && !frame) return;
        w.keySequences.forEach(k => { if (vcxNormalize(k.keySequence) === norm) out.push({ w, controlId: Number(k.controlId) }); });
      });
      return out;
    };
    const down = (e) => {
      const c = ref.current;
      if (c.edit || !c.qlc.online || vcxTyping() || VCX_MOD_KEYS[e.key]) return;
      const text = vcxKeyText(e);
      if (!text) return;
      const norm = vcxNormalize(text);
      const hits = matches(norm);
      if (!hits.length) return;
      e.preventDefault(); e.stopImmediatePropagation();
      if (e.repeat || held.current[e.code || norm]) return; // auto-repeat is swallowed, never re-fired
      held.current[e.code || norm] = hits;
      hits.forEach(h => trigger(h.w, h.controlId, true));
      const names = hits.map(h => ((h.w.style && h.w.style.caption) || h.w.widgetType) + ': ' + ((h.w.externalControls || []).find(x => Number(x.controlId) === h.controlId) || { name: 'control ' + h.controlId }).name);
      const what = Array.from(new Set(names)).join(', ');
      /* The app-wide key-cast toast (misc/shortcuts.jsx, FeedbackToast.qml's role) shows VC bindings
         and App shortcuts alike; this strip is only the fallback when that file is not loaded. */
      if (window.QLCKeyCast) { window.QLCKeyCast.show(text, what, { source: 'vc' }); return; }
      setCast({ keys: text, what });
      clearTimeout(castTimer.current);
      castTimer.current = setTimeout(() => setCast(null), 1600);
    };
    const up = (e) => {
      const key = e.code || vcxNormalize(vcxKeyText(e));
      const hits = held.current[key];
      if (!hits) return;
      delete held.current[key];
      e.preventDefault(); e.stopImmediatePropagation();
      hits.forEach(h => trigger(h.w, h.controlId, false));
    };
    /* A key released while the window is not focused never sends keyup: release Flash buttons on blur. */
    const blur = () => { Object.keys(held.current).forEach(k => { held.current[k].forEach(h => trigger(h.w, h.controlId, false)); }); held.current = {}; };
    window.addEventListener('keydown', down, true);
    window.addEventListener('keyup', up, true);
    window.addEventListener('blur', blur);
    return () => { window.removeEventListener('keydown', down, true); window.removeEventListener('keyup', up, true); window.removeEventListener('blur', blur); clearTimeout(castTimer.current); };
  }, []);

  if (!cast) return null;
  /* Key-cast strip (FeedbackToast.qml's role): which combination fired what. */
  return (
    <div data-vcx-keycast="" style={{ position: 'absolute', right: 16, bottom: 16, zIndex: 120, pointerEvents: 'none', display: 'flex', alignItems: 'center', gap: 8,
      padding: '6px 12px', background: 'var(--bg-strong)', border: '2px solid var(--selection)', borderRadius: 6, boxShadow: '0 2px 8px rgba(0,0,0,.4)' }}>
      <span style={{ font: '700 var(--text-size-small)/1 var(--font-mono)', color: 'var(--selection)' }}>{cast.keys}</span>
      <RobotoText label={cast.what} fontSize="var(--text-size-small)" height="auto" />
    </div>
  );
}

/* Blinking highlight for rows waiting on a controller signal / a key (the QML delegates pulse red). */
(function () {
  if (document.getElementById('vcx-style')) return;
  const s = document.createElement('style');
  s.id = 'vcx-style';
  s.textContent = '@keyframes vcx-blink { from { opacity: 1 } to { opacity: .55 } }';
  document.head.appendChild(s);
})();

window.QLCVCPropertiesCommon = (window.QLCVCPropertiesCommon || []).concat([VCExternalControls]);
window.QLCVCScreenAddons = (window.QLCVCScreenAddons || []).concat([VCKeyBindings]);
Object.assign(window, { VCExternalControls, VCKeyBindings, VCManualSourceDialog, VCCustomFeedbackDialog, vcxKeyText, vcxNormalize });
