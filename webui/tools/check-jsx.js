// Syntax-check every .jsx/.js under webui/ with the vendored Babel (no Node toolchain beyond node itself).
// Usage: node webui/tools/check-jsx.js [files...]   (default: every .jsx referenced by index.html + api/*.js)
const fs = require('fs'), path = require('path'), vm = require('vm');
const root = path.resolve(__dirname, '..');
const ctx = { console }; ctx.window = ctx; ctx.self = ctx; ctx.globalThis = ctx; ctx.document = {}; ctx.navigator = { userAgent: '' };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root, 'vendor/babel.min.js'), 'utf8'), ctx);
const Babel = ctx.Babel;
// Every text/babel tag in index.html must carry the same lean options (see the comment there):
// without them babel-standalone falls back to react + env with no targets (a full ES5 downlevel,
// about 5x the compile time). These are also the options the .jsx files are checked with.
const PRESETS = 'react', PLUGINS = 'transform-block-scoping';
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
let failed = 0;
for (const m of html.matchAll(/<script\b[^>]*type="text\/babel"[^>]*>/g)) {
  const tag = m[0];
  if (!tag.includes(`data-presets="${PRESETS}"`) || !tag.includes(`data-plugins="${PLUGINS}"`)) {
    failed++;
    console.log(`FAIL index.html: ${tag} lacks data-presets="${PRESETS}" data-plugins="${PLUGINS}"`);
  }
}
let files = process.argv.slice(2);
if (!files.length)
  files = Array.from(html.matchAll(/<script[^>]*src="([^"]+)"/g)).map(m => m[1]).filter(f => !f.startsWith('vendor/') && f !== '_ds_bundle.js');
for (const f of files) {
  const p = path.isAbsolute(f) ? f : path.join(root, f);
  try {
    const jsx = f.endsWith('.jsx');
    Babel.transform(fs.readFileSync(p, 'utf8'), { presets: jsx ? [PRESETS] : [], plugins: jsx ? [PLUGINS] : [], filename: f });
    console.log('ok   ' + f);
  } catch (e) { failed++; console.log('FAIL ' + f + ': ' + e.message.split('\n')[0]); }
}
process.exitCode = failed ? 1 : 0;
