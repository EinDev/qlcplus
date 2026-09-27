// Syntax-check every .jsx/.js under webui/ with the vendored Babel (no Node toolchain beyond node itself).
// Usage: node webui/tools/check-jsx.js [files...]   (default: every .jsx referenced by index.html + api/*.js)
const fs = require('fs'), path = require('path'), vm = require('vm');
const root = path.resolve(__dirname, '..');
const ctx = { console }; ctx.window = ctx; ctx.self = ctx; ctx.globalThis = ctx; ctx.document = {}; ctx.navigator = { userAgent: '' };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(root, 'vendor/babel.min.js'), 'utf8'), ctx);
const Babel = ctx.Babel;
let files = process.argv.slice(2);
if (!files.length) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
  files = Array.from(html.matchAll(/<script[^>]*src="([^"]+)"/g)).map(m => m[1]).filter(f => !f.startsWith('vendor/') && f !== '_ds_bundle.js');
}
let failed = 0;
for (const f of files) {
  const p = path.isAbsolute(f) ? f : path.join(root, f);
  try {
    Babel.transform(fs.readFileSync(p, 'utf8'), { presets: f.endsWith('.jsx') ? ['react'] : [], filename: f });
    console.log('ok   ' + f);
  } catch (e) { failed++; console.log('FAIL ' + f + ': ' + e.message.split('\n')[0]); }
}
process.exitCode = failed ? 1 : 0;
