// Does a slow API call freeze DMX output? Starts a function that changes DMX every tick, follows
// the live DMX stream of its universe on a second connection, then fires one slow request and
// reports the longest gap in the stream before / during / after it.
//
//   node webui/tools/perf/engine-stall.js --port 9340 [--function 0] [--universe 0]
//        [--heavy fixturedefs.list] [--params '{"manufacturer":"Chauvet"}']
//
// SANDBOX ONLY (it starts/stops a function). The DMX stream itself is delivered through the QLC+
// main thread, so a gap here proves the web clients stop seeing output; whether the physical output
// stops too is answered by Universe::tick() being a queued slot on the main thread (see the plan in
// docs/agent-reports/2026-09-27-webui-performance-plan.md) and was confirmed there with a temporary
// probe in Universe::run().

const { connect, pct, sleep } = require('./ws-client.js');
const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i < 0 ? d : process.argv[i + 1]; };
const port = Number(arg('port', 0));
if (!port || port === 9010) { console.error('pass --port <sandbox API port> (never 9010)'); process.exit(2); }
const url = `ws://localhost:${port}/`;
const fnId = String(arg('function', '0'));
const uniArg = arg('universe', 'all');
const heavy = arg('heavy', 'fixturedefs.list');
const heavyParams = JSON.parse(arg('params', '{"manufacturer":"Chauvet"}'));

(async () => {
  const ctl = await connect(url), watch = await connect(url);
  const unis = uniArg === 'all' ? (await ctl.call('io.universe.list', {})).universes.map(u => u.id) : [Number(uniArg)];
  const byUni = {};
  watch.onEvent((topic) => { const m = /^io\.dmx\.universe\.(\d+)\.changed$/.exec(topic); if (m) (byUni[m[1]] = byUni[m[1]] || []).push(performance.now()); });
  await watch.call('subscribe', { topics: unis.map(u => `io.dmx.universe.${u}.changed`) });
  await ctl.call('functions.start', { functionId: fnId });
  await sleep(3000);
  const tBefore = performance.now();
  await sleep(2000);
  const tHeavy0 = performance.now();
  let heavyMs = NaN;
  try { heavyMs = (await ctl.timed(heavy, heavyParams)).ms; } catch (e) { console.log('heavy call failed:', e.message); }
  const tHeavy1 = performance.now();
  await sleep(2000);
  const tEnd = performance.now();
  await ctl.call('functions.stop', { functionId: fnId });
  const busiest = Object.keys(byUni).sort((a, b) => byUni[b].length - byUni[a].length)[0];
  const arrivals = busiest ? byUni[busiest] : [];
  console.log('DMX stream followed: universe ' + busiest + ' (events per universe: ' + JSON.stringify(Object.fromEntries(Object.entries(byUni).map(([k, v]) => [k, v.length]))) + ')');
  const gaps = (a, b) => { const xs = arrivals.filter(t => t >= a && t <= b); const g = []; for (let i = 1; i < xs.length; i++) g.push(xs[i] - xs[i - 1]); return { n: xs.length, rate: xs.length / ((b - a) / 1000), p50: pct(g, 50), max: Math.max(0, ...g) }; };
  const fmt = (o) => `${o.n} events (${o.rate.toFixed(1)}/s), gap p50 ${o.p50.toFixed(1)} ms, max ${o.max.toFixed(0)} ms`;
  console.log(`${heavy} ${JSON.stringify(heavyParams)}: ${heavyMs.toFixed(0)} ms`);
  console.log('before :', fmt(gaps(tBefore, tHeavy0)));
  console.log('during :', fmt(gaps(tHeavy0 - 50, tHeavy1 + 200)));
  console.log('after  :', fmt(gaps(tHeavy1 + 200, tEnd)));
  console.log(`wall clock of the heavy call: ${new Date(Date.now() - (performance.now() - tHeavy0)).toISOString()} .. +${(tHeavy1 - tHeavy0).toFixed(0)} ms`);
  ctl.close(); watch.close();
})().catch(e => { console.error(e); process.exit(1); });
