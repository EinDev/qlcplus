const { ViewToolbar, ToolbarSpacer, MenuBarEntry, IconButton, RobotoText, ShortcutOverlay, ShortcutHint, ShortcutKeys, CustomPopupDialog, GenericButton, CustomTextInput } = window.PatchDesignSystem_5432c9;

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

const CONTEXTS = ['fx', 'vc', 'sd', 'io'];
function initialContext() {
  try {
    const q = new URLSearchParams(location.search).get('ctx');
    if (CONTEXTS.indexOf(q) !== -1) return q;
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
    const offs = ['core.project.loaded', 'core.project.saved', 'core.project.recentFilesChanged'].map(t => qlc.subscribeTo(t, refresh));
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

function App() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [ctx, setCtx] = React.useState(initialContext);
  const [about, setAbout] = React.useState(false);
  const [blackout, toggleBlackout] = useBlackout(qlc);
  const [mode, toggleMode] = useMode(qlc);
  const project = useProject(qlc);
  const held = (ShortcutKeys && ShortcutKeys.useHeldModifier ? ShortcutKeys.useHeldModifier : useHeldFallback)(['Control']);

  const save = React.useCallback(() => {
    if (!qlc.online) return;
    qlc.call('core.project.save').catch(() => {});
  }, [qlc.online]);

  React.useEffect(() => {
    const k = (e) => {
      if (!e.ctrlKey) return;
      const map = { '1': 'fx', '2': 'vc', '3': 'sd', '4': 'io' };
      if (map[e.key]) { e.preventDefault(); setCtx(map[e.key]); }
      if (e.key.toLowerCase() === 'b') { e.preventDefault(); toggleBlackout(); }
      if (e.key.toLowerCase() === 's') { e.preventDefault(); save(); }
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, [toggleBlackout, save]);

  const Screen = ctx === 'fx' ? FixturesFunctions : ctx === 'vc' ? VirtualConsole : ctx === 'sd' ? SimpleDesk : InputOutput;
  const entry = (id, icon, label, keys) => (
    <ShortcutHint keys={keys} placement="bottom">
      <MenuBarEntry imgSource={D.icon(icon)} entryText={label} checked={ctx === id} onClick={() => setCtx(id)} />
    </ShortcutHint>
  );
  const fileName = qlc.online
    ? (project ? (project.fileName || 'Untitled') + (project.isModified ? ' *' : '') : '…')
    : 'Winter Tour.qxw (mock)';

  return (
    <ShortcutOverlay active={!!held} heldKey="Ctrl" legend="held — release to dismiss">
      <div style={{ display: 'flex', flexDirection: 'column', height: '100vh', minHeight: 0, background: 'var(--bg-medium)' }}>
        <ViewToolbar variant="main">
          <img src={D.icon('qlcplus')} alt="QLC+" style={{ width: 30, height: 30, marginRight: 4, cursor: 'pointer' }} onClick={() => setAbout(true)} />
          {entry('fx', 'fixture', 'Fixtures & Functions', 'Ctrl 1')}
          {entry('vc', 'virtualconsole', 'Virtual Console', 'Ctrl 2')}
          {entry('sd', 'simpledesk', 'Simple Desk', 'Ctrl 3')}
          {entry('io', 'inputoutput', 'Input / Output', 'Ctrl 4')}
          <ToolbarSpacer />
          <ConnectionBar />
          <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
          <span style={{ flex: '0 1 170px', minWidth: 0, overflow: 'hidden' }} title={project && project.filePath ? project.filePath : ''}>
            <CustomTextInput text={fileName} width="100%" align="right" color="var(--fg-light)" />
          </span>
          <ShortcutHint keys="Ctrl S" placement="corner">
            <IconButton imgSource={D.icon('filesave')} disabled={!qlc.online} onClick={save}
              tooltip={qlc.online ? 'Save workspace (core.project.save)' : 'Save workspace — connect first'} />
          </ShortcutHint>
          <ShortcutHint keys="Ctrl B" placement="corner">
            <IconButton imgSource={D.icon('blackout')} checked={blackout} onClick={toggleBlackout}
              tooltip={qlc.online ? 'Blackout (live)' : 'Blackout (local preview only)'} />
          </ShortcutHint>
          <IconButton imgSource={D.icon('stopall')} disabled
            tooltip="Stop all functions — not available: the Control API has no stop-all method yet" />
          <GenericButton label={mode ? (mode === 'operate' ? 'Operate' : 'Design') : 'Mode'} width={72} height={26}
            fontSize="var(--text-size-menubar)" disabled={!mode} onClick={toggleMode}
            bgColor={mode === 'operate' ? 'var(--override-red)' : 'var(--bg-control)'}
            style={{ marginLeft: 4 }} title={mode ? 'Engine mode: ' + mode + ' — click to switch' : 'Engine mode — connect first'} />
          <span title="Beat indicator — BPM is not exposed by the Control API yet" style={{
            width: 18, height: 18, borderRadius: 9, marginLeft: 4,
            background: 'var(--bg-strong)', border: 'var(--border-dark)'
          }} />
          <IconButton faSource="fa_gear" tooltip="Preferences — not available in the web UI" disabled />
        </ViewToolbar>

        <div style={{ position: 'relative', flex: 1, minHeight: 0 }}>
          <Screen />
          {blackout ? (
            <div style={{ position: 'absolute', inset: 0, background: 'var(--dim-screen)', display: 'grid', placeItems: 'center', pointerEvents: 'none', zIndex: 90 }}>
              <span style={{ font: '700 40px/1 var(--font-roboto)', color: 'var(--override-red)', letterSpacing: '.08em' }}>BLACKOUT</span>
            </div>
          ) : null}
        </div>

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
