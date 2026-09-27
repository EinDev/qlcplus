// EXPERIMENT, not the build step itself: writes a copy of webui/ whose JSX is compiled ahead of
// time with the vendored Babel, so page-load.js can measure the "after" of a precompile build step
// against the same server. The copy loads no Babel at all.
//
//   node webui/tools/perf/precompile-experiment.js <out dir> [--targets modern|es5] [--bundle]
//
//  --targets modern (default): react preset + block scoping only (top-level const/let become var,
//            as they must: every file is a classic script sharing one global scope, and several
//            files declare the same top-level names) - output runs on any browser from ~2020 on.
//  --targets es5: what the browser does today (react + env, no targets) minus the inline source maps.
//  --bundle: one app.js instead of one .js per file (fewer requests; same parse/execute work).
// Then start a sandbox with -WebUiRoot <out dir> and run page-load.js against it.

const fs = require('fs'), path = require('path');
const src = path.resolve(__dirname, '..', '..');
const out = process.argv[2];
if (!out) { console.error('usage: precompile-experiment.js <out dir> [--targets modern|es5] [--bundle]'); process.exit(2); }
const targets = process.argv.includes('es5') ? 'es5' : 'modern';
const bundle = process.argv.includes('--bundle');
global.window = global;
const Babel = require(path.join(src, 'vendor', 'babel.min.js'));
const opts = (f) => targets === 'es5'
  ? { filename: f, presets: ['react', 'env'], plugins: ['transform-class-properties', 'transform-object-rest-spread', 'transform-flow-strip-types'], sourceMaps: false }
  : { filename: f, presets: ['react'], plugins: ['transform-block-scoping'], sourceMaps: false };

function copyDir(a, b) {
  fs.mkdirSync(b, { recursive: true });
  for (const e of fs.readdirSync(a, { withFileTypes: true })) {
    if (e.name === 'tools') continue;
    const s = path.join(a, e.name), d = path.join(b, e.name);
    if (e.isDirectory()) copyDir(s, d); else fs.copyFileSync(s, d);
  }
}
copyDir(src, out);

let html = fs.readFileSync(path.join(src, 'index.html'), 'utf8');
const t0 = performance.now();
const compiled = [];
html = html.replace(/<script type="text\/babel"([^>]*?)src="([^"]+)"><\/script>/g, (m, attrs, f) => {
  const code = Babel.transform(fs.readFileSync(path.join(src, f), 'utf8'), opts(f)).code;
  const js = f.replace(/\.jsx$/, '.js');
  if (bundle) { compiled.push('/* ' + f + ' */\n' + code); return ''; }
  fs.writeFileSync(path.join(out, js), code);
  return `<script src="${js}"></script>`;
});
html = html.replace(/<script type="text\/babel"[^>]*>([\s\S]*?)<\/script>/, (m, inline) => {
  const code = Babel.transform(inline, opts('inline.jsx')).code;
  if (bundle) { compiled.push(code); fs.writeFileSync(path.join(out, 'app.js'), compiled.join('\n;\n')); return '<script src="app.js"></script>'; }
  return `<script>${code}</script>`;
});
html = html.replace(/<script src="vendor\/babel\.min\.js"><\/script>\s*/, '');
fs.writeFileSync(path.join(out, 'index.html'), html);
console.log(`compiled in ${(performance.now() - t0).toFixed(0)} ms (${targets}${bundle ? ', single bundle' : ''}) -> ${out}`);
