// Merges the raw dumps written by hook.js into one coverage report of the webui/ sources.
//   node webui/tools/coverage/report.js <rawDir> <outDir>
// Writes <outDir>/index.html (per-file, per-line), coverage-summary.json and coverage-details.md,
// and prints the per-file table. vendor/ and the design-system bundle (_ds_bundle.js) are excluded.
const fs = require('fs'), path = require('path');
const MCR = require('monocart-coverage-reports');
const [rawDir, outDir] = process.argv.slice(2);
if (!rawDir || !outDir) { console.error('usage: node report.js <rawDir> <outDir>'); process.exit(2); }
const INLINE_MAP = /sourceMappingURL=data:application\/json;(?:charset=utf-8;)?base64,([A-Za-z0-9+/=]+)/;

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
        e.url = map.sources[0] + '.compiled.js';
      }
      keep.push(e);
    }
    await cr.add(keep);
  }
  await cr.generate();
})().catch(e => { console.error(e); process.exit(1); });
