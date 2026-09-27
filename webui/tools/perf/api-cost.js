// Server-side cost of the Control API calls the web UI makes on screen load.
//
//   node webui/tools/perf/api-cost.js --port 9340 [--reps 5] [--skip-defs] [--json out.json]
//
// Point it ONLY at a sandbox (dev-webui-sandbox.ps1), never at the live instance: it is read-only,
// but fixturedefs.list force-loads every fixture definition and blocks that app for seconds.
//
// For every call it prints the round trip (median / max over --reps), the response size, and the
// longest stall a second connection saw meanwhile (it pings io.grandMaster.get every 5 ms): the
// server runs every handler synchronously on the QLC+ main thread, so that stall is how long the
// desktop UI - and the queued Universe::tick() that drives DMX output - were blocked.
// It also calls functions.get / vc.widget.get once for EVERY function / widget to find the
// biggest payloads and the cost of a "fetch each item" (N+1) pattern.

const { connect, pct, stallProbe, sleep } = require('./ws-client.js');
const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i < 0 ? d : (process.argv[i + 1] && !process.argv[i + 1].startsWith('--') ? process.argv[i + 1] : true); };
const port = Number(arg('port', 0));
if (!port || port === 9010) { console.error('pass --port <sandbox API port> (never 9010)'); process.exit(2); }
const url = `ws://localhost:${port}/`;
const reps = Number(arg('reps', 5));

(async () => {
  const c = await connect(url);
  const probe = await stallProbe(url, 5);
  await sleep(300);
  const rows = [];
  async function measure(label, method, params, n = reps) {
    const times = []; let bytes = 0; let err = null;
    const mark = probe.mark();
    for (let i = 0; i < n; i++) {
      try { const r = await c.timed(method, params); times.push(r.ms); bytes = r.bytes; }
      catch (e) { err = e.message; break; }
    }
    await sleep(30);
    const stalls = probe.since(mark);
    const row = { label, method, n: times.length, medianMs: pct(times, 50), maxMs: Math.max(...times), firstMs: times[0], bytes, maxStallMs: Math.max(0, ...stalls), err };
    rows.push(row);
    console.log(`${label.padEnd(44)} med ${row.medianMs.toFixed(1).padStart(8)} ms  max ${row.maxMs.toFixed(1).padStart(8)} ms  ${(bytes / 1024).toFixed(1).padStart(8)} KiB  stall ${row.maxStallMs.toFixed(0).padStart(6)} ms${err ? '  ERR ' + err : ''}`);
    return row;
  }

  // --- inventory
  const fl = (await c.timed('functions.list', {})).result.functions;
  const fx = (await c.timed('fixtures.list', {})).result.fixtures;
  const pages = (await c.timed('vc.page.list', {})).result.pages;
  const wl = (await c.timed('vc.widget.list', {})).result.widgets;
  const unis = (await c.timed('io.universe.list', {})).result.universes;
  const byType = {}; fl.forEach(f => byType[f.type] = (byType[f.type] || 0) + 1);
  console.log(`project: ${fl.length} functions ${JSON.stringify(byType)}, ${fx.length} fixtures, ${unis.length} universes, ${pages.length} VC pages, ${wl.length} VC widgets`);

  // --- N+1: every functions.get / vc.widget.get once
  const perFn = [];
  let t0 = performance.now(); let mark = probe.mark();
  for (const f of fl) { try { const r = await c.timed('functions.get', { functionId: f.id }); perFn.push({ id: f.id, type: f.type, name: f.name, ms: r.ms, bytes: r.bytes }); } catch (e) { } }
  const fnAll = performance.now() - t0; const fnStall = Math.max(0, ...probe.since(mark));
  console.log(`functions.get x${perFn.length} sequential: ${fnAll.toFixed(0)} ms total, ${(perFn.reduce((s, r) => s + r.bytes, 0) / 1024).toFixed(0)} KiB, max single ${Math.max(...perFn.map(r => r.ms)).toFixed(1)} ms, stall max ${fnStall.toFixed(0)} ms`);
  const perW = [];
  t0 = performance.now();
  for (const w of wl) { try { const r = await c.timed('vc.widget.get', { widgetId: w.id }); perW.push({ id: w.id, ms: r.ms, bytes: r.bytes }); } catch (e) { } }
  console.log(`vc.widget.get x${perW.length} sequential: ${(performance.now() - t0).toFixed(0)} ms total`);
  const topFns = {};
  for (const r of perFn) if (!topFns[r.type] || topFns[r.type].bytes < r.bytes) topFns[r.type] = r;

  // --- the calls screens make on load
  await measure('hello', 'hello', { apiVersion: '1', clientName: 'x' });
  await measure('functions.list', 'functions.list', {});
  await measure('fixtures.list', 'fixtures.list', {});
  await measure('fixtures.get (first fixture)', 'fixtures.get', { fixtureId: fx[0].id });
  await measure('fixtures.group.list', 'fixtures.group.list', {});
  await measure('palette.list', 'palette.list', {});
  await measure('vc.page.list', 'vc.page.list', {});
  await measure('vc.widget.list (all pages)', 'vc.widget.list', {});
  for (const p of pages.slice(0, 3)) await measure(`vc.widget.list page ${p.index}`, 'vc.widget.list', { page: p.index });
  await measure('io.universe.list', 'io.universe.list', {});
  await measure('io.dmx.universe.get u0', 'io.dmx.universe.get', { universeId: 0 });
  await measure('io.simpleDesk.get u0', 'io.simpleDesk.get', { universeId: 0 });
  await measure('io.plugin.list', 'io.plugin.list', {});
  await measure('io.inputProfile.list', 'io.inputProfile.list', {});
  await measure('core.project.get', 'core.project.get', {});
  await measure('core.history.get', 'core.history.get', {});
  await measure('fixtures.monitor.get', 'fixtures.monitor.get', {});
  await measure('fixtures.defs.listManufacturers', 'fixtures.defs.listManufacturers', {});
  for (const [type, r] of Object.entries(topFns))
    await measure(`functions.get biggest ${type} (#${r.id})`, 'functions.get', { functionId: r.id });
  const rgb = fl.find(f => f.type === 'RGBMatrix');
  if (rgb) await measure(`functions.rgbmatrix.getPreview #${rgb.id}`, 'functions.rgbmatrix.getPreview', { functionId: rgb.id });
  const efx = fl.find(f => f.type === 'EFX');
  if (efx) await measure(`functions.efx.getPreview #${efx.id}`, 'functions.efx.getPreview', { functionId: efx.id, includeFixturePaths: true });
  if (!arg('skip-defs', false)) {
    await measure('fixturedefs.list (1st call = cold)', 'fixturedefs.list', {}, 1);
    await measure('fixturedefs.list (warm)', 'fixturedefs.list', {}, 2);
    await measure('fixturedefs.list manufacturer=SF3', 'fixturedefs.list', { manufacturer: 'SF3' }, 3);
  }

  await probe.stop(); c.close();
  const out = arg('json', null);
  if (out) require('fs').writeFileSync(out, JSON.stringify({ rows, perFn: perFn.sort((a, b) => b.bytes - a.bytes).slice(0, 15), fnAll, fnStall, counts: { functions: fl.length, byType, fixtures: fx.length, universes: unis.length, pages: pages.length, widgets: wl.length } }, null, 1));
})().catch(e => { console.error(e); process.exit(1); });
