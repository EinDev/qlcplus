const { ViewToolbar, ToolbarSpacer, MenuBarEntry, IconButton, RobotoText, ShortcutOverlay, ShortcutHint, ShortcutKeys, CustomPopupDialog, GenericButton, CustomTextInput, CustomSpinBox, ContextMenuEntry, FaIcon } = window.PatchDesignSystem_5432c9;

function useHeldFallback() {
  const [held, setHeld] = React.useState(null);
  React.useEffect(() => {
    const typing = () => {
      const el = document.activeElement;
      return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
    };
    const down = (e) => { if (!typing() && e.key === 'Control') setHeld('Ctrl'); };
    const up = (e) => { if (e.key === 'Control') setHeld(null); };
    const blur = () => setHeld(null);
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    window.addEventListener('blur', blur);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
      window.removeEventListener('blur', blur);
    };
  }, []);
  return held;
}

function isTyping() {
  const el = document.activeElement;
  return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
}

const CONTEXTS = ['fx', 'vc', 'sd', 'io'];

/* Extension registries, so a new screen / toolbar item / actions-menu entry lives in its own file
   (loaded after this one in index.html) instead of editing App.jsx:
   - window.QLCScreens[id] = { id, icon, label, keys, hotkey, component, order }
       A top-level context like the built-in four. `icon` is a D.icon() name, `keys` the shortcut
       hint text (e.g. 'Ctrl 5'), `hotkey` the digit for Ctrl+<digit>, `component` the screen.
       id 'show' replaces the disabled Show Manager placeholder in the toolbar.
   - window.QLCToolbarItems = [Component, ...]  rendered right of the mode button; props: { qlc }.
   - window.QLCMenuItems = [fn, ...]  each fn({ qlc, project, online, setDialog, setCtx }) returns an
       array of ActionsMenu items (or '-'), appended before "About".
   - window.QLCUISettingsDialog = Component  props { open, onClose, qlc }; enables the gear button. */
function registeredScreens() {
  const r = window.QLCScreens || {};
  return Object.keys(r).map(k => r[k]).filter(s => s && s.component).sort((a, b) => (a.order || 0) - (b.order || 0));
}
function initialContext() {
  try {
    const q = new URLSearchParams(location.search).get('ctx');
    if (CONTEXTS.indexOf(q) !== -1) return q;
    if (window.QLCScreens && window.QLCScreens[q]) return q;
  } catch (e) {}
  return 'fx';
}

/**
 * Project metadata (core.project.get), refreshed on core.project.loaded / .saved. Falls back to
 * the mock workspace name while offline.
 */
function useProject(qlc) {
  const [project, setProject] = React.useState(null);
  React.useEffect(() => {
    if (!qlc.online) { setProject(null); return; }
    let alive = true;
    const refresh = () => qlc.call('core.project.get').then(p => { if (alive) setProject(p); }).catch(() => {});
    refresh();
    /* Every structural event bumps docRevision, and with it isModified — refresh on those too. */
    const offs = ['core.project.loaded', 'core.project.saved', 'core.project.recentFilesChanged', 'core.history.changed',
      'vc.widget.created', 'vc.widget.deleted', 'vc.widget.updated', 'vc.widget.configChanged', 'vc.widget.repositioned',
      'vc.page.created', 'vc.page.deleted', 'vc.page.renamed', 'functions.created', 'functions.deleted', 'functions.updated', 'fixtures.created', 'fixtures.deleted']
      .map(t => qlc.subscribeTo(t, refresh));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [qlc.online]);
  return project;
}

/** Blackout state: live (io.blackout.get/set + io.blackout.changed) when online, local otherwise. */
function useBlackout(qlc) {
  const [blackout, setBlackoutState] = React.useState(false);
  React.useEffect(() => {
    if (!qlc.online) return;
    qlc.call('io.blackout.get').then(r => setBlackoutState(!!r.blackout)).catch(() => {});
    return qlc.subscribeTo('blackout', (on) => setBlackoutState(!!on));
  }, [qlc.online]);
  const toggle = React.useCallback(() => {
    if (qlc.online) qlc.call('io.blackout.set', { blackout: !blackout }).catch(() => {});
    else setBlackoutState(b => !b);
  }, [qlc.online, blackout]);
  return [blackout, toggle];
}

/** Engine mode (design/operate): core.mode.get/set + core.mode.changed. */
function useMode(qlc) {
  const [mode, setMode] = React.useState(null);
  React.useEffect(() => {
    if (!qlc.online) { setMode(null); return; }
    qlc.call('core.mode.get').then(r => setMode(r.mode)).catch(() => {});
    return qlc.subscribeTo('core.mode.changed', (d) => setMode(d.mode));
  }, [qlc.online]);
  const toggle = () => {
    if (!qlc.online || !mode) return;
    qlc.call('core.mode.set', { mode: mode === 'operate' ? 'design' : 'operate' }).catch(() => {});
  };
  return [mode, toggle];
}

/**
 * Undo / redo stack (core.history.get + core.history.changed, core.undo / core.redo). Probed once
 * per connection so the buttons grey out on a server predating the methods instead of failing on
 * the first click.
 */
function useHistory(qlc) {
  const [h, setH] = React.useState({ supported: null, canUndo: false, canRedo: false, undoText: '', redoText: '' });
  React.useEffect(() => {
    if (!qlc.online) { setH({ supported: null, canUndo: false, canRedo: false, undoText: '', redoText: '' }); return; }
    let alive = true;
    const apply = (r) => { if (alive && r) setH({ supported: true, canUndo: !!r.canUndo, canRedo: !!r.canRedo, undoText: r.undoText || '', redoText: r.redoText || '' }); };
    const refresh = () => qlc.call('core.history.get', {}).then(apply).catch(e => { if (alive && e && e.code === 'NOT_FOUND') setH(s => Object.assign({}, s, { supported: false })); });
    refresh();
    const off = qlc.subscribeTo('core.history.changed', (d) => { if (d && d.canUndo != null) apply(d); else refresh(); });
    return () => { alive = false; off(); };
  }, [qlc.online]);
  /* The result already carries the new canUndo/canRedo/texts (the coalesced core.history.changed follows). */
  const applyResult = (r) => { if (r && r.canUndo != null) setH({ supported: true, canUndo: !!r.canUndo, canRedo: !!r.canRedo, undoText: r.undoText || '', redoText: r.redoText || '' }); };
  const undo = React.useCallback(() => { if (qlc.online && h.supported !== false) qlc.call('core.undo', {}).then(applyResult).catch(() => {}); }, [qlc.online, h.supported]);
  const redo = React.useCallback(() => { if (qlc.online && h.supported !== false) qlc.call('core.redo', {}).then(applyResult).catch(() => {}); }, [qlc.online, h.supported]);
  return Object.assign({}, h, { undo, redo });
}

/** Global BPM (core.bpm.get/set/tap + core.bpm.changed) and the beat pulse (core.beat). */
function useBpm(qlc) {
  const empty = { supported: null, bpm: 0, generator: '', error: '' };
  const [bpm, setBpm] = React.useState(empty);
  const [beat, setBeat] = React.useState(0);
  React.useEffect(() => {
    if (!qlc.online) { setBpm(empty); return; }
    let alive = true;
    qlc.call('core.bpm.get', {}).then(r => { if (alive && r) setBpm({ supported: true, bpm: Number(r.bpm) || 0, generator: r.generator || '', error: '' }); })
      .catch(e => { if (alive && e && e.code === 'NOT_FOUND') setBpm(s => Object.assign({}, s, { supported: false })); });
    const offs = [
      qlc.subscribeTo('core.bpm.changed', (d) => { if (d) setBpm(s => ({ supported: true, bpm: Number(d.bpm) || 0, generator: d.generator != null ? d.generator : s.generator, error: '' })); }),
      qlc.subscribeTo('core.beat', () => setBeat(b => b + 1))
    ];
    return () => { alive = false; offs.forEach(f => f()); };
  }, [qlc.online]);
  /* set/tap answer INVALID_STATE while a plugin/audio generator owns the tempo — shown, not swallowed. */
  const onError = (e) => { if (e && e.code === 'INVALID_STATE') setBpm(s => Object.assign({}, s, { error: 'Tempo owned by ' + ((e.details && e.details.generator) || s.generator || 'another source'), generator: (e.details && e.details.generator) || s.generator })); };
  const set = React.useCallback((v) => { if (qlc.online) qlc.call('core.bpm.set', { bpm: Math.max(0, Math.min(1000, Math.round(v))) }).catch(onError); }, [qlc.online]);
  const tap = React.useCallback(() => { if (qlc.online && bpm.supported !== false) qlc.call('core.bpm.tap', {}).catch(onError); }, [qlc.online, bpm.supported]);
  /* "plugin"/"audio" generators own the tempo: the API may only read it. */
  const owned = bpm.generator === 'plugin' || bpm.generator === 'audio';
  return Object.assign({}, bpm, { beat, set, tap, owned });
}

/**
 * Running-function count for the Stop-all badge: seeded from functions.list's `running` field
 * (absent on older servers -> count unknown) and kept current by functions.status.changed.
 */
function useRunningFunctions(qlc) {
  const [running, setRunning] = React.useState(null);
  React.useEffect(() => {
    if (!qlc.online) { setRunning(null); return; }
    let alive = true;
    const load = () => qlc.call('functions.list').then(r => {
      if (!alive) return;
      const fns = r.functions || [];
      if (!fns.some(f => f.running != null)) { setRunning(null); return; }
      setRunning(new Set(fns.filter(f => f.running).map(f => String(f.id))));
    }).catch(() => {});
    load();
    const offs = [
      qlc.subscribeTo('functions.status.changed', (d) => {
        const fid = d ? (d.functionId != null ? d.functionId : d.id) : null;
        if (fid == null) return;
        setRunning(s => { const n = new Set(s || []); if (d.running) n.add(String(fid)); else n.delete(String(fid)); return n; });
      }),
      qlc.subscribeTo('core.project.loaded', load)
    ];
    return () => { alive = false; offs.forEach(f => f()); };
  }, [qlc.online]);
  return running ? running.size : null;
}

/** MainView.qml beatIndicator: a grey dot that flashes --beat-flash on every beat and fades back over half a beat. */
function BeatIndicator({ beat, bpm, supported }) {
  const dur = bpm ? Math.max(60, 30000 / bpm) : 200;
  return (
    <span title={supported === false ? 'Beat indicator — BPM is not exposed by this server' : supported ? 'Beat indicator' : 'Beat indicator — connect first'}
      style={{ width: 19, height: 19, borderRadius: 10, flex: 'none', border: '2px solid var(--bg-medium)', background: 'var(--fg-medium)', display: 'inline-block', position: 'relative', overflow: 'hidden' }}>
      {beat ? <span key={beat} style={{ position: 'absolute', inset: 0, borderRadius: 10, background: 'var(--beat-flash)', animation: 'qlc-beat-fade ' + dur + 'ms linear forwards' }} /> : null}
      <style>{'@keyframes qlc-beat-fade{from{opacity:1}to{opacity:0}}'}</style>
    </span>
  );
}

/** "BPM: 128" label (MainView.qml) — click for the tap / numeric panel. */
function BpmControl({ bpmState, disabled }) {
  const [open, setOpen] = React.useState(false);
  const [draft, setDraft] = React.useState(bpmState.bpm || 120);
  const anchor = React.useRef(null);
  const [box, setBox] = React.useState({ top: 0, left: 0 });
  React.useEffect(() => { if (!open) setDraft(bpmState.bpm || 120); }, [bpmState.bpm, open]);
  const place = () => { const r = anchor.current.getBoundingClientRect(); setBox({ top: Math.round(r.bottom + 2), left: Math.round(r.left) }); };
  React.useEffect(() => {
    if (!open) return;
    const onKey = (e) => { if (e.key === 'Escape') setOpen(false); };
    const onDown = (e) => { if (anchor.current && !anchor.current.contains(e.target)) setOpen(false); };
    window.addEventListener('keydown', onKey);
    document.addEventListener('mousedown', onDown, true);
    return () => { window.removeEventListener('keydown', onKey); document.removeEventListener('mousedown', onDown, true); };
  }, [open]);
  const label = 'BPM: ' + (bpmState.supported === false ? 'n/a' : bpmState.bpm > 0 && bpmState.generator !== 'disabled' ? bpmState.bpm : 'Off');
  const locked = bpmState.supported === false || bpmState.owned;
  return (
    <span ref={anchor} style={{ position: 'relative', display: 'inline-flex', alignSelf: 'stretch', alignItems: 'center', flex: 'none' }}>
      <button type="button" disabled={disabled} onClick={() => { if (!open) place(); setOpen(!open); }}
        title={disabled ? 'BPM — connect first' : bpmState.supported === false ? 'BPM — not exposed by this server' : bpmState.owned ? 'BPM — tempo owned by the ' + bpmState.generator + ' beat source' : 'BPM — click to tap or set'}
        style={{ height: '100%', padding: '0 6px', background: open ? 'var(--bg-light)' : 'transparent', border: 'none', color: 'var(--fg-main)', cursor: disabled ? 'default' : 'pointer',
          font: '400 var(--text-size-default) var(--font-roboto)', whiteSpace: 'nowrap' }}>{label}</button>
      {open ? (
        <span style={{ position: 'fixed', top: box.top, left: box.left, zIndex: 400, display: 'flex', flexDirection: 'column', gap: 6, padding: 8, width: 200, background: 'var(--bg-medium)', border: 'var(--border-dialog)' }}>
          <RobotoText label={'Beat generator: ' + (bpmState.generator || 'unknown')} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" wrapText height="auto" />
          {bpmState.owned || bpmState.error ? (
            <RobotoText label={bpmState.error || 'Tempo owned by ' + bpmState.generator + ' — set the generator to internal in the desktop app to edit it here'}
              fontSize="var(--text-size-menubar)" labelColor="var(--selection)" wrapText height="auto" />
          ) : null}
          <span style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
            <CustomSpinBox value={draft} from={0} to={1000} width={80} height={26} disabled={locked} onValueModified={setDraft} onKeyDown={(e) => { if (e.key === 'Enter' && !locked) { bpmState.set(draft); setOpen(false); } }} />
            <GenericButton label="Set" width={60} height={26} fontSize="var(--text-size-menubar)" onClick={() => { bpmState.set(draft); setOpen(false); }} disabled={locked} />
          </span>
          <ShortcutHint keys="Space" placement="corner">
            <GenericButton label="TAP" width="100%" height={34} fontSize="var(--text-size-default)" disabled={locked}
              bgColor="var(--keypad-enter)" hoverColor="var(--keypad-enter-hover)" pressedColor="var(--keypad-enter-pressed)"
              onPointerDown={(e) => { e.preventDefault(); if (!locked) bpmState.tap(); }} />
          </ShortcutHint>
          <GenericButton label="Off" width="100%" height={24} fontSize="var(--text-size-menubar)" disabled={locked || !bpmState.bpm || bpmState.generator === 'disabled'} onClick={() => { bpmState.set(0); setOpen(false); }} />
        </span>
      ) : null}
    </span>
  );
}

/** MainView.qml stopAllButton: red octagon, "STOP", the running-function count badge. */
function StopAllButton({ count, disabled, onClick, tooltip }) {
  return (
    <ShortcutHint keys="Ctrl ." placement="corner">
      <span style={{ position: 'relative', display: 'inline-flex', flex: 'none' }}>
        {/* fa_octagon is not in the free Font Awesome face the web UI ships, so the octagon is an inline SVG in the same red. */}
        <IconButton bgColor="transparent" borderWidth={0} disabled={disabled} onClick={onClick} tooltip={tooltip} style={{ opacity: disabled ? .45 : 1 }} />
        <svg viewBox="0 0 100 100" style={{ position: 'absolute', inset: 5, width: 'calc(100% - 10px)', height: 'calc(100% - 10px)', pointerEvents: 'none' }}>
          <polygon points="30,3 70,3 97,30 97,70 70,97 30,97 3,70 3,30" fill="var(--override-red)" />
        </svg>
        <span style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center', pointerEvents: 'none', color: 'var(--fg-main)', font: '700 9px var(--font-roboto)', letterSpacing: 0 }}>STOP</span>
        {count ? (
          <span style={{ position: 'absolute', left: '50%', top: '50%', minWidth: 15, height: 15, padding: '0 3px', borderRadius: 'var(--radius-bubble)', background: 'var(--highlight)', border: '1px solid var(--fg-main)',
            color: 'var(--fg-main)', font: '400 11px/13px var(--font-roboto)', textAlign: 'center', pointerEvents: 'none' }}>{count}</span>
        ) : null}
      </span>
    </ShortcutHint>
  );
}

/** ActionsMenu.qml, the part that makes sense in a browser: File entries, Undo/Redo, About. */
function ActionsMenu({ open, onClose, anchor, items }) {
  const [box, setBox] = React.useState({ top: 0, left: 0 });
  React.useEffect(() => {
    if (!open) return;
    const place = () => { if (anchor.current) { const r = anchor.current.getBoundingClientRect(); setBox({ top: Math.round(r.bottom + 1), left: Math.round(r.left) }); } };
    place();
    const onKey = (e) => { if (e.key === 'Escape') onClose(); };
    const onDown = (e) => { if (anchor.current && !anchor.current.contains(e.target)) onClose(); };
    window.addEventListener('keydown', onKey);
    window.addEventListener('resize', place);
    document.addEventListener('mousedown', onDown, true);
    return () => { window.removeEventListener('keydown', onKey); window.removeEventListener('resize', place); document.removeEventListener('mousedown', onDown, true); };
  }, [open]);
  if (!open) return null;
  const D = window.QLCData;
  return (
    <div role="menu" style={{ position: 'fixed', top: box.top, left: box.left, zIndex: 400, minWidth: 240, background: 'var(--bg-medium)', border: 'var(--border-menu)', display: 'flex', flexDirection: 'column' }}>
      {items.map((it, i) => it === '-' ? <span key={i} style={{ height: 1, background: 'var(--bg-light)', margin: '2px 0' }} /> : (
        <ContextMenuEntry key={it.label} imgSource={it.icon ? D.icon(it.icon) : undefined} faSource={it.fa} entryText={it.label + (it.detail ? ' — ' + it.detail : '')}
          iconHeight={26} disabled={!!it.disabled} title={it.tooltip || ''} style={{ minHeight: 30, opacity: it.disabled ? .45 : 1 }}
          onClick={() => { if (!it.disabled) { onClose(); it.onClick(); } }} />
      ))}
    </div>
  );
}

/** Recent-files list + a path field: core.project.open {source:'path'}. */
function OpenDialog({ open, qlc, onClose, onOpen }) {
  const [recent, setRecent] = React.useState([]);
  const [path, setPath] = React.useState('');
  React.useEffect(() => {
    if (!open) return;
    setPath('');
    qlc.call('core.project.recentFiles').then(r => setRecent(r.files || [])).catch(() => setRecent([]));
  }, [open]);
  const go = () => { const p = path.trim(); if (p) onOpen(p); };
  return (
    <CustomPopupDialog open={open} title="Open file" width={480} standardButtons={['Cancel', 'Open']} onClose={onClose}
      onClicked={(b) => { if (b === 'Open') go(); else onClose(); }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <RobotoText label="Recent files on the QLC+ machine" fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="auto" />
        <div style={{ maxHeight: 220, overflow: 'auto', border: 'var(--border-dark)', background: 'var(--bg-strong)' }}>
          {recent.length ? recent.map((f, i) => (
            <div key={i} role="option" onClick={() => setPath(f.filePath)} onDoubleClick={() => onOpen(f.filePath)}
              style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center', height: 'var(--icon-size-default)', padding: '0 8px', cursor: 'pointer', background: path === f.filePath ? 'var(--highlight)' : (i % 2 ? 'var(--bg-medium)' : 'transparent') }}>
              <RobotoText label={f.fileName} fontSize="var(--text-size-small)" height={18} />
              <RobotoText label={f.filePath} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height={14} />
            </div>
          )) : <RobotoText label="No recent files" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={8} />}
        </div>
        <RobotoText label="Path (.qxw) on the QLC+ machine" fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="auto" />
        <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
          <CustomTextInput text={path} editing width="100%" height={22} placeholder="D:\shows\show.qxw" onChange={(e) => setPath(e.target.value)} onTextConfirmed={setPath}
            onKeyDown={(e) => { if (e.key === 'Enter') { const p = e.target.value.trim(); if (p) onOpen(p); } }} style={{ fontSize: 'var(--text-size-small)' }} />
        </span>
      </div>
    </CustomPopupDialog>
  );
}

function SaveAsDialog({ open, initialPath, onClose, onSave }) {
  const [path, setPath] = React.useState(initialPath || '');
  React.useEffect(() => { if (open) setPath(initialPath || ''); }, [open, initialPath]);
  return (
    <CustomPopupDialog open={open} title="Save project as..." width={480} standardButtons={['Cancel', 'Save']} onClose={onClose}
      onClicked={(b) => { if (b === 'Save') { const p = path.trim(); if (p) onSave(p); } else onClose(); }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        <RobotoText label="Path on the QLC+ machine (.qxw is appended if missing)" fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="auto" wrapText />
        <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
          <CustomTextInput text={path} editing autoFocus width="100%" height={22} onChange={(e) => setPath(e.target.value)} onTextConfirmed={setPath}
            onKeyDown={(e) => { if (e.key === 'Enter') { const p = e.target.value.trim(); if (p) onSave(p); } }} style={{ fontSize: 'var(--text-size-small)' }} />
        </span>
      </div>
    </CustomPopupDialog>
  );
}

function App() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [ctx, setCtx] = React.useState(initialContext);
  const [about, setAbout] = React.useState(false);
  const [menu, setMenu] = React.useState(false);
  const [dialog, setDialog] = React.useState(null); // {kind:'open'|'saveAs'|'confirm'|'message', ...}
  const [uiSettings, setUiSettings] = React.useState(false);
  const [blackout, toggleBlackout] = useBlackout(qlc);
  const [mode, toggleMode] = useMode(qlc);
  const project = useProject(qlc);
  const history = useHistory(qlc);
  const bpm = useBpm(qlc);
  const runningCount = useRunningFunctions(qlc);
  const menuAnchor = React.useRef(null);
  const held = (ShortcutKeys && ShortcutKeys.useHeldModifier ? ShortcutKeys.useHeldModifier : useHeldFallback)(['Control']);

  const fail = (what) => (e) => { if (e && e.code !== 'NOT_CONNECTED') setDialog({ kind: 'message', title: what, text: (e && e.message) || 'failed' }); };
  const save = React.useCallback(() => {
    if (!qlc.online) return Promise.resolve();
    if (project && !project.filePath) { setDialog({ kind: 'saveAs' }); return Promise.resolve(); }
    return qlc.call('core.project.save').catch(fail('Save project'));
  }, [qlc.online, project]);
  const stopAll = React.useCallback(() => { if (qlc.online && !qlc.isUnsupported('functions.stopAll')) qlc.call('functions.stopAll', {}).catch(() => {}); }, [qlc.online, qlc.unsupported]);

  /* New / Open replace the document; mirror ActionsMenu.qml's "Your project has changes" prompt first. */
  const guarded = (label, run) => {
    if (project && project.isModified) setDialog({ kind: 'confirm', label, run });
    else run();
  };
  const newProject = () => guarded('New project', () => qlc.call('core.project.new', {}).catch(fail('New project')));
  const openProject = (path) => guarded('Open file', () => {
    setDialog(null);
    qlc.call('core.project.open', { source: 'path', path }).then(r => {
      if (r && r.warningsHtml) setDialog({ kind: 'message', title: 'Open file', text: String(r.warningsHtml).replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ').trim() });
    }).catch(fail('Open file'));
  });
  const saveAs = (path) => { setDialog(null); qlc.call('core.project.saveAs', { target: 'serverPath', path }).catch(fail('Save project as')); };

  React.useEffect(() => {
    const k = (e) => {
      /* Space = tap tempo, unless a button (VC button, any <button>) has focus and Space is its press. */
      const ae = document.activeElement;
      const onButton = !!ae && (ae.tagName === 'BUTTON' || ae.getAttribute('role') === 'button');
      if (e.key === ' ' && !e.ctrlKey && !isTyping() && !onButton && qlc.online && bpm.supported && !bpm.owned) { e.preventDefault(); bpm.tap(); return; }
      if (!e.ctrlKey) return;
      const map = { '1': 'fx', '2': 'vc', '3': 'sd', '4': 'io' };
      registeredScreens().forEach(s => { if (s.hotkey) map[String(s.hotkey)] = s.id; });
      if (map[e.key]) { e.preventDefault(); setCtx(map[e.key]); }
      const key = e.key.toLowerCase();
      if (key === 'b') { e.preventDefault(); toggleBlackout(); }
      else if (key === 's' && !isTyping()) { e.preventDefault(); if (e.shiftKey) setDialog({ kind: 'saveAs' }); else save(); }
      else if (key === 'o' && !isTyping()) { e.preventDefault(); if (qlc.online) setDialog({ kind: 'open' }); }
      else if (key === '.') { e.preventDefault(); stopAll(); }
      else if (key === 'z' && !isTyping()) { e.preventDefault(); if (e.shiftKey) history.redo(); else history.undo(); }
      else if (key === 'y' && !isTyping()) { e.preventDefault(); history.redo(); }
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, [toggleBlackout, save, stopAll, history.undo, history.redo, bpm.tap, bpm.supported, qlc.online]);

  const screens = registeredScreens();
  const registered = screens.find(s => s.id === ctx);
  const Screen = registered ? registered.component
    : ctx === 'fx' ? FixturesFunctions : ctx === 'vc' ? VirtualConsole : ctx === 'sd' ? SimpleDesk : InputOutput;
  const showScreen = screens.find(s => s.id === 'show');
  const UISettingsDialog = window.QLCUISettingsDialog || null;
  const entry = (id, icon, label, keys) => (
    <ShortcutHint keys={keys} placement="bottom">
      <MenuBarEntry imgSource={D.icon(icon)} entryText={label} checked={ctx === id} onClick={() => setCtx(id)} />
    </ShortcutHint>
  );
  const fileName = qlc.online
    ? (project ? (project.fileName || 'Untitled') + (project.isModified ? ' *' : '') : '…')
    : 'Winter Tour.qxw (mock)';
  const online = qlc.online;
  /* Only edits made in the desktop app are on the undo stack; edits made through the API are not. */
  const undoTip = history.supported === false ? 'Undo — not available on this server' : 'Undo' + (history.undoText ? ' ' + history.undoText : '') + ' (desktop-made edits only)';
  const redoTip = history.supported === false ? 'Redo — not available on this server' : 'Redo' + (history.redoText ? ' ' + history.redoText : '') + ' (desktop-made edits only)';
  const stopAllUnsupported = qlc.isUnsupported('functions.stopAll');

  const menuItems = [
    { label: 'New project', icon: 'filenew', disabled: !online, onClick: newProject },
    { label: 'Open file', icon: 'fileopen', disabled: !online, onClick: () => setDialog({ kind: 'open' }) },
    { label: 'Save project', icon: 'filesave', disabled: !online, onClick: save },
    { label: 'Save project as...', icon: 'filesaveas', disabled: !online, onClick: () => setDialog({ kind: 'saveAs' }) },
    '-',
    { label: 'Undo', icon: 'undo', detail: history.undoText, disabled: !online || history.supported === false || !history.canUndo, onClick: history.undo, tooltip: undoTip },
    { label: 'Redo', icon: 'redo', detail: history.redoText, disabled: !online || history.supported === false || !history.canRedo, onClick: history.redo, tooltip: redoTip },
    ...(window.QLCMenuItems || []).map(f => { try { return f({ qlc, project, online, setDialog, setCtx }) || []; } catch (e) { console.error(e); return []; } }).flat(),
    '-',
    { label: 'About', fa: 'fa_circle_info', onClick: () => setAbout(true) }
  ];

  return (
    <ShortcutOverlay active={!!held} heldKey="Ctrl" legend="held — release to dismiss">
      <div style={{ display: 'flex', flexDirection: 'column', height: '100vh', minHeight: 0, background: 'var(--bg-medium)', position: 'relative' }}>
        <ViewToolbar variant="main">
          <img src={D.icon('qlcplus')} alt="QLC+" style={{ width: 30, height: 30, marginLeft: 4, cursor: 'pointer', flex: 'none' }} onClick={() => setAbout(true)} />
          <span ref={menuAnchor} style={{ display: 'inline-flex', flex: 'none' }}>
            <IconButton faSource="fa_bars" faColor="var(--fg-main)" bgColor="transparent" borderWidth={0} checked={menu} tooltip="Actions menu" onClick={() => setMenu(!menu)} />
            <ActionsMenu open={menu} onClose={() => setMenu(false)} anchor={menuAnchor} items={menuItems} />
          </span>
          {entry('fx', 'fixture', 'Fixtures & Functions', 'Ctrl 1')}
          {entry('vc', 'virtualconsole', 'Virtual Console', 'Ctrl 2')}
          {entry('sd', 'simpledesk', 'Simple Desk', 'Ctrl 3')}
          {showScreen ? entry('show', showScreen.icon || 'showmanager', showScreen.label || 'Show Manager', showScreen.keys)
            : <MenuBarEntry imgSource={D.icon('showmanager')} entryText="Show Manager" disabled title="Show Manager — not available in the web UI yet" style={{ opacity: .5, cursor: 'default' }} />}
          {entry('io', 'inputoutput', 'Input / Output', 'Ctrl 4')}
          {screens.filter(s => s.id !== 'show').map(s => <React.Fragment key={s.id}>{entry(s.id, s.icon, s.label, s.keys)}</React.Fragment>)}
          <ToolbarSpacer />
          <ConnectionBar />
          <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
          <span style={{ flex: '0 1 170px', minWidth: 0, overflow: 'hidden' }} title={project && project.filePath ? project.filePath : ''}>
            <CustomTextInput text={fileName} width="100%" align="right" color="var(--fg-light)" />
          </span>
          <ShortcutHint keys="Ctrl S" placement="corner">
            <IconButton imgSource={D.icon('filesave')} disabled={!online} onClick={save}
              tooltip={online ? 'Save project' : 'Save project — connect first'} />
          </ShortcutHint>
          <ShortcutHint keys="Ctrl Z" placement="corner">
            <IconButton imgSource={D.icon('undo')} disabled={!online || history.supported === false || !history.canUndo} onClick={history.undo} tooltip={undoTip} />
          </ShortcutHint>
          <ShortcutHint keys="Ctrl Y" placement="corner">
            <IconButton imgSource={D.icon('redo')} disabled={!online || history.supported === false || !history.canRedo} onClick={history.redo} tooltip={redoTip} />
          </ShortcutHint>
          <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
          <BpmControl bpmState={bpm} disabled={!online} />
          <BeatIndicator beat={bpm.beat} bpm={bpm.bpm} supported={bpm.supported} />
          <ShortcutHint keys="Ctrl B" placement="corner">
            <IconButton imgSource={D.icon('blackout')} checked={blackout} onClick={toggleBlackout}
              tooltip={online ? 'Blackout' : 'Blackout (local preview only)'} />
          </ShortcutHint>
          <StopAllButton count={runningCount} disabled={!online || stopAllUnsupported} onClick={stopAll}
            tooltip={!online ? 'Stop all the running functions — connect first' : stopAllUnsupported ? 'Stop all the running functions — not available on this server' : 'Stop all the running functions' + (runningCount != null ? ' (' + runningCount + ' running)' : '')} />
          <GenericButton label={mode ? (mode === 'operate' ? 'Operate' : 'Design') : 'Mode'} width={72} height={26}
            fontSize="var(--text-size-menubar)" disabled={!mode} onClick={toggleMode}
            bgColor={mode === 'operate' ? 'var(--override-red)' : 'var(--bg-control)'}
            style={{ marginLeft: 4 }} title={mode ? 'Engine mode: ' + mode + ' — click to switch' : 'Engine mode — connect first'} />
          {(window.QLCToolbarItems || []).map((C, i) => <C key={i} qlc={qlc} />)}
          <IconButton faSource="fa_gear" tooltip={UISettingsDialog ? 'UI Settings' : 'UI Settings — not available in the web UI'} disabled={!UISettingsDialog} onClick={() => setUiSettings(true)} />
        </ViewToolbar>
        {UISettingsDialog ? <UISettingsDialog open={uiSettings} onClose={() => setUiSettings(false)} qlc={qlc} /> : null}

        <div style={{ position: 'relative', flex: 1, minHeight: 0 }}>
          <Screen />
          {blackout ? (
            <div style={{ position: 'absolute', inset: 0, background: 'var(--dim-screen)', display: 'grid', placeItems: 'center', pointerEvents: 'none', zIndex: 90 }}>
              <span style={{ font: '700 40px/1 var(--font-roboto)', color: 'var(--override-red)', letterSpacing: '.08em' }}>BLACKOUT</span>
            </div>
          ) : null}
        </div>

        <OpenDialog open={!!dialog && dialog.kind === 'open'} qlc={qlc} onClose={() => setDialog(null)} onOpen={openProject} />
        <SaveAsDialog open={!!dialog && dialog.kind === 'saveAs'} initialPath={project ? project.filePath || '' : ''} onClose={() => setDialog(null)} onSave={saveAs} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'confirm'} title="Your project has changes" width={420}
          message={'The current project has unsaved changes. Discard them and continue with "' + (dialog && dialog.label) + '"?'}
          standardButtons={['Cancel', 'Save first', 'Discard']} onClose={() => setDialog(null)}
          onClicked={(b) => {
            const run = dialog && dialog.run;
            if (b === 'Discard') { setDialog(null); if (run) run(); }
            else if (b === 'Save first') { setDialog(null); save().then(() => { if (run) run(); }); }
            else setDialog(null);
          }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'message'} title={dialog && dialog.title} width={420} message={dialog && dialog.text}
          standardButtons={['Close']} onClose={() => setDialog(null)} onClicked={() => setDialog(null)} />

        <CustomPopupDialog open={about} title="About QLC+" width={360}
          standardButtons={['Close']} onClicked={() => setAbout(false)} onClose={() => setAbout(false)}>
          <div style={{ display: 'flex', gap: 14, alignItems: 'center' }}>
            <img src={D.icon('qlcplus')} alt="" style={{ width: 64, height: 64 }} />
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              <RobotoText label="Q Light Controller+" fontBold fontSize={20} height={26} />
              <RobotoText label="Web UI for QLC+ 5 (Control API client)" fontSize={14} labelColor="var(--fg-light)" height={22} />
              <RobotoText label={qlc.online && qlc.serverInfo ? 'Server: QLC+ ' + qlc.serverInfo.version + ' on ' + qlc.host + ':' + qlc.port : 'Not connected — showing mock data'}
                fontSize={14} labelColor="var(--fg-light)" height={22} />
              <RobotoText label="Hold Ctrl to see shortcuts" fontSize={14} labelColor="var(--fg-medium)" height={22} />
            </div>
          </div>
        </CustomPopupDialog>
      </div>
    </ShortcutOverlay>
  );
}
function AppRoot() {
  return <QLCConnectionProvider><App /></QLCConnectionProvider>;
}
Object.assign(window, { App, AppRoot });
