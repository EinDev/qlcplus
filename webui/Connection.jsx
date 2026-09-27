/**
 * Live QLC+ Control API connection for the web UI.
 *
 * Wraps window.QLCPlusAPI (api/qlcplus-api.js) in a React context so any screen can read the
 * connection status, call methods and subscribe to pushed events.
 *
 * Where the target comes from, in priority order:
 *   1. a host/port the user typed into the network popover (remembered in localStorage as
 *      qlc.host / qlc.port — only written on an explicit Connect, never from a default);
 *   2. GET qlcplus-config.json served by QLC+ next to this page:
 *      {"apiPort": <number>, "apiHost": <string|null>} — apiHost null means "same host as the page";
 *   3. window.location.hostname + port 9010 when that file cannot be fetched (opened from disk,
 *      served by some other static server, ...).
 *
 * Auto-connect: when the page is served over http(s) we connect on load without asking — that is
 * the "QLC+ served this page" case, and the target is almost always right. Opened via file:// we
 * stay offline until the user presses Connect (Chrome cannot run the page from file:// anyway,
 * see README). Unconnected, every screen still works on local mock state.
 */
const QLCConnectionContext = React.createContext(null);

const FALLBACK_PORT = 9010;
const PAGE_PROTOCOL = typeof location !== 'undefined' ? location.protocol : '';
const SERVED_OVER_HTTP = PAGE_PROTOCOL === 'http:' || PAGE_PROTOCOL === 'https:';
/** ws:// from an https page is blocked by the browser and can never succeed — say so up front. */
const WS_BLOCKED = PAGE_PROTOCOL === 'https:';

function readStored(key) {
  try { return localStorage.getItem(key) || ''; } catch (e) { return ''; }
}
function writeStored(key, value) {
  try { if (value) localStorage.setItem(key, value); else localStorage.removeItem(key); } catch (e) {}
}

function diagnose(target) {
  return WS_BLOCKED
    ? 'Blocked: this page is served over https, and browsers refuse ws:// from a secure page. Serve the web UI over plain http.'
    : 'Could not reach ' + target + '. Check QLC+ is running with its Control API enabled (--api / --api-port <n>) and that host and port are right.';
}

function QLCConnectionProvider({ children }) {
  /* Server-provided default (qlcplus-config.json). `loaded` flips once the fetch settled either way. */
  const [config, setConfig] = React.useState({ apiHost: null, apiPort: null, loaded: false });
  /* User override, if any. Empty string = "use the server default". */
  const [storedHost, setStoredHost] = React.useState(() => readStored('qlc.host'));
  const [storedPort, setStoredPort] = React.useState(() => Number(readStored('qlc.port')) || 0);
  const [status, setStatus] = React.useState('offline');
  const [target, setTarget] = React.useState({ host: '', port: 0 });
  const [serverInfo, setServerInfo] = React.useState(null);
  const [reconnect, setReconnect] = React.useState(null);
  const [unsupported, setUnsupported] = React.useState({});
  const [log, setLog] = React.useState([]);
  const clientRef = React.useRef(null);
  const didOpen = React.useRef(false);
  const autoConnected = React.useRef(false);

  const push = (level, text) => setLog(l => l.concat([{ level, text, at: Date.now() }]).slice(-60));

  const pageHost = (typeof location !== 'undefined' && location.hostname) || 'localhost';
  const defaultHost = config.apiHost || pageHost;
  const defaultPort = config.apiPort || FALLBACK_PORT;
  const effectiveHost = storedHost || defaultHost;
  const effectivePort = storedPort || defaultPort;

  const disconnect = React.useCallback(() => {
    didOpen.current = true;
    if (clientRef.current) clientRef.current.disconnect();
    clientRef.current = null;
    setStatus('offline');
    setReconnect(null);
    push('info', 'Disconnected');
  }, []);

  /**
   * Open a connection. `remember` persists host/port as a user override; the auto-connect path
   * passes false so a later change of the server's apiPort is picked up on the next load.
   */
  const connect = React.useCallback((h, p, remember) => {
    const raw = String(h || '').trim();
    if (!raw) return;
    if (!window.QLCPlusAPI) { push('error', 'qlcplus-api.js not loaded'); return; }
    if (clientRef.current) clientRef.current.disconnect();

    /* A "host:port" typed straight into the host field still wins over the port field. A bracketed
       IPv6 literal ("[::1]", the page's own hostname when it was opened on IPv6 loopback) keeps its
       colons: only a single trailing ":digits" is a port. */
    const m = /^(\[[^\]]+\]|[^:]+)(?::(\d+))?$/.exec(raw);
    const targetHost = m ? m[1] : raw;
    const targetPort = m && m[2] ? Number(m[2]) : (Number(p) || FALLBACK_PORT);
    const label = targetHost + ':' + targetPort;

    if (remember) {
      writeStored('qlc.host', targetHost);
      writeStored('qlc.port', String(targetPort));
      setStoredHost(targetHost);
      setStoredPort(targetPort);
    }
    setTarget({ host: targetHost, port: targetPort });
    setStatus('connecting');
    setReconnect(null);
    didOpen.current = false;
    push('info', 'Connecting to ' + label);

    const client = new window.QLCPlusAPI(targetHost, { port: targetPort });
    client.on('open', () => {
      didOpen.current = true;
      push('ok', 'Socket open — saying hello…');
    });
    /* The handshake. A WebSocket upgrade only proves something answered on that port; every
       other method is rejected until "hello" completes, so 'ready' is the real "this is QLC+
       and we're allowed to talk to it" signal (the client sends hello automatically on open). */
    client.on('ready', (hello) => {
      setStatus('online');
      setReconnect(null);
      setServerInfo({ version: hello.serverVersion, clientId: hello.clientId });
      push('ok', 'QLC+ ready on ' + label + (hello.serverVersion ? ' (v' + hello.serverVersion + ')' : ''));
    });
    client.on('reconnecting', (info) => {
      setStatus('reconnecting');
      setReconnect(info);
      push('diag', 'Connection lost — retry ' + info.attempt + ' in ' + Math.round(info.delay / 1000) + 's');
    });
    client.on('close', () => {
      if (didOpen.current) { setStatus(s => s === 'reconnecting' ? s : 'offline'); push('info', 'Connection closed'); }
      else {
        setStatus('error');
        if (WS_BLOCKED) push('error', 'Connection failed');
        else push('diag', diagnose(label));
        /* A first attempt that never even opened is not retried: the address is probably wrong. */
        client.disconnect();
      }
    });
    client.on('error', () => { if (!didOpen.current) setStatus('error'); });
    client.on('unsupported', (method) => {
      setUnsupported(u => Object.assign({}, u, { [method]: true }));
      push('diag', 'Server has no method ' + method);
    });
    client.on('apiError', ({ method, error }) => {
      if (error && error.code === 'NOT_FOUND' && /^Unknown method/.test(error.message || '')) return;
      push('error', method + ': ' + ((error && error.message) || 'failed'));
    });
    client.connect();
    clientRef.current = client;
  }, []);

  /* Fetch the server's default target once. Relative URL: works wherever the page is mounted. */
  React.useEffect(() => {
    let cancelled = false;
    const settle = (cfg) => { if (!cancelled) setConfig(Object.assign({ apiHost: null, apiPort: null }, cfg, { loaded: true })); };
    if (!SERVED_OVER_HTTP) { settle({}); return () => { cancelled = true; }; }
    fetch('qlcplus-config.json', { cache: 'no-store' })
      .then(r => (r.ok ? r.json() : Promise.reject(new Error('HTTP ' + r.status))))
      .then(cfg => settle({ apiHost: cfg.apiHost || null, apiPort: Number(cfg.apiPort) || null }))
      .catch(() => settle({}));
    return () => { cancelled = true; };
  }, []);

  /* Auto-connect once the default is known, only when QLC+ (or some http server) served us. */
  React.useEffect(() => {
    if (!config.loaded || autoConnected.current || !SERVED_OVER_HTTP) return;
    autoConnected.current = true;
    connect(effectiveHost, effectivePort, false);
  }, [config.loaded]);

  React.useEffect(() => () => { if (clientRef.current) clientRef.current.disconnect(); }, []);

  const value = React.useMemo(() => ({
    host: target.host || effectiveHost,
    port: target.port || effectivePort,
    defaultHost, defaultPort,
    hasOverride: !!(storedHost || storedPort),
    configLoaded: config.loaded,
    status, log, connect, disconnect, reconnect, serverInfo, unsupported,
    online: status === 'online',
    wsBlocked: WS_BLOCKED,
    servedOverHttp: SERVED_OVER_HTTP,
    /** Forget the user override and reconnect to the server-provided default. */
    useDefault: () => {
      writeStored('qlc.host', ''); writeStored('qlc.port', '');
      setStoredHost(''); setStoredPort(0);
      connect(defaultHost, defaultPort, false);
    },
    client: () => clientRef.current,
    /** Promise-returning request; rejects with code NOT_CONNECTED when offline. */
    call: (method, params) => {
      const c = clientRef.current;
      if (!c || !c.connected()) { const e = new Error('not connected'); e.code = 'NOT_CONNECTED'; return Promise.reject(e); }
      return c.call(method, params);
    },
    docRevision: () => (clientRef.current ? clientRef.current.docRevision : null),
    isUnsupported: (method) => !!unsupported[method],
    /** Subscribe to one client event ('channels', 'blackout', 'unsupported', any server topic…).
        Returns an unsubscribe function. */
    subscribeTo: (event, fn) => {
      const c = clientRef.current;
      if (!c) return () => {};
      c.on(event, fn);
      return () => c.off(event, fn);
    }
  }), [target, effectiveHost, effectivePort, defaultHost, defaultPort, storedHost, storedPort, config.loaded,
    status, log, connect, disconnect, reconnect, serverInfo, unsupported]);

  return React.createElement(QLCConnectionContext.Provider, { value }, children);
}

function useQLC() {
  return React.useContext(QLCConnectionContext) || {
    host: '', port: FALLBACK_PORT, status: 'offline', online: false, log: [], unsupported: {},
    connect: () => {}, disconnect: () => {}, useDefault: () => {}, client: () => null,
    call: () => Promise.reject(new Error('not connected')), docRevision: () => null,
    isUnsupported: () => false, subscribeTo: () => () => {}
  };
}

/**
 * Toolbar affordance: one 38px icon button whose corner lamp carries the connection state.
 * Clicking opens a popover with host/port fields — the connect UI must never cost enough
 * width to push Blackout or Stop-all off a narrow toolbar.
 */
function ConnectionBar() {
  const { GenericButton, IconButton, RobotoText, CustomTextInput, CustomSpinBox } = window.PatchDesignSystem_5432c9;
  const qlc = useQLC();
  const [open, setOpen] = React.useState(false);
  const [draft, setDraft] = React.useState(qlc.host || 'localhost');
  const [draftPort, setDraftPort] = React.useState(qlc.port || FALLBACK_PORT);
  const anchor = React.useRef(null);
  const [box, setBox] = React.useState({ top: 0, right: 0 });

  /* Keep the drafts in step with whatever we actually ended up targeting (auto-connect fills
     them in after the config fetch), but never while the popover is open and being edited. */
  React.useEffect(() => {
    if (open) return;
    if (qlc.host) setDraft(qlc.host);
    if (qlc.port) setDraftPort(qlc.port);
  }, [qlc.host, qlc.port, open]);

  /* The panel is position:fixed, measured off the button, rather than absolutely positioned
     inside the toolbar: any ancestor that clips overflow would swallow an anchored dropdown. */
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
    const onDown = (e) => { if (anchor.current && !anchor.current.contains(e.target)) setOpen(false); };
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
    reconnecting: 'var(--selection)',
    online: 'var(--check-lime)',
    error: 'var(--override-red)'
  }[qlc.status];
  const title = {
    offline: 'not connected',
    connecting: 'connecting to ' + qlc.host + ':' + qlc.port + '…',
    reconnecting: 'connection lost — reconnecting' + (qlc.reconnect ? ' (attempt ' + qlc.reconnect.attempt + ')' : ''),
    online: 'live on ' + qlc.host + ':' + qlc.port + (qlc.serverInfo && qlc.serverInfo.version ? ' — QLC+ ' + qlc.serverInfo.version : ''),
    error: 'connection failed'
  }[qlc.status];
  const tip = 'QLC+ Control API — ' + title;
  const detail = (qlc.log.length ? qlc.log[qlc.log.length - 1] : null) || { level: 'info', text: 'Not connected' };
  const connectDraft = () => { qlc.connect(draft, draftPort, true); };

  return (
    <span ref={anchor} style={{ position: 'relative', display: 'inline-flex', flex: 'none' }}>
      <IconButton imgSource={window.QLCData.icon('network')} tooltip={tip}
        checked={qlc.status === 'online'} onClick={toggle} />
      <span style={{
        position: 'absolute', right: 3, bottom: 3, width: 9, height: 9, borderRadius: 5,
        background: lamp, border: '1px solid var(--border-color-dark)', pointerEvents: 'none',
        animation: qlc.status === 'connecting' || qlc.status === 'reconnecting' ? 'qlc-lamp-blink 1s steps(2) infinite' : 'none'
      }} />
      <style>{'@keyframes qlc-lamp-blink{to{opacity:.25}}'}</style>
      {open ? (
        <span style={{
          position: 'fixed', top: box.top, right: box.right, zIndex: 400,
          display: 'flex', flexDirection: 'column', gap: 6, padding: 8, width: 236,
          background: 'var(--bg-medium)', border: 'var(--border-dialog)'
        }}>
          <RobotoText label="QLC+ Control API" fontBold fontSize="var(--text-size-small)" height={20} />
          <RobotoText label={title} fontSize="var(--text-size-menubar)" wrapText labelColor={lamp} height="auto" />
          <span style={{ display: 'flex', gap: 6 }}>
            <span style={{
              flex: 1, minWidth: 0, height: 26, display: 'flex', alignItems: 'center',
              background: 'var(--bg-control)', border: '1px solid var(--spin-border)',
              borderRadius: 'var(--radius-spin)', padding: '0 5px'
            }}>
              {/* `editing` is forced on: CustomTextInput is read-only until double-clicked, which is
                 right for inline rename in a tree but wrong for the one field this dialog exists
                 to collect. Enter connects, since connecting is the dialog's only action. */}
              <CustomTextInput text={draft} editing onTextConfirmed={setDraft} autoFocus
                placeholder={qlc.defaultHost || 'localhost'}
                onKeyDown={(e) => {
                  if (e.key !== 'Enter') return;
                  const v = e.target.value.trim();
                  setDraft(v);
                  qlc.connect(v, draftPort, true);
                }}
                width="100%" height={22} color="var(--fg-main)" />
            </span>
            <CustomSpinBox value={draftPort} onValueModified={setDraftPort}
              from={1} to={65535} showControls={false} width={58} height={26}
              onKeyDown={(e) => { if (e.key === 'Enter') connectDraft(); }} />
          </span>
          <RobotoText label={'Server default: ' + (qlc.defaultHost || '?') + ':' + (qlc.defaultPort || '?')
              + (qlc.configLoaded && !qlc.servedOverHttp ? ' (page not served by QLC+)' : '')}
            fontSize="var(--text-size-menubar)" wrapText labelColor="var(--fg-medium)" height="auto" />
          {qlc.wsBlocked && qlc.status !== 'online' ? (
            <RobotoText label={diagnose(draft)} fontSize="var(--text-size-menubar)" wrapText
              labelColor="var(--selection)" height="auto" />
          ) : null}
          <RobotoText label={detail.text} fontSize="var(--text-size-menubar)" wrapText
            labelColor={detail.level === 'error' ? 'var(--override-red)'
              : detail.level === 'diag' ? 'var(--selection)'
              : detail.level === 'ok' ? 'var(--check-lime)' : 'var(--fg-light)'} height="auto" />
          {qlc.status === 'online' || qlc.status === 'connecting' || qlc.status === 'reconnecting'
            ? <GenericButton label="Disconnect" width="100%" height={26} fontSize="var(--text-size-menubar)"
                onClick={() => { qlc.disconnect(); }} />
            : <GenericButton label="Connect" width="100%" height={26} fontSize="var(--text-size-menubar)"
                bgColor="var(--keypad-enter)" hoverColor="var(--keypad-enter-hover)" pressedColor="var(--keypad-enter-pressed)"
                onClick={connectDraft} />}
          {qlc.hasOverride ? (
            <GenericButton label="Use server default" width="100%" height={26} fontSize="var(--text-size-menubar)"
              onClick={() => { qlc.useDefault(); }} />
          ) : null}
        </span>
      ) : null}
    </span>
  );
}

Object.assign(window, { QLCConnectionContext, QLCConnectionProvider, useQLC, ConnectionBar });
