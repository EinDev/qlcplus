/**
 * End-to-end driver for the "partial" rows of the Fixtures & Functions / palettes / 2D view parity
 * sections (docs/webui-parity.md): select every odd / Nth, lock / hide in the views, individual
 * head selection for the live tools and group assignment, arrange (align left, distribute, grid,
 * line, detect, face centre, centre, rotate), stage size / units / point of view, DMX-driven
 * position / rotation settings, the 2D background picture, Highlight, the colour and single-axis
 * tools, palettes (Position 3D / Shutter / Gobo / Zoom, rename, change value), the Sequence
 * bound-Scene picker, RGB Matrix image / font / offsets / typed script properties, fixture rename
 * with numbering, the Fixture Editor gobo picture picker and the initial point-of-view prompt.
 *
 *   .\dev-webui-sandbox.ps1 -Name partialsff -BuildDir .\build -WebUiRoot .\webui -ApiPort 9320 -WebUiPort 9321
 *   node webui/tools/e2e/partials-ff.js [--only select,heads,...]
 *   env: QLC_API (ws://127.0.0.1:9320/), QLC_WEB (http://localhost:9321/),
 *        QLC_SANDBOX (C:\qlcsandbox\partialsff: screenshots, saveAs target, test RGB script)
 *
 * The RGB section needs a test script with float / string properties (no bundled script has
 * those types). The driver writes it into the SANDBOX's RGBScripts folder (never the repo's or
 * the live install's); scripts are read at start-up, so on the very first run that section asks
 * for a sandbox relaunch. The "pov" section runs last: it starts a new, empty project.
 *
 * Every gesture is read back over a second raw API connection. Exit code 1 when a check fails or
 * the page logged a console error.
 */
const fs = require('fs');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const API = process.env.QLC_API || 'ws://127.0.0.1:9320/';
const WEB = process.env.QLC_WEB || 'http://localhost:9321/';
const SANDBOX = process.env.QLC_SANDBOX || 'C:\\qlcsandbox\\partialsff';
const SHOTS = path.join(SANDBOX, 'shots');
const onlyArg = process.argv.indexOf('--only');
const ONLY = onlyArg !== -1 ? process.argv[onlyArg + 1].split(',') : null;
const want = (s) => !ONLY || ONLY.indexOf(s) !== -1;

/* ---------------------------------------------------------------- raw API client */
class Api {
  constructor(url) { this.url = url; this.rev = 0; this.next = 1; this.pending = new Map(); }
  connect() {
    return new Promise((res, rej) => {
      this.ws = new WebSocket(this.url);
      this.ws.onerror = (e) => rej(new Error('API socket error ' + (e && e.message)));
      this.ws.onmessage = (m) => {
        const f = JSON.parse(m.data);
        if (f.type === 'response') {
          const p = this.pending.get(f.id); this.pending.delete(f.id);
          if (f.ok && f.result && f.result.docRevision != null) this.rev = f.result.docRevision;
          if (p) f.ok ? p.res(f.result) : p.rej(Object.assign(new Error(f.error.code + ': ' + f.error.message), f.error));
        } else if (f.type === 'event' && f.data && f.data.docRevision != null) this.rev = f.data.docRevision;
      };
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'partials-ff e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
    });
  }
  call(method, params = {}) {
    const id = 'e-' + (this.next++);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  /** document edit with the current revision */
  edit(method, params) { return this.call(method, Object.assign({ baseRevision: this.rev }, params)); }
  async monitorItem(fid, head = 0, linked = 0) { return (await this.call('fixtures.monitor.get')).items.find(i => i.fixtureId === String(fid) && i.headIndex === head && i.linkedIndex === linked); }
  close() { try { this.ws.close(); } catch (e) { } }
}

/* ---------------------------------------------------------------- checks */
const failures = [];
function check(cond, msg, extra) {
  if (cond) { console.log('  ok   ' + msg); return true; }
  failures.push(msg + (extra !== undefined ? ' -> ' + JSON.stringify(extra) : ''));
  console.log('  FAIL ' + msg + (extra !== undefined ? ' -> ' + JSON.stringify(extra) : ''));
  return false;
}
async function until(fn, what, timeout = 8000) {
  const t0 = Date.now();
  for (;;) {
    let v = null; try { v = await fn(); } catch (e) { }
    if (v) return v;
    if (Date.now() - t0 > timeout) throw new Error('timeout waiting for ' + what);
    await sleep(150);
  }
}
const soon = (fn, what, timeout) => until(fn, what, timeout).catch(() => null);
const near = (a, b, tol = 1) => Math.abs(a - b) <= tol;

/* ---------------------------------------------------------------- page helpers */
const CTRL = 2, ALT = 1;
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const byText = (tag, text, root) => `[...(${root ? q(root) : 'document'} || document).querySelectorAll(${JSON.stringify(tag)})].reverse().find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
const leafText = (text, root) => `[...(${root ? q(root) : 'document'} || document).querySelectorAll('*')].filter(e => e.children.length === 0 && (e.textContent || '').trim() === ${JSON.stringify(text)}).pop()`;
const byTitle = (prefix, root) => `[...(${root ? q(root) : 'document'} || document).querySelectorAll('[title]')].filter(e => e.title.indexOf(${JSON.stringify(prefix)}) === 0).pop()`;
async function clickFn(page, fnBody, what, opts) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'center', inline: 'center' }); })()`);
  await sleep(60);
  await page.click(new Function('return ' + fnBody), opts || {});
  await sleep(150);
}
const clickSel = (page, sel, opts) => clickFn(page, q(sel), sel, opts);
/** React-controlled <input>/<select>: native setter + the event React listens to. */
async function setInput(page, selectorFn, value, enter) {
  await page.eval(`(function(){ const el = (${selectorFn}); el.scrollIntoView({ block: 'center' }); el.focus();`
    + ' const proto = el.tagName === "SELECT" ? HTMLSelectElement.prototype : HTMLInputElement.prototype;'
    + ` Object.getOwnPropertyDescriptor(proto, "value").set.call(el, ${JSON.stringify(String(value))});`
    + ' el.dispatchEvent(new Event(el.tagName === "SELECT" ? "change" : "input", { bubbles: true })); })()');
  if (enter) await page.key('Enter');
  await sleep(200);
}
/** Design-system CustomSpinBox: its data-* attributes land on the <input>, which commits on change. */
async function spin(page, sel, value) { await setInput(page, q(sel), value, true); await page.eval('document.activeElement && document.activeElement.blur()'); await sleep(250); }
/** Design-system CustomComboBox (data-* on its wrapper): open it, pick the entry by label. */
async function pickCombo(page, sel, wanted) {
  await clickFn(page, `${q(sel)} && ${q(sel)}.querySelector(':scope > button')`, 'combo ' + sel);
  await sleep(200);
  await clickFn(page, `${q(sel)} && [...${q(sel)}.querySelectorAll(':scope > div button')].find(b => b.textContent.trim() === ${JSON.stringify(wanted)})`, 'combo entry ' + wanted);
  await sleep(250);
}
/** FF.Row whose first child reads `label`, inside `root`; returns an expression for `inner` in it. */
const rowPart = (label, inner, root) => `(function(){ const r = [...(${root ? q(root) : 'document'} || document).querySelectorAll('div')].filter(d => d.children.length >= 2 && (d.children[0].textContent || '').trim() === ${JSON.stringify(label)}).pop(); return r ? r.querySelectorAll(${JSON.stringify(inner)}) : []; })()`;
async function selectInTree(page, name, opts, search) {
  const searchOpen = await page.eval(`!![...document.querySelectorAll('input')].find(i => i.placeholder === 'Search…')`);
  if (!searchOpen) await clickFn(page, `[...document.querySelectorAll('[title="Search fixtures and functions"]')].pop()`, 'search button');
  await page.eval(`(function(){ const el = [...document.querySelectorAll('input')].find(i => i.placeholder === 'Search…');
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, ${JSON.stringify(search || name)});
    el.dispatchEvent(new Event('input', { bubbles: true })); el.dispatchEvent(new KeyboardEvent('keydown', { key: 'a', bubbles: true })); })()`);
  await sleep(500);
  await clickFn(page, leafText(name, '[data-ff-tree]'), 'tree node ' + name, opts);
  await sleep(300);
}
/* The right-hand rail toggles are clicked through the DOM: a synthetic CDP mouse click at their
   reported centre did not reach them in headless Chrome while the SidePanel was collapsed. */
const PANEL_PROBE = {
  'Fixture tools': `!!document.querySelector('[data-ff-tools-title]') || document.body.innerText.indexOf('Select one or more fixtures in the tree') !== -1`,
  'Palettes': `!![...document.querySelectorAll('*')].find(e => e.children.length === 0 && /^Palettes( \\(\\d+\\))?$/.test(e.textContent.trim()))`,
  'Fixture Groups': `!!document.querySelector('[title="Add a new fixture group"]')`
};
/** Open (or with open=false close) one of the right-hand panels, whatever state it is in. */
async function panel(page, title, open = true) {
  const sel = JSON.stringify('[title^="' + title + '"]');
  await page.waitFor(`!!document.querySelector(${sel})`, 5000);
  if (!!(await page.eval(PANEL_PROBE[title])) === open) return;
  await page.eval(`[...document.querySelectorAll(${sel})].pop().click()`);
  await page.waitFor(open ? PANEL_PROBE[title] : '!(' + PANEL_PROBE[title] + ')', 5000);
  await sleep(300);
}
const itemSel = (fid, head = 0, linked = 0) => `[data-ff-stage] g[data-item="${fid}:${head}:${linked}"]`;
const clickItem = (page, fid, opts) => clickFn(page, `document.querySelector('${itemSel(fid)} circle')`, 'stage item ' + fid, opts);
const toolBtn = (name) => `(function(){ const t = document.querySelector('[data-tool="${name}"]'); return t && (t.tagName === 'BUTTON' ? t : t.querySelector('button')); })()`;
const selectedOnStage = (page) => page.eval(`[...new Set([...document.querySelectorAll('[data-ff-stage] g[data-selected]')].map(g => g.getAttribute('data-fx-id')))]`);
async function view(page, id) { await clickFn(page, `document.querySelector('[data-ff-view-button="${id}"]')`, id + ' view'); await sleep(400); }
async function zoomIn(page, n) { for (let i = 0; i < n; i++) await clickFn(page, toolBtn('zoom-in'), 'zoom in'); }
/** A click on empty stage clears the selection (like the Qt view). */
async function clearStage(page) {
  const empty = await page.eval(`(function(){ const svg = document.querySelector('[data-ff-stage]'), wrap = svg.parentElement.getBoundingClientRect();
    for (let y = wrap.top + 20; y < wrap.bottom - 20; y += 23) for (let x = wrap.left + 20; x < wrap.right - 20; x += 23) {
      const el = document.elementFromPoint(x, y); if (el && svg.contains(el) && !el.closest('g[data-item]')) return { x, y }; }
    return null; })()`);
  await page.mouse('mouseMoved', empty.x, empty.y); await page.mouse('mousePressed', empty.x, empty.y); await page.mouse('mouseReleased', empty.x, empty.y);
  await soon(async () => (await selectedOnStage(page)).length === 0, 'cleared');
}
async function selectOnStage(page, ids) {
  await clearStage(page);
  await clickItem(page, ids[0]);
  for (const fid of ids.slice(1)) await clickItem(page, fid, { modifiers: CTRL });
  return soon(async () => { const s = await selectedOnStage(page); return s.length === ids.length && ids.every(id => s.indexOf(id) !== -1) ? s : null; }, ids.length + ' selected');
}
async function settings(page, open) {
  const isOpen = await page.eval(`!!document.querySelector('[data-ff-2d-settings]')`);
  if (isOpen !== open) await clickFn(page, toolBtn('settings'), 'settings');
  if (open) await page.waitFor(`!!document.querySelector('[data-ff-2d-settings]')`, 5000);
}
async function shot(page, name) { const f = path.join(SHOTS, 'pff-' + name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }
const dialogBtn = (text) => `[...document.querySelectorAll('button')].filter(b => b.textContent.trim() === ${JSON.stringify(text)}).pop()`;

/* Test RGB script with one float and one string property, for the sandbox's RGBScripts folder. */
const TEST_SCRIPT = `// E2E test script (webui/tools/e2e/partials-ff.js): one float and one string property.
var testAlgo;
(function () {
  var algo = new Object;
  algo.apiVersion = 2;
  algo.name = "E2E Typed Props";
  algo.author = "e2e";
  algo.acceptColors = 1;
  algo.properties = new Array();
  algo.gain = 1.5;
  algo.properties.push("name:gain|type:float|display:Gain|write:setGain|read:getGain");
  algo.setGain = function (v) { algo.gain = parseFloat(v); };
  algo.getGain = function () { return algo.gain; };
  algo.label = "hello";
  algo.properties.push("name:label|type:string|display:Label|write:setLabel|read:getLabel");
  algo.setLabel = function (v) { algo.label = v; };
  algo.getLabel = function () { return algo.label; };
  algo.rgbMap = function (width, height, rgb, step) {
    var map = new Array(height);
    for (var y = 0; y < height; y++) {
      map[y] = new Array(width);
      for (var x = 0; x < width; x++) map[y][x] = (x === step % width) ? rgb : 0;
    }
    return map;
  };
  algo.rgbMapStepCount = function (width, height) { return Math.max(1, width); };
  testAlgo = algo;
  return algo;
})();
`;

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const scriptDir = path.join(SANDBOX, 'RGBScripts');
  if (!/qlcsandbox/i.test(SANDBOX)) { console.log('refusing: QLC_SANDBOX must be a C:\\qlcsandbox\\<name> folder'); process.exit(2); }
  if (fs.existsSync(scriptDir) && !fs.existsSync(path.join(scriptDir, 'e2e-typed-props.js'))) {
    fs.writeFileSync(path.join(scriptDir, 'e2e-typed-props.js'), TEST_SCRIPT);
    console.log('wrote the test RGB script into ' + scriptDir + ' (relaunch the sandbox before the rgb section can use it)');
  }
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev);
  const fixtures = (await api.call('fixtures.list')).fixtures;
  const gobos = fixtures.filter(f => f.manufacturer === 'Gobo Spot').sort((a, b) => a.universe - b.universe || a.address - b.address);
  const four = gobos.slice(0, 4).map(f => f.id);
  const mon0 = await api.call('fixtures.monitor.get');
  const stage0 = mon0.stage;
  const place = (list) => api.edit('fixtures.monitor.setPlacement', { items: list.map(([id, x, z]) => ({ fixtureId: id, position: { x, y: 0, z }, rotation: { x: 0, y: 0, z: 0 }, locked: false, hidden: false })) });
  const spread = () => place(four.map((id, i) => [id, 20000 + i * 1500, 20000]));
  const items4 = async () => { const m = await api.call('fixtures.monitor.get'); return four.map(id => m.items.find(i => i.fixtureId === id && i.headIndex === 0 && i.linkedIndex === 0)); };
  await api.edit('fixtures.monitor.setStage', { pointOfView: 'TopView', gridUnits: 'Meters', gridSize: { x: 45, y: 3, z: 45 }, backgroundImage: '' });
  await spread();

  const browser = await launch({ headless: true });
  const page = await browser.open(WEB + '?ctx=fx', { width: 1700, height: 1000 });
  try {
    await page.waitFor(`!!document.querySelector('[data-ff-view-button="2d"]')`, 30000);
    await view(page, '2d');
    await page.waitFor(`document.querySelectorAll('[data-ff-stage] g[data-fx-id]').length > 0`, 15000);
    await zoomIn(page, 6);

    /* ================= select every odd / Nth ================= */
    if (want('select')) {
      console.log('\n[select odd / Nth]');
      check(!!await selectOnStage(page, four), 'four Gobo Spots selected on the stage');
      await clickFn(page, toolBtn('select-odd'), 'select odd');
      let sel = await soon(async () => { const s = await selectedOnStage(page); return s.length === 2 ? s : null; }, 'odd');
      check(sel && sel.indexOf(four[0]) !== -1 && sel.indexOf(four[2]) !== -1, 'select odd keeps the 1st and 3rd', sel || await selectedOnStage(page));
      await selectOnStage(page, four);
      await clickFn(page, toolBtn('select-nth'), 'select Nth');
      await page.waitFor(`!!document.querySelector('input[title="Keep every Nth fixture"]')`, 5000);
      await shot(page, 'nth-dialog');
      await setInput(page, q('input[title="Keep every Nth fixture"]'), 3);
      await clickFn(page, dialogBtn('OK'), 'OK');
      sel = await soon(async () => { const s = await selectedOnStage(page); return s.length === 2 ? s : null; }, 'every 3rd');
      check(sel && sel.indexOf(four[0]) !== -1 && sel.indexOf(four[3]) !== -1, 'Enter a number 3: keeps the 1st and 4th', sel || await selectedOnStage(page));
    }

    /* ================= lock / hide ================= */
    if (want('lockhide')) {
      console.log('\n[lock / hide]');
      const target = four[1];
      await spread(); await sleep(400);
      await clearStage(page); await clickItem(page, target);
      await view(page, 'list');
      const flag = (name) => `document.querySelector('[data-ff-placement] [data-item="${target}:0:0"] [data-flag="${name}"] button')`;
      await page.waitFor(`!!${flag('locked')}`, 10000);
      await clickFn(page, flag('locked'), 'Lock position');
      check(!!await soon(async () => (await api.monitorItem(target)).flags.locked, 'locked'), 'Lock position sets flags.locked');
      await view(page, '2d');
      await page.waitFor(`!!document.querySelector('${itemSel(target)}')`, 8000);
      const pre = await api.monitorItem(target);
      const r = await page.rectOf(new Function(`return document.querySelector('${itemSel(target)} circle')`));
      await page.drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 90, r.y + r.h / 2 + 40, 10);
      await sleep(800);
      const post = await api.monitorItem(target);
      check(near(post.position.x, pre.position.x, 0.5) && near(post.position.z, pre.position.z, 0.5), 'dragging a locked fixture does not move it', { pre: pre.position, post: post.position });
      await clickItem(page, four[2], { modifiers: CTRL });
      await clickFn(page, toolBtn('center'), 'move to centre');
      const status = await soon(async () => { const t = await page.eval(`document.querySelector('[data-status]').textContent`); return /locked skipped/.test(t) ? t : null; }, 'skipped');
      check(!!status, 'Move to centre reports the locked fixture as skipped (' + status + ')');
      const post2 = await api.monitorItem(target);
      check(near(post2.position.x, pre.position.x, 0.5), 'the locked fixture stayed put while the other one moved');
      await clearStage(page); await clickItem(page, target);
      await view(page, 'list');
      await page.waitFor(`!!${flag('locked')}`, 10000);
      await clickFn(page, flag('locked'), 'Unlock');
      check(!!await soon(async () => !(await api.monitorItem(target)).flags.locked, 'unlocked'), 'Unlock clears flags.locked');
      await clickFn(page, flag('hidden'), 'Hidden in views');
      check(!!await soon(async () => (await api.monitorItem(target)).flags.hidden, 'hidden'), 'Hidden in views sets flags.hidden');
      await shot(page, 'placement-flags');
      await view(page, '2d');
      await page.waitFor(`document.querySelectorAll('[data-ff-stage] g[data-fx-id]').length > 0`, 8000);
      check(await page.eval(`!document.querySelector('${itemSel(target)}')`), 'a hidden fixture is not drawn in the 2D view');
      await view(page, 'list');
      await page.waitFor(`!!${flag('hidden')}`, 10000);
      await clickFn(page, flag('hidden'), 'Show again');
      check(!!await soon(async () => !(await api.monitorItem(target)).flags.hidden, 'shown'), 'unchecking Hidden shows it again');
      await view(page, '2d');
      check(!!await soon(() => page.eval(`!!document.querySelector('${itemSel(target)}')`), 'drawn again'), 'the fixture is drawn again');
    }

    /* ================= multi-head: individual heads ================= */
    if (want('heads')) {
      console.log('\n[individual heads]');
      const strobe = mon0.items.find(i => (i.heads || 1) >= 6 && i.headIndex === 0 && i.linkedIndex === 0);
      check(!!strobe, 'SF3 has a multi-head fixture (' + (strobe && strobe.name) + ', ' + (strobe && strobe.heads) + ' heads)');
      const fid = strobe.fixtureId;
      await place([[fid, 30000, 30000]]);
      await sleep(500);
      await zoomIn(page, 4);
      const circle = (h) => `document.querySelector('${itemSel(fid)} circle[data-head="${h}"]')`;
      await clickFn(page, circle(0), 'head 0 (plain click)');
      await clickFn(page, circle(2), 'alt-click head 3', { modifiers: ALT });
      await clickFn(page, circle(5), 'alt-click head 6', { modifiers: ALT });
      const picked = await soon(async () => { const h = await page.eval(`[...document.querySelectorAll('${itemSel(fid)} circle[data-head-sel]')].map(c => c.getAttribute('data-head'))`); return h.length === 2 ? h : null; }, 'two heads');
      check(picked && picked.join(',') === '2,5', 'alt-click picks heads 3 and 6 of ' + strobe.name, picked);
      await shot(page, 'heads-2d');
      await panel(page, 'Fixture tools');
      const title = await soon(async () => { const t = await page.eval(`(document.querySelector('[data-ff-tools-title]') || {}).textContent || ''`); return /2 heads/.test(t) ? t : null; }, 'tools title');
      check(!!title, 'the tools panel works on 2 heads (' + title + ')');
      const detail = await api.call('fixtures.get', { fixtureId: fid });
      const dimIdx = (h) => strobe.headChannels[h].find(c => detail.channelList[c].group === 'Intensity');
      const dmx = async () => (await api.call('io.dmx.universe.get', { universeId: detail.universe })).values;
      const before = await dmx();
      await clickFn(page, byText('button', 'Full'), 'Intensity Full');
      const after = await soon(async () => { const v = await dmx(); return v[detail.address + dimIdx(2)] === 255 && v[detail.address + dimIdx(5)] === 255 ? v : null; }, 'head dimmers');
      check(!!after, 'Intensity Full drives the dimmers of heads 3 and 6', after && [after[detail.address + dimIdx(2)], after[detail.address + dimIdx(5)]]);
      const others = [0, 1, 3, 4, 6, 7].filter(h => h < strobe.heads).map(h => (after || before)[detail.address + dimIdx(h)]);
      check(after && others.every((v, i) => v === before[detail.address + dimIdx([0, 1, 3, 4, 6, 7][i])]), 'the other heads\' dimmers are untouched', others);
      await shot(page, 'heads-tools');
      await clickFn(page, byText('button', 'Release fixtures'), 'Release fixtures');
      // group assignment of the two heads
      await panel(page, 'Fixture Groups');
      const groupsBefore = (await api.call('fixtures.group.list')).groups.map(g => String(g.id));
      await clickFn(page, byTitle('Add a new fixture group'), 'new group');
      await setInput(page, `[...document.querySelectorAll('input')].find(i => i.value === 'New group')`, 'E2E Heads');
      await clickFn(page, dialogBtn('Create'), 'Create');
      const grp = await soon(async () => (await api.call('fixtures.group.list')).groups.find(g => groupsBefore.indexOf(String(g.id)) === -1 && g.name === 'E2E Heads'), 'group');
      check(!!grp, 'group "E2E Heads" created');
      await page.waitFor(`/Add 2 selected \\(heads\\)/.test((document.querySelector('[data-group-add]') || {}).textContent || '')`, 5000).catch(() => {});
      await clickFn(page, `document.querySelector('[data-group-add] button')`, 'Add 2 selected (heads)');
      const gd = await soon(async () => { const d = await api.call('fixtures.group.get', { groupId: String(grp.id) }); return d.heads.length === 2 ? d : null; }, 'heads assigned');
      check(gd && gd.heads.every(h => String(h.fixtureId) === fid) && gd.heads.map(h => h.headIndex).sort().join(',') === '2,5', 'the group holds exactly heads 3 and 6', gd && gd.heads);
      await shot(page, 'heads-group');
      await panel(page, 'Fixture Groups', false);
      if (grp) await api.edit('fixtures.group.delete', { groupId: String(grp.id) });
    }

    /* ================= arrange ================= */
    if (want('arrange')) {
      console.log('\n[align / distribute / arrange]');
      await spread(); await sleep(400);
      check(!!await selectOnStage(page, four), 'four Gobo Spots selected');
      // align left: every X equals the first selected's X (top view: left edge = x)
      await clickFn(page, toolBtn('align-left'), 'align left');
      let its = await soon(async () => { const a = await items4(); return a.every(i => near(i.position.x, a[0].position.x, 0.5)) ? a : null; }, 'align left');
      check(!!its, 'align left: equal X for all 4', its && its.map(i => i.position.x));
      // distribute horizontally: uneven X, equal gaps afterwards
      await place(four.map((id, i) => [id, [20000, 21000, 24000, 26500][i], 20000])); await sleep(400);
      await clickFn(page, toolBtn('distribute-h'), 'distribute horizontally');
      its = await soon(async () => { const x = (await items4()).map(i => i.position.x).sort((a, b) => a - b); const g = x.slice(1).map((v, i) => v - x[i]); return g.every(d => near(d, g[0], 1)) ? x : null; }, 'distribute h');
      check(!!its, 'distribute horizontally: equal gaps', its);
      await place(four.map((id, i) => [id, 20000 + i * 1500, [20000, 20500, 23000, 26000][i]])); await sleep(400);
      await clickFn(page, toolBtn('distribute-v'), 'distribute vertically');
      its = await soon(async () => { const z = (await items4()).map(i => i.position.z).sort((a, b) => a - b); const g = z.slice(1).map((v, i) => v - z[i]); return g.every(d => near(d, g[0], 1)) ? z : null; }, 'distribute v');
      check(!!its, 'distribute vertically: equal gaps', its);
      await shot(page, 'distributed');
      // grid 2 columns
      await spread(); await sleep(400);
      await clickFn(page, toolBtn('arrange'), 'arrange');
      await page.waitFor(`!!document.querySelector('[data-ff-arrange]')`, 5000);
      await clickFn(page, byText('button', 'Grid', '[data-ff-arrange]'), 'Grid');
      await setInput(page, q('[data-ff-arrange] input[title="Width"]'), 3000);
      await setInput(page, q('[data-ff-arrange] input[title="Height"]'), 2000);
      await setInput(page, q('[data-ff-arrange] input[title="Columns (0 = auto)"]'), 2);
      await shot(page, 'arrange-grid');
      await clickFn(page, dialogBtn('Apply'), 'Apply');
      its = await soon(async () => { const a = await items4(); const xs = new Set(a.map(i => Math.round(i.position.x))), zs = new Set(a.map(i => Math.round(i.position.z))); return xs.size === 2 && zs.size === 2 ? a : null; }, 'grid');
      check(!!its, 'grid with 2 columns: two distinct X and two distinct Z', its && its.map(i => [Math.round(i.position.x), Math.round(i.position.z)]));
      // line 4.5 m at 0 deg
      await clickFn(page, toolBtn('arrange'), 'arrange');
      await page.waitFor(`!!document.querySelector('[data-ff-arrange]')`, 5000);
      await clickFn(page, byText('button', 'Line', '[data-ff-arrange]'), 'Line');
      await setInput(page, q('[data-ff-arrange] input[title="Length"]'), 4500);
      await setInput(page, q('[data-ff-arrange] input[title="Angle"]'), 0);
      await clickFn(page, dialogBtn('Apply'), 'Apply');
      const keys = four.map(id => ({ fixtureId: id }));
      its = await soon(async () => { const a = await items4(); return a.every(i => near(i.position.z, a[0].position.z, 1)) ? a : null; }, 'line');
      const det = await api.call('fixtures.monitor.detectArrangement', { items: keys });
      check(!!its && near(det.lineLength, 4500, 60), 'line 4500 mm: one Z, detected line length ' + Math.round(det.lineLength), its && its.map(i => Math.round(i.position.x)));
      // detect from placement: after a circle, the dialog picks the diameter up
      await clickFn(page, toolBtn('arrange'), 'arrange');
      await page.waitFor(`!!document.querySelector('[data-ff-arrange]')`, 5000);
      await clickFn(page, byText('button', 'Circle', '[data-ff-arrange]'), 'Circle');
      await setInput(page, q('[data-ff-arrange] input[title="Diameter"]'), 3000);
      await clickFn(page, `document.querySelector('[data-ff-arrange] [data-flag="face"] button')`, 'Face centre');
      await clickFn(page, dialogBtn('Apply'), 'Apply');
      its = await soon(async () => { const a = await items4(); return new Set(a.map(i => Math.round(i.rotation.y))).size === 4 ? a : null; }, 'face');
      check(!!its, 'circle + Face centre: every fixture gets its own Y rotation', its && its.map(i => Math.round(i.rotation.y)));
      const det2 = await api.call('fixtures.monitor.detectArrangement', { items: keys });
      await clickFn(page, toolBtn('arrange'), 'arrange');
      await page.waitFor(`!!document.querySelector('[data-ff-arrange]')`, 5000);
      await setInput(page, q('[data-ff-arrange] input[title="Diameter"]'), 1234);
      await clickFn(page, `document.querySelector('[data-ff-arrange] [data-flag="detect"] button')`, 'Detect from placement');
      const detected = await soon(async () => { const v = Number(await page.eval(`document.querySelector('[data-ff-arrange] input[title="Diameter"]').value`)); return v !== 1234 ? v : null; }, 'detected');
      check(detected && near(detected, det2.circleDiameter, 1), 'Detect from placement fills the diameter (' + detected + ' / server ' + Math.round(det2.circleDiameter) + ')');
      await shot(page, 'arrange-detect');
      await clickFn(page, dialogBtn('Cancel'), 'Cancel');
      // rotate 90 deg around the centroid
      const pre = await items4();
      const c0 = { x: pre.reduce((s, i) => s + i.position.x, 0) / 4, z: pre.reduce((s, i) => s + i.position.z, 0) / 4 };
      await clickFn(page, toolBtn('rotate'), 'rotate');
      await page.waitFor(`!!document.querySelector('input[title="Angle"]')`, 5000);
      await setInput(page, q('input[title="Angle"]'), 90);
      await clickFn(page, dialogBtn('OK'), 'OK');
      its = await soon(async () => { const a = await items4(); return a.some((i, k) => !near(i.position.x, pre[k].position.x, 5)) ? a : null; }, 'rotated');
      const c1 = its && { x: its.reduce((s, i) => s + i.position.x, 0) / 4, z: its.reduce((s, i) => s + i.position.z, 0) / 4 };
      check(its && near(c1.x, c0.x, 5) && near(c1.z, c0.z, 5) && its.every((i, k) => near(Math.hypot(i.position.x - c1.x, i.position.z - c1.z), Math.hypot(pre[k].position.x - c0.x, pre[k].position.z - c0.z), 5)),
        'rotate 90: same centroid, same distances from it', its && its.map(i => [Math.round(i.position.x), Math.round(i.position.z)]));
      // move to the stage centre
      await clickFn(page, toolBtn('center'), 'move to centre');
      const gc = stage0.gridCenter;
      its = await soon(async () => { const a = await items4(); const cx = a.reduce((s, i) => s + i.position.x, 0) / 4, cz = a.reduce((s, i) => s + i.position.z, 0) / 4; return near(cx, gc.x, 400) && near(cz, gc.z, 400) ? a : null; }, 'centre');
      check(!!its, 'move to centre: the group sits at the stage centre ' + JSON.stringify(gc), its && its.map(i => [Math.round(i.position.x), Math.round(i.position.z)]));
      await shot(page, 'arranged');
    }

    /* ================= stage size / units / point of view ================= */
    if (want('stage')) {
      console.log('\n[stage settings]');
      await settings(page, true);
      await pickCombo(page, '[data-stage="units"]', 'Feet');
      let st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return s.gridUnits === 'Feet' ? s : null; }, 'feet');
      check(st && near(st.gridSize.x, 45 * 3.28084, 0.1), 'units Feet converts the size (' + (st && st.gridSize.x) + ' ft)');
      await spin(page, '[data-stage-size="x"]', 150);
      st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return near(s.gridSize.x, 150, 0.01) ? s : null; }, 'width');
      check(!!st, 'width 150 ft');
      await spin(page, '[data-stage-size="z"]', 120);
      st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return near(s.gridSize.z, 120, 0.01) ? s : null; }, 'depth');
      check(!!st, 'depth 120 ft');
      await shot(page, 'stage-feet');
      await pickCombo(page, '[data-stage="units"]', 'Meters');
      st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return s.gridUnits === 'Meters' ? s : null; }, 'meters');
      check(st && near(st.gridSize.x, 150 / 3.28084, 0.1), 'back to Meters (' + (st && st.gridSize.x) + ' m)');
      await pickCombo(page, '[data-stage="pov"]', 'Front view');
      st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return s.pointOfView === 'FrontView' ? s : null; }, 'front');
      check(!!st, 'point of view Front view');
      await sleep(500);
      await shot(page, 'pov-front');
      await pickCombo(page, '[data-stage="pov"]', 'Top view');
      check(!!await soon(async () => (await api.call('fixtures.monitor.get')).stage.pointOfView === 'TopView', 'top'), 'point of view back to Top view');
      await api.edit('fixtures.monitor.setStage', { gridUnits: 'Meters', gridSize: stage0.gridSize });
      await settings(page, false);
    }

    /* ================= DMX-driven position / rotation ================= */
    if (want('dmxt')) {
      console.log('\n[DMX position / rotation]');
      const drone = fixtures.find(f => f.model === 'FX Drone');
      check(!!drone, 'SF3 has an FX Drone (Position X/Y/Z channels)');
      await selectInTree(page, drone.name);
      await view(page, '2d');
      await settings(page, true);
      await api.edit('fixtures.monitor.setPlacement', { items: [{ fixtureId: drone.id, invertPositionX: false, invertRotationZ: false, rotationScale: 1, positionRange: 800 }] });
      await page.waitFor(`!!document.querySelector('[data-ff-2d-settings] [data-ff-dmx-transform]')`, 8000);
      await sleep(500);
      await clickFn(page, `document.querySelector('[data-ff-2d-settings] [data-ff-dmx-transform] [data-flag="invertPositionX"] button')`, 'Invert Position X');
      console.log('  after 1st click: ' + JSON.stringify((await api.monitorItem(drone.id)).flags));
      await clickFn(page, `document.querySelector('[data-ff-2d-settings] [data-ff-dmx-transform] [data-flag="invertRotationZ"] button')`, 'Invert Rotation Z');
      let it = await soon(async () => { const i = await api.monitorItem(drone.id); return i.flags.invertPositionX && i.flags.invertRotationZ ? i : null; }, 'flags');
      check(!!it, 'Invert Position X + Invert Rotation Z land on the drone', it ? it.flags : (await api.monitorItem(drone.id)).flags);
      await spin(page, '[data-ff-2d-settings] [data-dmx="rotationScale"]', 250);
      await spin(page, '[data-ff-2d-settings] [data-dmx="positionRange"]', 1200);
      it = await soon(async () => { const i = await api.monitorItem(drone.id); return near(i.rotationScale, 2.5, 0.001) && near(i.positionRange, 1200, 0.01) ? i : null; }, 'scale');
      check(!!it, 'rotation scale 250% and position range 1200 m', it && [it.rotationScale, it.positionRange]);
      await shot(page, 'dmx-transform');
      await view(page, 'list');
      await page.waitFor(`!!document.querySelector('[data-ff-placement] [data-ff-dmx-transform]')`, 8000);
      await clickFn(page, `document.querySelector('[data-ff-placement] [data-ff-dmx-transform] [data-flag="invertPositionX"] button')`, 'Invert Position X off (properties)');
      it = await soon(async () => { const i = await api.monitorItem(drone.id); return !i.flags.invertPositionX ? i : null; }, 'flag off');
      check(!!it, 'the fixture properties panel shows the same settings and toggles them');
      await selectInTree(page, gobos[0].name);
      await page.waitFor(`!!document.querySelector('[data-ff-placement]')`, 8000);
      await sleep(600);
      check(await page.eval(`!document.querySelector('[data-ff-placement] [data-ff-dmx-transform]')`), 'no DMX Position/Rotation settings for a fixture without such channels');
      await view(page, '2d');
      await settings(page, false);
    }

    /* ================= background picture ================= */
    if (want('background')) {
      console.log('\n[background picture]');
      await settings(page, true);
      await clickFn(page, `[...document.querySelectorAll('[data-ff-background] button')].find(b => b.textContent.trim() === 'Pick…')`, 'Pick…');
      await page.waitFor(`!!document.querySelector('.qlc-file-browser')`, 5000);
      await clickFn(page, `[...document.querySelectorAll('.qlc-file-browser div[title]')].find(d => /[\\\\/]Gobos$/.test(d.title))`, 'Gobos place');
      await clickFn(page, leafText('Others', '.qlc-fb-list'), 'Others');
      await clickFn(page, dialogBtn('Open'), 'Open folder');
      await page.waitFor(`!![...document.querySelectorAll('.qlc-fb-list *')].find(e => e.children.length === 0 && e.textContent.trim() === 'gobo00002.png')`, 5000);
      await clickFn(page, leafText('gobo00002.png', '.qlc-fb-list'), 'gobo00002.png');
      await shot(page, 'background-browser');
      await clickFn(page, dialogBtn('Open'), 'Open');
      const st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return /gobo00002\.png$/.test(s.backgroundImage) ? s : null; }, 'bg');
      check(!!st, 'the picked picture is the stage background (' + (st && st.backgroundImage) + ')');
      const drawn = await soon(() => page.eval(`(function(){ const i = document.querySelector('[data-ff-bg-image]'); return i && /^data:image\\/png;base64,/.test(i.getAttribute('href')) ? i.getAttribute('href').length : 0; })()`), 'drawn');
      check(!!drawn, 'the 2D view draws it (fixtures.monitor.getBackground, ' + drawn + ' chars)');
      await clickFn(page, toolBtn('zoom-fit'), 'fit');
      await sleep(500);
      await shot(page, 'background');
      await clickFn(page, `[...document.querySelectorAll('[data-ff-background] button')].find(b => b.textContent.trim() === 'Reset')`, 'Reset');
      check(!!await soon(async () => (await api.call('fixtures.monitor.get')).stage.backgroundImage === '', 'reset'), 'Reset clears it');
      check(!!await soon(() => page.eval(`!document.querySelector('[data-ff-bg-image]')`), 'gone'), 'and the view stops drawing it');
      await settings(page, false);
    }

    /* ================= Highlight, colour tool, single-axis tool ================= */
    if (want('tools')) {
      console.log('\n[Highlight / colour / single axis]');
      const bar = fixtures.find(f => f.model === 'Tilt Bar');
      const d = await api.call('fixtures.get', { fixtureId: bar.id });
      const idx = (g, c) => d.channelList.find(ch => ch.group === g && (c === undefined || ch.colour === c)).index;
      const dim = idx('Intensity', undefined), red = idx('Intensity', 'Red'), green = idx('Intensity', 'Green'), blue = idx('Intensity', 'Blue'), tilt = idx('Tilt');
      const dmx = async () => { const v = (await api.call('io.dmx.universe.get', { universeId: d.universe })).values; return (i) => v[d.address + i]; };
      await selectInTree(page, bar.name);
      await panel(page, 'Fixture tools');
      await page.waitFor(`/1 fixture/.test((document.querySelector('[data-ff-tools-title]') || {}).textContent || '')`, 8000);
      const before = await dmx();
      await clickFn(page, `document.querySelector('[data-ff-highlight] button')`, 'Highlight');
      let v = await soon(async () => { const g = await dmx(); return g(dim) === 255 && g(red) === 255 && g(green) === 255 && g(blue) === 255 ? g : null; }, 'highlight');
      check(!!v, 'Highlight: full intensity + white on ' + bar.name);
      await shot(page, 'highlight');
      await clickFn(page, `document.querySelector('[data-ff-highlight] button')`, 'Highlight off');
      v = await soon(async () => { const g = await dmx(); return g(dim) === before(dim) && g(red) === before(red) ? g : null; }, 'released');
      check(!!v, 'Highlight off releases exactly those channels (back to ' + before(dim) + ')');
      // colour tool: basic palette and typed hex (full picker)
      await clickFn(page, `[...document.querySelectorAll('img[alt="red"]')].pop()`, 'basic colour red');
      v = await soon(async () => { const g = await dmx(); return g(red) === 255 && g(green) === 0 && g(blue) === 0 ? g : null; }, 'red');
      check(!!v, 'basic colour red writes 255 / 0 / 0');
      await setInput(page, `[...document.querySelectorAll('input[title^="Hex colour"]')].pop()`, '#00ff80');
      v = await soon(async () => { const g = await dmx(); return g(red) === 0 && g(green) === 255 && g(blue) === 128 ? g : null; }, 'hex');
      check(!!v, 'typed hex #00ff80 writes 0 / 255 / 128');
      await page.eval('document.activeElement && document.activeElement.blur()');
      // single-axis tool: the Tilt Bar has Tilt but no Pan
      check(await page.eval(`!!document.querySelector('[data-single-axis="tilt"]') && !document.querySelector('[data-single-axis="pan"]')`), 'a tilt-only fixture gets the single-axis (tilt) tool');
      const half = await page.eval(`[...document.querySelectorAll('[data-single-axis="tilt"] button')].map(b => b.textContent.trim()).filter(t => /°$/.test(t))`);
      await clickFn(page, `[...document.querySelectorAll('[data-single-axis="tilt"] button')].find(b => b.textContent.trim() === ${JSON.stringify(half[2])})`, 'tilt ' + half[2]);
      v = await soon(async () => { const g = await dmx(); return near(g(tilt), 127.5, 1) ? g : null; }, 'tilt');
      check(!!v, 'single-axis ' + half[2] + ' writes tilt ~127 (' + (v ? v(tilt) : (await dmx())(tilt)) + ')');
      await shot(page, 'single-axis');
      await clickFn(page, byText('button', 'Release fixtures'), 'Release fixtures');
    }

    /* ================= palettes ================= */
    if (want('palettes')) {
      console.log('\n[palettes]');
      /* setup: drop palettes an earlier run against the same sandbox left behind */
      for (const old of (await api.call('palette.list')).palettes.filter(x => /^E2E /.test(x.name))) await api.edit('palette.delete', { paletteId: old.id });
      await panel(page, 'Palettes');
      const create = async (typeLabel, name, fill) => {
        await clickFn(page, byTitle('Create a palette'), 'create palette');
        await page.waitFor(`!!document.querySelector('[data-e2e=palette-create]')`, 5000);
        const typeRow = rowPart('Type', 'button', '[data-e2e=palette-create]');
        await clickFn(page, `${typeRow}[0]`, 'type combo');
        await clickFn(page, `[...(${rowPart('Type', 'button', '[data-e2e=palette-create]')})].find(b => b.textContent.trim() === ${JSON.stringify(typeLabel)})`, 'type ' + typeLabel);
        await setInput(page, q('[data-e2e=palette-name]'), name);
        await fill();
        await shot(page, 'palette-' + typeLabel.replace(/\W/g, ''));
        await clickFn(page, dialogBtn('Create'), 'Create');
        const p = await soon(async () => (await api.call('palette.list')).palettes.find(x => x.name === name), name);
        return p ? api.call('palette.get', { paletteId: p.id }) : null;
      };
      const spinRow = async (label, value) => { await setInput(page, `${rowPart(label, 'input', '[data-e2e=palette-create]')}[0]`, value, true); };
      let p = await create('Position 3D', 'E2E Pos3D', async () => { await spinRow('X', 1000); await spinRow('Y', 2000); await spinRow('Z', -500); });
      check(p && p.type === 'Position3D' && p.values.map(Number).join(',') === '1000,2000,-500', 'Position 3D palette 1000 / 2000 / -500 mm', p && p.values);
      p = await create('Shutter', 'E2E Shutter', async () => {
        await clickFn(page, `${rowPart('Effect', 'button', '[data-e2e=palette-create]')}[0]`, 'effect combo');
        await clickFn(page, `[...(${rowPart('Effect', 'button', '[data-e2e=palette-create]')})].find(b => b.textContent.trim() === 'Strobe random')`, 'Strobe random');
        await spinRow('Amount', 60);
      });
      check(p && p.type === 'Shutter' && Number(p.values[0]) === 11 && Number(p.values[1]) === 60, 'Shutter palette: Strobe random at 60%', p && p.values);
      p = await create('Gobo', 'E2E Gobo', async () => { await spinRow('DMX', 64); });
      check(p && p.type === 'Gobo' && Number(p.values[0]) === 64, 'Gobo palette DMX 64', p && p.values);
      const zoom = await create('Zoom', 'E2E Zoom', async () => { await spinRow('Zoom', 30); });
      check(zoom && zoom.type === 'Zoom' && Number(zoom.values[0]) === 30, 'Zoom palette 30%', zoom && zoom.values);
      // edit: rename + change value
      if (zoom) {
        await clickFn(page, leafText('E2E Zoom'), 'E2E Zoom in the list');
        await page.waitFor(`!!${rowPart('Zoom', 'input')}[0]`, 5000);
        const nameEl = `[...document.querySelectorAll('input')].find(i => i.value === 'E2E Zoom')`;
        await page.waitFor("!!(" + nameEl + ")", 5000);
        const r = await page.rectOf(new Function('return ' + nameEl));
        const x = r.x + r.w / 2, y = r.y + r.h / 2;
        await page.mouse('mouseMoved', x, y);
        await page.mouse('mousePressed', x, y, { clickCount: 1 }); await page.mouse('mouseReleased', x, y, { clickCount: 1 });
        await page.mouse('mousePressed', x, y, { clickCount: 2 }); await page.mouse('mouseReleased', x, y, { clickCount: 2 });
        await sleep(300);
        const editing = await page.eval(`document.activeElement && document.activeElement.tagName === 'INPUT' && document.activeElement.value === 'E2E Zoom'`);
        check(editing, 'double-click puts the palette name into edit mode');
        await page.eval(`document.activeElement && document.activeElement.select && document.activeElement.select()`);
        await page.s.send('Input.insertText', { text: 'E2E Zoom Wide' });
        await page.key('Enter');
        check(!!await soon(async () => (await api.call('palette.get', { paletteId: zoom.id })).name === 'E2E Zoom Wide', 'renamed'), 'palette renamed to "E2E Zoom Wide"');
        await setInput(page, `${rowPart('Zoom', 'input')}[0]`, 75, true);
        check(!!await soon(async () => Number((await api.call('palette.get', { paletteId: zoom.id })).values[0]) === 75, 'value'), 'palette value changed to 75%');
        await shot(page, 'palette-edit');
      }
      console.log('  (Pan / Tilt / Pan+Tilt palettes: left to the degrees fix in FixtureDialogs.jsx, not driven here)');
      await panel(page, 'Palettes', false);
    }

    /* ================= Sequence: bound Scene picker ================= */
    if (want('sequence')) {
      console.log('\n[Sequence bound scene]');
      const fns = (await api.call('functions.list')).functions;
      const seq = fns.find(f => f.type === 'Sequence');
      const seqD = await api.call('functions.get', { functionId: String(seq.id) });
      const orig = String(seqD.typeDetail.boundSceneId);
      const other = fns.find(f => f.type === 'Scene' && String(f.id) !== orig && !f.hidden);
      await view(page, 'list');
      await selectInTree(page, seq.name);
      await page.waitFor(`!!document.querySelector('[data-e2e=seq-bound-scene]')`, 10000);
      await setInput(page, q('[data-e2e=seq-bound-scene]'), String(other.id));
      check(!!await soon(async () => String((await api.call('functions.get', { functionId: String(seq.id) })).typeDetail.boundSceneId) === String(other.id), 'bound'), 'the picker binds "' + seq.name + '" to "' + other.name + '"');
      await shot(page, 'sequence-bound');
      await setInput(page, q('[data-e2e=seq-bound-scene]'), orig);
      check(!!await soon(async () => String((await api.call('functions.get', { functionId: String(seq.id) })).typeDetail.boundSceneId) === orig, 'restored'), 'and back to its original scene');
    }

    /* ================= RGB Matrix editor ================= */
    if (want('rgb')) {
      console.log('\n[RGB Matrix: typed script properties, font, offsets, image]');
      const fns = (await api.call('functions.list')).functions;
      const rgb = fns.find(f => f.type === 'RGBMatrix' && f.name === 'New RGB Matrix 117') || fns.find(f => f.type === 'RGBMatrix');
      const cfg = async () => (await api.call('functions.get', { functionId: String(rgb.id) })).typeDetail.config;
      const algos = (await api.call('functions.rgbmatrix.listAlgorithms')).algorithms;
      await view(page, 'list');
      await selectInTree(page, rgb.name);
      await page.waitFor(`!!document.querySelector('[data-rgb=editor]')`, 10000);
      if (!algos.some(a => a.name === 'E2E Typed Props')) {
        check(false, 'the test script "E2E Typed Props" is loaded (restart the sandbox once after the first run)');
      } else {
        await setInput(page, q('[data-rgb="algorithm"]'), 'E2E Typed Props');
        await page.waitFor(`!!document.querySelector('[data-rgb="prop-gain"]') && !!document.querySelector('[data-rgb="prop-label"]')`, 8000);
        await setInput(page, q('[data-rgb="prop-gain"]'), '2.75', true);
        await setInput(page, q('[data-rgb="prop-label"]'), 'world', true);
        const c = await soon(async () => { const a = (await cfg()).algorithm; const pv = (n) => (a.scriptProperties.find(p => p.name === n) || {}).value; return Number(pv('gain')) === 2.75 && pv('label') === 'world' ? a : null; }, 'props');
        check(!!c, 'float property gain = 2.75 and string property label = "world"', c ? c.scriptProperties : (await cfg()).algorithm.scriptProperties);
        await shot(page, 'rgb-typed-props');
      }
      await setInput(page, q('[data-rgb="algorithm"]'), 'Text');
      await page.waitFor(`!!document.querySelector('[data-rgb=text]')`, 8000);
      await sleep(400);
      await setInput(page, `${rowPart('Font', 'input', '[data-rgb=editor]')}[1]`, 24, true);
      await clickFn(page, `document.querySelector('[data-rgb=editor] button[title="Bold"]')`, 'Bold');
      await clickFn(page, `document.querySelector('[data-rgb=editor] button[title="Italic"]')`, 'Italic');
      await setInput(page, `${rowPart('Offset', 'input', '[data-rgb=editor]')}[0]`, 3, true);
      await setInput(page, `${rowPart('Offset', 'input', '[data-rgb=editor]')}[1]`, -2, true);
      let a = await soon(async () => { const x = (await cfg()).algorithm; return x.font && x.font.pointSize === 24 && x.font.bold && x.font.italic && x.xOffset === 3 && x.yOffset === -2 ? x : null; }, 'text');
      check(!!a, 'Text: font 24 pt bold italic, offset 3 / -2', a || (await cfg()).algorithm);
      await shot(page, 'rgb-text');
      await setInput(page, q('[data-rgb="algorithm"]'), 'Image');
      await page.waitFor(`!!document.querySelector('[data-rgb="image-browse"] button')`, 8000);
      await clickFn(page, `document.querySelector('[data-rgb="image-browse"] button')`, 'Browse…');
      await page.waitFor(`!!document.querySelector('.qlc-file-browser')`, 5000);
      await clickFn(page, `[...document.querySelectorAll('.qlc-file-browser div[title]')].find(d => /[\\\\/]Gobos$/.test(d.title))`, 'Gobos place');
      await clickFn(page, leafText('Others', '.qlc-fb-list'), 'Others');
      await clickFn(page, dialogBtn('Open'), 'Open folder');
      await page.waitFor(`!![...document.querySelectorAll('.qlc-fb-list *')].find(e => e.children.length === 0 && e.textContent.trim() === 'gobo00003.png')`, 5000);
      await clickFn(page, leafText('gobo00003.png', '.qlc-fb-list'), 'gobo00003.png');
      await clickFn(page, dialogBtn('Open'), 'Open');
      a = await soon(async () => { const x = (await cfg()).algorithm; return x.type === 'image' && /gobo00003\.png$/.test(x.imagePath || '') ? x : null; }, 'image');
      check(!!a, 'Image: picked picture ' + (a && a.imagePath));
      await setInput(page, `${rowPart('Offset', 'input', '[data-rgb=editor]')}[0]`, -4, true);
      a = await soon(async () => { const x = (await cfg()).algorithm; return x.xOffset === -4 ? x : null; }, 'image offset');
      check(!!a, 'Image: X offset -4');
      await sleep(800);
      await shot(page, 'rgb-image');
    }

    /* ================= rename fixtures with numbering ================= */
    if (want('rename')) {
      console.log('\n[rename fixtures with numbering]');
      await view(page, 'list');
      const lights = fixtures.filter(f => f.model === 'Mobile Light').slice(0, 3);
      await selectInTree(page, lights[0].name, undefined, "Mobile Light");
      for (const f of lights.slice(1)) await clickFn(page, leafText(f.name, '[data-ff-tree]'), 'ctrl ' + f.name, { modifiers: CTRL });
      await clickFn(page, byTitle('Rename the selected item'), 'Rename');
      await page.waitFor(`!!document.querySelector('[data-e2e=rename-numbered]')`, 5000);
      await setInput(page, q('[data-e2e=rename-base]'), 'E2E Light');
      await setInput(page, `${rowPart('Start number', 'input', '[data-e2e=rename-numbered]')}[0]`, 5, true);
      await setInput(page, `${rowPart('Digits', 'input', '[data-e2e=rename-numbered]')}[0]`, 2, true);
      await shot(page, 'rename-fixtures');
      await clickFn(page, dialogBtn('Rename'), 'Rename');
      const names = await soon(async () => { const l = (await api.call('fixtures.list')).fixtures; const n = lights.map(f => l.find(x => x.id === f.id).name); return n.join(',') === 'E2E Light 05,E2E Light 06,E2E Light 07' ? n : null; }, 'renamed');
      check(!!names, 'three fixtures renamed E2E Light 05 / 06 / 07', names || lights.map(f => f.name));
    }

    /* ================= save and grep the project ================= */
    if (want('save')) {
      console.log('\n[saveAs]');
      const out = path.join(SANDBOX, 'partials-out.qxw');
      await api.call('core.project.saveAs', { target: 'serverPath', path: out });
      const xml = fs.readFileSync(out, 'utf8');
      if (want('palettes')) check(/<Palette [^>]*Type="Zoom"[^>]*Name="E2E Zoom Wide"|<Palette [^>]*Name="E2E Zoom Wide"[^>]*Type="Zoom"/.test(xml) && /Name="E2E Pos3D"/.test(xml), 'out.qxw holds the new palettes');
      if (want('dmxt')) check(/PositionRange="1200"/.test(xml), 'out.qxw holds the drone position range');
      if (want('rename')) check(/<Name>E2E Light 06<\/Name>/.test(xml), 'out.qxw holds the renamed fixtures');
    }

    /* ================= Fixture Editor: gobo picture ================= */
    if (want('gobo')) {
      console.log('\n[Fixture Editor gobo picture]');
      await page.goto(WEB + '?ctx=fxeditor');
      await page.waitFor(`!!document.querySelector('[data-fe="tb-new"]')`, 30000);
      await sleep(800);
      const sessionsBefore = (await api.call('fixturedefs.session.list')).sessions.map(s => s.sessionId);
      await clickSel(page, '[data-fe="tb-new"]');
      const sid = (await soon(async () => (await api.call('fixturedefs.session.list')).sessions.find(s => sessionsBefore.indexOf(s.sessionId) === -1), 'session')).sessionId;
      await clickSel(page, '[data-fe-tab="channels"]');
      await pickCombo(page, '[data-fe="add-channel-preset"]', 'Gobo Wheel');
      await clickSel(page, '[data-fe="add-channel"]');
      const def = await soon(async () => { const d = (await api.call('fixturedefs.session.get', { sessionId: sid })).definition; return d.channels.length ? d : null; }, 'channel');
      const goboCh = def && def.channels.find(c => c.group === 'Gobo' || /gobo/i.test(c.name));
      check(!!goboCh, 'the new definition has a Gobo channel (' + (goboCh && goboCh.name) + ')');
      await clickSel(page, '[data-channel-name="' + goboCh.name + '"]');
      /* a preset channel's capabilities are generated; Custom makes them editable (keeps group Gobo) */
      await pickCombo(page, '[data-fe="channel-preset"]', 'Custom');
      await soon(async () => (await api.call('fixturedefs.session.get', { sessionId: sid })).definition.channels[0].preset === 'Custom', 'custom');
      await clickSel(page, '[data-cap-index="0"] [data-fe="cap-name"]');
      await pickCombo(page, '[data-fe="cap-preset"]', 'Gobo Macro');
      await clickFn(page, `document.querySelector('[data-fe="cap-picture-browse"] button')`, 'Browse…');
      const where = await soon(() => page.eval(`(document.querySelector('.qlc-fb-path') || {}).value`), 'browser path');
      check(/[\\/]Gobos$/.test(where || ''), 'the picker opens in the gobo folder (' + where + ')');
      await clickFn(page, leafText('Others', '.qlc-fb-list'), 'Others');
      await clickFn(page, dialogBtn('Open'), 'Open folder');
      await page.waitFor(`!![...document.querySelectorAll('.qlc-fb-list *')].find(e => e.children.length === 0 && e.textContent.trim() === 'gobo00003.png')`, 5000);
      await clickFn(page, leafText('gobo00003.png', '.qlc-fb-list'), 'gobo00003.png');
      await shot(page, 'gobo-browser');
      await clickFn(page, dialogBtn('Open'), 'Open');
      const cap = await soon(async () => { const c = (await api.call('fixturedefs.session.get', { sessionId: sid })).definition.channels.find(x => x.name === goboCh.name).capabilities[0]; return c.preset === 'GoboMacro' && /Gobos\/Others\/gobo00003\.png$/.test(String(c.resources[0])) ? c : null; }, 'picture');
      check(!!cap, 'the capability picture is the picked gobo (' + (cap && cap.resources[0]) + ')');
      const xml = await api.call('fixturedefs.export', { sessionId: sid }).catch(() => null);
      const text = xml && (xml.qxf || (xml.qxfBase64 && Buffer.from(xml.qxfBase64, 'base64').toString('utf8')) || '');
      if (text) check(/Res1="Others\/gobo00003\.png"|Res="Others\/gobo00003\.png"/.test(text), 'the exported .qxf stores it relative to the gobo folder', (text.match(/<Capability[^>]*gobo00003[^>]*>/) || [''])[0]);
      await shot(page, 'gobo-picture');
      await api.call('fixturedefs.session.close', { sessionId: sid, discard: true }).catch(() => api.call('fixturedefs.session.close', { sessionId: sid }).catch(() => {}));
      await page.goto(WEB + '?ctx=fx');
      await page.waitFor(`!!document.querySelector('[data-ff-view-button="2d"]')`, 30000);
    }

    /* ================= initial point of view prompt (new, empty project) ================= */
    if (want('pov')) {
      console.log('\n[initial point of view prompt]');
      await api.call('core.project.new');
      await sleep(1000);
      await page.goto(WEB + '?ctx=fx');
      await page.waitFor(`!!document.querySelector('[data-ff-view-button="2d"]')`, 30000);
      await view(page, '2d');
      const st0 = (await api.call('fixtures.monitor.get')).stage;
      check(st0.pointOfView === 'Undefined', 'a new project has no point of view yet (' + st0.pointOfView + ')');
      await page.waitFor(`!!document.querySelector('[data-ff-pov]')`, 8000).catch(() => {});
      check(await page.eval(`!!document.querySelector('[data-ff-pov]')`), 'the 2D view asks for the initial point of view');
      await shot(page, 'pov-prompt');
      await clickFn(page, byText('button', 'Front view', '[data-ff-pov]'), 'Front view');
      const st = await soon(async () => { const s = (await api.call('fixtures.monitor.get')).stage; return s.pointOfView === 'FrontView' ? s : null; }, 'pov');
      check(!!st, 'picking Front view sets the point of view', st);
      check(await soon(() => page.eval(`!document.querySelector('[data-ff-pov]')`), 'closed'), 'the prompt closes');
    }
  } catch (e) {
    await shot(page, 'failure').catch(() => {});
    console.log('  page text: ' + (await page.eval(() => document.body.innerText.slice(0, 600)).catch(() => '')).replace(/\s+/g, ' '));
    failures.push('ERROR ' + e.message);
    console.log('ERROR ' + e.stack);
  } finally {
    const errs = page.consoleErrors.filter(e => !/favicon/i.test(e));
    check(errs.length === 0, 'no console errors', errs.slice(0, 5));
    await browser.close();
    api.close();
  }

  console.log('\n' + (failures.length ? failures.length + ' FAILED:\n  ' + failures.join('\n  ') : 'ALL CHECKS PASSED'));
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error('ERROR ' + e.stack); process.exit(1); });
