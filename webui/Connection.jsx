/**
 * Live QLC+ Control API connection for the UI kit.
 *
 * Wraps window.QLCPlusAPI (api/qlcplus-api.js) in a React context so any screen can
 * read connection status and push values to a real desk. Every screen keeps working
 * unconnected — the hook simply reports status "offline" and the send helpers no-op,
 * so the kit stays a click-through mock until someone types a host.
 */
const QLCConnectionContext = React.createContext(null);

/** ws:// from an https page is blocked by the browser and can never succeed — say so up front. */
const WS_BLOCKED = typeof location !== 'undefined' && location.protocol === 'https:';

function diagnose(host) {
  return WS_BLOCKED
    ? 'Blocked: this page is served over https, and browsers refuse ws:// from a secure page. Open the kit from disk or over plain http.'
    : 'Could not reach ' + host + '. Check QLC+ was started with --api (or --api-port <n> if not using 9010), and that the port is right.';
}

function QLCConnectionProvider({ children }) {
  const [host, setHost] = React.useState(() => {
    try { return localStorage.getItem('qlc.host') || ''; } catch (e) { return ''; }
  });
  const [port, setPort] = React.useState(() => {
    try { return Number(localStorage.getItem('qlc.port')) || 9010; } catch (e) { return 9010; }
  });
  const [status, setStatus] = React.useState('offline');
  const [log, setLog] = React.useState([]);
  const clientRef = React.useRef(null);
  const didOpen = React.useRef(false);
  const listeners = React.useRef(new Set());

  const push = (level, text) => setLog(l => l.concat([{ level, text }]).slice(-40));

  const disconnect = React.useCallback(() => {
    didOpen.current = true;
    if (clientRef.current) clientRef.current.disconnect();
    clientRef.current = null;
    setStatus('offline');
    push('info', 'Disconnected');
  }, []);

  const connect = React.useCallback((h, p) => {
    const raw = (h || '').trim();
    if (!raw) return;
    if (!window.QLCPlusAPI) { push('error', 'qlcplus-api.js not loaded'); return; }
    if (clientRef.current) clientRef.current.disconnect();

    /* A "host:port" typed straight into the host field still wins, so pasting one keeps
       working even though there's now a dedicated port field. */
    const parts = raw.split(':');
    const targetHost = parts[0];
    const targetPort = parts[1] ? Number(parts[1]) : (p != null ? Number(p) : 9010);
    const target = targetHost + ':' + targetPort;

    try {
      localStorage.setItem('qlc.host', targetHost);
      localStorage.setItem('qlc.port', String(targetPort));
    } catch (e) {}
    setHost(targetHost);
    setPort(targetPort);
    setStatus('connecting');
    didOpen.current = false;
    push('info', 'Connecting to ' + target);

    const client = new window.QLCPlusAPI(targetHost, {
      port: targetPort,
      autoReconnect: false
    });
    client.on('open', () => {
      didOpen.current = true;
      push('ok', 'Socket open — saying hello…');
    });
    /* The handshake. A WebSocket upgrade only proves something answered on that port; every
       other method 403s until "hello" completes, so 'ready' is the real "this is QLC+ and
       we're allowed to talk to it" signal (the client sends hello automatically on open). */
    client.on('ready', (hello) => {
      setStatus('online');
      push('ok', 'QLC+ ready on ' + target + (hello.serverVersion ? ' (v' + hello.serverVersion + ')' : ''));
      client.getProject().then((proj) => {
        push('info', proj.fileName ? 'Workspace: ' + proj.fileName : 'No workspace saved yet (untitled)');
      }).catch(() => {});
      client.getFunctionsList();
      client.getWidgetsList();
    });
    client.on('close', () => {
      if (didOpen.current) { setStatus('offline'); push('info', 'Connection closed'); }
      else {
        setStatus('error');
        /* When ws:// is blocked the standing warning already carries the explanation, so the
           log gets only the short status — otherwise the popover prints it twice. */
        if (WS_BLOCKED) push('error', 'Connection failed');
        else push('diag', diagnose(target));
      }
    });
    client.on('error', () => { setStatus('error'); push('error', 'Connection failed'); });
    client.on('message', (frame) => {
      listeners.current.forEach(fn => { try { fn(frame); } catch (e) {} });
      if (frame.type === 'event') push('info', frame.topic);
    });
    client.connect();
    clientRef.current = client;
  }, []);

  React.useEffect(() => () => { if (clientRef.current) clientRef.current.disconnect(); }, []);

  const value = React.useMemo(() => ({
    host, port, status, log, connect, disconnect,
    online: status === 'online',
    client: () => clientRef.current,
    /** Subscribe to every inbound frame. Returns an unsubscribe function. */
    subscribe: (fn) => { listeners.current.add(fn); return () => listeners.current.delete(fn); },
    /** Subscribe to one decoded event ('channels', 'grandmaster', 'blackout', 'alert',
        'getWidgetsList', 'getFunctionsList'…) — see api/qlcplus-api.js's own on() doc. */
    subscribeTo: (event, fn) => {
      const c = clientRef.current;
      if (!c) return () => {};
      c.on(event, fn);
      return () => { const l = c._handlers[event]; if (l) l.splice(l.indexOf(fn) >>> 0, 1); };
    },
    absoluteChannel: (u, a) => ((Math.max(1, u | 0) - 1) * 512) + (a | 0),
    setChannel: (ch, v) => { if (clientRef.current) clientRef.current.setChannel(ch, v); },
    resetChannel: (ch) => { if (clientRef.current) clientRef.current.resetChannel(ch); },
    resetUniverse: (u) => { if (clientRef.current) clientRef.current.resetUniverse(u); },
    startPolling: (u, a, n) => { if (clientRef.current) clientRef.current.startPolling(u, a, n); },
    stopPolling: () => { if (clientRef.current) clientRef.current.stopPolling(); },
    setWidget: (id, v) => { if (clientRef.current) clientRef.current.setWidget(id, v); },
    startFunction: (id) => { if (clientRef.current) clientRef.current.startFunction(id); },
    stopFunction: (id) => { if (clientRef.current) clientRef.current.stopFunction(id); }
  }), [host, port, status, log, connect, disconnect]);

  return React.createElement(QLCConnectionContext.Provider, { value }, children);
}

function useQLC() {
  return React.useContext(QLCConnectionContext) || {
    host: '', port: 9010, status: 'offline', online: false, log: [],
    connect: () => {}, disconnect: () => {}, subscribe: () => () => {},
    client: () => null, setChannel: () => {}, setWidget: () => {},
    startFunction: () => {}, stopFunction: () => {}
  };
}

/**
 * Toolbar affordance: one 38px icon button whose glyph carries the connection state.
 * Clicking opens a popover with the host field — the connect UI must never cost enough
 * width to push Blackout or Stop-all off a narrow toolbar.
 */
function ConnectionBar() {
  const { GenericButton, IconButton, RobotoText, CustomTextInput, CustomSpinBox } = window.PatchDesignSystem_5432c9;
  const qlc = useQLC();
  const [open, setOpen] = React.useState(false);
  const [draft, setDraft] = React.useState(qlc.host || 'localhost');
  const [draftPort, setDraftPort] = React.useState(qlc.port || 9010);
  const anchor = React.useRef(null);
  const [box, setBox] = React.useState({ top: 0, right: 0 });

  /* The panel is position:fixed, measured off the button, rather than absolutely positioned
     inside the toolbar. A toolbar is a 38px band, so ANY ancestor that clips overflow would
     swallow a dropdown anchored within it — fixed escapes that whole class of bug. */
  const place = () => {
    const el = anchor.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    setBox({ top: Math.round(r.bottom + 2), right: Math.round(window.innerWidth - r.right) });
  };
  const toggle = () => { if (!open) place(); setOpen(!open); };

  React.useEffect(() => {
    if (!open) return;
    const onKey = (e) => { if (e.key === 'Escape') setOpen(false); };
    const onDown = (e) => {
      if (anchor.current && !anchor.current.contains(e.target)) setOpen(false);
    };
    window.addEventListener('keydown', onKey);
    window.addEventListener('resize', place);
    document.addEventListener('mousedown', onDown, true);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('resize', place);
      document.removeEventListener('mousedown', onDown, true);
    };
  }, [open]);
  const lamp = {
    offline: 'var(--fg-medium)',
    connecting: 'var(--selection)',
    online: 'var(--check-lime)',
    error: 'var(--override-red)'
  }[qlc.status];
  const title = {
    offline: 'not connected',
    connecting: 'connecting…',
    online: 'live on ' + qlc.host + ':' + qlc.port,
    error: 'connection failed'
  }[qlc.status];
  const tip = 'QLC+ Web API — ' + title;
  /* Show the newest log line, not the bare status: the useful part of a failure is the
     diagnostic ("…is https, ws:// is blocked" / "check --web"), and it has nowhere else to go. */
  const detail = (qlc.log.length ? qlc.log[qlc.log.length - 1] : null)
    || { level: 'info', text: 'Not connected' };

  return (
    <span ref={anchor} style={{ position: 'relative', display: 'inline-flex', flex: 'none' }}>
      <IconButton imgSource={window.QLCData.icon('network')} tooltip={tip}
        checked={qlc.status === 'online'} onClick={toggle} />
      <span style={{
        position: 'absolute', right: 3, bottom: 3, width: 9, height: 9, borderRadius: 5,
        background: lamp, border: '1px solid var(--border-color-dark)', pointerEvents: 'none'
      }} />
      {open ? (
        <span style={{
          position: 'fixed', top: box.top, right: box.right, zIndex: 400,
          display: 'flex', flexDirection: 'column', gap: 6, padding: 8, width: 216,
          background: 'var(--bg-medium)', border: 'var(--border-dialog)'
        }}>
          <RobotoText label="QLC+ Web API" fontBold fontSize="var(--text-size-small)" height={20} />
          <span style={{ display: 'flex', gap: 6 }}>
            <span style={{
              flex: 1, minWidth: 0, height: 26, display: 'flex', alignItems: 'center',
              background: 'var(--bg-control)', border: '1px solid var(--spin-border)',
              borderRadius: 'var(--radius-spin)', padding: '0 5px'
            }}>
              {/* `editing` is forced on, NOT allowDoubleClick: CustomTextInput is read-only until
                 double-clicked, which is right for inline rename in a tree but wrong for the one
                 field this dialog exists to collect — it would swallow every keystroke silently.
                 Enter connects, since connecting is the dialog's only action. */}
              <CustomTextInput text={draft} editing onTextConfirmed={setDraft} autoFocus
                placeholder="localhost"
                onKeyDown={(e) => {
                  if (e.key !== 'Enter') return;
                  const v = e.target.value.trim();
                  setDraft(v);
                  qlc.connect(v, draftPort);
                }}
                width="100%" height={22} color="var(--fg-main)" />
            </span>
            {/* Dedicated port field: most people never guessed "host:port" could go in one box.
               A "host:port" string typed into the host field still overrides this, so old habits
               and saved shortcuts keep working. */}
            <CustomSpinBox value={draftPort} onValueModified={setDraftPort}
              from={1} to={65535} showControls={false} width={58} height={26}
              onKeyDown={(e) => { if (e.key === 'Enter') qlc.connect(draft, draftPort); }} />
          </span>
          {WS_BLOCKED && qlc.status !== 'online' ? (
            <RobotoText label={diagnose(draft)} fontSize="var(--text-size-menubar)" wrapText
              labelColor="var(--selection)" height="auto" />
          ) : null}
          <RobotoText label={detail.text} fontSize="var(--text-size-menubar)" wrapText
            labelColor={detail.level === 'error' ? 'var(--override-red)'
              : detail.level === 'diag' ? 'var(--selection)'
              : detail.level === 'ok' ? 'var(--check-lime)' : 'var(--fg-light)'} height="auto" />
          {qlc.status === 'online' || qlc.status === 'connecting'
            ? <GenericButton label="Disconnect" width="100%" height={26} fontSize="var(--text-size-menubar)"
                onClick={() => { qlc.disconnect(); setOpen(false); }} />
            : <GenericButton label="Connect" width="100%" height={26} fontSize="var(--text-size-menubar)"
                bgColor="var(--keypad-enter)" hoverColor="var(--keypad-enter-hover)" pressedColor="var(--keypad-enter-pressed)"
                onClick={() => qlc.connect(draft, draftPort)} />}        </span>
      ) : null}
    </span>
  );
}

Object.assign(window, { QLCConnectionContext, QLCConnectionProvider, useQLC, ConnectionBar });
