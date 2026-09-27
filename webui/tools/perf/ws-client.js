// Minimal Control API client for performance measurements (Node >= 22: global WebSocket, no npm).
//
//   const { connect } = require('./ws-client.js');
//   const c = await connect('ws://localhost:9340/');     // sends hello, resolves when ready
//   const { result, ms, bytes } = await c.timed('functions.list', {});
//   c.onEvent((topic, data, bytes) => ...);               // every inbound event frame
//   c.close();
//
// `ms` is the client-side round trip (send -> full response parsed). On loopback that is almost
// entirely server time: the handler runs synchronously on the QLC+ main thread, so the round trip
// is also how long that call blocked the desktop UI (and everything else queued on that thread).

function connect(url, { name = 'perf-probe' } = {}) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(url);
    let nextId = 1;
    const pending = new Map();
    const eventHandlers = [];
    const c = {
      ws, clientId: null, bytesIn: 0, framesIn: 0,
      call(method, params = {}) { return c.timed(method, params).then(r => r.result); },
      timed(method, params = {}) {
        return new Promise((res, rej) => {
          const id = String(nextId++);
          const t0 = performance.now();
          pending.set(id, { res, rej, t0, method });
          ws.send(JSON.stringify({ type: 'request', id, method, params }));
        });
      },
      onEvent(fn) { eventHandlers.push(fn); return () => eventHandlers.splice(eventHandlers.indexOf(fn), 1); },
      close() { try { ws.close(); } catch (e) { } }
    };
    ws.onmessage = (ev) => {
      const raw = typeof ev.data === 'string' ? ev.data : Buffer.from(ev.data).toString('utf8');
      const bytes = Buffer.byteLength(raw);
      c.bytesIn += bytes; c.framesIn++;
      const f = JSON.parse(raw);
      if (f.type === 'response') {
        const p = pending.get(f.id); if (!p) return;
        pending.delete(f.id);
        const ms = performance.now() - p.t0;
        if (f.ok) p.res({ result: f.result, ms, bytes });
        else { const e = new Error(p.method + ': ' + (f.error && f.error.code) + ' ' + (f.error && f.error.message)); e.code = f.error && f.error.code; e.ms = ms; p.rej(e); }
      } else if (f.type === 'event') {
        for (const h of eventHandlers.slice()) h(f.topic, f.data, bytes, f.originClientId);
      }
    };
    ws.onerror = (e) => reject(new Error('websocket error ' + url));
    ws.onopen = async () => {
      try {
        const hello = await c.call('hello', { apiVersion: '1', clientName: name });
        c.clientId = hello.clientId;
        resolve(c);
      } catch (e) { reject(e); }
    };
  });
}

/** Percentile over a numeric array (p in 0..100). */
function pct(values, p) {
  if (!values.length) return NaN;
  const s = values.slice().sort((a, b) => a - b);
  return s[Math.min(s.length - 1, Math.floor(s.length * p / 100))];
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

/**
 * Main-thread stall probe: a separate connection sends a cheap request (io.grandMaster.get) every
 * `intervalMs` and records each round trip. While a slow handler runs on the server, these pile up
 * and come back late, so max(rtt) during a window ~= the longest main-thread stall in it.
 */
async function stallProbe(url, intervalMs = 5) {
  const c = await connect(url, { name: 'perf-stall-probe' });
  const samples = [];
  let running = true;
  const loop = (async () => {
    while (running) {
      const t = performance.now();
      try { const r = await c.timed('io.grandMaster.get', {}); samples.push({ t, ms: r.ms }); } catch (e) { }
      const wait = intervalMs - (performance.now() - t);
      if (wait > 0) await sleep(wait);
    }
  })();
  return {
    mark() { return samples.length; },
    since(i) { return samples.slice(i).map(s => s.ms); },
    async stop() { running = false; await loop; c.close(); }
  };
}

module.exports = { connect, pct, sleep, stallProbe };
