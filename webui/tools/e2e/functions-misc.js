// End-to-end check of the "function-side leftovers" slice against a sandbox instance
// (dev-webui-sandbox.ps1 -Name fnmisc -ApiPort 9240 -WebUiPort 9241, SF3 project): Chaser speed modes,
// preview / next step, shuffle, auto durations, print, tap; Sequence step values, capture live, bound
// scene fixtures, preview; clone, usage, autostart, rename with numbering, select fixtures in function,
// intensity; Scene "add a fixture group"; palettes (Pan + "also create a Scene", colour fanning); media
// store actions (collect / reload changed / remove unused), Detect BPM, audio mute, video volume / mute /
// Spout size; RGB Matrix "save to Sequence". Every change is read back through a second API connection
// and a core.project.saveAs into the sandbox.
//
//   node webui/tools/e2e/functions-misc.js [--api 9240] [--web 9241] [--out <dir>] [--keep]
//
// Prerequisites (sandbox only): C:\qlcsandbox\fnmisc\Plugins\Audio\sndfileplugin.dll (the audio decoder
// the BPM analysis needs; decoders cannot output DMX) and C:\qlcsandbox\fnmisc\SF3.qxw.assets\335e235f3c78\
// RzgSTjFyjEM.mp3 (a real external media file for Collect). Missing either, those checks report FAIL.
// Writes only under C:\qlcsandbox\fnmisc.

const path = require('path'), fs = require('fs'), os = require('os');
const { launch } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', 9240)), WEB_PORT = Number(opt('web', 9241));
const OUT = opt('out', path.join(os.tmpdir(), 'qlc-e2e-fnmisc'));
const KEEP = args.indexOf('--keep') !== -1;
const SANDBOX_DIR = 'C:\\qlcsandbox\\fnmisc';
fs.mkdirSync(OUT, { recursive: true });

const CHASER = { id: '15', name: 'smol_big' };
const SEQUENCE = { id: '131', name: 'mainscreen_fade_in' };
const SCENE_VC = { id: '7', name: 'White cover' };            // used by a VC button on SF3
const SCENE_2 = { id: '1', name: 'Tilt bars on' };
const AUDIO = { id: '3', name: 'RzgSTjFyjEM.mp3' };
const VIDEO = { id: '116', name: 'testStream.mp4' };
const MATRIX = { id: '17', name: 'Dimmer Random' };

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
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; }).then(() => call('hello', { apiVersion: '1', clientName: 'e2e fnmisc' }));
  return { call, ready, events, close: () => ws.close() };
}

/* ---- page helpers ----------------------------------------------------------------------------- */
const rootExpr = (root) => root ? 'document.querySelector(' + JSON.stringify(root) + ')' : 'document';
/** click the deepest element inside `root` whose own text equals `text` (last match) */
async function clickText(page, text, root, opts) {
  const found = await page.eval(`(function(){
    const root = ${rootExpr(root)};
    if (!root) return false;
    const hit = Array.from(root.querySelectorAll('*')).filter(e => e.children.length === 0 && (e.textContent || '').trim() === ${JSON.stringify(text)}).pop();
    if (!hit) return false;
    hit.scrollIntoView({ block: 'center' }); window.__e2e = hit; return true;
  })()`);
  if (!found) throw new Error('text not found: ' + text);
  await sleep(60);
  await page.click(() => window.__e2e, opts || {});
}
/** click the element whose title starts with `prefix` (last match inside root) */
async function clickTitle(page, prefix, root) {
  const found = await page.eval(`(function(){
    const root = ${rootExpr(root)};
    const hit = root && Array.from(root.querySelectorAll('[title]')).filter(e => e.title.indexOf(${JSON.stringify(prefix)}) === 0).pop();
    if (!hit) return false; hit.scrollIntoView({ block: 'center' }); window.__e2e = hit; return true;
  })()`);
  if (!found) throw new Error('title not found: ' + prefix);
  await sleep(60);
  await page.click(() => window.__e2e);
}
async function titleDisabled(page, prefix, root) {
  return page.eval(`(function(){ const root = ${rootExpr(root)}; const e = root && Array.from(root.querySelectorAll('[title]')).filter(e => e.title.indexOf(${JSON.stringify(prefix)}) === 0).pop();
    if (!e) return null; const b = e.tagName === 'BUTTON' ? e : e.querySelector('button') || e; return !!(b.disabled || e.getAttribute('aria-disabled') === 'true' || getComputedStyle(e).opacity < 0.6); })()`);
}
/** the FF.Row whose label is `label`, then `selector` inside it */
async function rowEl(page, label, selector, root) {
  const found = await page.eval(`(function(){
    const root = ${rootExpr(root)};
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
  await sleep(200);
}
async function shot(page, name) { const f = path.join(OUT, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }
async function waitApi(fn, what, timeout = 8000) {
  const t0 = Date.now();
  for (;;) { try { if (await fn()) return true; } catch (e) { /* retry */ } if (Date.now() - t0 > timeout) { console.log('  (timed out waiting for ' + what + ')'); return false; } await sleep(200); }
}

(async () => {
  const api = apiClient();
  await api.ready;
  const project = await api.call('core.project.get');
  console.log('project:', project.filePath || '(untitled)');

  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=fx');
  /** find a function / fixture row through the tree search, then click it (opts: right click) */
  const openInTree = async (name, opts) => {
    const searching = await page.eval(`!!document.querySelector('[title="Search fixtures and functions"]') && !!Array.from(document.querySelectorAll('input')).find(i => i.placeholder === 'Search…' && !i.closest('[role=dialog]'))`);
    if (!searching) await clickTitle(page, 'Search fixtures and functions');
    await sleep(100);
    await page.eval(`(function(){ const i = Array.from(document.querySelectorAll('input')).find(i => i.placeholder === 'Search…'); i.focus(); i.select(); return true; })()`);
    await insertText(page, name); await page.key('Enter');
    await page.waitFor(`Array.from(document.querySelectorAll('[data-ff-tree] *')).some(e => e.children.length === 0 && (e.textContent||'').trim() === ${JSON.stringify(name)})`, 8000);
    await clickText(page, name, '[data-ff-tree]', opts);
    await sleep(250);
  };
  const menuItem = async (text) => { await page.waitFor('!!document.querySelector("[role=menu]")', 5000); await clickText(page, text, '[role=menu]'); await sleep(300); };
  const fnByName = async (name) => (await api.call('functions.list')).functions.filter(f => f.name === name);

  try {
    await page.waitFor(`(document.querySelector('[data-ff-tree]') || {}).textContent && document.querySelector('[data-ff-tree]').textContent.indexOf('Functions') !== -1 && document.body.textContent.indexOf('functions') !== -1`, 30000);
    await sleep(1000);
    console.log('connected, tree loaded');

    /* ================= Chaser ================= */
    console.log('Chaser editor');
    await openInTree(CHASER.name);
    await page.waitFor('!!document.querySelector("[data-e2e=chaser-steps]")', 10000);
    // speed modes
    await page.eval(`(function(){ const row = Array.from(document.querySelectorAll('div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Fade In').pop(); window.__e2e = Array.from(row.querySelectorAll('button')).find(b => b.textContent === 'Common'); return true; })()`);
    await page.click(() => window.__e2e); await sleep(400);
    let d = await api.call('functions.get', { functionId: CHASER.id });
    check(d.typeDetail.fadeInMode === 'Common', 'speed mode: Fade In -> Common (' + d.typeDetail.fadeInMode + ')');
    // preview from step 1, next step
    await page.click('[data-e2e=chaser-steps] tr[data-step="0"] td');
    await sleep(150);
    await clickTitle(page, 'Preview on the output from the selected step');
    check(await waitApi(async () => (await api.call('functions.get', { functionId: CHASER.id })).running, 'chaser running'), 'preview: chaser runs on the output');
    const evBefore = api.events.length;
    await sleep(300);
    await clickTitle(page, 'Preview the next step');
    const stepped = await waitApi(async () => api.events.slice(evBefore).some(e => e.topic === 'functions.chaser.currentStepChanged' && e.data.functionId === CHASER.id && e.data.stepIndex === 1), 'currentStepChanged 1');
    check(stepped, 'next step: functions.chaser.currentStepChanged stepIndex 1');
    await page.waitFor(`document.body.textContent.indexOf('step 2 · ') !== -1`, 5000).catch(() => {});
    check(await page.eval(`document.body.textContent.indexOf('step 2 · ') !== -1`), 'editor shows the playing step (step 2)');
    await shot(page, '01-chaser-preview');
    await clickTitle(page, 'Preview the previous step');
    check(await waitApi(async () => api.events.slice(evBefore).some(e => e.topic === 'functions.chaser.currentStepChanged' && e.data.stepIndex === 0), 'currentStepChanged 0'), 'previous step: back to step 1');
    await clickTitle(page, 'Stop the preview');
    check(await waitApi(async () => !(await api.call('functions.get', { functionId: CHASER.id })).running, 'chaser stopped'), 'preview stopped');
    // tap (running not required, just the call path)
    await clickText(page, 'Tap', null);
    await sleep(200);
    // shuffle keeps the same steps
    const stepsBefore = (await api.call('functions.get', { functionId: CHASER.id })).typeDetail.steps.map(s => s.targetFunctionId).sort().join(',');
    await clickTitle(page, 'Randomize the selected step(s) order');
    await sleep(500);
    const stepsAfter = (await api.call('functions.get', { functionId: CHASER.id })).typeDetail.steps.map(s => s.targetFunctionId).sort().join(',');
    check(stepsBefore === stepsAfter, 'shuffle: same steps, possibly reordered (' + stepsAfter + ')');
    // auto-set durations
    await clickTitle(page, 'Auto-set step durations');
    await sleep(800);
    d = await api.call('functions.get', { functionId: CHASER.id });
    const targets = await Promise.all(d.typeDetail.steps.map(s => api.call('functions.get', { functionId: s.targetFunctionId })));
    check(d.typeDetail.durationMode === 'PerStep' && d.typeDetail.steps.every((s, i) => s.duration === (targets[i].totalDuration || 1000)),
      'auto durations: Per Step, each step = its function total (' + d.typeDetail.steps.map(s => s.duration).join(',') + ')');
    // print: an iframe with the step table is handed to the browser's print
    await clickTitle(page, 'Print the steps');
    await sleep(100);
    const printed = await page.eval(`(function(){ const f = document.querySelector('[data-e2e=print-frame]'); return f ? f.contentDocument.querySelectorAll('tbody tr').length : -1; })()`);
    check(printed === d.typeDetail.steps.length, 'print: step table rendered for printing (' + printed + ' rows)');
    await shot(page, '02-chaser-editor');

    /* ================= Sequence ================= */
    console.log('Sequence editor');
    await openInTree(SEQUENCE.name);
    await page.waitFor('!!document.querySelector("[data-e2e=sequence-panel]")', 10000);
    const seq0 = await api.call('functions.get', { functionId: SEQUENCE.id });
    const boundId = seq0.typeDetail.boundSceneId;
    await page.click('[data-e2e=chaser-steps] tr[data-step="1"] td');
    await page.waitFor('!!document.querySelector("[data-e2e^=seq-console-] img")', 8000);
    // set the first channel of the first fixture console to 123
    const valKey = await page.eval(`(function(){ const c = document.querySelector('[data-e2e^=seq-console-]'); const cons = Array.from(c.children).filter(e => e.tagName === 'DIV').pop(); window.__e2e = cons.children[0].children[2]; return c.getAttribute('data-e2e').replace('seq-console-', ''); })()`);
    // click the lower right corner of the value box: the fader handle at 0 overlaps its upper part
    const vr = await page.rectOf(() => window.__e2e);
    await page.mouse('mouseMoved', vr.x + vr.w - 5, vr.y + vr.h - 4);
    await page.mouse('mousePressed', vr.x + vr.w - 5, vr.y + vr.h - 4); await page.mouse('mouseReleased', vr.x + vr.w - 5, vr.y + vr.h - 4);
    await sleep(100);
    await selectAll(page); await insertText(page, '123'); await page.key('Enter');
    await sleep(500);
    d = await api.call('functions.get', { functionId: SEQUENCE.id });
    check(d.typeDetail.steps[1].values[valKey + '.0'] === 123, 'step 2 value ' + valKey + '.0 = 123 via the console (' + JSON.stringify(d.typeDetail.steps[1].values) + ')');
    // capture live: put a known value on the bound scene's channel through the Simple Desk, capture into step 2
    const bound = await api.call('functions.get', { functionId: boundId });
    const key = Object.keys(bound.typeDetail.values)[0];
    const [fxId, ch] = key.split('.').map(Number);
    const fx = await api.call('fixtures.get', { fixtureId: String(fxId) });
    const abs = fx.universe * 512 + fx.address + ch;
    await api.call('io.simpleDesk.setChannel', { address: abs, value: 77 });
    await sleep(400);
    await clickText(page, 'Capture live', '[data-e2e=sequence-panel]');
    await sleep(800);
    d = await api.call('functions.get', { functionId: SEQUENCE.id });
    check(d.typeDetail.steps[1].values[key] === 77, 'capture live: step 2 ' + key + ' = 77 from the output (' + JSON.stringify(d.typeDetail.steps[1].values) + ')');
    await api.call('io.simpleDesk.resetChannel', { address: abs }).catch(() => {});
    // preview on the output: the bound scene runs with the step values
    await clickTitle(page, 'Preview the selected step on the output', '[data-e2e=sequence-panel]');
    check(await waitApi(async () => (await api.call('functions.get', { functionId: boundId })).running, 'bound scene running'), 'preview: bound scene runs');
    const bv = (await api.call('functions.get', { functionId: boundId })).typeDetail.values;
    check(bv[key] === 77, 'preview: bound scene holds the step values (' + JSON.stringify(bv) + ')');
    await clickTitle(page, 'Stop previewing the selected step', '[data-e2e=sequence-panel]');
    check(await waitApi(async () => !(await api.call('functions.get', { functionId: boundId })).running, 'bound scene stopped'), 'preview off: bound scene stopped');
    // add / remove a fixture of the bound scene
    const fixtures = (await api.call('fixtures.list')).fixtures;
    const extra = fixtures.find(f => bound.typeDetail.fixtures.indexOf(String(f.id)) === -1);
    await clickTitle(page, 'Add fixtures to the bound scene', '[data-e2e=sequence-panel]');
    await page.waitFor(`document.body.textContent.indexOf('Add fixtures to the bound scene') !== -1`, 5000);
    await page.eval(`(function(){ const i = Array.from(document.querySelectorAll('input')).filter(i => i.placeholder === 'Search…').pop(); i.focus(); return true; })()`);
    await insertText(page, extra.name);
    await sleep(200);
    await clickText(page, extra.name, '[role=dialog]').catch(async () => clickText(page, extra.name));
    await clickText(page, 'Add');
    await sleep(600);
    let bf = (await api.call('functions.get', { functionId: boundId })).typeDetail.fixtures;
    check(bf.indexOf(String(extra.id)) !== -1, 'bound scene: fixture "' + extra.name + '" added (' + bf.join(',') + ')');
    await shot(page, '03-sequence-editor');
    await clickTitle(page, 'Remove this fixture from the bound scene', '[data-e2e=sequence-panel]');
    await sleep(600);
    bf = (await api.call('functions.get', { functionId: boundId })).typeDetail.fixtures;
    check(bf.indexOf(String(extra.id)) === -1, 'bound scene: fixture removed again (' + bf.join(',') + ')');

    /* ================= Clone / usage / autostart / select fixtures ================= */
    console.log('Clone, usage, autostart');
    const before = (await api.call('functions.list')).functions;
    const nFn = before.length, beforeIds = new Set(before.map(f => f.id));
    await openInTree(SCENE_VC.name, { button: 'right' });
    await menuItem('Clone');
    await openInTree(CHASER.name, { button: 'right' });
    await menuItem('Clone');
    await openInTree(SCENE_2.name, { button: 'right' });
    await menuItem('Clone');
    await sleep(500);
    const list = (await api.call('functions.list')).functions;
    const copies = [SCENE_VC.name, CHASER.name, SCENE_2.name].map(n => list.find(f => !beforeIds.has(f.id) && f.name === n + ' (Copy)'));
    check(copies.every(Boolean) && list.length === nFn + 3, 'clone: Scene + Chaser + Scene copies listed (' + copies.map(c => c && c.id).join(',') + ')');
    if (copies[1]) {
      const cc = await api.call('functions.get', { functionId: copies[1].id });
      const orig = await api.call('functions.get', { functionId: CHASER.id });
      check(cc.typeDetail.steps.length === orig.typeDetail.steps.length, 'clone: the Chaser copy has the same steps');
    }
    // usage of a Scene used by a VC button
    await openInTree(SCENE_VC.name, { button: 'right' });
    await menuItem('Usage…');
    await page.waitFor('!!document.querySelector("[data-e2e=usage-list]") && document.querySelector("[data-e2e=usage-list]").textContent.indexOf("Loading") === -1', 8000);
    const usageText = await page.eval('document.querySelector("[data-e2e=usage-list]").textContent');
    const usage = await api.call('functions.usage', { functionId: SCENE_VC.id });
    check(usage.widgets.length > 0 && usageText.indexOf('Virtual Console widgets') !== -1 && usageText.indexOf(usage.widgets[0].widgetType) !== -1,
      'usage: dialog lists the VC widget(s) (' + usage.widgets.map(w => w.widgetType + '#' + w.id).join(',') + ')');
    await shot(page, '04-usage');
    await clickText(page, 'Close');
    // select fixtures in function
    await openInTree(SCENE_2.name, { button: 'right' });
    await menuItem('Select fixtures in function');
    await sleep(800);
    const sceneFx = (await api.call('functions.get', { functionId: SCENE_2.id })).typeDetail.fixtures.length;
    const selText = await page.eval(`document.body.textContent.match(/(\\d+) selected/) ? document.body.textContent.match(/(\\d+) selected/)[1] : '0'`);
    check(Number(selText) === sceneFx, 'select fixtures in function: ' + selText + ' fixtures selected in the tree (scene has ' + sceneFx + ')');
    // autostart from the header
    await openInTree(SCENE_2.name);
    await clickTitle(page, 'Set as the autostart function');
    await sleep(400);
    check((await api.call('core.project.get')).startupFunctionId === SCENE_2.id, 'autostart: core.project.get startupFunctionId = ' + SCENE_2.id);
    check(await page.eval(`!!Array.from(document.querySelectorAll('[title]')).find(e => e.title.indexOf('This function starts automatically') === 0)`), 'autostart: header button shows the set state');
    // intensity slider in the header
    const sl = await page.eval(`(function(){ const s = document.querySelector('[data-e2e=fn-intensity]'); if (!s) return null; const r = s.getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; })()`);
    if (sl) {
      await page.drag(sl.x + sl.w - 45, sl.y + sl.h / 2, sl.x + 30, sl.y + sl.h / 2, 10);
      await sleep(500);
      const attr = (await api.call('functions.get', { functionId: SCENE_2.id })).attributes.find(a => a.name === 'Intensity');
      check(attr && attr.value < 1, 'intensity slider: Intensity attribute lowered (' + (attr && attr.value) + ')');
      await api.call('functions.adjustAttribute', { functionId: SCENE_2.id, attributeName: 'Intensity', value: 1 });
    } else check(false, 'intensity slider present in the header');
    await shot(page, '05-scene-header');

    // Scene editor: add a fixture group as member
    const groups = (await api.call('fixtures.group.list')).groups;
    await clickTitle(page, 'Add a fixture group');
    await page.waitFor(`document.body.textContent.indexOf('Add fixture groups to the scene') !== -1 && Array.from(document.querySelectorAll('*')).some(e => e.children.length === 0 && (e.textContent||'').trim() === ${JSON.stringify(groups[0].name)})`, 8000);
    await clickText(page, groups[0].name, null);
    await clickText(page, 'Add');
    await sleep(700);
    const sg = (await api.call('functions.get', { functionId: SCENE_2.id })).typeDetail;
    const grp = await api.call('fixtures.group.get', { groupId: groups[0].id });
    check(sg.fixtureGroups.indexOf(groups[0].id) !== -1 && grp.heads.every(h => sg.fixtures.indexOf(String(h.fixtureId)) !== -1),
      'scene: group "' + groups[0].name + '" and its fixtures added');

    /* ================= Rename with numbering ================= */
    console.log('Rename with numbering');
    await openInTree(copies[0].name);
    await page.eval(`(function(){ const i = Array.from(document.querySelectorAll('input')).find(i => i.placeholder === 'Search…'); i.focus(); i.select(); return true; })()`);
    await insertText(page, '(Copy)'); await page.key('Enter');
    await sleep(400);
    for (const c of copies.slice(1)) { await clickText(page, c.name, '[data-ff-tree]', { modifiers: 2 }); await sleep(120); }
    check(await page.eval(`/3 selected/.test(document.body.textContent)`), 'three functions selected in the tree');
    await clickTitle(page, 'Rename the selected item');
    await page.waitFor('!!document.querySelector("[data-e2e=rename-numbered]")', 5000);
    await page.eval(`(function(){ const i = document.querySelector('[data-e2e=rename-base]'); i.focus(); i.select(); return true; })()`);
    await insertText(page, 'E2E Item');
    await setSpin(page, 'Start number', 5, '[data-e2e=rename-numbered]');
    await setSpin(page, 'Digits', 2, '[data-e2e=rename-numbered]');
    await shot(page, '06-rename-numbered');
    await clickText(page, 'Rename');
    await sleep(800);
    const renamed = (await api.call('functions.list')).functions.filter(f => /^E2E Item \d\d$/.test(f.name)).map(f => f.name).sort();
    check(renamed.join(',') === 'E2E Item 05,E2E Item 06,E2E Item 07', 'rename with numbering: ' + renamed.join(', '));

    /* ================= Palettes ================= */
    console.log('Palettes');
    await clickTitle(page, 'Palettes');
    await sleep(300);
    await clickTitle(page, 'Create a palette');
    await page.waitFor(`document.body.textContent.indexOf('New palette') !== -1`, 5000);
    // type combo: open it and pick Pan
    await page.click(await rowEl(page, 'Type', null, '[role=dialog]'));
    await page.eval(`(function(){ const row = Array.from(document.querySelectorAll('[role=dialog] div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Type').pop(); window.__e2e = row.children[1]; return true; })()`);
    await page.click(() => window.__e2e); await sleep(150);
    await clickText(page, 'Pan');
    await sleep(150);
    await page.eval(`(function(){ const i = document.querySelector('[data-e2e=palette-name]'); i.focus(); return true; })()`);
    await insertText(page, 'E2E Pan');
    await setSpin(page, 'Pan', 90, '[role=dialog]');
    await page.click('[data-e2e=palette-also-scene]');
    await sleep(100);
    await page.eval(`(function(){ const i = document.querySelector('[data-e2e=palette-scene-name]'); i.focus(); return true; })()`);
    await insertText(page, 'E2E Pan Scene');
    await shot(page, '07-palette-create');
    await clickText(page, 'Create');
    await sleep(1000);
    const pals = (await api.call('palette.list')).palettes;
    const panPal = pals.find(p => p.name === 'E2E Pan');
    check(panPal && panPal.type === 'Pan', 'palette: "E2E Pan" created with type Pan');
    const panScene = (await fnByName('E2E Pan Scene'))[0];
    const panSceneD = panScene ? await api.call('functions.get', { functionId: panScene.id }) : null;
    check(panSceneD && panPal && panSceneD.typeDetail.palettes.indexOf(String(panPal.id)) !== -1, 'palette: "Also create a Scene" made "E2E Pan Scene" holding it');
    if (panPal) { const pd = await api.call('palette.get', { paletteId: panPal.id }); check(Number(pd.values[0]) === 90, 'palette: Pan value 90 (' + JSON.stringify(pd.values) + ')'); }
    // fan a colour palette
    const colour = pals.find(p => p.type === 'Color');
    await clickText(page, colour.name);
    await page.waitFor('!!document.querySelector("[data-e2e=palette-fanning]")', 5000);
    await clickTitle(page, 'Linear', '[data-e2e=palette-fanning]');
    await sleep(400);
    await setSpin(page, 'Amount', 60, '[data-e2e=palette-fanning]');
    await sleep(300);
    const fan = (await api.call('palette.get', { paletteId: colour.id })).fanning;
    check(fan && fan.type === 'Linear' && fan.amount === 60, 'palette fanning: ' + colour.name + ' Linear 60% (' + JSON.stringify(fan) + ')');
    await shot(page, '08-palette-fanning');

    /* ================= Media store ================= */
    console.log('Media store');
    const collectPath = SANDBOX_DIR + '\\collect\\show.qxw';
    fs.mkdirSync(path.dirname(collectPath), { recursive: true });
    await api.call('core.project.saveAs', { target: 'serverPath', path: collectPath });
    await clickTitle(page, 'Actions menu');
    await sleep(200);
    await clickText(page, 'Collect media into project');
    await page.waitFor('!!document.querySelector("[data-e2e=media-dialog-collect]") && document.querySelector("[data-e2e=media-dialog-collect]").textContent.indexOf("Loading") === -1', 8000);
    await shot(page, '09-media-collect');
    await clickText(page, 'Collect');
    await page.waitFor(`document.querySelector('[data-e2e=media-dialog-collect]').textContent.indexOf('Copied ') !== -1`, 15000);
    const collectText = await page.eval(`document.querySelector('[data-e2e=media-dialog-collect]').textContent`);
    console.log('  collect: ' + collectText.slice(collectText.indexOf('Copied')));
    const managed = await waitApi(async () => (await api.call('functions.get', { functionId: AUDIO.id })).typeDetail.managed, 'audio managed', 20000);
    const ad = await api.call('functions.get', { functionId: AUDIO.id });
    check(managed && ad.typeDetail.config.sourceFileName.indexOf('show.qxw.assets') !== -1, 'collect: the SF3 audio now lives in collect\\show.qxw.assets (' + ad.typeDetail.config.sourceFileName + ')');
    await clickText(page, 'Close');
    // reload changed: nothing changed on disk
    await clickTitle(page, 'Actions menu'); await sleep(200);
    await clickText(page, 'Reload changed media');
    await page.waitFor(`!!document.querySelector('[data-e2e=media-dialog-reload]') && document.querySelector('[data-e2e=media-dialog-reload]').textContent.indexOf('Loading') === -1`, 8000);
    check(await page.eval(`document.querySelector('[data-e2e=media-dialog-reload]').textContent.indexOf('No media changed on disk') !== -1`), 'reload changed media: dialog reports nothing changed');
    await clickText(page, 'Close');
    // remove unused: an orphan in the store's <sha12>/ layout
    const store = (await api.call('functions.media.status')).storeDir;
    const orphan = path.join(store, '0123456789ab', 'orphan.mp3');
    fs.mkdirSync(path.dirname(orphan), { recursive: true });
    fs.copyFileSync(ad.typeDetail.config.sourceFileName, orphan);
    await clickTitle(page, 'Actions menu'); await sleep(200);
    await clickText(page, 'Remove unused media');
    await page.waitFor(`!!document.querySelector('[data-e2e=media-dialog-remove]') && document.querySelector('[data-e2e=media-dialog-remove]').textContent.indexOf('orphan.mp3') !== -1`, 8000);
    await shot(page, '10-media-remove');
    await clickText(page, 'Remove');
    await page.waitFor(`document.querySelector('[data-e2e=media-dialog-remove]').textContent.indexOf('Removed ') !== -1`, 8000);
    check(!fs.existsSync(orphan) && fs.existsSync(ad.typeDetail.config.sourceFileName), 'remove unused: orphan deleted, the referenced copy kept');
    await clickText(page, 'Close');

    /* ================= Audio / Video ================= */
    console.log('Audio / Video editors');
    await openInTree(AUDIO.name);
    await page.waitFor('!!document.querySelector(".qlc-audio-editor")', 10000);
    await clickTitle(page, 'Detect BPM', '.qlc-audio-editor');
    const bpmDone = await waitApi(async () => { const s = (await api.call('functions.get', { functionId: AUDIO.id })).typeDetail.config.bpm.state; return s === 'done' || s === 'failed'; }, 'bpm analysis', 60000);
    const bpm = (await api.call('functions.get', { functionId: AUDIO.id })).typeDetail.config.bpm;
    check(bpmDone && bpm.state === 'done' && bpm.value > 0, 'detect BPM: ' + bpm.state + ' ' + bpm.value + ' (confidence ' + bpm.confidence + ')');
    await page.waitFor(`document.querySelector('.qlc-audio-editor').textContent.indexOf(${JSON.stringify(Number(bpm.value || 0).toFixed(1))}) !== -1`, 5000).catch(() => {});
    check(await page.eval(`document.querySelector('.qlc-audio-editor').textContent.indexOf(${JSON.stringify(Number(bpm.value || 0).toFixed(1))}) !== -1`), 'detect BPM: the editor shows the result');
    await page.click(await rowEl(page, 'Volume', '[role=checkbox], input[type=checkbox], button', '.qlc-audio-editor')).catch(() => {});
    await sleep(400);
    check((await api.call('functions.get', { functionId: AUDIO.id })).typeDetail.config.muted === true, 'audio mute set');
    await shot(page, '11-audio-editor');
    await openInTree(VIDEO.name);
    await page.waitFor('!!document.querySelector(".qlc-video-editor")', 10000);
    await setSpin(page, 'Volume', 55, '.qlc-video-editor');
    await page.click(await rowEl(page, 'Volume', '[role=checkbox], input[type=checkbox], button', '.qlc-video-editor')).catch(() => {});
    await sleep(300);
    const vd = (await api.call('functions.get', { functionId: VIDEO.id })).typeDetail.config;
    check(vd.volume === 55 && vd.muted === true, 'video volume 55 and mute (' + vd.volume + ', ' + vd.muted + ')');
    if (vd.outputMode === 'spout') {
      await clickText(page, 'Custom', '.qlc-video-editor');
      await sleep(300);
      await page.eval(`(function(){ const row = Array.from(document.querySelectorAll('.qlc-video-editor div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Sender size').pop(); window.__e2e = row.querySelectorAll('input')[0]; return true; })()`);
      await page.click(() => window.__e2e); await selectAll(page); await insertText(page, '1280'); await page.key('Enter');
      await sleep(400);
      const sz = (await api.call('functions.get', { functionId: VIDEO.id })).typeDetail.config.spoutSize;
      check(sz.width === 1280 && sz.height > 0, 'video Spout sender size 1280x' + sz.height);
    }
    await shot(page, '12-video-editor');

    /* ================= RGB Matrix -> Sequence ================= */
    console.log('RGB Matrix save to Sequence');
    await openInTree(MATRIX.name);
    await clickText(page, 'Save to Sequence');
    await page.waitFor(`!!document.querySelector('[data-e2e=rgb-save-seq-msg]') && document.querySelector('[data-e2e=rgb-save-seq-msg]').textContent.indexOf('Created') !== -1`, 10000);
    const rs = (await fnByName(MATRIX.name + ' Sequence'))[0];
    const rsd = rs ? await api.call('functions.get', { functionId: rs.id }) : null;
    check(rsd && rsd.type === 'Sequence' && rsd.typeDetail.steps.length > 0 && Object.keys(rsd.typeDetail.steps[0].values).length > 0,
      'RGB matrix -> "' + MATRIX.name + ' Sequence" with ' + (rsd ? rsd.typeDetail.steps.length : 0) + ' steps');
    await shot(page, '13-rgb-save-seq');

    /* ================= persist ================= */
    const outPath = SANDBOX_DIR + '\\out.qxw';
    await api.call('core.project.saveAs', { target: 'serverPath', path: outPath });
    const xml = fs.readFileSync(outPath, 'utf8');
    check(new RegExp('Autostart="' + SCENE_2.id + '"').test(xml), 'out.qxw: Autostart="' + SCENE_2.id + '"');
    check(/<Palette[^>]*Name="E2E Pan"[^>]*Type="Pan"|<Palette[^>]*Type="Pan"[^>]*Name="E2E Pan"/.test(xml), 'out.qxw: Pan palette saved');
    check(/Fan="Linear"/.test(xml) && /Amount="60"/.test(xml), 'out.qxw: fanning Linear / Amount 60 saved');
    check(/E2E Item 05/.test(xml), 'out.qxw: renamed functions saved');

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
