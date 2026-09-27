// How much does compiling the web UI's JSX cost? Runs the vendored @babel/standalone in Node over
// every <script type="text/babel"> of webui/index.html, once with the exact options the browser
// uses for script tags (babel-standalone defaults: presets react+env with no targets = full ES5
// downlevel, 3 plugins, inline source maps) and once with a lean variant (react preset only, no
// source maps), and prints per-file and total times plus output sizes.
//
//   node webui/tools/perf/babel-cost.js [--runs 3]
//
// Node's V8 is roughly the speed of desktop Chrome's; a 4x CPU-throttled tablet is ~4x slower.

const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '..', '..');
const runs = Number((process.argv.find((a, i) => process.argv[i - 1] === '--runs')) || 3);

global.window = global; // babel.min.js is a UMD bundle; in Node it takes the CommonJS branch
const Babel = require(path.join(root, 'vendor', 'babel.min.js'));

const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
const files = [...html.matchAll(/<script type="text\/babel"[^>]*src="([^"]+)"/g)].map(m => m[1]);

const browserOpts = (f) => ({ filename: f, presets: ['react', 'env'],
  plugins: ['transform-class-properties', 'transform-object-rest-spread', 'transform-flow-strip-types'],
  sourceMaps: 'inline', sourceFileName: f });
const leanOpts = (f) => ({ filename: f, presets: ['react'], sourceMaps: false });

function bench(optsFn) {
  const per = {};
  let outBytes = 0, inBytes = 0;
  for (let r = 0; r < runs; r++) {
    for (const f of files) {
      const src = fs.readFileSync(path.join(root, f), 'utf8');
      const t0 = performance.now();
      const out = Babel.transform(src, optsFn(f)).code;
      const ms = performance.now() - t0;
      (per[f] = per[f] || []).push(ms);
      if (r === 0) { outBytes += Buffer.byteLength(out); inBytes += Buffer.byteLength(src); }
    }
  }
  // first run includes V8 warm-up of Babel itself, which a page load also pays; report both
  const first = files.reduce((s, f) => s + per[f][0], 0);
  const best = files.reduce((s, f) => s + Math.min(...per[f]), 0);
  return { per, first, best, outBytes, inBytes };
}

const b = bench(browserOpts), l = bench(leanOpts);
console.log(`files: ${files.length}, JSX source ${(b.inBytes / 1024).toFixed(0)} KiB`);
console.log(`browser-default options: first pass ${b.first.toFixed(0)} ms, warm ${b.best.toFixed(0)} ms, output ${(b.outBytes / 1024).toFixed(0)} KiB (incl. inline source maps)`);
console.log(`lean (react only, no maps): first pass ${l.first.toFixed(0)} ms, warm ${l.best.toFixed(0)} ms, output ${(l.outBytes / 1024).toFixed(0)} KiB`);
console.log('\nslowest files (browser options, warm ms):');
files.map(f => [f, Math.min(...b.per[f])]).sort((x, y) => y[1] - x[1]).slice(0, 10)
  .forEach(([f, ms]) => console.log(`  ${ms.toFixed(0).padStart(5)}  ${f}`));
