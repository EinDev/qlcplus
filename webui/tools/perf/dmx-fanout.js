// Server cost of the live DMX stream fan-out: N clients each subscribed to every universe while
// every RGB Matrix runs. Prints what the clients received; read the server side from the sandbox
// log (a temporary PERFPROBE in ApiServer::broadcast, see the performance plan) or from the QLC+
// process CPU time (--proc).
//
//   node webui/tools/perf/dmx-fanout.js --port 9340 [--clients 5] [--seconds 10] [--proc qlc-perf]
//
// SANDBOX ONLY (starts/stops functions).

const { connect, sleep, pct } = require('./ws-client.js');
const { execSync } = require('child_process');
const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i < 0 ? d : process.argv[i + 1]; };
const port = Number(arg('port', 0));
if (!port || port === 9010) { console.error('pass --port <sandbox API port> (never 9010)'); process.exit(2); }
const url = `ws://localhost:${port}/`, nClients = Number(arg('clients', '5')), seconds = Number(arg('seconds', '10')), proc = arg('proc', '');
const cpuMs = () => { if (!proc) return NaN; try { return Number(execSync(`powershell -NoProfile -Command "(Get-Process -Name ${proc}).TotalProcessorTime.TotalMilliseconds -as [long]"`).toString().trim()); } catch (e) { return NaN; } };

(async () => {
  const ctl = await connect(url, { name: 'perf-ctl' });
  const unis = (await ctl.call('io.universe.list', {})).universes.map(u => u.id);
  const rgb = (await ctl.call('functions.list', {})).functions.filter(f => f.type === 'RGBMatrix').map(f => f.id);
  for (const id of rgb) await ctl.call('functions.start', { functionId: id }).catch(() => {});
  await sleep(1000);
  async function window(n) {
    const clients = [];
    for (let i = 0; i < n; i++) {
      const c = await connect(url, { name: 'perf-dmx-' + i });
      const st = { frames: 0, bytes: 0, changes: 0 };
      c.onEvent((topic, data, bytes) => { if (topic.startsWith('io.dmx.universe.')) { st.frames++; st.bytes += bytes; st.changes += (data.changes || []).length; } });
      await c.call('subscribe', { topics: unis.map(u => `io.dmx.universe.${u}.changed`) });
      clients.push({ c, st });
    }
    const c0 = cpuMs(), w0 = Date.now(); // before the pinger: execSync blocks Node and would show up as a fake stall
    const probe = [];
    const pinger = (async () => { const t0 = performance.now(); while (performance.now() - t0 < seconds * 1000) { probe.push((await ctl.timed('io.grandMaster.get', {})).ms); await sleep(20); } })();
    await pinger;
    const c1 = cpuMs(), w1 = Date.now();
    const f = clients.reduce((s, x) => s + x.st.frames, 0), b = clients.reduce((s, x) => s + x.st.bytes, 0), ch = clients.reduce((s, x) => s + x.st.changes, 0);
    console.log(`${n} client(s) x ${unis.length} universes: ${(f / seconds).toFixed(0)} frames/s, ${(b / 1024 / seconds).toFixed(0)} KiB/s total, ${(ch / Math.max(1, f)).toFixed(0)} changed channels per frame, ${(b / Math.max(1, f)).toFixed(0)} B per frame; QLC+ CPU ${((c1 - c0) / (w1 - w0)).toFixed(2)} core-s/s; main-thread ping p50 ${pct(probe, 50).toFixed(1)} ms p99 ${pct(probe, 99).toFixed(1)} ms max ${Math.max(...probe).toFixed(0)} ms`);
    for (const x of clients) x.c.close();
    await sleep(500);
  }
  await window(0);
  await window(1);
  await window(nClients);
  for (const id of rgb) await ctl.call('functions.stop', { functionId: id }).catch(() => {});
  ctl.close();
})().catch(e => { console.error(e); process.exit(1); });
