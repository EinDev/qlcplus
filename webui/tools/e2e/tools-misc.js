// End-to-end check of the "toolbar / settings / Simple Desk leftovers" slice against a sandbox instance
// (dev-webui-sandbox.ps1 -Name tools -ApiPort 9260 -WebUiPort 9261 -UserFixtureDir C:\qlcsandbox\tools\UserFixtures,
// SF3 project): open the project by dropping the .qxw on the window (core.project.open upload, saved
// with an older Creator/Version so the legacy Show timing dialog comes up and one Show gets converted),
// Open-dialog upload button present, beat generator source, DMX dump into an existing Scene (non-zero
// only) and of the selected fixture into a new one, Simple Desk channel value debug, DMX Address tool,
// UI Settings (colour + scale, persisted across a reload, save file), keyboard shortcuts (rebind the
// blackout key, key-cast toast, persisted, collision warning, export / import, defaults), a dropped .qxf
// landing in the sandbox user fixture folder. Every effect is read back through a second API client.
//
//   node webui/tools/e2e/tools-misc.js [--api 9260] [--web 9261] [--out <dir>] [--userdir C:\qlcsandbox\tools\UserFixtures]
//
// Writes only under C:\qlcsandbox\tools (the uploaded project stays in memory; the .qxf goes to --userdir,
// which must be inside C:\qlcsandbox and is removed again). core.settings.set is NOT called: the sandbox
// shares the desktop's QSettings (registry) with the live install.

const path = require('path'), fs = require('fs'), os = require('os');
const { launch } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', process.env.E2E_API_PORT || 9260)), WEB_PORT = Number(opt('web', process.env.E2E_WEB_PORT || 9261));
const OUT = path.resolve(opt('out', process.env.E2E_OUT || path.join(os.tmpdir(), 'qlc-e2e-tools')));
const SANDBOX_DIR = 'C:\\qlcsandbox\\tools';
const USER_DIR = opt('userdir', path.join(SANDBOX_DIR, 'UserFixtures'));
const REAL_DIR = path.join(os.homedir(), 'QLC+', 'Fixtures');
fs.mkdirSync(OUT, { recursive: true });

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
async function until(fn, timeout = 8000, interval = 150) { const t0 = Date.now(); for (;;) { let v = false; try { v = await fn(); } catch (e) { } if (v) return v; if (Date.now() - t0 > timeout) return false; await sleep(interval); } }

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
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; }).then(() => call('hello', { apiVersion: '1', clientName: 'e2e tools' }));
  return { call, ready, events, close: () => ws.close() };
}

/* ---- page helpers ----------------------------------------------------------------------------- */
async function clickSel(page, sel) { await page.eval(`(function(){ const e = document.querySelector(${JSON.stringify(sel)}); if (e) e.scrollIntoView({ block: 'center' }); return !!e; })()`); await sleep(50); await page.click(sel); await sleep(150); }
/** the deepest element under root whose own text is `text` (last match) */
async function clickText(page, text, root) {
  const ok = await page.eval(`(function(){ const r = ${root ? 'document.querySelector(' + JSON.stringify(root) + ')' : 'document'}; if (!r) return false;
    const hit = Array.from(r.querySelectorAll('*')).filter(e => e.children.length === 0 && (e.textContent || '').trim() === ${JSON.stringify(text)}).pop();
    if (!hit) return false; hit.scrollIntoView({ block: 'center' }); window.__e2e = hit; return true; })()`);
  if (!ok) throw new Error('text not found: ' + text);
  await sleep(50); await page.click(() => window.__e2e); await sleep(150);
}
async function dialogButton(page, text) {
  const ok = await page.eval(`(function(){ const b = Array.from(document.querySelectorAll('button')).filter(x => x.textContent.trim() === ${JSON.stringify(text)}); window.__e2e = b[b.length - 1] || null; return !!window.__e2e; })()`);
  if (!ok) throw new Error('button not found: ' + text);
  await page.click(() => window.__e2e); await sleep(200);
}
async function hasText(page, text) { return page.eval(`document.body.innerText.indexOf(${JSON.stringify(text)}) !== -1`); }
async function key(page, k, code, vk, modifiers) {
  const e = { key: k, code, windowsVirtualKeyCode: vk, modifiers: modifiers || 0 };
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'rawKeyDown' }, e));
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, e));
  await sleep(150);
}
const CTRL = 2, SHIFT = 8, ALT = 1;
/** set a native input's value the way React notices, then fire input + change */
async function setInput(page, sel, value) {
  return page.eval(`(function(){ const i = document.querySelector(${JSON.stringify(sel)}); if (!i) return false;
    const set = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set; set.call(i, ${JSON.stringify(String(value))});
    i.dispatchEvent(new Event('input', { bubbles: true })); i.dispatchEvent(new Event('change', { bubbles: true })); return true; })()`);
}
/** a File built in the page from base64, dropped on the App root (window-wide drop zone) */
async function dropFile(page, name, base64, type) {
  return page.eval(`(function(){
    const bin = atob(${JSON.stringify(base64)}); const bytes = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    const file = new File([bytes], ${JSON.stringify(name)}, { type: ${JSON.stringify(type || 'application/xml')} });
    const dt = new DataTransfer(); dt.items.add(file);
    const target = document.querySelector('#root > *') ? document.querySelector('#root').firstElementChild.firstElementChild || document.querySelector('#root').firstElementChild : document.body;
    const t = document.elementFromPoint(innerWidth / 2, innerHeight / 2) || target;
    t.dispatchEvent(new DragEvent('dragover', { bubbles: true, cancelable: true, dataTransfer: dt }));
    t.dispatchEvent(new DragEvent('drop', { bubbles: true, cancelable: true, dataTransfer: dt }));
    return true; })()`);
}
/** a File given to a hidden <input type=file> (change event) */
async function inputFile(page, sel, name, text) {
  return page.eval(`(function(){ const i = document.querySelector(${JSON.stringify(sel)}); if (!i) return false;
    const dt = new DataTransfer(); dt.items.add(new File([${JSON.stringify(text)}], ${JSON.stringify(name)}, { type: 'application/json' }));
    i.files = dt.files; i.dispatchEvent(new Event('change', { bubbles: true })); return true; })()`);
}
async function shot(page, name) { const f = path.join(OUT, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }
async function menu(page, text) { await clickSel(page, '[title="Actions menu"]'); await page.waitFor('!!document.querySelector("[role=menu]")', 5000); await clickText(page, text, '[role=menu]'); await sleep(300); }
async function connected(page) {
  return page.waitFor(`(function(){ const b = Array.from(document.querySelectorAll('button')).find(x => /^BPM: /.test(x.textContent)); return !!b && !b.disabled && document.body.innerText.indexOf('(mock)') === -1; })()`, 30000);
}
/** functions.get Scene typeDetail.values {"fixtureId.channel": value} -> [{fixtureId, channel, value}] */
function sceneValues(f) { const v = (f && f.typeDetail && f.typeDetail.values) || {}; return Object.keys(v).map(k => ({ fixtureId: k.split('.')[0], channel: Number(k.split('.')[1]), value: Number(v[k]) })); }
function snapshotDir(dir) { try { return fs.readdirSync(dir).map(f => { const s = fs.statSync(path.join(dir, f)); return f + ':' + s.size + ':' + s.mtimeMs; }).sort(); } catch (e) { return []; } }

(async () => {
  if (!/qlcsandbox/i.test(USER_DIR)) { console.log('refusing: --userdir must be inside C:\\qlcsandbox'); process.exit(2); }
  const api = apiClient();
  await api.ready;
  const realBefore = snapshotDir(REAL_DIR);

  /* the sandbox copy of SF3, saved "by 5.2.0": its two beat-based Shows get flagged (ADR 0001) */
  const projectPath = path.join(SANDBOX_DIR, 'project.qxw');
  const xml = fs.readFileSync(projectPath, 'utf8').replace(/<Version>[^<]*<\/Version>/, '<Version>5.2.0</Version>');
  const showsInFile = Array.from(xml.matchAll(/<Function ID="(\d+)" Type="Show" Name="([^"]*)"/g)).map(m => ({ id: m[1], name: m[2] }));

  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=sd');
  await page.s.send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: OUT }).catch(() => page.s.send('Page.setDownloadBehavior', { behavior: 'allow', downloadPath: OUT }));
  try {
    await connected(page);
    await page.eval(`(function(){ try { localStorage.removeItem('qlcplus.webui.shortcuts'); localStorage.removeItem('qlcplus.webui.shortcutHints'); localStorage.removeItem('qlcplus.webui.uiSettings'); } catch (e) {} return true; })()`);
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=sd');
    await connected(page);
    console.log('connected');

    /* ================= 1. open a project from this computer ================= */
    console.log('Open a project from the local machine');
    await menu(page, 'Open file');
    check(await page.eval('!!document.querySelector("[data-role=open-upload]") && !!document.querySelector("[data-role=open-upload-file]")'), 'Open dialog offers "Open a file from this computer…"');
    await dialogButton(page, 'Cancel');
    const revBefore = (await api.call('core.project.get')).docRevision;
    await dropFile(page, 'SF3.qxw', Buffer.from(xml, 'utf8').toString('base64'));
    /* the current sandbox project may be modified: the "Your project has changes" guard comes first */
    await sleep(400);
    if (await hasText(page, 'Your project has changes')) await dialogButton(page, 'Discard');
    const loaded = await until(async () => { const p = await api.call('core.project.get'); return p.fileName === 'SF3.qxw' && p.filePath == null ? p : false; }, 20000);
    check(!!loaded, 'core.project.get after the drop: fileName SF3.qxw, filePath null (' + JSON.stringify(loaded && { fileName: loaded.fileName, filePath: loaded.filePath }) + ')');
    const fnList = (await api.call('functions.list')).functions || [];
    check(fnList.length > 100 && fnList.some(f => f.name === 'Tilt bars on'), 'the dropped SF3 is loaded (' + fnList.length + ' functions)');
    check(await until(() => page.eval(`Array.from(document.querySelectorAll('input')).some(i => i.value === 'SF3.qxw')`), 5000), 'toolbar shows the uploaded file name');

    /* ================= 2. legacy Show timing dialog ================= */
    console.log('Legacy Show timing (ADR 0001)');
    const legacy = await api.call('functions.show.legacyTiming.get');
    check(legacy.source === 'upload' && legacy.creatorVersion === '5.2.0', 'server reads the uploaded Creator/Version (' + legacy.source + ' ' + legacy.creatorVersion + ')');
    check(legacy.shows.length >= 1, 'flagged Shows: ' + legacy.shows.map(s => s.name + ' ' + s.bpm + ' BPM').join(', '));
    check(await until(() => page.eval('!!document.querySelector("[data-role=legacy-timing]")'), 8000), 'the web UI asks about the flagged Shows after the load');
    await shot(page, '01-legacy-timing');
    if (legacy.shows.length) {
      const target = legacy.shows[0];
      const before = await api.call('functions.get', { functionId: target.id });
      const items0 = JSON.stringify(before.typeDetail || before);
      await clickSel(page, '[data-legacy-show="' + target.id + '"] [data-role=legacy-convert]');
      check(await until(() => page.eval('document.querySelectorAll("[data-role=legacy-preview] tr").length > 0'), 5000), 'the convert dialog previews old → new times');
      await shot(page, '02-legacy-convert');
      await dialogButton(page, 'Convert');
      const after = await until(async () => { const f = await api.call('functions.get', { functionId: target.id }); return JSON.stringify(f.typeDetail || f) !== items0 ? f : false; }, 6000);
      check(!!after, 'Show "' + target.name + '" timeline changed on the server after Convert');
      const again = await api.call('functions.show.legacyTiming.get');
      check(!again.shows.some(s => s.id === target.id), 'the converted Show is no longer flagged');
      if (again.shows.length) {
        await clickSel(page, '[data-legacy-show="' + again.shows[0].id + '"] [data-role=legacy-dismiss]');
        check(await until(async () => !(await api.call('functions.show.legacyTiming.get')).shows.some(s => s.id === again.shows[0].id)), '"Already correct" dismisses "' + again.shows[0].name + '"');
      }
      if (await page.eval('!!document.querySelector("[data-role=legacy-timing]")')) await dialogButton(page, 'Later');
    }

    /* ================= 3. beat generator source ================= */
    console.log('Beat generator source');
    const bpmBtn = `(function(){ return Array.from(document.querySelectorAll('button')).find(x => /^BPM: /.test(x.textContent)); })()`;
    await page.click(() => Array.from(document.querySelectorAll('button')).find(x => /^BPM: /.test(x.textContent)));
    await page.waitFor('!!document.querySelector("[data-role=beat-generators]")', 4000);
    await shot(page, '03-beat-panel');
    const results = {};
    for (const g of ['internal', 'audio', 'plugin', 'disabled', 'internal']) {
      await clickSel(page, '[data-generator="' + g + '"]');
      await until(async () => (await api.call('core.bpm.get')).generator === g, 3000);
      results[g] = (await api.call('core.bpm.get')).generator;
      const ui = await page.eval(`(function(){ const b = document.querySelector('[data-generator="${g}"] span'); return b ? getComputedStyle(b).backgroundColor : ''; })()`);
      console.log('    ' + g + ' -> server ' + results[g] + ', indicator ' + ui);
    }
    check(results.internal === 'internal' && results.disabled === 'disabled', 'internal / disabled switch the engine generator');
    check(results.audio === 'audio' && results.plugin === 'plugin', 'audio / plugin are accepted by the engine (plugin-less sandbox: no beats arrive)');
    await page.eval(`(function(){ const b = ${bpmBtn}; if (b) b.click(); return true; })()`);
    await sleep(200);

    /* ================= 4. DMX dump ================= */
    console.log('DMX dump into an existing Scene');
    const fixtures = (await api.call('fixtures.list')).fixtures || [];
    const u0 = fixtures.filter(f => f.universe === 0).sort((a, b) => a.address - b.address);
    const fxA = u0[0], fxB = u0[1];
    const created = await api.call('functions.create', { type: 'Scene', name: 'E2E dump target', baseRevision: (await api.call('core.project.get')).docRevision });
    const targetId = String(created.functionId || created.id || (created.function && created.function.id));
    check(!!targetId && targetId !== 'undefined', 'created an empty Scene "E2E dump target" (' + targetId + ')');
    await api.call('io.simpleDesk.setChannels', { channels: [{ address: fxA.address, value: 201 }, { address: fxB.address, value: 77 }] });
    await sleep(300);
    await clickSel(page, '[data-role=toolbar-dmxdump]');
    await page.waitFor('!!document.querySelector("[data-role=dmx-dump]")', 4000);
    await clickSel(page, '[data-role=dump-existing]');
    await page.click('[data-role=dump-scene] button');
    await sleep(200);
    await clickText(page, 'E2E dump target (' + targetId + ')');
    check(await page.eval('document.querySelector("[data-role=dump-scene]").textContent.indexOf("E2E dump target") !== -1'), 'target Scene picked in the combo');
    await shot(page, '04-dump-dialog');
    await dialogButton(page, 'Dump');
    check(await until(() => page.eval('(document.querySelector("[data-role=dump-note]") || {}).textContent'), 5000), 'dialog reports: ' + await page.eval('(document.querySelector("[data-role=dump-note]") || {}).textContent'));
    const scene = await api.call('functions.get', { functionId: targetId });
    const flat = sceneValues(scene);
    const hitA = flat.find(v => v.fixtureId === String(fxA.id) && v.channel === 0);
    const hitB = flat.find(v => v.fixtureId === String(fxB.id) && v.channel === 0);
    check(hitA && Number(hitA.value) === 201 && hitB && Number(hitB.value) === 77, 'existing Scene now holds ' + fxA.name + ' ch1 = 201 and ' + fxB.name + ' ch1 = 77 (' + flat.length + ' values)');
    check(flat.every(v => Number(v.value) !== 0), 'non-zero only: no zero values in the Scene');
    await dialogButton(page, 'Close');

    console.log('DMX dump of the selected fixture (Simple Desk)');
    await clickSel(page, '[data-fixture="' + fxA.id + '"]');
    await clickSel(page, '[title="Dump DMX values to a scene"]');
    await page.waitFor('!!document.querySelector("[data-role=dmx-dump]")', 4000);
    check(await page.eval('document.querySelector("[data-role=dmx-dump]").innerText.indexOf("(1 selected)") !== -1'), 'Simple Desk opens the dump preset to its selected fixture');
    await page.eval(`(function(){ const i = document.querySelector('[data-role=dmx-dump] input[placeholder="New Scene"]'); if (!i) return false; const set = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set; set.call(i, 'E2E only A'); i.dispatchEvent(new Event('input', { bubbles: true })); return true; })()`);
    await clickSel(page, '[data-role="dump-group-Pan"]'); // untick one channel type: the rest stay
    await dialogButton(page, 'Dump');
    const note2 = await until(() => page.eval('(document.querySelector("[data-role=dump-note]") || {}).textContent'), 5000);
    const newId = /Scene (\d+) created/.exec(note2 || '');
    check(!!newId, 'new Scene created: ' + note2);
    if (newId) {
      const s2 = await api.call('functions.get', { functionId: newId[1] });
      const f2 = sceneValues(s2);
      check(f2.length >= 1 && f2.every(v => v.fixtureId === String(fxA.id)), 'the new Scene holds only ' + fxA.name + ' values (' + f2.length + ')');
      check(s2.name === 'E2E only A', 'named from the dialog (' + s2.name + ')');
    }
    await dialogButton(page, 'Close');

    /* ================= 5. channel value debug ================= */
    console.log('Simple Desk channel value debug');
    await page.eval(`(function(){ const s = document.querySelector('[data-channel="${fxA.address}"]'); if (s) s.scrollIntoView({ inline: 'center' }); return !!s; })()`);
    await page.eval(`(function(){ const s = document.querySelector('[data-channel="${fxA.address}"]'); window.__e2e = s && (s.querySelector('[title^="Debug this channel"]') || s.parentElement.querySelector('[title^="Debug this channel"]')); return !!window.__e2e; })()`);
    await page.click(() => window.__e2e);
    check(await until(() => page.eval('!!document.querySelector("[data-role=channel-inspect]")'), 5000), 'the debug popup shows the engine trace');
    const inspectText = await page.eval('(document.querySelector("[data-role=channel-inspect]") || {}).innerText || ""');
    check(/override: YES, value 201/.test(inspectText), 'it names the web Simple Desk override (201)');
    check(await page.eval('!!document.querySelector("[data-inspect-fader=controlApiSimpleDesk]")'), 'and lists the API desk fader');
    const insp = await api.call('io.dmx.channel.inspect', { universeId: 0, channel: fxA.address });
    check(insp.simpleDeskOverride === 201 && insp.fixture && String(insp.fixture.id) === String(fxA.id), 'io.dmx.channel.inspect agrees (' + insp.fixture.name + ', ' + insp.faders.length + ' fader(s))');
    await shot(page, '05-channel-debug');
    await dialogButton(page, 'Ok');
    await api.call('io.simpleDesk.resetUniverse', { universeId: 0 }).catch(() => {});

    /* ================= 6. DMX Address tool ================= */
    console.log('DMX Address tool');
    await menu(page, 'DMX Address tool');
    await page.waitFor('!!document.querySelector("[data-role=address-tool]")', 4000);
    await page.click('[data-role=address-value] input').catch(() => {});
    await page.eval(`(function(){ const i = document.querySelector('[data-role=address-tool] input'); i.focus(); i.select(); return true; })()`);
    await page.s.send('Input.insertText', { text: '100' });
    await page.key('Enter');
    await sleep(200);
    const on = await page.eval('Array.from(document.querySelectorAll("[data-dip]")).filter(b => b.getAttribute("data-on") === "1").map(b => Number(b.getAttribute("data-dip"))).sort((a,b)=>a-b).join(",")');
    check(on === '2,5,6', 'address 100 = DIP switches 3, 6, 7 on (bits ' + on + ')');
    await clickSel(page, '[data-dip="0"]');
    check(await page.eval('document.querySelector("[data-role=address-tool] input").value') === '101', 'clicking switch 1 makes it 101');
    await clickSel(page, '[data-role=address-fliph]');
    await clickSel(page, '[data-dip-color=blue]');
    await shot(page, '06-address-tool');
    await dialogButton(page, 'Close');

    /* ================= 7. UI settings ================= */
    console.log('UI Settings');
    await clickSel(page, '[title="UI Settings"]');
    await page.waitFor('!!document.querySelector("[data-role=ui-settings]")', 4000);
    check(await until(() => page.eval('!!document.querySelector("[data-role=ui-engine]")'), 5000), 'engine settings (core.settings.get) are shown');
    await setInput(page, '[data-ui-color=bgMedium] input[type=color]', '#224466');
    await setInput(page, '[data-role=ui-scale]', '1.25');
    await sleep(200);
    const applied = await page.eval(`({ c: getComputedStyle(document.documentElement).getPropertyValue('--bg-medium').trim(), z: document.body.style.zoom })`);
    check(applied.c === '#224466' && applied.z === '1.25', 'colour + scale applied live (' + JSON.stringify(applied) + ')');
    const saved = path.join(OUT, 'qlcplusUiStyle.json');
    if (fs.existsSync(saved)) fs.unlinkSync(saved);
    await dialogButton(page, 'Save to file');
    await until(() => fs.existsSync(saved) && fs.statSync(saved).size > 0, 8000);
    const savedJson = fs.existsSync(saved) ? JSON.parse(fs.readFileSync(saved, 'utf8')) : null;
    check(savedJson && savedJson.colors.bgMedium === '#224466' && savedJson.sizes.scalingFactor === 1.25, 'Save to file writes the desktop qlcplusUiStyle.json shape');
    await dialogButton(page, 'Close');
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=sd');
    await connected(page);
    const persisted = await page.eval(`({ c: getComputedStyle(document.documentElement).getPropertyValue('--bg-medium').trim(), z: document.body.style.zoom,
      fit: Math.abs(document.querySelector('#root').firstElementChild.getBoundingClientRect().height * 1.25 - innerHeight) < 3 || Math.abs(document.querySelector('#root').firstElementChild.getBoundingClientRect().height - innerHeight) < 3 })`);
    check(persisted.c === '#224466' && persisted.z === '1.25', 'colour + scale survive a reload (' + JSON.stringify(persisted) + ')');
    check(await page.eval('document.documentElement.scrollHeight <= innerHeight + 2'), 'the zoomed page still fits the window (no vertical overflow)');
    await shot(page, '07-ui-settings-applied');
    await clickSel(page, '[title="UI Settings"]');
    await page.waitFor('!!document.querySelector("[data-role=ui-settings]")', 4000);
    await clickSel(page, '[data-role=ui-reset]');
    check(await page.eval(`getComputedStyle(document.documentElement).getPropertyValue('--bg-medium').trim() === '#333333' && !document.body.style.zoom`), 'Reset to defaults restores the tokens');
    await inputFile(page, '[data-role=ui-load-file]', 'qlcplusUiStyle.json', JSON.stringify(savedJson || {}));
    check(await until(() => page.eval(`getComputedStyle(document.documentElement).getPropertyValue('--bg-medium').trim() === '#224466'`), 3000), 'Load from file applies the saved file again');
    await clickSel(page, '[data-role=ui-reset]');
    await dialogButton(page, 'Close');

    /* ================= 8. keyboard shortcuts ================= */
    console.log('Keyboard shortcuts');
    const blackout = async () => (await api.call('io.blackout.get')).blackout;
    const b0 = await blackout();
    await key(page, 'b', 'KeyB', 66, CTRL);
    check(await until(async () => (await blackout()) !== b0, 3000), 'Ctrl+B toggles blackout (default binding)');
    check(await until(() => page.eval('(document.querySelector("[data-keycast]") || {}).textContent || ""'), 2000), 'key-cast toast: ' + await page.eval('(document.querySelector("[data-keycast]") || {}).innerText || ""'));
    await key(page, 'b', 'KeyB', 66, CTRL); // back
    await until(async () => (await blackout()) === b0, 3000);
    await menu(page, 'Keyboard shortcuts');
    await page.waitFor('!!document.querySelector("[data-role=shortcuts-editor]")', 4000);
    await clickSel(page, '[data-shortcut="io.blackoutToggle"] [data-role=shortcut-sequence]');
    await key(page, 'k', 'KeyK', 75, CTRL | ALT);
    check(await page.eval('document.querySelector("[data-shortcut=\\"io.blackoutToggle\\"] [data-role=shortcut-sequence]").textContent') === 'Ctrl+Alt+K', 'blackout rebound to Ctrl+Alt+K');
    /* collision: bind Save to Ctrl+Alt+K too -> warning, cancel */
    await clickSel(page, '[data-shortcut="app.save"] [data-role=shortcut-sequence]');
    await key(page, 'k', 'KeyK', 75, CTRL | ALT);
    check(await until(() => hasText(page, 'already assigned to "Toggle blackout"'), 2000), 'binding Save to the same keys warns about the collision');
    await dialogButton(page, 'Cancel');
    await shot(page, '08-shortcuts-editor');
    const exported = path.join(OUT, 'qlcplusShortcuts.json');
    if (fs.existsSync(exported)) fs.unlinkSync(exported);
    await dialogButton(page, 'Export');
    await until(() => fs.existsSync(exported) && fs.statSync(exported).size > 0, 8000);
    const exp = fs.existsSync(exported) ? JSON.parse(fs.readFileSync(exported, 'utf8')) : null;
    check(exp && exp['io.blackoutToggle'] === 'Ctrl+Alt+K' && Object.keys(exp).length === 1, 'Export writes the desktop override format ' + JSON.stringify(exp));
    await dialogButton(page, 'Close');
    await key(page, 'k', 'KeyK', 75, CTRL | ALT);
    check(await until(async () => (await blackout()) !== b0, 3000), 'Ctrl+Alt+K now toggles blackout');
    check(await until(() => page.eval('((document.querySelector("[data-keycast]") || {}).innerText || "").indexOf("Ctrl+Alt+K") !== -1'), 2000), 'the toast shows the new combination');
    await shot(page, '09-keycast');
    await key(page, 'k', 'KeyK', 75, CTRL | ALT);
    await until(async () => (await blackout()) === b0, 3000);
    await key(page, 'b', 'KeyB', 66, CTRL);
    await sleep(500);
    check((await blackout()) === b0, 'Ctrl+B no longer toggles blackout');
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=sd');
    await connected(page);
    check(await page.eval(`JSON.parse(localStorage.getItem('qlcplus.webui.shortcuts') || '{}')['io.blackoutToggle'] === 'Ctrl+Alt+K'`), 'the binding is persisted across a reload');
    await clickSel(page, '[title="Blackout"]');
    check(await until(() => page.eval('((document.querySelector("[data-keycast]") || {}).innerText || "").indexOf("Tip: shortcut") !== -1'), 2000), 'clicking the Blackout button shows the click hint');
    await until(async () => (await blackout()) !== b0, 3000);
    await clickSel(page, '[title="Blackout"]');
    await until(async () => (await blackout()) === b0, 3000);
    await menu(page, 'Keyboard shortcuts');
    await page.waitFor('!!document.querySelector("[data-role=shortcuts-editor]")', 4000);
    await clickSel(page, '[data-role=shortcuts-defaults]');
    await dialogButton(page, 'Ok');
    check(await page.eval('document.querySelector("[data-shortcut=\\"io.blackoutToggle\\"] [data-role=shortcut-sequence]").textContent') === 'Ctrl+B', 'Load Defaults restores Ctrl+B');
    await inputFile(page, '[data-role=shortcuts-import-file]', 'qlcplusShortcuts.json', JSON.stringify({ 'app.dmxDump': 'Ctrl+Alt+D' }));
    check(await until(() => page.eval('document.querySelector("[data-shortcut=\\"app.dmxDump\\"] [data-role=shortcut-sequence]").textContent === "Ctrl+Alt+D"'), 2000), 'Import applies a desktop-format file');
    await clickSel(page, '[data-role=shortcut-hints]');
    check(await page.eval(`localStorage.getItem('qlcplus.webui.shortcutHints') === 'false'`), '"Show shortcut hints" toggle persists');
    await clickSel(page, '[data-role=shortcut-hints]');
    await clickSel(page, '[data-role=shortcuts-defaults]');
    await dialogButton(page, 'Ok');
    await dialogButton(page, 'Close');
    await key(page, 'd', 'KeyD', 68, CTRL | SHIFT);
    check(await until(() => page.eval('!!document.querySelector("[data-role=dmx-dump]")'), 2000), 'Ctrl+Shift+D opens the DMX dump dialog');
    await dialogButton(page, 'Cancel');

    /* ================= 9. .qxf drop ================= */
    console.log('Fixture definition drop');
    const src = '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE FixtureDefinition>\n<FixtureDefinition xmlns="http://www.qlcplus.org/FixtureDefinition">\n'
      + ' <Creator><Name>Q Light Controller Plus</Name><Version>5.0.0</Version><Author>e2e</Author></Creator>\n'
      + ' <Manufacturer>E2E</Manufacturer>\n <Model>Dropped Par</Model>\n <Type>Color Changer</Type>\n'
      + ' <Channel Name="Dimmer"><Group Byte="0">Intensity</Group><Capability Min="0" Max="255">Intensity</Capability></Channel>\n'
      + ' <Channel Name="Red"><Group Byte="0">Intensity</Group><Colour>Red</Colour><Capability Min="0" Max="255">Red</Capability></Channel>\n'
      + ' <Mode Name="2 Channel">\n  <Channel Number="0">Dimmer</Channel>\n  <Channel Number="1">Red</Channel>\n </Mode>\n</FixtureDefinition>\n';
    const dest = path.join(USER_DIR, 'E2E-Dropped-Par.qxf');
    if (fs.existsSync(dest)) fs.unlinkSync(dest);
    await dropFile(page, 'E2E-Dropped-Par.qxf', Buffer.from(src, 'utf8').toString('base64'));
    check(await until(() => hasText(page, 'was added to the user fixture library'), 8000), 'the drop reports the import');
    await shot(page, '10-fixture-import');
    check(fs.existsSync(dest), 'E2E-Dropped-Par.qxf written to the sandbox user folder');
    const lib = await api.call('fixturedefs.list', { manufacturer: 'E2E' });
    const entry = (lib.entries || []).find(e => e.model === 'Dropped Par');
    check(!!entry && entry.isUser, 'fixturedefs.list knows E2E / Dropped Par as a user definition');
    await dialogButton(page, 'Open in Fixture Editor');
    check(await until(() => page.eval('!!document.querySelector("[data-fe=\\"screen\\"]")'), 8000), '"Open in Fixture Editor" switches to the editor');
    await sleep(800);
    for (const s of ((await api.call('fixturedefs.session.list')).sessions || [])) if (s.manufacturer === 'E2E') await api.call('fixturedefs.session.close', { sessionId: s.sessionId }).catch(() => {});
    if (entry) await api.call('fixturedefs.delete', { manufacturer: 'E2E', model: 'Dropped Par', baseRevision: entry.defRevision }).catch(e => console.log('    cleanup: ' + e.message));
    check(JSON.stringify(snapshotDir(REAL_DIR)) === JSON.stringify(realBefore), 'the real user fixture folder is untouched');

    /* ================= 10. console ================= */
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=fx');
    await connected(page);
    await sleep(800);
    await shot(page, '11-final');
    check(page.consoleErrors.length === 0, 'no console errors' + (page.consoleErrors.length ? ': ' + page.consoleErrors.slice(0, 5).join(' | ') : ''));
  } catch (e) {
    failures++;
    console.log('  FAIL exception: ' + (e && e.stack || e));
    await shot(page, 'zz-exception').catch(() => {});
  } finally {
    await b.close();
    api.close();
  }
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
})();
