/**
 * App-level keyboard shortcuts for the web UI: qmlui/shortcutmanager.cpp + ShortcutsEditor.qml +
 * FeedbackToast.qml, client-side.
 *
 *  - window.QLCShortcuts: the registry of the actions App.jsx executes itself (ids and default
 *    sequences mirror App::registerShortcuts() in qmlui/app.cpp; the web keeps Ctrl+4 = Input/Output and
 *    Ctrl+5 = Show Manager, see DEFAULTS). User overrides persist in localStorage under
 *    'qlcplus.webui.shortcuts' in the desktop's own qlcplusShortcuts.json format ({ "<actionId>":
 *    "Ctrl+Shift+S" }, Qt portable text, "" = unbound), so a desktop export imports here and back.
 *    match(KeyboardEvent) -> actionId, sequence(id), hint(id) (the <ShortcutHint keys> text),
 *    findCollision(seq, excludeId), set / reset / resetAll / importJson / exportJson,
 *    hintsEnabled / setHintsEnabled, subscribe(fn) -> off, useShortcuts() (re-render on change).
 *  - window.QLCKeyCast.show(primary, secondary, {source}): the one bottom-centre toast (App renders
 *    <KeyCastToast/>); fed by App shortcuts, clickHint(id) (a toolbar button that has a shortcut was
 *    clicked with the mouse) and the Virtual Console key bindings (vc/vc-external.jsx). Gated by the
 *    "Show shortcut hints" toggle, like the desktop.
 *  - ShortcutsEditorDialog: rebind (press a key; Esc cancels, Backspace / Delete clears), collision
 *    warning, reset one / load defaults, import / export JSON, hints toggle. Opened from the Actions
 *    menu or with window.dispatchEvent(new CustomEvent('qlc-open-shortcuts')).
 *
 * Scope: only what App.jsx runs (context switches, file actions, undo / redo, blackout, stop all,
 * DMX dump, tap tempo, fullscreen). The per-screen keys of the other screens (Virtual Console,
 * Simple Desk keypad, Show Manager, ...) are handled by those screens and are not rebindable here.
 */
(function () {
  'use strict';
  const { RobotoText, GenericButton, IconButton, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

  const STORE_KEY = 'qlcplus.webui.shortcuts';
  const HINTS_KEY = 'qlcplus.webui.shortcutHints';

  /* id, default sequence, description (qmlui/app.cpp wording), whileTyping = also fires with focus in a
     text field (the context / safety keys, as before this registry existed). */
  const DEFAULTS = [
    { id: 'context.switchFixturesAndFunctions', seq: 'Ctrl+1', desc: 'Switch to the Fixtures & Functions tab', whileTyping: true },
    { id: 'context.switchVirtualConsole', seq: 'Ctrl+2', desc: 'Switch to the Virtual Console tab', whileTyping: true },
    { id: 'context.switchSimpleDesk', seq: 'Ctrl+3', desc: 'Switch to the Simple Desk tab', whileTyping: true },
    /* The desktop binds Show Manager to Ctrl+4 and Input/Output to Ctrl+5; the web UI has always had
       Input/Output on Ctrl+4 (the Show Manager screen registers Ctrl+5), kept to not move muscle memory. */
    { id: 'context.switchIOManager', seq: 'Ctrl+4', desc: 'Switch to the Input/Output Manager tab', whileTyping: true },
    { id: 'context.switchShowManager', seq: 'Ctrl+5', desc: 'Switch to the Show Manager tab', whileTyping: true, screen: 'show' },
    { id: 'app.save', seq: 'Ctrl+S', desc: 'Save the current project' },
    { id: 'app.saveProjectAs', seq: 'Ctrl+Shift+S', desc: 'Save the project with a new name' },
    { id: 'app.openProject', seq: 'Ctrl+O', desc: 'Open a project' },
    { id: 'app.newProject', seq: 'Ctrl+N', desc: 'Create a new project' },
    { id: 'app.undo', seq: 'Ctrl+Z', desc: 'Undo the last action' },
    { id: 'app.redo', seq: 'Ctrl+Shift+Z', desc: 'Redo the last undone action' },
    { id: 'app.redoAlt', seq: 'Ctrl+Y', desc: 'Redo the last undone action (alternate binding)' },
    { id: 'io.blackoutToggle', seq: 'Ctrl+B', desc: 'Toggle blackout', whileTyping: true },
    { id: 'app.panic', seq: 'Ctrl+.', desc: 'Stop all running functions', whileTyping: true },
    { id: 'app.dmxDump', seq: 'Ctrl+Shift+D', desc: 'Dump DMX values to a Scene' },
    { id: 'app.toggleFullscreen', seq: 'F11', desc: 'Toggle fullscreen mode' },
    /* web only: MainView.qml's BPM panel TAP button */
    { id: 'app.tapTempo', seq: 'Space', desc: 'Tap tempo (internal beat generator)' }
  ];
  /* Combinations Chrome / Firefox keep for themselves: the page never sees the key press. */
  const BROWSER_RESERVED = { 'CTRL|N': 1, 'CTRL|T': 1, 'CTRL|W': 1, 'CTRL|SHIFT|N': 1, 'CTRL|SHIFT|T': 1, 'CTRL|SHIFT|W': 1, 'CTRL|TAB': 1, 'CTRL|SHIFT|TAB': 1 };

  const MOD_KEYS = { Control: 1, Shift: 1, Alt: 1, Meta: 1, AltGraph: 1, CapsLock: 1, NumLock: 1, ScrollLock: 1, OS: 1, Fn: 1 };
  const KEY_NAMES = { ' ': 'Space', Enter: 'Return', Escape: 'Esc', Delete: 'Del', Insert: 'Ins', PageUp: 'PgUp', PageDown: 'PgDown',
    ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down', Backspace: 'Backspace', Tab: 'Tab', Home: 'Home', End: 'End' };
  /** KeyboardEvent -> Qt portable text ("Ctrl+Shift+S"); the Virtual Console's converter when loaded. */
  function keyText(e) {
    if (window.vcxKeyText) return window.vcxKeyText(e);
    if (!e || MOD_KEYS[e.key]) return null;
    const code = e.code || '';
    let key;
    if (/^Key[A-Z]$/.test(code)) key = code.slice(3);
    else if (/^Digit[0-9]$/.test(code) && !e.shiftKey) key = code.slice(5);
    else if (KEY_NAMES[e.key]) key = KEY_NAMES[e.key];
    else if (/^F([1-9]|[12][0-9])$/.test(e.key)) key = e.key;
    else if (e.key && e.key.length === 1) key = e.key.toUpperCase();
    else return null;
    const mods = [];
    if (e.ctrlKey) mods.push('Ctrl');
    if (e.altKey) mods.push('Alt');
    if (e.shiftKey) mods.push('Shift');
    if (e.metaKey) mods.push('Meta');
    return mods.concat([key]).join('+');
  }
  /** Canonical comparison form ("shift+ctrl+s" == "Ctrl+Shift+S"). */
  function normalize(text) {
    if (window.vcxNormalize) return window.vcxNormalize(text);
    if (!text) return '';
    let s = String(text).trim(), key = null;
    if (s === '+') return '+';
    if (s.endsWith('++')) { key = '+'; s = s.slice(0, -2); }
    const parts = s.split('+').map(p => p.trim().toUpperCase()).filter(Boolean);
    if (key == null) key = parts.pop() || '';
    return ['CTRL', 'ALT', 'SHIFT', 'META'].filter(m => parts.indexOf(m) !== -1).concat([key.toUpperCase()]).join('|');
  }

  function load(key, fallback) { try { const v = localStorage.getItem(key); return v == null ? fallback : JSON.parse(v); } catch (e) { return fallback; } }
  function store(key, value) { try { localStorage.setItem(key, JSON.stringify(value)); } catch (e) { /* private window: session only */ } }

  let overrides = load(STORE_KEY, {}) || {};
  let hintsEnabled = load(HINTS_KEY, true) !== false;
  const extra = [];            // actions added at run time (registered screens with a hotkey)
  const listeners = new Set();
  const notify = () => listeners.forEach(fn => { try { fn(); } catch (e) { console.error(e); } });

  const all = () => DEFAULTS.concat(extra);
  const find = (id) => all().find(a => a.id === id) || null;
  const sequence = (id) => {
    const a = find(id);
    if (!a) return '';
    return Object.prototype.hasOwnProperty.call(overrides, id) ? String(overrides[id] || '') : a.seq;
  };

  const S = {
    actions: () => all().map(a => ({ id: a.id, description: a.desc, defaultSequence: a.seq, sequence: sequence(a.id), isDefault: normalize(sequence(a.id)) === normalize(a.seq), screen: a.screen || null })),
    /** A registered screen's Ctrl+<digit> (App.jsx calls this for window.QLCScreens entries). */
    ensureScreenAction(screen) {
      if (!screen || !screen.hotkey) return;
      if (screen.id === 'show') return;
      const id = 'context.switch.' + screen.id;
      if (find(id)) return;
      extra.push({ id, seq: 'Ctrl+' + screen.hotkey, desc: 'Switch to the ' + (screen.label || screen.id) + ' tab', whileTyping: true, screen: screen.id });
    },
    sequence,
    description: (id) => { const a = find(id); return a ? a.desc : ''; },
    /** ShortcutHint `keys` text: "Ctrl+Shift+S" -> "Ctrl Shift S" ('' when unbound). */
    hint: (id) => { const s = sequence(id); return s === '+' ? '+' : s.replace(/\+(?!$)/g, ' '); },
    keyText, normalize,
    isReserved: (seq) => !!BROWSER_RESERVED[normalize(seq)],
    /** The action bound to this key press (null if none); whileTyping actions only when typing. */
    match(e, typing) {
      const text = keyText(e);
      if (!text) return null;
      const n = normalize(text);
      const hit = all().find(a => { const s = sequence(a.id); return s && normalize(s) === n; });
      if (!hit) return null;
      if (typing && !hit.whileTyping) return null;
      return hit.id;
    },
    findCollision(seq, excludeId) {
      const n = normalize(seq);
      if (!n) return null;
      const hit = all().find(a => a.id !== excludeId && sequence(a.id) && normalize(sequence(a.id)) === n);
      return hit ? { id: hit.id, description: hit.desc } : null;
    },
    set(id, seq) {
      const a = find(id);
      if (!a) return;
      if (normalize(seq) === normalize(a.seq)) delete overrides[id]; else overrides[id] = seq || '';
      store(STORE_KEY, overrides); notify();
    },
    reset(id) { delete overrides[id]; store(STORE_KEY, overrides); notify(); },
    resetAll() { overrides = {}; store(STORE_KEY, overrides); notify(); },
    /** The overrides only, like ShortcutManager::exportOverrides(). */
    exportJson: () => JSON.stringify(overrides, null, 4),
    /** Replaces every override (ShortcutManager::importOverrides()); throws on malformed JSON. */
    importJson(text) {
      const obj = JSON.parse(text);
      if (!obj || typeof obj !== 'object' || Array.isArray(obj)) throw new Error('Expected a JSON object of "actionId": "sequence" pairs');
      const next = {};
      Object.keys(obj).forEach(k => { next[k] = obj[k] == null ? '' : String(obj[k]); });
      overrides = next; store(STORE_KEY, overrides); notify();
    },
    hintsEnabled: () => hintsEnabled,
    setHintsEnabled(on) { hintsEnabled = !!on; store(HINTS_KEY, hintsEnabled); notify(); },
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    /** Key-cast of an action that just fired from the keyboard. */
    cast(id) { if (hintsEnabled && window.QLCKeyCast) window.QLCKeyCast.show(sequence(id), S.description(id), { source: 'app' }); },
    /** Mouse click on a control that has a shortcut: "Tip: shortcut for ...". */
    clickHint(id) {
      const s = sequence(id);
      if (hintsEnabled && s && window.QLCKeyCast) window.QLCKeyCast.show(s, 'Tip: shortcut for "' + S.description(id) + '"', { source: 'hint' });
    }
  };
  function useShortcuts() {
    const [, bump] = React.useState(0);
    React.useEffect(() => S.subscribe(() => bump(n => n + 1)), []);
    return S;
  }
  S.useShortcuts = useShortcuts;

  /* ---------------------------------------------------------------- key-cast toast */
  const castListeners = new Set();
  window.QLCKeyCast = {
    show(primary, secondary, opts) {
      if (!hintsEnabled) return;
      const item = { primary: String(primary || ''), secondary: String(secondary || ''), source: (opts && opts.source) || 'app', at: Date.now() };
      castListeners.forEach(fn => fn(item));
    }
  };
  /** FeedbackToast.qml: bold key text + description, bottom centre, never takes input, 1.5 s hold. */
  function KeyCastToast() {
    const [item, setItem] = React.useState(null);
    const timer = React.useRef(null);
    React.useEffect(() => {
      const fn = (it) => { setItem(it); clearTimeout(timer.current); timer.current = setTimeout(() => setItem(null), 1600); };
      castListeners.add(fn);
      return () => { castListeners.delete(fn); clearTimeout(timer.current); };
    }, []);
    if (!item) return null;
    const vc = item.source === 'vc' ? { 'data-vcx-keycast': '' } : {};
    return (
      <div data-keycast={item.source} {...vc} style={{ position: 'absolute', left: '50%', bottom: 'calc(var(--icon-size-default) * 1.5)', transform: 'translateX(-50%)', zIndex: 200,
        pointerEvents: 'none', display: 'flex', alignItems: 'center', gap: 10, maxWidth: 'calc(100% - var(--icon-size-default))', padding: '6px 14px',
        background: 'var(--bg-strong)', border: '2px solid var(--selection)', borderRadius: 6, boxShadow: '0 2px 8px rgba(0,0,0,.4)' }}>
        <span style={{ font: '700 var(--text-size-default)/1 var(--font-roboto)', color: 'var(--selection)', whiteSpace: 'nowrap' }}>{item.primary}</span>
        {item.secondary ? <RobotoText label={item.secondary} fontSize="var(--text-size-small)" height="auto" style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} /> : null}
      </div>
    );
  }

  /* ---------------------------------------------------------------- editor */
  function download(name, text) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  function ShortcutsEditorDialog({ open, onClose }) {
    const s = useShortcuts();
    const [listening, setListening] = React.useState(null);  // action id waiting for a key
    const [pending, setPending] = React.useState(null);      // {id, seq, clash} awaiting "assign anyway"
    const [note, setNote] = React.useState(null);            // {text, error}
    const [confirmDefaults, setConfirmDefaults] = React.useState(false);
    const fileRef = React.useRef(null);

    React.useEffect(() => { if (!open) { setListening(null); setPending(null); setNote(null); } }, [open]);
    /* Capture the next key press for the row being edited, before App / the VC see it. */
    React.useEffect(() => {
      if (!listening) return undefined;
      const down = (e) => {
        if (MOD_KEYS[e.key]) return;
        e.preventDefault(); e.stopImmediatePropagation();
        if (e.key === 'Escape' && !e.ctrlKey && !e.altKey && !e.shiftKey) { setListening(null); return; }
        const seq = (e.key === 'Backspace' || e.key === 'Delete') && !e.ctrlKey && !e.altKey && !e.shiftKey ? '' : keyText(e);
        if (seq == null) return;
        const id = listening;
        setListening(null);
        const clash = seq ? s.findCollision(seq, id) : null;
        if (clash) { setPending({ id, seq, clash }); return; }
        s.set(id, seq);
        setNote(seq && s.isReserved(seq) ? { text: '"' + seq + '" is kept by the browser: the page never receives it, so this binding will not fire.', error: true } : null);
      };
      window.addEventListener('keydown', down, true);
      return () => window.removeEventListener('keydown', down, true);
    }, [listening]);

    const onImport = (file) => {
      if (!file) return;
      const r = new FileReader();
      r.onload = () => {
        try { s.importJson(String(r.result)); setNote({ text: 'Shortcuts successfully imported', error: false }); }
        catch (e) { setNote({ text: 'Unable to import shortcuts from the selected file: ' + e.message, error: true }); }
      };
      r.readAsText(file);
    };
    const rows = s.actions().filter(a => !a.screen || (window.QLCScreens && window.QLCScreens[a.screen]));

    return (
      <>
        <CustomPopupDialog open={open} title="Keyboard Shortcuts" width={620} standardButtons={['Close']} onClicked={onClose} onClose={() => { if (!listening) onClose(); }}>
          <div data-role="shortcuts-editor" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
              <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }} title="Briefly show the shortcut that just fired, and hint at the shortcut when a button that has one is clicked">
                <CustomCheckBox checked={s.hintsEnabled()} onToggled={(v) => s.setHintsEnabled(v)} data-role="shortcut-hints" />
                <RobotoText label="Show shortcut hints" fontSize="var(--text-size-small)" height="auto" />
              </span>
              <span style={{ flex: 1 }} />
              <GenericButton label="Load Defaults" width={120} height={26} fontSize="var(--text-size-menubar)" onClick={() => setConfirmDefaults(true)} data-role="shortcuts-defaults" />
              <GenericButton label="Export" width={80} height={26} fontSize="var(--text-size-menubar)" onClick={() => download('qlcplusShortcuts.json', s.exportJson())} data-role="shortcuts-export" />
              <GenericButton label="Import" width={80} height={26} fontSize="var(--text-size-menubar)" onClick={() => fileRef.current && fileRef.current.click()} data-role="shortcuts-import" />
              <input ref={fileRef} type="file" accept=".json,application/json" style={{ display: 'none' }} data-role="shortcuts-import-file"
                onChange={(e) => { onImport(e.target.files && e.target.files[0]); e.target.value = ''; }} />
            </div>
            {note ? <RobotoText label={note.text} fontSize="var(--text-size-menubar)" labelColor={note.error ? 'var(--override-red)' : 'var(--check-lime)'} wrapText height="auto" data-role="shortcuts-note" /> : null}
            <div style={{ maxHeight: 380, overflow: 'auto', border: 'var(--border-dark)', background: 'var(--bg-strong)' }}>
              {rows.map((a, i) => {
                const isListening = listening === a.id;
                return (
                  <div key={a.id} data-shortcut={a.id} style={{ display: 'flex', alignItems: 'center', gap: 8, minHeight: 'var(--list-item-height)', padding: '2px 8px', background: i % 2 ? 'var(--bg-medium)' : 'transparent' }}>
                    <RobotoText label={a.description} fontSize="var(--text-size-small)" height="auto" style={{ flex: 1, minWidth: 0 }} title={a.id} />
                    <button type="button" onClick={() => setListening(isListening ? null : a.id)} data-role="shortcut-sequence"
                      title="Click, then press the new key combination (Esc cancels, Backspace clears)"
                      style={{ width: 200, height: 24, flex: 'none', cursor: 'pointer', border: 'none', borderRadius: 'var(--radius-spin)',
                        background: isListening ? 'var(--selection)' : 'var(--bg-light)', color: isListening ? 'var(--bg-strong)' : (a.isDefault ? 'var(--fg-main)' : 'var(--selection)'),
                        font: '400 var(--text-size-small)/1 var(--font-roboto)' }}>
                      {isListening ? 'Press a key... (Esc to cancel)' : (a.sequence || '(none)')}
                    </button>
                    <IconButton imgSource={window.QLCData.icon('undo')} size={24} disabled={a.isDefault} tooltip={'Reset to default (' + (a.defaultSequence || 'none') + ')'} onClick={() => s.reset(a.id)} />
                  </div>
                );
              })}
            </div>
            <RobotoText label="Stored in this browser. Export writes the desktop's qlcplusShortcuts.json format, so the file can be imported in the desktop app too. Keys of the individual screens (Virtual Console bindings, the Simple Desk keypad, Show Manager) are not listed here."
              fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" />
          </div>
        </CustomPopupDialog>
        <CustomPopupDialog open={!!pending} title="Keyboard shortcut already in use" width={440} standardButtons={['Cancel', 'Assign anyway']}
          message={pending ? '"' + pending.seq + '" is already assigned to "' + pending.clash.description + '". Assigning it here too means only one of the two will react when the key is pressed. Assign it anyway?' : ''}
          onClose={() => setPending(null)}
          onClicked={(b) => { if (b === 'Assign anyway' && pending) s.set(pending.id, pending.seq); setPending(null); }} />
        <CustomPopupDialog open={confirmDefaults} title="Load default shortcuts" width={420} standardButtons={['Cancel', 'Ok']}
          message="This will discard every customized keyboard shortcut and restore the built-in defaults. Continue?"
          onClose={() => setConfirmDefaults(false)}
          onClicked={(b) => { if (b === 'Ok') { s.resetAll(); setNote(null); } setConfirmDefaults(false); }} />
      </>
    );
  }

  /** Always-mounted host: opens on 'qlc-open-shortcuts'. */
  function ShortcutsEditorHost() {
    const [open, setOpen] = React.useState(false);
    React.useEffect(() => {
      const fn = () => setOpen(true);
      window.addEventListener('qlc-open-shortcuts', fn);
      return () => window.removeEventListener('qlc-open-shortcuts', fn);
    }, []);
    return <ShortcutsEditorDialog open={open} onClose={() => setOpen(false)} />;
  }

  window.QLCShortcuts = S;
  window.QLCAppOverlays = (window.QLCAppOverlays || []).concat([KeyCastToast, ShortcutsEditorHost]);
  window.QLCMenuItems = (window.QLCMenuItems || []).concat([() => [
    { label: 'Keyboard shortcuts', icon: 'keybinding', onClick: () => window.dispatchEvent(new CustomEvent('qlc-open-shortcuts')) }
  ]]);
  Object.assign(window, { KeyCastToast, ShortcutsEditorDialog });
})();
