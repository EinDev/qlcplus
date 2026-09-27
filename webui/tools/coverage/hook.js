// Preload for the webui/tools/e2e drivers (`node -r webui/tools/coverage/hook.js <driver>`):
// wraps cdp.js launch() so every page the driver opens records V8 precise coverage, and dumps
// the raw entries (with each script's source, needed for the source maps) as JSON into
// $QLC_JSCOV_DIR whenever the page navigates away or the browser closes. report.js turns the
// dumps into a report. Driver behaviour is unchanged apart from Debugger/Profiler being enabled.
//
// Env: QLC_JSCOV_DIR (required) output directory; QLC_JSCOV_TAG file-name prefix (driver name).
const fs = require('fs'), path = require('path');
const cdp = require(path.resolve(__dirname, '..', 'cdp.js'));   // same module instance the driver gets
const OUT = process.env.QLC_JSCOV_DIR;
if (!OUT) throw new Error('hook.js: set QLC_JSCOV_DIR');
fs.mkdirSync(OUT, { recursive: true });
const TAG = process.env.QLC_JSCOV_TAG || 'run';
let dumps = 0;

async function startCoverage(page) {
  await page.s.send('Debugger.enable');   // needed for Debugger.getScriptSource at dump time
  await page.s.send('Profiler.enable');
  await page.s.send('Profiler.startPreciseCoverage', { callCount: true, detailed: true });
  page.__jscov = true;
}

async function dump(page) {
  if (!page.__jscov) return;
  let r;
  try { r = await page.s.send('Profiler.takePreciseCoverage'); } catch (e) { return; }
  const entries = [];
  for (const e of r.result) {
    // Page-served files have an http URL. Babel standalone compiles every text/babel script into
    // an inline <script> that V8 reports with an EMPTY url; those carry an inline source map
    // (sourceMaps: "inline") whose sources[0] is the original .jsx URL - report.js uses that.
    if (e.url && !/^https?:\/\/(127\.0\.0\.1|localhost|\[::1\]):\d+\//.test(e.url)) continue;
    if (/\/vendor\/|_ds_bundle\.js/.test(e.url)) continue;
    let source;
    try { source = (await page.s.send('Debugger.getScriptSource', { scriptId: e.scriptId })).scriptSource; } catch (err) { continue; }
    if (!e.url && !/sourceMappingURL=data:/.test(source)) continue;   // inline, but not from Babel
    entries.push({ url: e.url, scriptId: e.scriptId, source, functions: e.functions });
  }
  fs.writeFileSync(path.join(OUT, `${TAG}-${process.pid}-${++dumps}.json`), JSON.stringify(entries));
}

const origLaunch = cdp.launch;
cdp.launch = async function (...args) {
  const b = await origLaunch(...args);
  const origOpen = b.open.bind(b), origClose = b.close.bind(b);
  b.open = async function (url, opts) {
    const page = await origOpen(null, opts);   // blank tab first, so coverage starts before the page loads
    await startCoverage(page);
    const origGoto = page.goto.bind(page);
    page.goto = async (u) => { await dump(page); return origGoto(u); };   // a navigation discards the old scripts
    if (url) await origGoto(url);
    return page;
  };
  b.close = async function () { for (const p of b.pages) await dump(p); return origClose(); };
  return b;
};
