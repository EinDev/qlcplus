// What does a running show cost each open screen? Starts functions, opens one headless tab per
// screen, and for --seconds records per tab: events and bytes received (by topic), renderer
// main-thread busy %, long tasks. Then stops the functions.
//
//   node webui/tools/perf/live-screens.js --url http://localhost:9341/ --port 9340
//        [--ctx sd,vc,show,fx] [--start rgbmatrix|<id,id,...>] [--seconds 10] [--throttle 1] [--json out.json]
//
// SANDBOX ONLY (starts/stops functions). --start rgbmatrix starts every RGB Matrix of the project,
// the busiest realistic load (every one of them rewrites its fixtures every tick).

const { launch, sleep } = require('../cdp.js');
const { connect } = require('./ws-client.js');
const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i < 0 ? d : process.argv[i + 1]; };
const base = arg('url', ''), port = Number(arg('port', 0));
if (!base || !port || port === 9010 || /:9011\b/.test(base)) { console.error('pass --url <sandbox web UI> --port <sandbox API port> (never 9010/9011)'); process.exit(2); }
const ctxs = arg('ctx', 'sd,vc,show,fx').split(','), seconds = Number(arg('seconds', '10')), throttle = Number(arg('throttle', '1'));

const INSTRUMENT = `(() => {
  const t = (window.__perf = { ev: {}, bytes: 0, n: 0, long: [], lastResponse: null, wsReady: null });
  try { new PerformanceObserver(l => l.getEntries().forEach(e => t.long.push(Math.round(e.duration)))).observe({ type: 'longtask', buffered: true }); } catch (e) {}
  const WS = window.WebSocket;
  window.WebSocket = function (u, p) {
    const ws = p ? new WS(u, p) : new WS(u); const pend = {}; const send = ws.send.bind(ws);
    ws.send = (d) => { try { const f = JSON.parse(d); if (f.type === 'request') pend[f.id] = f.method; } catch (e) {} return send(d); };
    ws.addEventListener('message', (ev) => {
      let f; try { f = JSON.parse(ev.data); } catch (e) { return; }
      if (f.type === 'response') { if (pend[f.id] === 'hello') t.wsReady = performance.now(); delete pend[f.id]; t.lastResponse = performance.now(); }
      else if (f.type === 'event') { const k = f.topic.replace(/\\.\\d+\\./, '.N.'); t.ev[k] = (t.ev[k] || 0) + 1; t.bytes += ev.data.length; t.n++; }
    });
    return ws;
  };
  window.WebSocket.prototype = WS.prototype; Object.assign(window.WebSocket, { OPEN: 1, CLOSED: 3, CONNECTING: 0, CLOSING: 2 });
})();`;

(async () => {
  const c = await connect(`ws://localhost:${port}/`, { name: 'perf-live' });
  const fl = (await c.call('functions.list', {})).functions;
  const startArg = arg('start', 'rgbmatrix');
  const ids = startArg === 'rgbmatrix' ? fl.filter(f => f.type === 'RGBMatrix').map(f => f.id) : startArg.split(',');
  const b = await launch();
  const tabs = [];
  for (const ctx of ctxs) {
    const page = await b.open(null);
    await page.s.send('Performance.enable');
    await page.s.send('Page.addScriptToEvaluateOnNewDocument', { source: INSTRUMENT });
    await page.goto(base + (base.includes('?') ? '&' : '?') + 'ctx=' + ctx);
    const t0 = Date.now();
    for (;;) { await sleep(250); const st = await page.eval('window.__perf && window.__perf.wsReady && window.__perf.lastResponse && performance.now() - window.__perf.lastResponse > 1500').catch(() => false); if (st || Date.now() - t0 > 90000) break; }
    if (throttle > 1) await page.s.send('Emulation.setCPUThrottlingRate', { rate: throttle });
    tabs.push({ ctx, page });
  }
  const metric = async (p) => Object.fromEntries((await p.s.send('Performance.getMetrics')).metrics.map(m => [m.name, m.value]));
  // background tabs are throttled by Chrome; measure one tab at a time, each in front
  const rows = [];
  for (const id of ids) await c.call('functions.start', { functionId: id }).catch(e => console.log('start', id, e.message));
  await sleep(1500);
  for (const { ctx, page } of tabs) {
    await page.bringToFront();
    await sleep(500);
    await page.eval('window.__perf.ev = {}; window.__perf.bytes = 0; window.__perf.n = 0; window.__perf.long = []');
    const m0 = await metric(page);
    await sleep(seconds * 1000);
    const m1 = await metric(page);
    const d = await page.eval('({ ev: window.__perf.ev, bytes: window.__perf.bytes, n: window.__perf.n, long: window.__perf.long, dom: document.getElementsByTagName("*").length })');
    const busy = Math.round(100 * (m1.TaskDuration - m0.TaskDuration) / (m1.Timestamp - m0.Timestamp));
    const row = { ctx, throttle, eventsPerSec: +(d.n / seconds).toFixed(1), kibPerSec: +(d.bytes / 1024 / seconds).toFixed(1), busyPct: busy, longTasks: d.long.length, longMax: Math.max(0, ...d.long), dom: d.dom, topics: d.ev };
    rows.push(row);
    console.log(`${ctx.padEnd(5)} x${throttle}: ${row.eventsPerSec} events/s, ${row.kibPerSec} KiB/s, main thread busy ${busy}%, long tasks ${row.longTasks} (max ${row.longMax} ms), DOM ${d.dom}; ${JSON.stringify(d.ev)}`);
  }
  for (const id of ids) await c.call('functions.stop', { functionId: id }).catch(() => {});
  console.log(`(running: ${ids.length} functions ${ids.slice(0, 20).join(',')})`);
  const out = arg('json', null);
  if (out) require('fs').writeFileSync(out, JSON.stringify(rows, null, 1));
  c.close(); await b.close();
})().catch(e => { console.error(e); process.exit(1); });
