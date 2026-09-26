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

function App() {
  const D = window.QLCData;
  const [ctx, setCtx] = React.useState('fx');
  const [blackout, setBlackout] = React.useState(false);
  const [about, setAbout] = React.useState(false);
  const [beat, setBeat] = React.useState(false);
  const held = (ShortcutKeys && ShortcutKeys.useHeldModifier ? ShortcutKeys.useHeldModifier : useHeldFallback)(['Control']);

  React.useEffect(() => {
    const t = setInterval(() => { setBeat(b => !b); }, 500);
    return () => clearInterval(t);
  }, []);
  React.useEffect(() => {
    const k = (e) => {
      if (!e.ctrlKey) return;
      const map = { '1': 'fx', '2': 'vc', '3': 'sd', '4': 'io' };
      if (map[e.key]) { e.preventDefault(); setCtx(map[e.key]); }
      if (e.key.toLowerCase() === 'b') { e.preventDefault(); setBlackout(b => !b); }
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, []);

  const Screen = ctx === 'fx' ? FixturesFunctions : ctx === 'vc' ? VirtualConsole : ctx === 'sd' ? SimpleDesk : InputOutput;
  const entry = (id, icon, label, keys) => (
    <ShortcutHint keys={keys} placement="bottom">
      <MenuBarEntry imgSource={D.icon(icon)} entryText={label} checked={ctx === id} onClick={() => setCtx(id)} />
    </ShortcutHint>
  );

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
          <span style={{ flex: '0 1 130px', minWidth: 0, overflow: 'hidden' }}>
            <CustomTextInput text="Winter Tour.qxw" width="100%" align="right" color="var(--fg-light)" />
          </span>
          <ShortcutHint keys="Ctrl S" placement="corner"><IconButton imgSource={D.icon('filesave')} tooltip="Save workspace" /></ShortcutHint>
          <ShortcutHint keys="Ctrl B" placement="corner">
            <IconButton imgSource={D.icon('blackout')} checked={blackout} onClick={() => setBlackout(!blackout)} tooltip="Blackout" />
          </ShortcutHint>
          <IconButton imgSource={D.icon('stopall')} tooltip="Stop all functions" />
          <span title="Beat indicator — 120 BPM" style={{
            width: 18, height: 18, borderRadius: 9, marginLeft: 4,
            background: beat ? 'var(--beat-flash)' : 'var(--bg-strong)', border: 'var(--border-dark)'
          }} />
          <IconButton faSource="fa_gear" tooltip="Preferences" />
        </ViewToolbar>

        <div style={{ position: 'relative', flex: 1, minHeight: 0 }}>
          <Screen />
          {blackout ? (
            <div style={{ position: 'absolute', inset: 0, background: 'var(--dim-screen)', display: 'grid', placeItems: 'center', pointerEvents: 'none', zIndex: 90 }}>
              <span style={{ font: '700 40px/1 var(--font-roboto)', color: 'var(--override-red)', letterSpacing: '.08em' }}>BLACKOUT</span>
            </div>
          ) : null}
        </div>

        <CustomPopupDialog open={about} title="About QLC+" width={340}
          standardButtons={['Close']} onClicked={() => setAbout(false)} onClose={() => setAbout(false)}>
          <div style={{ display: 'flex', gap: 14, alignItems: 'center' }}>
            <img src={D.icon('qlcplus')} alt="" style={{ width: 64, height: 64 }} />
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              <RobotoText label="Q Light Controller+" fontBold fontSize={20} height={26} />
              <RobotoText label="Version 5 (qmlui) — design-system recreation" fontSize={14} labelColor="var(--fg-light)" height={22} />
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
