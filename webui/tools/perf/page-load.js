// Page-load timeline of the web UI per screen, in headless Chrome.
//
//   node webui/tools/perf/page-load.js --url http://localhost:9341/ [--ctx fx,vc,sd,io,show,fxeditor]
//        [--throttle 4] [--runs 2] [--json out.json]
//
// For each screen (?ctx=...) and run it records, relative to navigation start:
//   scriptsLoaded  - DOMContentLoaded (all classic <script> tags fetched + executed)
//   firstRender    - #root got its first child (React mounted, i.e. every text/babel file compiled + run)
//   wsReady        - the "hello" response arrived (connected)
//   dataShown      - the last API response of the initial burst (no further response for 1.5 s)
// plus: bytes/requests over HTTP, API calls made on load (method -> count, bytes), long tasks,
// and a CPU profile split by script URL (babel.min.js self time = in-browser JSX compile cost).
// Chrome's HTTP cache is disabled for "cold" runs; run 2+ reuses the tab with the cache enabled
// ("warm") - with the server's current Cache-Control: no-cache and no validators that still
// re-downloads everything.

const { launch, sleep } = require('../cdp.js');
const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i < 0 ? d : process.argv[i + 1]; };
const base = arg('url', '');
if (!base || /:9011\b/.test(base)) { console.error('pass --url http://localhost:<sandbox web UI port>/ (never 9011)'); process.exit(2); }
const ctxs = arg('ctx', 'fx,vc,sd,io,show,fxeditor').split(',');
const throttle = Number(arg('throttle', '1'));
const runs = Number(arg('runs', '2'));

const INSTRUMENT = `(() => {
  const t = (window.__perf = { api: [], long: [], firstRender: null, wsReady: null, lastResponse: null, events: 0, eventBytes: 0 });
  try { new PerformanceObserver(l => l.getEntries().forEach(e => t.long.push([Math.round(e.startTime), Math.round(e.duration)]))).observe({ type: 'longtask', buffered: true }); } catch (e) {}
  const mo = new MutationObserver(() => { const r = document.getElementById('root'); if (r && r.childElementCount && t.firstRender === null) { t.firstRender = performance.now(); mo.disconnect(); } });
  document.addEventListener('DOMContentLoaded', () => mo.observe(document.getElementById('root'), { childList: true }));
  const WS = window.WebSocket;
  window.WebSocket = function (u, p) {
    const ws = p ? new WS(u, p) : new WS(u); const pend = {};
    const send = ws.send.bind(ws);
    ws.send = (d) => { try { const f = JSON.parse(d); if (f.type === 'request') pend[f.id] = { m: f.method, t0: performance.now() }; } catch (e) {} return send(d); };
    ws.addEventListener('message', (ev) => {
      const n = ev.data.length; let f; try { f = JSON.parse(ev.data); } catch (e) { return; }
      if (f.type === 'response' && pend[f.id]) {
        const p = pend[f.id]; delete pend[f.id]; const now = performance.now();
        t.api.push({ m: p.m, t0: Math.round(p.t0), ms: Math.round(now - p.t0), bytes: n });
        t.lastResponse = now; if (p.m === 'hello') t.wsReady = now;
      } else if (f.type === 'event') { t.events++; t.eventBytes += n; }
    });
    return ws;
  };
  window.WebSocket.prototype = WS.prototype; Object.assign(window.WebSocket, { OPEN: 1, CLOSED: 3, CONNECTING: 0, CLOSING: 2 });
})();`;

(async () => {
  const b = await launch();
  const results = [];
  for (const ctx of ctxs) {
    const page = await b.open(null);
    const s = page.s;
    await s.send('Network.enable');
    await s.send('Performance.enable');
    await s.send('Profiler.enable');
    await s.send('Page.addScriptToEvaluateOnNewDocument', { source: INSTRUMENT });
    if (throttle > 1) await s.send('Emulation.setCPUThrottlingRate', { rate: throttle });
    for (let run = 0; run < runs; run++) {
      await s.send('Network.setCacheDisabled', { cacheDisabled: run === 0 });
      let httpBytes = 0, httpReqs = 0;
      const onData = (p) => { httpBytes += p.encodedDataLength; httpReqs++; };
      s.on('Network.loadingFinished', onData);
      await s.send('Profiler.setSamplingInterval', { interval: 1000 });
      await s.send('Profiler.start');
      const url = base + (base.includes('?') ? '&' : '?') + 'ctx=' + ctx;
      await s.send('Page.navigate', { url });
      // wait for: connected + an initial burst that has been quiet for 1.5 s (max 90 s)
      const t0 = Date.now();
      for (;;) {
        await sleep(250);
        const st = await page.eval('window.__perf ? { r: window.__perf.wsReady, l: window.__perf.lastResponse, now: performance.now() } : null').catch(() => null);
        if (st && st.r && st.l && st.now - st.l > 1500) break;
        if (Date.now() - t0 > 90000) { console.log('  timeout waiting for', ctx); break; }
      }
      const prof = (await s.send('Profiler.stop')).profile;
      // idle cost: renderer main-thread busy time over 5 s with nobody touching the page
      const metric = async () => Object.fromEntries((await s.send('Performance.getMetrics')).metrics.map(m => [m.name, m.value]));
      const m0 = await metric(); await sleep(5000); const m1 = await metric();
      const idleBusyPct = Math.round(100 * (m1.TaskDuration - m0.TaskDuration) / (m1.Timestamp - m0.Timestamp));
      s.handlers.get('Network.loadingFinished').splice(0);
      const perf = await page.eval(`(() => { const p = window.__perf; const nav = performance.getEntriesByType('navigation')[0];
        return { dcl: nav && Math.round(nav.domContentLoadedEventEnd), load: nav && Math.round(nav.loadEventEnd), firstRender: Math.round(p.firstRender), wsReady: Math.round(p.wsReady),
          dataShown: Math.round(p.lastResponse), api: p.api, long: p.long, events: p.events, eventBytes: p.eventBytes,
          dom: document.getElementsByTagName('*').length, heapMB: performance.memory ? Math.round(performance.memory.usedJSHeapSize / 1048576) : null }; })()`);
      // self time per script URL from the CPU profile
      const byUrl = {}; const nodes = new Map(prof.nodes.map(n => [n.id, n]));
      const dt = prof.timeDeltas; let total = 0;
      prof.samples.forEach((id, i) => { const n = nodes.get(id); const u = (n.callFrame.url || '(' + n.callFrame.functionName + ')').replace(/^https?:\/\/[^/]+\//, ''); const d = (dt[i] || 0) / 1000; byUrl[u] = (byUrl[u] || 0) + d; total += d; });
      const babelMs = Math.round(byUrl['vendor/babel.min.js'] || 0);
      const top = Object.entries(byUrl).sort((a, b) => b[1] - a[1]).slice(0, 6).map(([u, ms]) => `${u}=${Math.round(ms)}`);
      const apiBy = {}; perf.api.forEach(a => { const x = apiBy[a.m] = apiBy[a.m] || { n: 0, bytes: 0, maxMs: 0 }; x.n++; x.bytes += a.bytes; x.maxMs = Math.max(x.maxMs, a.ms); });
      const longTotal = perf.long.reduce((s2, l) => s2 + l[1], 0), longMax = Math.max(0, ...perf.long.map(l => l[1]));
      const r = { ctx, run: run === 0 ? 'cold' : 'warm', throttle, httpKiB: Math.round(httpBytes / 1024), httpReqs, dcl: perf.dcl, firstRender: perf.firstRender, wsReady: perf.wsReady, dataShown: perf.dataShown,
        babelMs, cpuMs: Math.round(total), apiCalls: perf.api.length, apiKiB: Math.round(perf.api.reduce((s2, a) => s2 + a.bytes, 0) / 1024), apiBy, events: perf.events,
        idleBusyPct, longTasks: perf.long.length, longTotal, longMax, dom: perf.dom, heapMB: perf.heapMB, consoleErrors: page.consoleErrors.splice(0), top };
      results.push(r);
      console.log(`${ctx.padEnd(9)} ${r.run} x${throttle}: DCL ${r.dcl} ms, first render ${r.firstRender} ms, ws ready ${r.wsReady} ms, data shown ${r.dataShown} ms | babel ${babelMs} ms of ${r.cpuMs} ms CPU | HTTP ${r.httpReqs} req ${r.httpKiB} KiB | API ${r.apiCalls} calls ${r.apiKiB} KiB | long tasks ${r.longTasks} (max ${longMax} ms) | idle CPU ${r.idleBusyPct}% | DOM ${r.dom} | heap ${r.heapMB} MB | console errors ${r.consoleErrors.length}`);
      if (r.consoleErrors.length) console.log('          first error: ' + r.consoleErrors[0].slice(0, 200));
      console.log(`          api: ${Object.entries(apiBy).map(([m, x]) => `${m}${x.n > 1 ? ' x' + x.n : ''} (${(x.bytes / 1024).toFixed(0)}K)`).join(', ')}`);
      console.log(`          cpu by script: ${top.join(', ')}`);
    }
    await s.send('Page.close').catch(() => {});
  }
  const out = arg('json', null);
  if (out) require('fs').writeFileSync(out, JSON.stringify(results, null, 1));
  await b.close();
})().catch(e => { console.error(e); process.exit(1); });
