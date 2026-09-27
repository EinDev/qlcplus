// End-to-end check of the Script / Audio / Video editors and the server-side file browser against a
// sandbox instance (dev-webui-sandbox.ps1 -Name media -ApiPort 9140 -WebUiPort 9141, SF3 project).
// Drives headless Chrome through webui/tools/cdp.js like an operator would, then reads every change
// back through a second API connection (functions.get) and a core.project.saveAs into the sandbox.
//
//   node webui/tools/e2e/media.js [--api 9140] [--web 9141] [--out <dir>] [--keep]
//
// Exit code 0 = every assertion held and the page logged no console errors. Screenshots land in --out
// (default: the OS temp dir). Only ever run against a sandbox: it edits functions and writes
// C:\qlcsandbox\<name>\out.qxw.

const path = require('path'), fs = require('fs'), os = require('os');
const { launch } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', 9140)), WEB_PORT = Number(opt('web', 9141));
const OUT = opt('out', path.join(os.tmpdir(), 'qlc-e2e-media'));
const KEEP = args.indexOf('--keep') !== -1;
fs.mkdirSync(OUT, { recursive: true });

const SCRIPT_ID = '16', AUDIO_ID = '3', VIDEO_ID = '116';
const SCRIPT_NAME = 'New Script 16', AUDIO_NAME = 'RzgSTjFyjEM.mp3', VIDEO_NAME = 'testStream.mp4';
const SANDBOX_DIR = 'C:\\qlcsandbox\\media';
const ASSET_DIR = '<shows-dir>/SF3.qxw.assets/335e235f3c78';

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

/* ---- a second API client for read-back ------------------------------------------------------- */
function apiClient() {
  const ws = new WebSocket('ws://127.0.0.1:' + API_PORT + '/');
  let id = 0; const pending = new Map(); const events = [];
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'response' && pending.has(m.id)) { const { res, rej } = pending.get(m.id); pending.delete(m.id); m.ok ? res(m.result) : rej(Object.assign(new Error(m.error.message), m.error)); }
    else if (m.type === 'event') events.push(m);
  };
  const call = (method, params) => new Promise((res, rej) => { const rid = 'e2e-' + (++id); pending.set(rid, { res, rej }); ws.send(JSON.stringify({ type: 'request', id: rid, method, params: params || {} })); });
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; }).then(() => call('hello', { apiVersion: '1', clientName: 'e2e media' }));
  return { call, ready, events, close: () => ws.close() };
}

/* ---- page helpers ----------------------------------------------------------------------------- */
function el(page, fn) { return page.click(fn); }
/** click the deepest element inside `root` whose own text equals `text` */
async function clickText(page, text, root) {
  const found = await page.eval(`(function(){
    const root = ${root ? 'document.querySelector(' + JSON.stringify(root) + ')' : 'document'};
    if (!root) return false;
    const all = Array.from(root.querySelectorAll('*'));
    const hit = all.filter(e => e.children.length === 0 && (e.textContent || '').trim() === ${JSON.stringify(text)}).pop();
    if (!hit) return false;
    hit.scrollIntoView({ block: 'center' });
    window.__e2e = hit; return true;
  })()`);
  if (!found) throw new Error('text not found: ' + text);
  await sleep(80);
  await page.click(() => window.__e2e);
}
async function clickTitle(page, title, root) {
  const found = await page.eval(`(function(){
    const root = ${root ? 'document.querySelector(' + JSON.stringify(root) + ')' : 'document'};
    const hit = root && root.querySelector('[title=' + JSON.stringify(${JSON.stringify(title)}) + ']');
    if (!hit) return false; hit.scrollIntoView({ block: 'center' }); window.__e2e = hit; return true;
  })()`);
  if (!found) throw new Error('title not found: ' + title);
  await page.click(() => window.__e2e);
}
/** the FF.Row whose label is `label`, then `selector` inside it */
async function rowEl(page, label, selector, root) {
  const found = await page.eval(`(function(){
    const root = ${root ? 'document.querySelector(' + JSON.stringify(root) + ')' : 'document'};
    const rows = Array.from(root.querySelectorAll('div')).filter(d => d.children.length >= 2 && (d.children[0].textContent || '').trim() === ${JSON.stringify(label)});
    const row = rows.pop(); if (!row) return false;
    const hit = ${selector ? 'row.querySelector(' + JSON.stringify(selector) + ')' : 'row'};
    if (!hit) return false; hit.scrollIntoView({ block: 'center' }); window.__e2e = hit; return true;
  })()`);
  if (!found) throw new Error('row/element not found: ' + label + ' ' + (selector || ''));
  return () => window.__e2e;
}
async function selectAll(page) {
  const k = { key: 'a', code: 'KeyA', windowsVirtualKeyCode: 65, modifiers: 2 };
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyDown' }, k));
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, k));
}
async function insertText(page, text) { await page.s.send('Input.insertText', { text }); }
async function setSpin(page, label, value, root) {
  await page.click(await rowEl(page, label, 'input', root));
  await selectAll(page); await insertText(page, String(value)); await page.key('Enter');
  await sleep(150);
}
async function setInline(page, label, text, root) {
  await page.click(await rowEl(page, label, 'div[title]', root));
  await sleep(80);
  await page.click(await rowEl(page, label, 'input', root));
  await selectAll(page); await insertText(page, text); await page.key('Enter');
  await sleep(150);
}
async function shot(page, name) { const f = path.join(OUT, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }

(async () => {
  const api = apiClient();
  await api.ready;
  const project = await api.call('core.project.get');
  console.log('project:', project.fileName || project.filePath || '(untitled)');

  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=fx');
  try {
    await page.waitFor(`(document.querySelector('[data-ff-tree]') || {}).textContent && document.querySelector('[data-ff-tree]').textContent.indexOf(${JSON.stringify(SCRIPT_NAME)}) !== -1`, 30000);
    console.log('connected, tree loaded');

    /* ---------------- Script ---------------- */
    console.log('Script editor');
    await clickText(page, SCRIPT_NAME, '[data-ff-tree]');
    await page.waitFor('!!document.querySelector(".qlc-script-editor textarea")', 10000);
    await page.click('.qlc-script-editor textarea');
    await selectAll(page); await insertText(page, '// e2e\n');
    // insert-method menu
    await clickTitle(page, 'Add a method call at cursor position', '.qlc-script-editor');
    await page.waitFor('!!document.querySelector("[role=menu]")', 5000);
    await clickText(page, 'Start function', '[role=menu]');
    await sleep(100);
    let text = await page.eval('document.querySelector(".qlc-script-editor textarea").value');
    check(text.indexOf('Engine.startFunction();') !== -1, 'snippet inserted: ' + JSON.stringify(text));
    // function id picker inserts at the caret (between the parentheses)
    await clickTitle(page, 'Insert a function ID at the cursor', '.qlc-script-editor');
    await page.waitFor(`document.body.textContent.indexOf('Insert a function ID') !== -1`, 5000);
    // the dialog is rendered after the tree, so the last text match is the picker's row
    await clickText(page, 'New Collection 4');
    await clickText(page, 'Insert');
    await sleep(100);
    text = await page.eval('document.querySelector(".qlc-script-editor textarea").value');
    check(/Engine\.startFunction\(\d+\);/.test(text), 'function id inserted into the call: ' + JSON.stringify(text));
    // an invalid line, then Check syntax (saves first, validates on the server)
    await page.click('.qlc-script-editor textarea');
    await page.s.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'End', code: 'End', windowsVirtualKeyCode: 35, modifiers: 2 });
    await page.s.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'End', code: 'End', windowsVirtualKeyCode: 35, modifiers: 2 });
    await insertText(page, 'this is not a script\n');
    await clickText(page, 'Check syntax', '.qlc-script-editor');
    await page.waitFor(`document.querySelector('.qlc-script-editor').textContent.indexOf('Line ') !== -1`, 10000);
    const errText = await page.eval('document.querySelector(".qlc-script-editor").textContent');
    const badLine = (await page.eval('document.querySelector(".qlc-script-editor textarea").value')).split('\n').indexOf('this is not a script') + 1;
    check(errText.indexOf('Line ' + badLine) !== -1, 'syntax error reported on line ' + badLine);
    const gutterRed = await page.eval(`Array.from(document.querySelectorAll('.qlc-script-editor div[title]')).filter(d => d.title && d.textContent === String(${badLine})).length`);
    check(gutterRed === 1, 'gutter marks the error line');
    await shot(page, '01-script-errors');
    // fix, save, read back
    await page.click('.qlc-script-editor textarea');
    await selectAll(page);
    const finalScript = 'Engine.startFunction(4);\nEngine.waitTime(500);\nEngine.stopFunction(4);\n';
    await insertText(page, finalScript);
    await clickText(page, 'Save (Ctrl+S)', '.qlc-script-editor');
    await page.waitFor(`document.querySelector('.qlc-script-editor').textContent.indexOf('Saved') !== -1`, 10000);
    await sleep(300);
    let d = await api.call('functions.get', { functionId: SCRIPT_ID });
    check(d.typeDetail.source === finalScript, 'functions.get: script source saved (' + JSON.stringify(d.typeDetail.source) + ')');
    check(d.typeDetail.syntaxErrors === undefined, 'functions.get does not evaluate the script (no syntaxErrors in typeDetail)');
    const v = await api.call('functions.script.validate', { functionId: SCRIPT_ID });
    check(Array.isArray(v.syntaxErrors) && v.syntaxErrors.length === 0 && v.functionRefs.length === 1 && v.functionRefs[0].functionId === '4', 'functions.script.validate: no errors, one function ref (' + JSON.stringify(v) + ')');
    const okText = await page.eval('document.querySelector(".qlc-script-editor").textContent');
    check(okText.indexOf('No errors found') !== -1, 'editor shows "No errors found." after save');
    await shot(page, '02-script-saved');

    /* ---------------- Audio ---------------- */
    console.log('Audio editor');
    await clickText(page, AUDIO_NAME, '[data-ff-tree]');
    await page.waitFor('!!document.querySelector(".qlc-audio-editor")', 10000);
    await sleep(300);
    await setSpin(page, 'Volume', 40, '.qlc-audio-editor');
    await setInline(page, 'Fade in', '1.5s', '.qlc-audio-editor');
    await setInline(page, 'Fade out', '250', '.qlc-audio-editor');
    await clickText(page, 'Looped', '.qlc-audio-editor');
    await sleep(400);
    d = await api.call('functions.get', { functionId: AUDIO_ID });
    check(Math.abs(d.typeDetail.config.volume - 0.4) < 1e-6, 'functions.get: audio volume 0.4 (' + d.typeDetail.config.volume + ')');
    check(d.fadeInSpeed === 1500, 'functions.get: fade in 1500 (' + d.fadeInSpeed + ')');
    check(d.fadeOutSpeed === 250, 'functions.get: fade out 250 (' + d.fadeOutSpeed + ')');
    check(d.runOrder === 'Loop', 'functions.get: run order Loop (' + d.runOrder + ')');
    // output device: pick the last entry of the combo, read back, then back to default
    const caps = await api.call('functions.audio.listCapabilities');
    check(caps.devices && caps.devices[0].id === '' && caps.devices[0].name === 'Default device', 'listCapabilities: default device first (' + caps.devices.length + ' devices)');
    if (caps.devices.length > 1) {
      const dev = caps.devices[caps.devices.length - 1];
      // the combo root is the row's second child (after the label); it opens on click
      await page.eval(`(function(){ const rows = Array.from(document.querySelectorAll('.qlc-audio-editor div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Output device'); window.__e2e = rows.pop().children[1]; return true; })()`);
      await page.click(() => window.__e2e);
      await sleep(150);
      await clickText(page, dev.name, '.qlc-audio-editor');
      await sleep(400);
      d = await api.call('functions.get', { functionId: AUDIO_ID });
      check(d.typeDetail.config.audioDevice === dev.id, 'functions.get: audio device "' + dev.id + '" (' + d.typeDetail.config.audioDevice + ')');
    }
    // replace the file through the server-side browser
    const before = d.typeDetail;
    let pickDir = ASSET_DIR, pickName = AUDIO_NAME;
    const probe = await api.call('core.fs.list', { path: ASSET_DIR, extensions: caps.extensions }).catch(() => null);
    if (!probe || !probe.entries.some(e => e.name === AUDIO_NAME)) {
      // fall back to any audio file the browser can find under the SF3 assets
      const root = await api.call('core.fs.list', { path: '<shows-dir>/SF3.qxw.assets' }).catch(() => ({ entries: [] }));
      for (const sub of root.entries.filter(e => e.isDir)) {
        const l = await api.call('core.fs.list', { path: sub.path, extensions: caps.extensions }).catch(() => ({ entries: [] }));
        if (l.entries.length) { pickDir = sub.path; pickName = l.entries[0].name; break; }
      }
    }
    console.log('  picking', pickDir, pickName);
    await clickText(page, 'Replace…', '.qlc-audio-editor');
    await page.waitFor('!!document.querySelector(".qlc-file-browser")', 5000);
    await page.click('.qlc-fb-path');
    await selectAll(page); await insertText(page, pickDir.replace(/\//g, '\\')); await page.key('Enter');
    await page.waitFor(`document.querySelector('.qlc-fb-list') && document.querySelector('.qlc-fb-list').textContent.indexOf(${JSON.stringify(pickName)}) !== -1`, 10000);
    const listed = await page.eval(`document.querySelectorAll('.qlc-fb-list > div').length`);
    check(listed >= 1, 'file browser lists ' + listed + ' entries for the audio filter');
    await shot(page, '03-file-browser');
    await clickText(page, pickName, '.qlc-fb-list');
    // the dialog's standard buttons sit outside the content div; the dialog is the last "Open" in the DOM
    await clickText(page, 'Open');
    await page.waitFor('!document.querySelector(".qlc-file-browser")', 5000);
    await page.waitFor(`document.querySelector('.qlc-audio-editor') && document.querySelector('.qlc-audio-editor').textContent.indexOf('Managed: ') !== -1`, 15000);
    await sleep(500);
    d = await api.call('functions.get', { functionId: AUDIO_ID });
    check(d.typeDetail.managed === true, 'functions.get: audio source is a managed copy (' + d.typeDetail.source + ')');
    check(/^project\.qxw\.assets\//.test(d.typeDetail.source), 'functions.get: source relative to the sandbox project (' + d.typeDetail.source + ')');
    check(d.typeDetail.source !== before.source, 'source changed from ' + before.source);
    check((d.typeDetail.origin || '').toLowerCase().replace(/\\/g, '/') === (pickDir + '/' + pickName).toLowerCase(), 'origin recorded (' + d.typeDetail.origin + ')');
    check(fs.existsSync(path.join(SANDBOX_DIR, d.typeDetail.source)), 'copy exists in the sandbox media store');
    await shot(page, '04-audio-editor');

    /* ---------------- Video ---------------- */
    console.log('Video editor');
    await clickText(page, VIDEO_NAME, '[data-ff-tree]');
    await page.waitFor('!!document.querySelector(".qlc-video-editor")', 10000);
    await sleep(300);
    d = await api.call('functions.get', { functionId: VIDEO_ID });
    console.log('  initial outputMode', d.typeDetail.config.outputMode);
    await clickText(page, 'Windowed', '.qlc-video-editor');
    await sleep(300);
    await clickText(page, 'Custom', '.qlc-video-editor');
    await page.waitFor(`document.querySelector('.qlc-video-editor').textContent.indexOf('Size') !== -1`, 5000);
    await setSpin(page, 'Position', 10, '.qlc-video-editor');
    await page.click(await rowEl(page, 'Size', 'input', '.qlc-video-editor'));
    await selectAll(page); await insertText(page, '800'); await page.key('Enter'); await sleep(150);
    // rotation Z is the third spin in the row
    const rotZ = await page.eval(`(function(){ const rows = Array.from(document.querySelectorAll('.qlc-video-editor div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Rotation'); const inputs = rows.pop().querySelectorAll('input'); window.__e2e = inputs[inputs.length - 1]; return inputs.length; })()`);
    check(rotZ === 3, 'rotation row has 3 spin boxes');
    await page.click(() => window.__e2e); await selectAll(page); await insertText(page, '45'); await page.key('Enter'); await sleep(150);
    await setSpin(page, 'Layer', 3, '.qlc-video-editor');
    await sleep(400);
    d = await api.call('functions.get', { functionId: VIDEO_ID });
    const cfg = d.typeDetail.config;
    check(cfg.outputMode === 'windowed' && cfg.fullscreen === false, 'functions.get: output mode windowed (' + cfg.outputMode + ')');
    check(cfg.customGeometry && cfg.customGeometry.x === 10 && cfg.customGeometry.width === 800, 'functions.get: geometry x=10 w=800 (' + JSON.stringify(cfg.customGeometry) + ')');
    check(cfg.rotation && cfg.rotation.z === 45, 'functions.get: rotation z=45 (' + JSON.stringify(cfg.rotation) + ')');
    check(cfg.zIndex === 3, 'functions.get: layer 3 (' + cfg.zIndex + ')');
    // fullscreen on screen 0 through the mode chips, then back to spout so the project keeps its shape
    await clickText(page, 'Fullscreen', '.qlc-video-editor');
    await sleep(400);
    d = await api.call('functions.get', { functionId: VIDEO_ID });
    check(d.typeDetail.config.outputMode === 'fullscreen' && d.typeDetail.config.fullscreen === true, 'functions.get: fullscreen (' + d.typeDetail.config.outputMode + ')');
    await shot(page, '05-video-editor');

    /* ---------------- persist + grep ---------------- */
    const outPath = SANDBOX_DIR + '\\out.qxw';
    const saved = await api.call('core.project.saveAs', { target: 'serverPath', path: outPath });
    console.log('saved', saved.filePath);
    const xml = fs.readFileSync(outPath, 'utf8');
    const fn = (id) => { const m = new RegExp('<Function ID="' + id + '"[\\s\\S]*?</Function>').exec(xml); return m ? m[0] : ''; };
    const scriptXml = fn(SCRIPT_ID), audioXml = fn(AUDIO_ID), videoXml = fn(VIDEO_ID);
    check(/Engine\.startFunction\(4\)/.test(decodeURIComponent(scriptXml.replace(/\+/g, ' '))) || scriptXml.indexOf('Engine.startFunction(4)') !== -1, 'out.qxw: script body persisted');
    check(/Volume="0\.4"/.test(audioXml), 'out.qxw: audio Volume="0.4"');
    check(/<Source[^>]*>[^<]*RzgSTjFyjEM\.mp3<\/Source>|<Source[^>]*>[^<]*\.(mp3|wav|ogg|flac)<\/Source>/i.test(audioXml) && /out\.qxw\.assets\/|project\.qxw\.assets\//.test(audioXml), 'out.qxw: audio source points into the media store (' + (/<Source[^>]*>([^<]*)</.exec(audioXml) || [])[1] + ')');
    check(/<RunOrder>Loop<\/RunOrder>/.test(audioXml), 'out.qxw: audio RunOrder Loop');
    check(/FadeIn="1500"/.test(audioXml) && /FadeOut="250"/.test(audioXml), 'out.qxw: audio fades');
    check(/Geometry="10,0,800,720"/.test(videoXml), 'out.qxw: video Geometry="10,0,800,720" (' + (/Geometry="[^"]*"/.exec(videoXml) || ['none'])[0] + ')');
    check(/Rotation="0,0,45"/.test(videoXml), 'out.qxw: video Rotation="0,0,45"');
    check(/ZIndex="3"/.test(videoXml), 'out.qxw: video ZIndex="3"');
    check(/Fullscreen="1"/.test(videoXml), 'out.qxw: video Fullscreen="1"');

    check(page.consoleErrors.length === 0, 'no console errors' + (page.consoleErrors.length ? ':\n    ' + page.consoleErrors.join('\n    ') : ''));
  } catch (e) {
    failures++;
    console.log('  FAIL exception: ' + (e.stack || e));
    try { await shot(page, '99-failure'); } catch (e2) { }
    if (page.consoleErrors.length) console.log('  console errors:\n    ' + page.consoleErrors.join('\n    '));
  } finally {
    if (!KEEP) await b.close();
    api.close();
  }
  console.log(failures ? failures + ' FAILURE(S)' : 'ALL OK');
  process.exit(failures ? 1 : 0);
})();
