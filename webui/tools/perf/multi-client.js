// Several web UI tabs on the Virtual Console while another client drags a fader, plus one VC edit.
//
//   node webui/tools/perf/multi-client.js --url http://localhost:9341/ --port 9340 [--tabs 4]
//        [--hz 30] [--seconds 6] [--proc qlc-perf] [--json out.json]
//
// SANDBOX ONLY (it moves a slider and edits a widget's geometry).
//  1. opens N headless tabs on ?ctx=vc one after another (each waits until its data is shown, so
//     N Babel compiles don't overlap), on the page that holds the first Slider widget;
//  2. a Node client sends vc.slider.setValue at --hz (the UI's own drag throttle is 33 ms = 30 Hz)
//     for --seconds, stamping every send; each tab stamps the matching event on arrival, so
//     latency = arrival - send (same machine, same clock);
//  3. per tab: events received, renderer main-thread busy % and long tasks during the drag;
//     server: CPU seconds of the QLC+ process per wall second (from --proc via PowerShell);
//  4. one vc.widget.update (geometry x+1, then back) from the Node client, and what every tab
//     fetched in the 2 s after it (the refetch fan-out).

const { launch, sleep } = require('../cdp.js');
const { connect, pct } = require('./ws-client.js');
const { execSync } = require('child_process');
const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i < 0 ? d : process.argv[i + 1]; };
const base = arg('url', ''), port = Number(arg('port', 0));
if (!base || !port || port === 9010 || /:9011\b/.test(base)) { console.error('pass --url <sandbox web UI> --port <sandbox API port> (never 9010/9011)'); process.exit(2); }
const nTabs = Number(arg('tabs', '4')), hz = Number(arg('hz', '30')), seconds = Number(arg('seconds', '6'));
const proc = arg('proc', '');

const INSTRUMENT = `(() => {
  const t = (window.__perf = { api: [], ev: [], long: [], lastResponse: null, wsReady: null });
  try { new PerformanceObserver(l => l.getEntries().forEach(e => t.long.push([Math.round(e.startTime), Math.round(e.duration)]))).observe({ type: 'longtask', buffered: true }); } catch (e) {}
  const WS = window.WebSocket;
  window.WebSocket = function (u, p) {
    const ws = p ? new WS(u, p) : new WS(u); const pend = {}; const send = ws.send.bind(ws);
    ws.send = (d) => { try { const f = JSON.parse(d); if (f.type === 'request') pend[f.id] = { m: f.method, t0: Date.now() }; } catch (e) {} return send(d); };
    ws.addEventListener('message', (ev) => {
      let f; try { f = JSON.parse(ev.data); } catch (e) { return; }
      const now = Date.now();
      if (f.type === 'response' && pend[f.id]) { const p = pend[f.id]; delete pend[f.id]; t.api.push({ m: p.m, at: now, bytes: ev.data.length }); t.lastResponse = performance.now(); if (p.m === 'hello') t.wsReady = performance.now(); }
      else if (f.type === 'event') t.ev.push({ topic: f.topic, at: now, bytes: ev.data.length, w: f.data && f.data.widgetId, v: f.data && f.data.value });
    });
    return ws;
  };
  window.WebSocket.prototype = WS.prototype; Object.assign(window.WebSocket, { OPEN: 1, CLOSED: 3, CONNECTING: 0, CLOSING: 2 });
})();`;

function procCpuMs() {
  if (!proc) return NaN;
  try { return Number(execSync(`powershell -NoProfile -Command "(Get-Process -Name ${proc}).TotalProcessorTime.TotalMilliseconds -as [long]"`).toString().trim()); } catch (e) { return NaN; }
}

(async () => {
  const c = await connect(`ws://localhost:${port}/`, { name: 'perf-fader' });
  const widgets = (await c.call('vc.widget.list', {})).widgets;
  const slider = widgets.find(w => w.widgetType === 'Slider' && !w.isDisabled);
  if (!slider) throw new Error('no Slider widget in this project');
  try { await c.call('vc.page.select', { page: slider.page }); } catch (e) { console.log('vc.page.select:', e.message); }
  console.log(`slider #${slider.id} "${slider.style && slider.style.caption}" on page ${slider.page}`);

  const b = await launch();
  const tabs = [];
  for (let i = 0; i < nTabs; i++) {
    const page = await b.open(null);
    await page.s.send('Performance.enable');
    await page.s.send('Page.addScriptToEvaluateOnNewDocument', { source: INSTRUMENT });
    await page.goto(base + (base.includes('?') ? '&' : '?') + 'ctx=vc');
    const t0 = Date.now();
    for (;;) { await sleep(250); const st = await page.eval('window.__perf && window.__perf.wsReady && window.__perf.lastResponse && performance.now() - window.__perf.lastResponse > 1500').catch(() => false); if (st || Date.now() - t0 > 90000) break; }
    tabs.push(page);
    console.log(`tab ${i + 1} ready after ${Date.now() - t0} ms`);
  }
  const metric = async (p) => Object.fromEntries((await p.s.send('Performance.getMetrics')).metrics.map(m => [m.name, m.value]));

  // --- fader drag
  for (const p of tabs) await p.eval('window.__perf.ev = []; window.__perf.long = []; window.__perf.api = []');
  const m0 = await Promise.all(tabs.map(metric)); const cpu0 = procCpuMs(); const wall0 = Date.now();
  const sends = new Map(); const rtts = [];
  const nSends = Math.round(hz * seconds);
  for (let i = 0; i < nSends; i++) {
    const v = Math.round(127 + 120 * Math.sin(i / 10)) | 0;
    const tsend = Date.now(); sends.set(v, tsend);
    c.timed('vc.slider.setValue', { widgetId: String(slider.id), value: v }).then(r => rtts.push(r.ms), () => {});
    await sleep(1000 / hz);
  }
  await sleep(500);
  const cpu1 = procCpuMs(); const wall1 = Date.now(); const m1 = await Promise.all(tabs.map(metric));
  console.log(`fader: ${nSends} vc.slider.setValue at ${hz} Hz, sender round trip p50 ${pct(rtts, 50).toFixed(1)} ms, p95 ${pct(rtts, 95).toFixed(1)} ms; QLC+ process CPU ${((cpu1 - cpu0) / (wall1 - wall0)).toFixed(2)} core-seconds per second`);
  const result = { slider: slider.id, tabs: [], fanout: [] };
  for (let i = 0; i < tabs.length; i++) {
    const d = await tabs[i].eval('({ ev: window.__perf.ev, long: window.__perf.long, api: window.__perf.api })');
    const mine = d.ev.filter(e => e.w === String(slider.id));
    const lat = mine.filter(e => sends.has(e.v)).map(e => e.at - sends.get(e.v));
    const topics = {}; d.ev.forEach(e => { topics[e.topic] = (topics[e.topic] || 0) + 1; });
    const busy = Math.round(100 * (m1[i].TaskDuration - m0[i].TaskDuration) / (m1[i].Timestamp - m0[i].Timestamp));
    const row = { tab: i + 1, events: d.ev.length, kib: Math.round(d.ev.reduce((s, e) => s + e.bytes, 0) / 1024), latP50: pct(lat, 50), latP95: pct(lat, 95), latMax: Math.max(0, ...lat), busyPct: busy, longTasks: d.long.length, longMax: Math.max(0, ...d.long.map(l => l[1])), apiCalls: d.api.length, topics };
    result.tabs.push(row);
    console.log(`  tab ${row.tab}: ${row.events} events (${row.kib} KiB), slider latency p50 ${row.latP50} ms p95 ${row.latP95} ms max ${row.latMax} ms, main thread busy ${busy}%, long tasks ${row.longTasks} (max ${row.longMax} ms), API calls made ${row.apiCalls}; ${JSON.stringify(topics)}`);
  }

  // --- one VC edit, and what every tab refetches because of it
  for (const p of tabs) await p.eval('window.__perf.ev = []; window.__perf.api = []');
  const w = (await c.call('vc.widget.get', { widgetId: String(slider.id) })).widget || slider;
  const g = w.geometry || slider.geometry;
  const rev = () => c.call('core.project.get', {}).then(r => r.docRevision).catch(() => undefined);
  let br = await rev();
  const edit = async (x) => { try { const r = await c.timed('vc.widget.update', { widgetId: String(slider.id), baseRevision: br, geometry: Object.assign({}, g, { x }) }); br = r.result.docRevision || await rev(); return r.ms; } catch (e) { console.log('edit failed:', e.message); br = await rev(); return NaN; } };
  const editMs = await edit(g.x + 1);
  await sleep(2000);
  for (let i = 0; i < tabs.length; i++) {
    const d = await tabs[i].eval('({ ev: window.__perf.ev.map(e => e.topic), api: window.__perf.api.map(a => a.m + ":" + a.bytes) })');
    const apiBy = {}; let bytes = 0; d.api.forEach(s => { const [m, n] = s.split(':'); apiBy[m] = (apiBy[m] || 0) + 1; bytes += Number(n); });
    result.fanout.push({ tab: i + 1, events: d.ev, api: apiBy, bytes });
    console.log(`  after one vc.widget.update (${editMs.toFixed(1)} ms): tab ${i + 1} got events ${JSON.stringify(d.ev)} and made ${d.api.length} calls ${JSON.stringify(apiBy)} = ${(bytes / 1024).toFixed(1)} KiB`);
  }
  await edit(g.x); // put it back
  const out = arg('json', null);
  if (out) require('fs').writeFileSync(out, JSON.stringify(result, null, 1));
  c.close(); await b.close();
})().catch(e => { console.error(e); process.exit(1); });
