/**
 * End-to-end driver for the fixture-side leftovers of Fixtures & Functions: mode change after
 * patching, per-channel behaviour (forced LTP, can-fade, modifier, apply to same type), the
 * channel modifier templates editor, fixture + universe summaries (print), the RGB panel dialog,
 * the fixture group grid editor (size, drag, swap, rotate), the colour filters tool and the
 * fixture console (copy to same type, multi-channel selection, pan/tilt mode, fader window), run
 * against a sandbox started with a redirected modifiers folder:
 *
 *   .\dev-webui-sandbox.ps1 -Name fxmisc -BuildDir .\build -WebUiRoot .\webui -ApiPort 9250 -WebUiPort 9251 `
 *       -UserModifiersDir C:\qlcsandbox\fxmisc\UserModifiers
 *   node webui/tools/e2e/fixtures-misc.js
 *   env: QLC_API (ws://127.0.0.1:9250/), QLC_WEB (http://localhost:9251/), QLC_OUT (saveAs path,
 *        C:\qlcsandbox\fxmisc\out.qxw), QLC_SHOTS (screenshots, C:\qlcsandbox\fxmisc),
 *        QLC_MODDIR (the sandbox modifiers folder, C:\qlcsandbox\fxmisc\UserModifiers)
 *
 * Every gesture is read back over a second raw API connection (fixtures.get, fixtures.group.get,
 * fixtures.modifiers.list, io.dmx.universe.get) and the run ends with core.project.saveAs and a
 * grep of the written XML. Exit code 1 when a check fails or the page logged a console error.
 */
const fs = require('fs');
const path = require('path');
const os = require('os');
const { launch, sleep } = require('../cdp.js');

const API = process.env.QLC_API || 'ws://127.0.0.1:9250/';
const WEB = process.env.QLC_WEB || 'http://localhost:9251/';
const OUT = process.env.QLC_OUT || 'C:\\qlcsandbox\\fxmisc\\out.qxw';
const SHOTS = process.env.QLC_SHOTS || 'C:\\qlcsandbox\\fxmisc';
const MODDIR = process.env.QLC_MODDIR || 'C:\\qlcsandbox\\fxmisc\\UserModifiers';
const REAL_MODDIR = path.join(os.homedir(), 'QLC+', 'ModifiersTemplates');

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
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'fixtures-misc e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
    });
  }
  call(method, params = {}) {
    const id = 'e-' + (this.next++);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
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
const soft = (p) => p.catch(() => null);

/* ---------------------------------------------------------------- page helpers */
const CTRL = 2;
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const leafText = (text, root) => `(function(){ const r = ${root ? q(root) : 'document'}; if (!r) return null; return [...r.querySelectorAll('*')].filter(e => e.children.length === 0 && e.textContent.trim() === ${JSON.stringify(text)}).pop() || null; })()`;
const btnText = (text, root) => `(function(){ const r = ${root ? q(root) : 'document'}; if (!r) return null; return [...r.querySelectorAll('button')].filter(e => e.textContent.trim() === ${JSON.stringify(text)}).pop() || null; })()`;
async function clickFn(page, fnBody, what, opts) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'center', inline: 'center' }); })()`);
  await sleep(60);
  await page.click(new Function('return ' + fnBody), opts || {});
  await sleep(150);
}
const clickSel = (page, sel, opts) => clickFn(page, q(sel), sel, opts);
/** Fixture Tools section header (icon + text in one span). */
const sectionHead = (text) => `[...document.querySelectorAll('span')].reverse().find(s => s.textContent.trim() === ${JSON.stringify(text)} && s.querySelector('img'))`;
async function setInput(page, selectorFn, value) {
  await page.eval(`(function(){ const el = (${selectorFn}); el.focus();`
    + ' const proto = el.tagName === "SELECT" ? HTMLSelectElement.prototype : HTMLInputElement.prototype;'
    + ` Object.getOwnPropertyDescriptor(proto, "value").set.call(el, ${JSON.stringify(String(value))});`
    + ' el.dispatchEvent(new Event(el.tagName === "SELECT" ? "change" : "input", { bubbles: true })); })()');
  await sleep(150);
}
/** Design-system CustomComboBox inside `scope`: click its current label, then the wanted entry. */
async function pickCombo(page, scope, currentLabel, wantedLabel) {
  await clickFn(page, leafText(currentLabel, scope), 'combo ' + currentLabel + ' in ' + scope);
  await sleep(250);
  await clickFn(page, `(function(){ return [...document.querySelectorAll('*')].filter(e => e.children.length === 0 && e.textContent.trim() === ${JSON.stringify(wantedLabel)}).pop() || null; })()`, 'combo entry ' + wantedLabel);
  await sleep(200);
}
/** Design-system CustomSpinBox inside `scope`: type the value into its input and commit. */
async function setSpin(page, scope, value) {
  const inp = `document.querySelector(${JSON.stringify(scope + ' input')})`;
  await page.waitFor(`!!${inp}`, 5000);
  await page.click(new Function('return ' + inp));
  await page.eval(`(function(){ const el = ${inp}; el.select && el.select(); })()`);
  await page.key('Backspace');
  await page.type(String(value));
  await page.key('Enter');
  await sleep(200);
}
/** Select a fixture in the F&F tree through the search box. */
async function selectInTree(page, name) {
  const searchOpen = await page.eval(`!!document.querySelector('[data-ff-tree]') && !![...document.querySelectorAll('input')].find(i => i.placeholder === 'Search…')`);
  if (!searchOpen) await clickFn(page, `[...document.querySelectorAll('img')].map(i => i.closest('button,[role=button],div')).find(b => b && b.title === 'Search fixtures and functions') || [...document.querySelectorAll('[title="Search fixtures and functions"]')][0]`, 'search button');
  await page.eval(`(function(){ const el = [...document.querySelectorAll('input')].find(i => i.placeholder === 'Search…');
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, ${JSON.stringify(name)});
    el.dispatchEvent(new Event('input', { bubbles: true })); el.dispatchEvent(new KeyboardEvent('keydown', { key: 'a', bubbles: true })); })()`);
  await sleep(500);
  await clickFn(page, leafText(name, '[data-ff-tree]'), 'tree node ' + name);
  await sleep(300);
}
async function shot(page, name) { const f = path.join(SHOTS, 'fxmisc-' + name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }
const closeDialog = (page, label = 'Close') => clickFn(page, btnText(label), label + ' button');

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const realBefore = fs.existsSync(REAL_MODDIR) ? fs.readdirSync(REAL_MODDIR) : [];
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev);
  const get = (id) => api.call('fixtures.get', { fixtureId: String(id) });
  /* templates left in the sandbox folder by an earlier run */
  for (const n of ['E2E Curve', 'E2E Curve 2']) await soft(api.call('fixtures.modifiers.delete', { name: n }));

  /* ---- setup over the API: a multi-mode library fixture (no SF3 fixture type has two modes) */
  const universes = (await api.call('io.universe.list')).universes.sort((a, b) => a.id - b.id);
  const free = async (uid, channels) => {
    const fx = (await api.call('fixtures.list')).fixtures.filter(f => f.universe === uid);
    const used = new Array(512).fill(false); fx.forEach(f => { for (let i = 0; i < f.channels; i++) used[f.address + i] = true; });
    for (let a = 0; a + channels <= 512; a++) { let ok = true; for (let i = 0; i < channels; i++) if (used[a + i]) { ok = false; break; } if (ok) return a; }
    return -1;
  };
  let parUni = null, parAddr = -1;
  for (const u of universes) { const a = await free(u.id, 12); if (a >= 0) { parUni = u; parAddr = a; break; } }
  const PAR = 'E2E Mode Par';
  const patched = await api.call('fixtures.patch', { universe: parUni.id, address: parAddr, manufacturer: 'Eurolite', model: 'LED PAR 56 RGB DMX', mode: '3 Channels', name: PAR, baseRevision: api.rev });
  const parId = patched.fixtureIds[0];
  const parName = (await get(parId)).name;
  console.log('patched ' + parName + ' (id ' + parId + ') at U' + (parUni.id + 1) + ' ' + (parAddr + 1));

  const all = (await api.call('fixtures.list')).fixtures;
  const blinder = all.find(f => f.manufacturer === 'SF3' && f.model === 'Cob Blinder') || all.find(f => f.manufacturer === 'SF3');
  const sameType = all.filter(f => f.manufacturer === blinder.manufacturer && f.model === blinder.model && f.mode === blinder.mode);
  console.log('SF3 fixture for channel behaviour: ' + blinder.name + ' (' + sameType.length + ' of the same type)');

  const browser = await launch({ headless: true });
  const page = await browser.open(WEB + '?ctx=fx', { width: 1800, height: 1050 });
  try {
    await page.waitFor(`!!document.querySelector('[data-ff-tree]') && document.querySelector('[data-ff-tree]').textContent.length > 50`, 30000);

    /* ---- 1. mode change ---- */
    console.log('\n[mode change]');
    await selectInTree(page, parName);
    await page.waitFor(`!!document.querySelector('[data-fx-mode]')`, 10000);
    await pickCombo(page, '[data-fx-mode]', '3 Channels (3 ch)', '5 Channels (5 ch)');
    const par5 = await until(async () => { const d = await get(parId); return d.channels === 5 ? d : null; }, 'mode 5ch').catch(() => null);
    check(!!par5 && par5.mode === '5 Channels' && par5.channelList.length === 5, 'mode combo switches the Eurolite to 5 Channels (fixtures.get channels 5)', par5 && par5.channels);
    check(await until(() => page.eval(`document.querySelectorAll('[data-fx-channel]').length === 5`), 'detail rows').catch(() => false), 'the detail channel list shows 5 channels');
    await shot(page, 'mode');

    /* ---- 2. per-channel behaviour on an SF3 fixture, applied to the same type ---- */
    console.log('\n[channel behaviour]');
    await selectInTree(page, blinder.name);
    await page.waitFor(`!!document.querySelector('[data-fx-channel="0"]')`, 10000);
    const bl0 = await get(blinder.id);
    const intIdx = bl0.channelList.findIndex(c => c.group === 'Intensity');
    const otherIdx = bl0.channelList.findIndex(c => c.group !== 'Intensity');
    console.log('  channels: ' + bl0.channelList.map(c => c.name + '/' + c.group).join(', '));
    await clickSel(page, '[data-fx-sametype] span, [data-fx-sametype] div');
    const precScope = '[data-fx-prec="' + intIdx + '"]';
    await pickCombo(page, precScope, 'Auto (HTP)', 'Forced LTP');
    const ltpAll = await until(async () => { const ds = await Promise.all(sameType.map(f => get(f.id))); return ds.every(d => d.channelList[intIdx].precedence === 'ltp') ? ds : null; }, 'forced LTP on all').catch(() => null);
    check(!!ltpAll, 'Forced LTP on "' + bl0.channelList[intIdx].name + '" reaches all ' + sameType.length + ' fixtures of the same type');
    const fadeIdx = otherIdx >= 0 ? otherIdx : intIdx;
    await clickSel(page, '[data-fx-fade="' + fadeIdx + '"] span, [data-fx-fade="' + fadeIdx + '"] div');
    const noFade = await until(async () => { const ds = await Promise.all(sameType.map(f => get(f.id))); return ds.every(d => d.channelList[fadeIdx].canFade === false) ? ds : null; }, 'no fade on all').catch(() => null);
    check(!!noFade, 'unchecking "Fade" on channel ' + (fadeIdx + 1) + ' excludes it from fades on the whole type');
    if (otherIdx >= 0) {
      await pickCombo(page, '[data-fx-prec="' + otherIdx + '"]', 'Auto (LTP)', 'Forced HTP');
      const htp = await until(async () => (await get(blinder.id)).channelList[otherIdx].precedence === 'htp', 'forced htp').catch(() => false);
      check(htp, 'Forced HTP on the non-intensity channel "' + bl0.channelList[otherIdx].name + '"');
    }

    /* ---- 3. modifier editor: create a template, attach it to the same type ---- */
    console.log('\n[modifier editor]');
    await clickSel(page, '[data-fx-modedit="' + intIdx + '"]');
    await page.waitFor(`!!document.querySelector('[data-mod-editor]')`, 5000);
    check(await page.eval(`document.querySelectorAll('[data-mod-template]').length > 5`), 'the editor lists the system templates');
    await clickSel(page, '[data-mod-template=""]');
    await clickFn(page, btnText('Add handler', '[data-mod-editor]'), 'Add handler');
    const pts = await page.eval(`document.querySelectorAll('[data-mod-point]').length`);
    check(pts === 3, 'Add handler adds a point to the curve', pts);
    /* drag the new middle handler down: output lower than input */
    const r = await page.rectOf(`[data-mod-point="1"]`);
    await page.drag(r.x + r.w / 2, r.y + r.h / 2, r.x + r.w / 2 + 10, r.y + r.h / 2 + 60, 8);
    await sleep(200);
    await setInput(page, `document.querySelector('[data-mod-name]')`, 'E2E Curve');
    await clickFn(page, btnText('Save as template', '[data-mod-editor]'), 'Save as template');
    const saved = await until(async () => (await api.call('fixtures.modifiers.list')).templates.find(t => t.name === 'E2E Curve'), 'saved template').catch(() => null);
    check(saved && saved.isUser, 'Save as template creates the user template "E2E Curve"', saved);
    check(fs.existsSync(path.join(MODDIR, 'E2E Curve.qxmt')), 'the template file lands in the sandbox modifiers folder ' + MODDIR);
    const curve = await api.call('fixtures.modifiers.get', { name: 'E2E Curve' });
    check(curve.points.length === 3 && curve.points[1].modified < curve.points[1].original, 'the dragged handler is saved (modified < original)', curve.points);
    await shot(page, 'modifier-editor');
    await clickFn(page, btnText('Apply to channel'), 'Apply to channel');
    const modAll = await until(async () => { const ds = await Promise.all(sameType.map(f => get(f.id))); return ds.every(d => d.channelList[intIdx].modifier === 'E2E Curve') ? ds : null; }, 'modifier on all').catch(() => null);
    check(!!modAll, 'Apply to channel attaches "E2E Curve" to channel ' + (intIdx + 1) + ' of every fixture of the type');
    /* rename through the editor: channels follow the new name */
    await clickSel(page, '[data-fx-modedit="' + intIdx + '"]');
    await page.waitFor(`!!document.querySelector('[data-mod-template="E2E Curve"]')`, 5000);
    await clickSel(page, '[data-mod-template="E2E Curve"]');
    await sleep(300);
    await setInput(page, `document.querySelector('[data-mod-name]')`, 'E2E Curve 2');
    await clickFn(page, btnText('Rename', '[data-mod-editor]'), 'Rename');
    const renamed = await until(async () => (await get(blinder.id)).channelList[intIdx].modifier === 'E2E Curve 2', 'renamed').catch(() => false);
    check(renamed, 'Rename: the fixture channel now reports "E2E Curve 2"');
    check(fs.existsSync(path.join(MODDIR, 'E2E Curve 2.qxmt')) && !fs.existsSync(path.join(MODDIR, 'E2E Curve.qxmt')), 'Rename moves the template file');
    await clickFn(page, btnText('Cancel'), 'Cancel');
    await sleep(300);
    check(await until(() => page.eval(`(function(){ const el = document.querySelector('[data-fx-mod="${intIdx}"]'); return el && el.textContent.indexOf('E2E Curve 2') !== -1; })()`), 'combo label').catch(() => false), 'the channel row shows the renamed modifier');
    await shot(page, 'channel-behaviour');

    /* ---- 4. fixture summary ---- */
    console.log('\n[fixture summary]');
    await clickSel(page, '[data-fx-summary-open]');
    await page.waitFor(`!!document.querySelector('[data-fx-summary]')`, 8000);
    const sumText = await page.eval(`document.querySelector('[data-fx-summary]').innerText`);
    check(/Physical/.test(sumText) && /Address range/.test(sumText) && sumText.indexOf(bl0.channelList[0].name) !== -1, 'the summary shows definition, addressing, physical block and channels');
    await page.eval(`window.__printed = 0; window.print = function(){ window.__printed++; }`);
    await clickSel(page, '[data-fx-print]');
    check(await until(() => page.eval(`window.__printed > 0 && (document.getElementById('qlc-print-root') || {}).innerText.indexOf(${JSON.stringify(blinder.name)}) !== -1`), 'print').catch(() => false), 'Print fills the print area and calls window.print()');
    await shot(page, 'fixture-summary');
    await page.s.send('Emulation.setEmulatedMedia', { media: 'print' });
    await sleep(300);
    const printVisible = await page.eval(`(function(){ const r = document.getElementById('qlc-print-root'); const app = document.getElementById('root'); return getComputedStyle(r).display !== 'none' && (!app || getComputedStyle(app).display === 'none'); })()`);
    check(printVisible, 'print media shows only the summary');
    await shot(page, 'fixture-summary-print');
    await page.s.send('Emulation.setEmulatedMedia', { media: '' });
    await closeDialog(page);

    /* ---- 5. universe summary ---- */
    console.log('\n[universe summary]');
    await clickSel(page, '[data-ff-unisummary]');
    await page.waitFor(`!!document.querySelector('[data-uni-summary]')`, 8000);
    const inUni = (await api.call('fixtures.list')).fixtures.filter(f => f.universe === blinder.universe);
    const rows = await until(async () => { const n = await page.eval(`document.querySelectorAll('[data-uni-row]').length`); return n === inUni.length ? n : null; }, 'rows').catch(() => null);
    check(rows === inUni.length, 'the universe summary lists the ' + inUni.length + ' fixtures of ' + (blinder.universe + 1), rows);
    const used = inUni.reduce((a, f) => a + f.channels, 0);
    const totals = await until(async () => { const t = await page.eval(`document.querySelector('[data-uni-totals]').innerText`); return t.indexOf('…') === -1 ? t : null; }, 'totals').catch(() => '');
    check(totals.indexOf(used + ' / 512') !== -1 && /kg/.test(totals) && / W/.test(totals), 'channels used, weight and power totals', totals.replace(/\s+/g, ' '));
    await clickSel(page, '[data-uni-col="dip"] span, [data-uni-col="dip"] div');
    check(await page.eval(`document.querySelectorAll('[data-uni-row] [title^="DIP"]').length`) === inUni.length, 'DIP switch column per fixture');
    await clickSel(page, '[data-uni-print]');
    check(await until(() => page.eval(`window.__printed > 1 && document.getElementById('qlc-print-root').querySelectorAll('tr').length > ${inUni.length}`), 'uni print').catch(() => false), 'universe summary print');
    await shot(page, 'universe-summary');
    await closeDialog(page);

    /* ---- 6. RGB panel 4x4 ---- */
    console.log('\n[RGB panel]');
    let panelUni = null;
    for (const u of universes) if (await free(u.id, 48) >= 0) { panelUni = u; break; }
    await clickSel(page, '[data-ff-rgbpanel]');
    await page.waitFor(`!!document.querySelector('[data-rgbpanel]')`, 5000);
    await setInput(page, `document.querySelector('[data-rgbpanel-name]')`, 'E2E Panel');
    await setSpin(page, '[data-rgbpanel-cols]', 4);
    await setSpin(page, '[data-rgbpanel-rows]', 4);
    const uniLabel = universes[0].name;
    if (panelUni.id !== universes[0].id) await pickCombo(page, '[data-rgbpanel]', uniLabel, panelUni.name);
    await sleep(500); /* findAvailableAddress moves the address to a free block */
    await shot(page, 'rgb-panel-dialog');
    await clickFn(page, btnText('Add'), 'Add');
    const rowsFx = await until(async () => { const l = (await api.call('fixtures.list')).fixtures.filter(f => /^E2E Panel - Row \d$/.test(f.name)); return l.length === 4 ? l : null; }, 'panel rows').catch(() => null);
    check(!!rowsFx && rowsFx.every(f => f.channels === 12 && f.heads === 4), '4x4 RGB panel: 4 row fixtures of 12 channels / 4 heads', rowsFx && rowsFx.map(f => f.name + ':' + f.channels));
    const groups = (await api.call('fixtures.group.list')).groups;
    const panelGroup = groups.find(g => g.name === 'E2E Panel');
    check(panelGroup && panelGroup.headCount === 16 && panelGroup.size.columns === 4 && panelGroup.size.rows === 4, 'group "E2E Panel" 4x4 with 16 heads', panelGroup);

    /* ---- 7. grid editor ---- */
    console.log('\n[group grid editor]');
    const panelIds = rowsFx.sort((a, b) => a.address - b.address).map(f => f.id);
    await clickFn(page, `[...document.querySelectorAll('[title="Fixture Groups"]')].pop()`, 'Fixture Groups panel');
    await clickFn(page, leafText('E2E Panel'), 'group E2E Panel');
    await clickSel(page, '[data-group-grid-open]');
    await page.waitFor(`document.querySelectorAll('[data-grid-cell]').length === 16`, 8000);
    const ggrp = () => api.call('fixtures.group.get', { groupId: panelGroup.id });
    const headAt = (g, x, y) => { const h = g.heads.find(h => h.x === x && h.y === y); return h ? h.fixtureId + ':' + h.headIndex : null; };
    let g0 = await ggrp();
    const a0 = headAt(g0, 0, 0), b0 = headAt(g0, 1, 1);
    await clickSel(page, '[data-grid-cell="0,0"]');
    await clickSel(page, '[data-grid-cell="1,1"]', { modifiers: CTRL });
    await clickSel(page, '[data-grid-swap]');
    const swapped = await until(async () => { const g = await ggrp(); return headAt(g, 0, 0) === b0 && headAt(g, 1, 1) === a0 ? g : null; }, 'swap').catch(() => null);
    check(!!swapped, 'Swap exchanges the heads at (1,1) and (2,2) of the grid', { a0, b0 });
    /* rotate the whole group 90 degrees clockwise: (x,y) -> (3-y, x) */
    await clickSel(page, '[data-grid-cell="0,0"]'); /* deselect */
    await page.eval(`void 0`);
    g0 = await ggrp();
    const beforeRot = {}; g0.heads.forEach(h => { beforeRot[h.fixtureId + ':' + h.headIndex] = [h.x, h.y]; });
    const selNow = await page.eval(`[...document.querySelectorAll('[data-grid-cell]')].filter(c => c.style.border.indexOf('2px') !== -1).length`);
    if (selNow) await clickSel(page, '[data-grid-cell="0,0"]');
    await clickSel(page, '[data-grid-rotate="90"]');
    const rotated = await until(async () => { const g = await ggrp(); return g.heads.every(h => { const b = beforeRot[h.fixtureId + ':' + h.headIndex]; return b && h.x === 3 - b[1] && h.y === b[0]; }) ? g : null; }, 'rotate', 12000).catch(() => null);
    check(!!rotated && rotated.heads.length === 16, 'Rotate 90° moves every head (x,y) -> (3-y,x), none lost');
    /* grow to 5 columns and drag a head into the new empty column */
    await setSpin(page, '[data-grid-cols]', 5);
    await clickSel(page, '[data-grid-setsize]');
    await page.waitFor(`document.querySelectorAll('[data-grid-cell]').length === 20`, 8000);
    const g1 = await ggrp();
    check(g1.size.columns === 5, 'Set size 5x4');
    const moving = headAt(g1, 2, 3);
    const rc = await page.rectOf(`[data-grid-cell="2,3"]`), rt = await page.rectOf(`[data-grid-cell="4,0"]`);
    await page.drag(rc.x + rc.w / 2, rc.y + rc.h / 2, rt.x + rt.w / 2, rt.y + rt.h / 2, 6);
    const dragged = await until(async () => { const g = await ggrp(); return headAt(g, 4, 0) === moving && !headAt(g, 2, 3) ? g : null; }, 'drag').catch(() => null);
    check(!!dragged, 'dragging the head at (3,4) onto the empty cell (5,1) moves it', moving);
    await shot(page, 'grid-editor');
    /* regenerate in DMX order: 16 heads -> 4x4, row r = the r-th panel row by address */
    await clickFn(page, btnText('Regenerate in DMX order', '[data-grid-editor]'), 'Regenerate');
    const regen = await until(async () => { const g = await ggrp(); return g.size.columns === 4 && g.size.rows === 4 && g.heads.length === 16 && g.heads.every(h => h.fixtureId === panelIds[h.y] && h.headIndex === h.x) ? g : null; }, 'regenerate', 12000).catch(() => null);
    check(!!regen, 'Regenerate in DMX order: 4x4, row y holds the y-th panel row, head x at column x');
    await page.waitFor(`document.querySelectorAll('[data-grid-cell]').length === 16`, 8000);
    const transformCheck = async (label, sel, fn) => {
      const g = await ggrp(); const before = {}; g.heads.forEach(h => { before[h.fixtureId + ':' + h.headIndex] = [h.x, h.y]; });
      await clickFn(page, sel, label);
      const ok = await until(async () => { const n = await ggrp(); return n.heads.length === 16 && n.heads.every(h => { const b = before[h.fixtureId + ':' + h.headIndex]; const t = fn(b[0], b[1]); return b && h.x === t[0] && h.y === t[1]; }) ? n : null; }, label, 12000).catch(() => null);
      check(!!ok, label + ' on the whole group');
    };
    await transformCheck('Flip horizontally', q('[data-grid-flip="h"]'), (x, y) => [3 - x, y]);
    await transformCheck('Flip vertically', q('[data-grid-flip="v"]'), (x, y) => [x, 3 - y]);
    await transformCheck('Rotate 180°', btnText('180°', '[data-grid-editor]'), (x, y) => [3 - x, 3 - y]);
    await transformCheck('Rotate 270°', btnText('270°', '[data-grid-editor]'), (x, y) => [y, 3 - x]);
    /* remove one head, then place it back by picking it and clicking the empty cell */
    const gp = await ggrp();
    const h00 = gp.heads.find(h => h.x === 0 && h.y === 0);
    await clickSel(page, '[data-grid-cell="0,0"]');
    await clickFn(page, btnText('Remove', '[data-grid-editor]'), 'Remove');
    check(await until(async () => { const g = await ggrp(); return g.heads.length === 15 && !headAt(g, 0, 0); }, 'removed').catch(() => false), 'Remove takes the selected head out of the group');
    await clickSel(page, `[data-grid-head="${h00.fixtureId}:${h00.headIndex}"]`);
    await clickSel(page, '[data-grid-cell="0,0"]');
    check(await until(async () => { const g = await ggrp(); return g.heads.length === 16 && headAt(g, 0, 0) === h00.fixtureId + ':' + h00.headIndex; }, 'placed').catch(() => false), 'picking head ' + (h00.headIndex + 1) + ' of ' + h00.fixtureId + ' and clicking the empty cell (1,1) places it there');
    await shot(page, 'grid-editor-2');
    await clickFn(page, btnText('Reset', '[data-grid-editor]'), 'Reset');
    await clickFn(page, btnText('Reset'), 'confirm Reset');
    check(await until(async () => (await ggrp()).heads.length === 0, 'reset').catch(() => false), 'Reset empties the group');
    await closeDialog(page);

    /* ---- 8. colour filters + 9. console on the panel rows ---- */
    console.log('\n[colour filters]');
    await clickFn(page, `[...document.querySelectorAll('[title^="Fixture tools"]')].pop()`, 'Fixture tools panel');
    await selectInTree(page, 'E2E Panel - Row 1');
    const row1 = await get(panelIds[0]);
    await clickFn(page, sectionHead('Color filters'), 'Color filters section');
    await page.waitFor(`!!document.querySelector('[data-color-filter]')`, 8000);
    await setInput(page, `document.querySelector('[data-color-filter-search]')`, 'Gold');
    await clickSel(page, '[data-color-filter="Gold"]');
    const dmx = async (f) => (await api.call('io.dmx.universe.get', { universeId: f.universe })).values;
    const gold = await until(async () => { const v = await dmx(row1); return v[row1.address] === 255 && v[row1.address + 1] === 215 && v[row1.address + 2] === 0 ? v : null; }, 'gold').catch(() => null);
    check(!!gold, 'picking the "Gold" filter writes #FFD700 to the row', gold && [gold[row1.address], gold[row1.address + 1], gold[row1.address + 2]]);
    await shot(page, 'color-filters');

    console.log('\n[fixture console]');
    await clickFn(page, sectionHead('Channels'), 'Channels section');
    await page.waitFor(`!!document.querySelector('[data-fx-console] [data-console-fader="0"]')`, 8000);
    await setInput(page, `document.querySelector('[data-console-fader="0"]')`, 200);
    await clickSel(page, '[data-console-multi]');
    await clickSel(page, '[data-console-sel="1"]');
    await clickSel(page, '[data-console-sel="2"]');
    await setInput(page, `document.querySelector('[data-console-fader="1"]')`, 77);
    const multi = await until(async () => { const v = await dmx(row1); return v[row1.address + 1] === 77 && v[row1.address + 2] === 77 ? v : null; }, 'multi').catch(() => null);
    check(!!multi, 'multiple channel selection: one fader moves channels 2 and 3 together');
    await clickSel(page, '[data-console-copy]');
    const others = rowsFx.filter(f => f.id !== row1.id);
    const copied = await until(async () => { for (const f of others) { const v = await dmx(f); if (v[f.address + 1] !== 77 || v[f.address + 2] !== 77) return null; } return true; }, 'copy').catch(() => false);
    check(copied, 'copy to all fixtures of the same type writes the selected channels to the other 3 rows');
    const pages1 = await page.eval(`document.querySelector('[data-console-page]').innerText`);
    check(pages1 === '1/1', 'a 12-channel row fits one fader window', pages1);
    await shot(page, 'console');

    /* pan/tilt mode + fader window on a Gobo Spot (more than 12 channels, pan and tilt) */
    const spot = all.find(f => f.manufacturer === 'Gobo Spot') || all.find(f => f.channels > 12);
    if (spot) {
      await selectInTree(page, spot.name);
      await page.waitFor(`!!document.querySelector('[data-console-fader="0"]')`, 8000).catch(() => {});
      const spotDetail = await get(spot.id);
      const pages = await page.eval(`(document.querySelector('[data-console-page]') || {}).innerText`);
      if (spotDetail.channels > 12) {
        await clickSel(page, '[data-console-next]');
        const shifted = await page.eval(`!!document.querySelector('[data-console-fader="12"]') && !document.querySelector('[data-console-fader="0"]')`);
        check(shifted, 'fader window shift: the next page shows channel 13 on (' + pages + ')');
      }
      await clickSel(page, '[data-console-pantilt]');
      const ptFaders = await page.eval(`[...document.querySelectorAll('[data-console-fader]')].map(e => e.title)`);
      check(ptFaders.length >= 2 && ptFaders.every(t => /pan|tilt/i.test(t)), 'pan/tilt mode shows only the pan/tilt faders', ptFaders);
      await clickSel(page, '[data-console-pantilt]');
    }

    /* ---- single-axis position tool (only for pan-only / tilt-only fixtures) ---- */
    const oneAxis = [];
    for (const f of all.filter((f, i, a) => a.findIndex(x => x.manufacturer === f.manufacturer && x.model === f.model && x.mode === f.mode) === i)) {
      const d = await get(f.id);
      const p = d.channelList.some(c => c.group === 'Pan'), t = d.channelList.some(c => c.group === 'Tilt');
      if (p !== t) { oneAxis.push({ f, axis: p ? 'pan' : 'tilt' }); break; }
    }
    if (oneAxis.length) {
      const { f, axis } = oneAxis[0];
      await selectInTree(page, f.name);
      const shown = await until(() => page.eval(`!!document.querySelector('[data-single-axis="${axis}"]')`), 'single axis').catch(() => false);
      check(shown, 'the ' + axis + '-only fixture "' + f.name + '" gets the single-axis position tool');
      if (shown) await shot(page, 'single-axis');
    } else console.log('  (no pan-only / tilt-only fixture in the project: single-axis tool not exercised)');

    /* ---- saveAs + XML ---- */
    console.log('\n[saveAs]');
    await api.call('core.project.saveAs', { target: 'serverPath', path: OUT });
    const xml = fs.readFileSync(OUT, 'utf8');
    const fxXml = (id) => (xml.match(new RegExp('<Fixture>\\s*<Manufacturer>[^<]*</Manufacturer>\\s*<Model>[^<]*</Model>\\s*<Mode>[^<]*</Mode>\\s*<ID>' + id + '</ID>[\\s\\S]*?</Fixture>')) || [''])[0];
    check(/<Mode>5 Channels<\/Mode>/.test(fxXml(parId)), 'out.qxw: the Eurolite is saved in "5 Channels"', fxXml(parId).slice(0, 200));
    const blXml = fxXml(blinder.id);
    check(new RegExp('<ForcedLTP>' + intIdx).test(blXml) && /<ExcludeFade>/.test(blXml) && /<Modifier Channel="\d+" Name="E2E Curve 2"/.test(blXml), 'out.qxw: forced LTP, exclude-fade and the modifier on the SF3 fixture', blXml.slice(0, 600));
    check(/<Name>E2E Panel<\/Name>/.test(xml) && (xml.match(/<Model>RGBPanel<\/Model>|E2E Panel - Row/g) || []).length >= 4, 'out.qxw: the RGB panel rows and group');

    /* ---- modifier delete from the editor ---- */
    console.log('\n[modifier delete]');
    await clickFn(page, `[...document.querySelectorAll('[title^="Fixture tools"]')].pop()`, 'close tools').catch(() => {});
    await selectInTree(page, blinder.name);
    await clickSel(page, '[data-fx-modedit="' + intIdx + '"]');
    await clickSel(page, '[data-mod-template="E2E Curve 2"]');
    await sleep(300);
    await clickFn(page, btnText('Delete', '[data-mod-editor]'), 'Delete');
    await clickFn(page, btnText('Delete'), 'confirm Delete');
    const detached = await until(async () => (await get(blinder.id)).channelList[intIdx].modifier === null, 'detached').catch(() => false);
    check(detached && !fs.existsSync(path.join(MODDIR, 'E2E Curve 2.qxmt')), 'Delete removes the file and detaches the template from the channels');
    await clickFn(page, btnText('Cancel'), 'Cancel');

    const realAfter = fs.existsSync(REAL_MODDIR) ? fs.readdirSync(REAL_MODDIR) : [];
    check(JSON.stringify(realAfter) === JSON.stringify(realBefore), 'nothing landed in the real ' + REAL_MODDIR, realAfter);
    await shot(page, 'final');
  } catch (e) {
    await shot(page, 'failure').catch(() => {});
    console.log('  ERROR ' + e.message);
    failures.push('exception: ' + e.message);
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
