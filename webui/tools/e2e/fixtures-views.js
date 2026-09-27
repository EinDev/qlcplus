/**
 * End-to-end driver for the fixture placement & views slice: the Fixtures & Functions 2D view
 * (selection, arrange, align, drag, gel colour, select every 2nd, invert selection in groups,
 * Pick a 3D point), the fixture placement properties, the DMX view, the universe grid (cut /
 * paste), the Fixture Remap dialog and saveAs, against a sandbox (dev-webui-sandbox.ps1).
 *
 *   .\dev-webui-sandbox.ps1 -Name fixtures -BuildDir .\build -WebUiRoot .\webui -ApiPort 9210 -WebUiPort 9211
 *   node webui/tools/e2e/fixtures-views.js
 *   env: QLC_API (ws://127.0.0.1:9210/), QLC_WEB (http://localhost:9211/), QLC_OUT (saveAs path,
 *        C:\qlcsandbox\fixtures\out.qxw), QLC_SHOTS (screenshot dir, default C:\qlcsandbox\fixtures)
 *
 * Every gesture is followed by a read-back over a second raw API connection (fixtures.monitor.get,
 * io.dmx.universe.get, fixtures.get, functions.get) and the run ends with core.project.saveAs and
 * a grep of the <Monitor> / <FxItem> XML. Exit code 1 when a check fails or the page logged a
 * console error.
 */
const fs = require('fs');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const API = process.env.QLC_API || 'ws://127.0.0.1:9210/';
const WEB = process.env.QLC_WEB || 'http://localhost:9211/';
const OUT = process.env.QLC_OUT || 'C:\\qlcsandbox\\fixtures\\out.qxw';
const SHOTS = process.env.QLC_SHOTS || 'C:\\qlcsandbox\\fixtures';

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
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'fixtures-views e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
    });
  }
  call(method, params = {}) {
    const id = 'e-' + (this.next++);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  async monitorItem(fid) { return (await this.call('fixtures.monitor.get')).items.find(i => i.fixtureId === String(fid) && i.headIndex === 0 && i.linkedIndex === 0); }
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
const near = (a, b, tol = 1) => Math.abs(a - b) <= tol;

/* ---------------------------------------------------------------- page helpers */
const CTRL = 2;
const itemSel = (fid) => `document.querySelector('[data-ff-stage] g[data-item="${fid}:0:0"]')`;
const byText = (tag, text) => `[...document.querySelectorAll(${JSON.stringify(tag)})].reverse().find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
async function clickFn(page, fnBody, what, opts) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'center', inline: 'center' }); })()`);
  await sleep(60);
  await page.click(new Function('return ' + fnBody), opts || {});
  await sleep(120);
}
/** Click the fixture's first head circle (its centre is inside the item even when neighbours overlap the rect). */
const clickItem = (page, fid, opts) => clickFn(page, `document.querySelector('[data-ff-stage] g[data-item="${fid}:0:0"] circle')`, 'stage item ' + fid, opts);
const tool = (name) => `document.querySelector('[data-tool="${name}"]')`;
const toolBtn = (name) => `(document.querySelector('[data-tool="${name}"]').tagName === 'BUTTON' ? document.querySelector('[data-tool="${name}"]') : document.querySelector('[data-tool="${name}"] button'))`;
async function setInput(page, selectorFn, value) {
  await page.eval(`(function(){ const el = (${selectorFn}); el.focus();`
    + ' const proto = el.tagName === "SELECT" ? HTMLSelectElement.prototype : HTMLInputElement.prototype;'
    + ` Object.getOwnPropertyDescriptor(proto, "value").set.call(el, ${JSON.stringify(String(value))});`
    + ' el.dispatchEvent(new Event(el.tagName === "SELECT" ? "change" : "input", { bubbles: true })); })()');
  await sleep(150);
}
const selectedOnStage = (page) => page.eval(`[...document.querySelectorAll('[data-ff-stage] g[data-selected]')].map(g => g.getAttribute('data-fx-id'))`);
async function shot(page, name) { const f = path.join(SHOTS, 'fixtures-' + name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev);

  const fixtures = (await api.call('fixtures.list')).fixtures;
  const gobos = fixtures.filter(f => f.manufacturer === 'Gobo Spot').sort((a, b) => a.universe - b.universe || a.address - b.address);
  check(gobos.length >= 4, 'SF3 has Gobo Spots', gobos.length);
  const four = gobos.slice(0, 4).map(f => f.id);
  const mon0 = await api.call('fixtures.monitor.get');
  check(mon0.stage && mon0.items.length >= fixtures.length, 'fixtures.monitor.get lists every fixture', { items: mon0.items.length, fixtures: fixtures.length });
  const stage = mon0.stage;
  console.log('stage', JSON.stringify(stage));

  const browser = await launch({ headless: true });
  const page = await browser.open(WEB + '?ctx=fx', { width: 1700, height: 1000 });
  try {
    await page.waitFor(`!!document.querySelector('[data-ff-view-button="2d"]')`, 30000);
    /* ---- 2D view ---- */
    console.log('\n[2D view]');
    await clickFn(page, `document.querySelector('[data-ff-view-button="2d"]')`, '2D view button');
    await page.waitFor(`document.querySelectorAll('[data-ff-stage] g[data-fx-id]').length > 0`, 15000);
    const drawn = await page.eval(`document.querySelectorAll('[data-ff-stage] g[data-fx-id]').length`);
    check(drawn > 100, 'the stage draws the SF3 fixtures', drawn);
    for (let i = 0; i < 6; i++) await clickFn(page, toolBtn('zoom-in'), 'zoom in');
    const zoomText = await page.eval(`document.querySelector('[data-zoom]').textContent`);
    check(/px\//.test(zoomText), 'zoom label updates (' + zoomText + ')');

    // select the four Gobo Spots: click + ctrl-click
    await clickItem(page, four[0]);
    for (const fid of four.slice(1)) await clickItem(page, fid, { modifiers: CTRL });
    let sel = await until(async () => { const s = await selectedOnStage(page); return s.length === 4 ? s : null; }, '4 selected').catch(() => null);
    check(sel && four.every(id => sel.indexOf(id) !== -1), 'click + ctrl-click selects the 4 Gobo Spots', sel || await selectedOnStage(page));

    // arrange in a circle of 4 m
    const before = await Promise.all(four.map(id => api.monitorItem(id)));
    const cx = before.reduce((s, it) => s + it.position.x, 0) / 4, cz = before.reduce((s, it) => s + it.position.z, 0) / 4;
    await clickFn(page, toolBtn('arrange'), 'arrange button');
    await page.waitFor(`!!document.querySelector('[data-ff-arrange]')`);
    await setInput(page, `document.querySelector('[data-ff-arrange] input[title="Diameter"]')`, 4000);
    await clickFn(page, byText('button', 'Apply'), 'Apply');
    const circ = await until(async () => { const its = await Promise.all(four.map(id => api.monitorItem(id))); return its.every(it => near(Math.hypot(it.position.x - cx, it.position.z - cz), 2000, 2)) ? its : null; }, 'circle').catch(() => null);
    check(!!circ, 'arrange circle: all 4 are 2000 mm from their old centroid (' + (stage.pointOfView) + ')');
    await sleep(400);
    await shot(page, '2d-circle');

    // align top (TopView: Z of the first selected)
    await clickFn(page, toolBtn('align-top'), 'align top');
    const aligned = await until(async () => { const its = await Promise.all(four.map(id => api.monitorItem(id))); const z = its.map(i => i.position.z); return z.every(v => near(v, z[0], 0.5)) ? its : null; }, 'aligned').catch(() => null);
    check(!!aligned, 'align top: equal depth for all 4');

    // drag one alone by +80 px horizontally
    const target = four[1];
    for (const fid of four.filter(id => id !== target)) await clickItem(page, fid, { modifiers: CTRL }); // ctrl-click deselects
    check(await until(async () => { const s = await selectedOnStage(page); return s.length === 1 && s[0] === target; }, 'single selection').catch(() => false), 'ctrl-click deselects, leaving one Gobo Spot selected');
    const pre = await api.monitorItem(target);
    const r = await page.rectOf(new Function(`return document.querySelector('[data-ff-stage] g[data-item="${target}:0:0"] circle')`));
    const pxPerMm = await page.eval(`(function(){ const s = document.querySelector('[data-ff-stage]'); return s.getBoundingClientRect().width / s.viewBox.baseVal.width; })()`);
    await page.drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 80, r.y + r.h / 2, 10);
    const moved = await until(async () => { const it = await api.monitorItem(target); return Math.abs(it.position.x - pre.position.x) > 10 ? it : null; }, 'drag commit').catch(() => null);
    check(moved && near(moved.position.x - pre.position.x, 80 / pxPerMm, 3 / pxPerMm), 'drag moves the fixture by 80 px = ' + (80 / pxPerMm).toFixed(0) + ' mm', moved && moved.position.x - pre.position.x);
    const others = await Promise.all(four.filter(id => id !== target).map(id => api.monitorItem(id)));
    check(others.every((it, i) => near(it.position.x, aligned[four.indexOf(four.filter(id => id !== target)[i])].position.x, 0.5)), 'the unselected Gobo Spots did not move');

    // gel colour from the toolbar picker
    await setInput(page, `document.querySelector('input[data-tool="gel"]')`, '#ff2000');
    const gel = await until(async () => { const it = await api.monitorItem(target); return it.gelColor === '#ff2000' ? it : null; }, 'gel').catch(() => null);
    check(!!gel, 'toolbar gel colour lands on the selected fixture');

    /* ---- properties panel ---- */
    console.log('\n[properties panel]');
    await clickFn(page, `document.querySelector('[data-ff-view-button="list"]')`, 'details view');
    await page.waitFor(`!!document.querySelector('[data-ff-placement] [data-item="${target}:0:0"]')`, 10000);
    await setInput(page, `document.querySelector('[data-ff-placement] [data-item="${target}:0:0"] input[title="Rotation Y (deg)"]')`, 45);
    await setInput(page, `document.querySelector('[data-ff-placement] [data-item="${target}:0:0"] input[data-gel]')`, '#00ff80');
    await clickFn(page, `document.querySelector('[data-ff-placement] [data-item="${target}:0:0"] [data-flag="invertPan"] button')`, 'invert pan');
    const props = await until(async () => { const it = await api.monitorItem(target); return it.rotation.y === 45 && it.gelColor === '#00ff80' && it.flags.invertPan ? it : null; }, 'props').catch(() => null);
    check(!!props, 'properties panel: rotation Y 45, gel #00ff80, invert pan', props || await api.monitorItem(target));
    await clickFn(page, `document.querySelector('[data-ff-placement] [data-item="${target}:0:0"] [data-flag="invertPan"] button')`, 'invert pan off');
    await until(async () => !(await api.monitorItem(target)).flags.invertPan, 'invert pan off').catch(() => check(false, 'invert pan toggles back off'));
    await shot(page, 'properties');

    /* ---- selection tools ---- */
    console.log('\n[selection tools]');
    await clickFn(page, `document.querySelector('[data-ff-view-button="2d"]')`, '2D view button');
    await page.waitFor(`!!document.querySelector('[data-ff-stage]')`);
    for (let i = 0; i < 6; i++) await clickFn(page, toolBtn('zoom-in'), 'zoom in');
    await clickItem(page, four[0]);
    for (const fid of four.slice(1)) await clickItem(page, fid, { modifiers: CTRL });
    await until(async () => (await selectedOnStage(page)).length === 4, '4 selected again');
    await clickFn(page, toolBtn('select-even'), 'select even');
    sel = await until(async () => { const s = await selectedOnStage(page); return s.length === 2 ? s : null; }, 'every 2nd').catch(() => null);
    check(sel && sel.indexOf(four[1]) !== -1 && sel.indexOf(four[3]) !== -1, 'select every 2nd keeps the 2nd and 4th', sel || await selectedOnStage(page));
    const groups = (await api.call('fixtures.group.list')).groups;
    const details = await Promise.all(groups.map(g => api.call('fixtures.group.get', { groupId: String(g.id) })));
    const containing = details.filter(d => d.heads.some(h => sel && sel.indexOf(h.fixtureId) !== -1));
    const expected = new Set();
    containing.forEach(d => d.heads.forEach(h => { if (sel.indexOf(h.fixtureId) === -1) expected.add(h.fixtureId); }));
    await clickFn(page, toolBtn('invert-groups'), 'invert selection in groups');
    if (containing.length > 1) {
      await page.waitFor(`!!document.querySelector('[data-ff-invert-groups]')`, 5000);
      await clickFn(page, byText('button', 'Invert'), 'Invert');
    }
    const inv = await until(async () => { const s = await selectedOnStage(page); return s.length && s.every(id => sel.indexOf(id) === -1) ? s : null; }, 'inverted').catch(() => null);
    const invIds = inv ? Array.from(new Set(inv)) : [];
    check(inv && invIds.length === expected.size && invIds.every(id => expected.has(id)), 'invert selection in ' + containing.length + ' group(s) selects the other members', { got: invIds, expected: Array.from(expected) });
    await clickFn(page, toolBtn('select-all'), 'select all');
    const all = await until(async () => { const s = await selectedOnStage(page); return s.length > 100 ? s : null; }, 'select all').catch(() => null);
    check(!!all, 'select all selects every fixture in the view', all ? all.length : 0);
    await clickFn(page, toolBtn('select-all'), 'deselect all');
    check((await until(async () => (await selectedOnStage(page)).length === 0, 'deselect all').catch(() => false)), 'the second select-all deselects');

    /* ---- Pick a 3D point ---- */
    console.log('\n[aim at a point]');
    const aimFx = gobos.find(f => { const it = mon0.items.find(i => i.fixtureId === f.id); return it && it.hasPan && it.hasTilt; });
    check(!!aimFx, 'a Gobo Spot with Pan and Tilt exists');
    const aimDetail = await api.call('fixtures.get', { fixtureId: aimFx.id });
    const pt = aimDetail.channelList.filter(c => c.group === 'Pan' || c.group === 'Tilt').map(c => c.absoluteAddress);
    const dmxOf = async () => { const v = (await api.call('io.dmx.universe.get', { universeId: aimFx.universe })).values; return pt.map(a => v[a - aimFx.universe * 512]); };
    const dmxBefore = await dmxOf();
    await clickItem(page, aimFx.id);
    await clickFn(page, toolBtn('aim'), 'aim tool');
    await page.waitFor(`!!document.querySelector('[data-ff-aim]')`);
    const u = stage.gridUnits === 'Feet' ? 304.8 : 1000;
    const itAim = await api.monitorItem(aimFx.id);
    await setInput(page, `document.querySelector('input[data-aim-axis="x"]')`, ((itAim.position.x + 6000) / u).toFixed(2));
    await setInput(page, `document.querySelector('input[data-aim-axis="y"]')`, 0);
    await setInput(page, `document.querySelector('input[data-aim-axis="z"]')`, ((itAim.position.z + 3000) / u).toFixed(2));
    await clickFn(page, `[...document.querySelectorAll('[data-ff-aim] button')].find(b => b.textContent.trim() === 'Aim')`, 'Aim');
    const dmxAfter = await until(async () => { const d = await dmxOf(); return d.some((v, i) => v !== dmxBefore[i]) ? d : null; }, 'pan/tilt DMX', 6000).catch(() => null);
    check(!!dmxAfter, 'aimAt changes the Pan/Tilt DMX output of ' + aimFx.name, { before: dmxBefore, after: dmxAfter || await dmxOf() });
    const statusText = await page.eval(`document.querySelector('[data-status]').textContent`);
    check(/Aimed 1 fixture/.test(statusText), 'status line reports the aim (' + statusText + ')');
    await shot(page, '2d-aim');
    await clickFn(page, toolBtn('aim'), 'close aim tool');

    /* ---- stage settings ---- */
    console.log('\n[stage settings]');
    await clickFn(page, toolBtn('settings'), 'settings');
    await page.waitFor(`!!document.querySelector('[data-ff-2d-settings]')`);
    await clickFn(page, `document.querySelector('[data-ff-2d-settings] [data-flag="labels"] button')`, 'labels');
    check(await until(async () => (await api.call('fixtures.monitor.get')).stage.showLabels === !stage.showLabels, 'labels').catch(() => false), 'Show labels toggles the stage setting');
    await clickFn(page, `document.querySelector('[data-ff-2d-settings] [data-flag="groups"] button')`, 'groups overlay');
    const groupBoxes = await until(async () => { const n = await page.eval(`document.querySelectorAll('[data-ff-stage] [data-group]').length`); return n > 0 ? n : null; }, 'group overlay').catch(() => 0);
    check(groupBoxes > 0, 'fixture group overlay drawn', groupBoxes);
    await clickFn(page, toolBtn('zoom-fit'), 'fit');
    await sleep(500);
    await shot(page, '2d-settings');
    await clickFn(page, `document.querySelector('[data-ff-2d-settings] [data-flag="labels"] button')`, 'labels back');
    await clickFn(page, toolBtn('settings'), 'close settings');

    /* ---- DMX view ---- */
    console.log('\n[DMX view]');
    await clickFn(page, `document.querySelector('[data-ff-view-button="dmx"]')`, 'DMX view button');
    await page.waitFor(`document.querySelectorAll('[data-ff-view="dmx"] [data-fx-id]').length > 0`, 10000);
    await sleep(1500);
    const card = await page.eval(`(function(){ const c = document.querySelector('[data-ff-view="dmx"] [data-fx-id="${aimFx.id}"]'); if (!c) return null; return [...c.querySelectorAll('[data-channel]')].map(e => ({ ch: e.getAttribute('data-channel'), v: e.getAttribute('data-value') })); })()`);
    const panIdx = aimDetail.channelList.find(c => c.group === 'Pan').index;
    const panCell = card && card.find(c => Number(c.ch) === panIdx);
    check(!!card, 'DMX view shows a card for ' + aimFx.name);
    check(panCell && Number(panCell.v) === dmxAfter[0], 'DMX view shows the aimed pan value', { cell: panCell, dmx: dmxAfter && dmxAfter[0] });
    await shot(page, 'dmx');

    /* ---- Universe grid ---- */
    console.log('\n[universe grid]');
    await clickFn(page, `document.querySelector('[data-ff-view-button="grid"]')`, 'grid view button');
    await page.waitFor(`document.querySelectorAll('[data-ff-view="grid"] [data-address]').length === 512`, 10000);
    const moveFx = gobos[3];
    await clickFn(page, `document.querySelector('[data-ff-view="grid"] [data-address="${moveFx.address}"]')`, 'grid cell of ' + moveFx.name);
    await clickFn(page, `document.querySelector('[data-ff-view="grid"] [data-action="cut"]')`, 'Cut');
    await clickFn(page, `document.querySelector('[data-ff-view="grid"] [data-action="paste"]')`, 'Paste');
    const pasted = await until(async () => { const d = await api.call('fixtures.get', { fixtureId: moveFx.id }); return d.address !== moveFx.address ? d : null; }, 'paste').catch(() => null);
    check(!!pasted, 'grid cut / paste re-addresses ' + moveFx.name, pasted ? (moveFx.address + 1) + ' -> ' + (pasted.address + 1) : 'unchanged');
    if (pasted) check(await page.eval(`!!document.querySelector('[data-ff-view="grid"] [data-address="${pasted.address}"][data-fx-id="${moveFx.id}"]')`), 'the grid shows it at its new address');
    await shot(page, 'grid');

    /* ---- Remap ---- */
    console.log('\n[remap]');
    const remapFx = gobos[0];
    const scenes = (await api.call('functions.list')).functions.filter(f => f.type === 'Scene');
    let scene = null;
    for (const s of scenes) {
      const d = await api.call('functions.get', { functionId: String(s.id) });
      if (Object.keys((d.typeDetail && d.typeDetail.values) || {}).some(k => k.split('.')[0] === remapFx.id)) { scene = d; break; }
    }
    check(!!scene, 'a Scene references ' + remapFx.name);
    const beforeKeys = scene ? Object.keys(scene.typeDetail.values).filter(k => k.split('.')[0] === remapFx.id) : [];
    await clickFn(page, `document.querySelector('[data-ff-remap]')`, 'Remap button');
    await page.waitFor(`!!document.querySelector('[data-ff-remap-dialog]')`, 5000);
    await clickFn(page, `document.querySelector('[data-remap-source="${remapFx.id}"] [data-action="clone"]')`, 'clone ' + remapFx.name);
    await page.waitFor(`!!document.querySelector('[data-remap-row][data-source-id="${remapFx.id}"]')`);
    await shot(page, 'remap');
    const fxBefore = (await api.call('fixtures.list')).fixtures.map(f => f.id);
    await clickFn(page, byText('button', 'Apply remap'), 'Apply remap');
    const after = await until(async () => { const l = (await api.call('fixtures.list')).fixtures; return l.some(f => f.id === remapFx.id) ? null : l; }, 'remap applied', 10000).catch(() => null);
    check(!!after, 'the source fixture was replaced');
    const created = after ? after.filter(f => fxBefore.indexOf(f.id) === -1) : [];
    check(created.length === 1 && created[0].manufacturer === 'Gobo Spot', 'one new Gobo Spot was created', created.map(f => f.name + ' @' + (f.address + 1)));
    if (created.length === 1 && scene) {
      console.log('  remapped ' + remapFx.name + ' U' + (remapFx.universe + 1) + '.' + (remapFx.address + 1) + ' -> id ' + created[0].id + ' U' + (created[0].universe + 1) + '.' + (created[0].address + 1));
      const s2 = await api.call('functions.get', { functionId: String(scene.id) });
      const keys = Object.keys(s2.typeDetail.values);
      check(!keys.some(k => k.split('.')[0] === remapFx.id) && beforeKeys.every(k => s2.typeDetail.values[created[0].id + '.' + k.split('.')[1]] === scene.typeDetail.values[k]),
        'Scene "' + scene.name + '" now references the new fixture with the same values', { before: beforeKeys.length });
      const mi = await api.monitorItem(created[0].id);
      check(mi && mi.placed, 'the monitor placement moved to the new fixture');
    }
    await page.waitFor(`!document.querySelector('[data-ff-remap-dialog]')`, 5000).catch(() => check(false, 'the remap dialog closes after apply'));

    /* ---- saveAs ---- */
    console.log('\n[saveAs]');
    await api.call('core.project.saveAs', { target: 'serverPath', path: OUT });
    const xml = fs.readFileSync(OUT, 'utf8');
    const monitorXml = (xml.match(/<Monitor[\s\S]*?<\/Monitor>/) || [''])[0];
    check(/<Grid [^>]*POV="\d"/.test(monitorXml), 'out.qxw has the <Monitor> grid');
    check(new RegExp(`<FxItem ID="${target}"[^>]*YRot="45"[^>]*GelColor="#00ff80"`, 'i').test(monitorXml) || new RegExp(`<FxItem ID="${target}"[^>]*GelColor="#00ff80"[^>]*`, 'i').test(monitorXml) && new RegExp(`<FxItem ID="${target}"[^>]*YRot="45"`).test(monitorXml),
      'out.qxw FxItem of the edited fixture carries YRot 45 and the gel', (monitorXml.match(new RegExp(`<FxItem ID="${target}"[^>]*>`)) || [''])[0]);
    console.log('  FxItems: ' + (monitorXml.match(/<FxItem /g) || []).length);

    await shot(page, 'final');
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
