// Merges the raw dumps written by hook.js into one coverage report of the webui/ sources.
//   node webui/tools/coverage/report.js <rawDir> <outDir>
// Writes <outDir>/index.html (per-file, per-line), coverage-summary.json and coverage-details.md,
// and prints the per-file table. vendor/ and the design-system bundle (_ds_bundle.js) are excluded.
const fs = require('fs'), path = require('path');
const MCR = require('monocart-coverage-reports');
const [rawDir, outDir] = process.argv.slice(2);
if (!rawDir || !outDir) { console.error('usage: node report.js <rawDir> <outDir>'); process.exit(2); }
// Drivers open the page on different hosts (vc-live.js uses [::1] because another program can hold
// 127.0.0.1:9180). monocart turns an IPv6 host into a separate folder, so every URL is rewritten to
// localhost before it is added; sourcePath then drops the host and port.
const normHost = (u) => u.replace(/^(https?:\/\/)(\[[^\]]*\]|127\.0\.0\.1)(?=[:/])/, '$1localhost');
const INLINE_MAP =/sourceMappingURL=data:application\/json;(?:charset=utf-8;)?base64,([A-Za-z0-9+/=]+)/;

(async () => {
  const cr = MCR({
    name: 'QLC+ web UI - JS coverage from the e2e drivers',
    outputDir: outDir, cleanCache: true,
    reports: ['v8', 'console-details', 'json-summary', 'markdown-details'],
    // http://localhost:9121/ff/EfxEditor.jsx -> webui/ff/EfxEditor.jsx, so runs on different ports merge.
    sourcePath: (p) => 'webui/' + p.replace(/^https?:\/\/[^/]+\//, '').replace(/^localhost-\d+\//, '').replace(/\?.*$/, ''),
    sourceFilter: (p) => !/vendor\/|_ds_bundle|\.compiled\.js$/.test(p),
  });
  const files = fs.readdirSync(rawDir).filter(f => f.endsWith('.json'));
  if (!files.length) { console.error('no coverage dumps in ' + rawDir); process.exit(1); }
  for (const f of files) {
    const keep = [];
    for (const e of JSON.parse(fs.readFileSync(path.join(rawDir, f), 'utf8'))) {
      if (!e.url) {
        // Babel output: name it after its original file. The scriptId-based name would collide
        // across runs and pages (each assigns ids in load order) and merge unrelated files.
        const m = e.source.match(INLINE_MAP);
        if (!m) continue;
        const map = JSON.parse(Buffer.from(m[1], 'base64').toString('utf8'));
        if (!map.sources || !/^https?:/.test(map.sources[0])) continue;   // the inline bootstrap <script>
        if (map.sources.some(s => s !== normHost(s))) {
          // The map names the source files, so fix the host there too. It sits after the code,
          // so rewriting it leaves every coverage offset valid.
          map.sources = map.sources.map(normHost);
          e.source = e.source.replace(m[1], Buffer.from(JSON.stringify(map), 'utf8').toString('base64'));
        }
        e.url = map.sources[0] + '.compiled.js';
      }
      e.url = normHost(e.url);
      keep.push(e);
    }
    await cr.add(keep);
  }
  await cr.generate();
})().catch(e => { console.error(e); process.exit(1); });
