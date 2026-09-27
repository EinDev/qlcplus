/**
 * End-to-end driver for the Virtual Console layout / configuration slice (Frame, SoloFrame, Label,
 * Button and Slider properties, PIN prompts, align / distribute / bulk style, add-from-functions,
 * create-matrix, usage) against a sandbox instance (dev-webui-sandbox.ps1) in headless Chrome.
 *
 *   node webui/tools/e2e/vc-layout.js
 *   env: QLC_API (ws://127.0.0.1:9150/), QLC_WEB (http://localhost:9151/), QLC_OUT (saveAs path,
 *        C:\qlcsandbox\vclayout\out.qxw), QLC_SHOTS (screenshot dir)
 *
 * Every UI gesture is followed by a read-back over a second, raw API connection (vc.widget.get /
 * vc.widget.list / io.dmx.universe.get), and the run ends with core.project.saveAs + a grep of the
 * written .qxw. Exit code 1 when any check fails or the page logged a console error.
 */
const fs = require('fs');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const API = process.env.QLC_API || 'ws://127.0.0.1:9150/';
const WEB = process.env.QLC_WEB || 'http://localhost:9151/';
const OUT = process.env.QLC_OUT || 'C:\\qlcsandbox\\vclayout\\out.qxw';
const SHOTS = process.env.QLC_SHOTS || path.join(process.cwd(), 'e2e-shots');

/* ---------------------------------------------------------------- raw API client */
class Api {
  constructor(url) { this.url = url; this.rev = 0; this.next = 1; this.pending = new Map(); this.events = []; }
  connect() {
    return new Promise((res, rej) => {
      this.ws = new WebSocket(this.url);
      this.ws.onerror = (e) => rej(new Error('API socket error ' + (e && e.message)));
      this.ws.onmessage = (m) => {
        const f = JSON.parse(m.data);
        if (f.type === 'response') {
          const p = this.pending.get(f.id); this.pending.delete(f.id);
          if (f.ok && f.result && f.result.docRevision != null) this.rev = f.result.docRevision;
          if (!f.ok && f.error && f.error.details && f.error.details.docRevision != null) this.rev = f.error.details.docRevision;
          if (p) f.ok ? p.res(f.result) : p.rej(Object.assign(new Error(f.error.message), f.error));
        } else if (f.type === 'event') {
          this.events.push(f);
          if (f.data && f.data.docRevision != null) this.rev = f.data.docRevision;
        }
      };
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'vc-layout e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
    });
  }
  call(method, params = {}) {
    const id = 'e-' + (this.next++);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  /** Structural call: current baseRevision, one retry on CONFLICT. */
  structural(method, params = {}) {
    const go = () => this.call(method, Object.assign({}, params, { baseRevision: this.rev }));
    return go().catch(e => { if (e.code === 'CONFLICT') return go(); throw e; });
  }
  widget(id) { return this.call('vc.widget.get', { widgetId: String(id) }); }
  close() { try { this.ws.close(); } catch (e) { } }
}

/* ---------------------------------------------------------------- checks */
const failures = [];
let checks = 0;
function check(cond, msg, extra) {
  checks++;
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

/* ---------------------------------------------------------------- page helpers */
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const byText = (tag, text) => `[...document.querySelectorAll(${JSON.stringify(tag)})].find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
async function clickFn(page, fnBody, what) {
  /* Also wait for the control to be enabled: a field gated on a config flag stays disabled until the
     browser's own copy of the widget catches up with the server (the API read-back is a step ahead). */
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  /* Page tabs and long lists overflow-scroll: bring the target into view first or the click lands on whatever covers it. */
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest', inline: 'nearest' }); })()`);
  await page.click(new Function('return ' + fnBody));
}
const clickText = (page, tag, text) => clickFn(page, byText(tag, text), tag + ' "' + text + '"');
/** CheckRow: the aria-pressed button whose row text is the label (minus the Font Awesome check glyph
    the button itself renders while checked - a private-use codepoint). */
const checkRow = (label) => `[...document.querySelectorAll('button[aria-pressed]')].find(b => b.parentElement && b.parentElement.textContent.replace(/[\\uE000-\\uF8FF]/g, '').trim() === ${JSON.stringify(label)})`;
/** $currently (optional): wait until the box renders that state first - the API read-back runs
    ahead of the browser's own copy, so a second click right after the first would toggle a stale box. */
async function clickCheck(page, label, currently) {
  if (currently !== undefined) await page.waitFor(`(function(){ const b = ${checkRow(label)}; return !!b && b.getAttribute('aria-pressed') === ${JSON.stringify(String(currently))}; })()`);
  await clickFn(page, checkRow(label), 'check "' + label + '"');
}
async function typeInto(page, fnBody, text, what) {
  await clickFn(page, fnBody, what);
  /* The click puts the caret wherever it landed; select everything so the typed text replaces the value. */
  await page.eval(`(function(){ const el = document.activeElement; if (el && el.select) el.select(); })()`);
  await page.type(text);
  await page.key('Enter');
}
const toolbarTool = (name) => `document.querySelector('[data-vc-tool="${name}"]')`;
/** FunctionPicker: type the name into "Search functions" and click the matching row (retrying the
    typing once, a property-panel re-render right after a mode switch can swallow the first keystrokes). */
async function pickFunction(page, fn, verify) {
  const row = `[...document.querySelectorAll('div[role="button"]')].find(e => e.textContent.trim().startsWith(${JSON.stringify(fn.name)}) && e.textContent.includes(${JSON.stringify(fn.type)}))`;
  for (let attempt = 0; ; attempt++) {
    await clickFn(page, q('input[placeholder="Search functions"]'), 'function search');
    await page.eval(`(function(){ const el = document.activeElement; if (el && el.select) el.select(); })()`);
    await page.type(fn.name);
    const found = await page.waitFor(`!!(${row})`, 2500).catch(() => false);
    if (found) {
      await sleep(150); // let the match list settle before aiming at the row
      await clickFn(page, row, 'function row ' + fn.name);
      const done = await until(verify, 'function pick to land', 2500).catch(() => false);
      if (done) return;
    }
    if (attempt >= 2) throw new Error('picking function ' + fn.name + ' did not take');
  }
}
/** Click the page canvas at PAGE coordinates (the canvas is CSS-scaled to fit the viewport). */
async function canvasClick(page, x, y) {
  const r = await page.rectOf(() => document.querySelector('div[style*="crosshair"]'));
  const scale = await page.eval(`(function(){ const c = document.querySelector('div[style*="crosshair"]'); return c.getBoundingClientRect().width / (c.offsetWidth || 1); })()`);
  const sx = r.x + x * scale, sy = r.y + y * scale;
  await page.mouse('mouseMoved', sx, sy);
  await page.mouse('mousePressed', sx, sy); await page.mouse('mouseReleased', sx, sy);
}
/** A free page-root spot below every widget currently on $pageIndex (so the click lands on the canvas, not on a widget). */
async function freeSpot(api, pageIndex, x = 40) {
  const ws = (await api.call('vc.widget.list', { page: pageIndex })).widgets.filter(w => !w.parentId);
  const bottom = ws.reduce((m, w) => Math.max(m, (w.geometry.y || 0) + (w.geometry.height || 0)), 0);
  return { x, y: bottom + 40 };
}
async function placeWidget(page, api, paletteName, x, y, type) {
  const before = new Set((await api.call('vc.widget.list')).widgets.map(w => w.id));
  /* The side panel shows either the palette or the properties: open the palette when it is not showing. */
  if (!(await page.eval(`document.body.textContent.includes('Pick a widget')`))) {
    await clickFn(page, `document.querySelector('button[title="Add a new widget to the console"]')`, 'palette rail button');
    await page.waitFor(`document.body.textContent.includes('Pick a widget')`);
    await sleep(400);
  }
  /* The side panel slides open with a CSS transition: a click computed while a row is still moving
     misses it, so retry until the page reports the "placing" state (crosshair cursor). */
  for (let attempt = 0; ; attempt++) {
    await clickFn(page, `[...document.querySelectorAll('div[role="button"]')].find(e => e.textContent.trim() === ${JSON.stringify(paletteName)} && e.querySelector('img'))`, 'palette ' + paletteName);
    const placing = await page.waitFor(`!!document.querySelector('div[style*="crosshair"]')`, 1500).catch(() => false);
    if (placing) break;
    if (attempt >= 3) throw new Error('palette entry ' + paletteName + ' never entered placing mode');
    await sleep(400);
  }
  await canvasClick(page, x, y);
  const created = await until(async () => (await api.call('vc.widget.list')).widgets.find(w => !before.has(w.id) && w.widgetType === type), 'placed ' + paletteName);
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${created.id}"][data-vc-selected]')`);
  return created;
}

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev);

  if ((await api.call('core.mode.get')).mode !== 'design') await api.call('core.mode.set', { mode: 'design' });
  const fixtures = (await api.call('fixtures.list')).fixtures;
  const functions = (await api.call('functions.list')).functions.filter(f => !f.hidden);
  const scenes = functions.filter(f => f.type === 'Scene');
  check(fixtures.length > 0 && scenes.length >= 3, 'sandbox project has fixtures and scenes', { fixtures: fixtures.length, scenes: scenes.length });

  // A scratch page at the end of the console
  const pagesBefore = (await api.call('vc.page.list')).pages;
  await api.structural('vc.page.create', { index: pagesBefore.length });
  const scratch = pagesBefore.length;
  const scratchName = (await api.call('vc.page.list')).pages[scratch].name;
  console.log('scratch page #' + (scratch + 1) + ' "' + scratchName + '"');

  const browser = await launch({ headless: true });
  const page = await browser.open(WEB + '?ctx=vc', { width: 1700, height: 1000 });
  try {
    await page.waitFor(`document.querySelectorAll('[title^="Page "]').length >= ${scratch + 1}`, 30000);
    await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${scratch + 1}'))`, 'scratch page tab');
    await page.waitFor(`document.body.textContent.includes('This page has no widgets')`);
    // Edit mode
    await clickFn(page, `document.querySelector('button img[src$="/edit.svg"]').closest('button')`, 'edit button');
    await page.waitFor(`document.body.textContent.includes('Pick a widget')`);
    await sleep(500); // side panel slide-in
    console.log('edit mode on');

    /* ---- Frame: multipage, labels, header, enable button, PIN ---- */
    console.log('\n[Frame]');
    const frame = await placeWidget(page, api, 'Frame', 40, 40, 'Frame');
    let cfg = (await api.widget(frame.id)).typeConfig;
    check(cfg.showHeader === true && cfg.multiPageMode === false && cfg.totalPagesNumber === 1 && Array.isArray(cfg.pageLabels), 'frame typeConfig exposes VcFrameConfig', cfg);
    await clickCheck(page, 'Enable pages');
    await until(async () => (await api.widget(frame.id)).typeConfig.multiPageMode === true, 'multiPageMode');
    check(true, 'Enable pages -> multiPageMode true');
    await typeInto(page, q('input[data-vc-frame-pages]'), '3', 'pages number');
    await until(async () => (await api.widget(frame.id)).typeConfig.totalPagesNumber === 3, 'totalPagesNumber 3');
    cfg = (await api.widget(frame.id)).typeConfig;
    check(cfg.pageLabels.length === 3 && cfg.pageLabels[2].label === 'Page 3', 'three default page labels', cfg.pageLabels);
    await typeInto(page, `document.querySelector('input[placeholder="Page 2"]')`, 'Intro', 'page 2 label');
    await until(async () => (await api.widget(frame.id)).typeConfig.pageLabels[1].label === 'Intro', 'page label Intro');
    check(true, 'page 2 label -> "Intro"');
    await clickCheck(page, 'Circular pages scrolling');
    await until(async () => (await api.widget(frame.id)).typeConfig.pagesLoop === true, 'pagesLoop');
    check(true, 'Circular pages scrolling -> pagesLoop true');
    // VCFrame defaults showEnable to true: off -> the header's Enable button disappears, on -> back
    await clickCheck(page, 'Show enable button', true);
    await until(async () => (await api.widget(frame.id)).typeConfig.showEnable === false, 'showEnable false');
    await page.waitFor(`!document.querySelector('[data-vc-widget="${frame.id}"] [data-vc-frame-enable]')`);
    await clickCheck(page, 'Show enable button', false);
    await until(async () => (await api.widget(frame.id)).typeConfig.showEnable === true, 'showEnable true');
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${frame.id}"] [data-vc-frame-enable]')`);
    check(true, 'Show enable button -> showEnable follows and the Enable button is drawn in the header only when on');
    await clickCheck(page, 'Show header', true);
    await until(async () => (await api.widget(frame.id)).typeConfig.showHeader === false, 'showHeader false');
    await page.waitFor(`!document.querySelector('[data-vc-widget="${frame.id}"] img[src$="/frame.svg"]')`);
    check(true, 'Show header off -> header gone');
    await clickCheck(page, 'Show header', false);
    await until(async () => (await api.widget(frame.id)).typeConfig.showHeader === true, 'showHeader true');
    // Frame page flip shows the page label in the pager
    await api.call('vc.frame.gotoPage', { widgetId: String(frame.id), page: 1 });
    await page.waitFor(`(document.querySelector('[data-vc-widget="${frame.id}"] [data-vc-frame-pager]') || {textContent: ''}).textContent.includes('Intro')`);
    check(true, 'pager shows the page label after vc.frame.gotoPage');
    await api.call('vc.frame.gotoPage', { widgetId: String(frame.id), page: 0 });
    // Clone first page: put a button on page 0 then clone
    const child = await api.structural('vc.widget.create', { widgetType: 'Button', page: scratch, parentId: String(frame.id), geometry: { x: 10, y: 30, width: 40, height: 40 }, style: { caption: 'C' } });
    await sleep(300);
    await clickFn(page, q('[data-vc-frame-clone]'), 'clone first page');
    await until(async () => (await api.call('vc.widget.list', { parentId: String(frame.id) })).widgets.length === 3, '3 children after clone');
    check(true, 'Clone first page widgets -> 2 copies of the button (one per further page)');
    // PIN
    await clickFn(page, q('[data-vc-frame-pin]'), 'Set a PIN');
    await clickFn(page, q('input[data-vc-pin-field="new"]'), 'new PIN'); await page.type('1234');
    await clickFn(page, q('input[data-vc-pin-field="confirm"]'), 'confirm PIN'); await page.type('1234');
    await clickText(page, 'button', 'Apply');
    await until(async () => (await api.widget(frame.id)).typeConfig.hasPin === true, 'hasPin');
    check(true, 'Set a PIN -> hasPin true');
    check((await api.call('vc.frame.validatePin', { widgetId: String(frame.id), pin: '1234' })).valid === true && (await api.call('vc.frame.validatePin', { widgetId: String(frame.id), pin: '0000' })).valid === false, 'vc.frame.validatePin accepts 1234 only');
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${frame.id}"] [data-vc-frame-locked]')`);
    check(true, 'frame body is locked behind the PIN');
    await clickText(page, 'button', 'Unlock');
    await clickFn(page, q('input[data-vc-pin-input]'), 'PIN input'); await page.type('9999'); await page.key('Enter');
    await page.waitFor(`document.body.textContent.includes('invalid or incorrect')`);
    check(true, 'wrong PIN is refused');
    await page.type('1234'); await page.key('Enter');
    await page.waitFor(`!document.querySelector('[data-vc-widget="${frame.id}"] [data-vc-frame-locked]')`);
    check(true, 'correct PIN unlocks the frame for this session');
    await page.screenshot(path.join(SHOTS, 'vc-layout-1-frame.png'));

    /* ---- Page PIN prompt ---- */
    console.log('\n[Page PIN]');
    await api.structural('vc.page.setPin', { index: scratch, newPIN: '2468' });
    // vc.page.updated tells the browser about the new PIN: the tab gains the lock glyph (FA U+F023)
    await page.waitFor(`(function(){ const t = [...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${scratch + 1}')); return !!t && t.textContent.indexOf('\\uf023') !== -1; })()`);
    check(true, 'vc.page.updated puts the lock on the page tab');
    await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page 1'))`, 'page 1 tab');
    await page.waitFor(`!document.querySelector('[data-vc-widget="${frame.id}"]')`);
    await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${scratch + 1}'))`, 'scratch page tab');
    await page.waitFor(`!!document.querySelector('input[data-vc-pin-input]')`);
    check(true, 'switching to a PIN-protected page asks for the PIN');
    await clickFn(page, q('input[data-vc-pin-input]'), 'PIN input'); await page.type('2468'); await page.key('Enter');
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${frame.id}"]')`);
    check(true, 'page PIN accepted, page shown');
    // The page switch dropped edit mode's selection but not edit mode itself; make sure edit mode is still on
    await page.waitFor(`document.body.textContent.includes('Pick a widget') || document.body.textContent.includes('Select a widget first')`);
    await api.structural('vc.page.setPin', { index: scratch, currentPIN: '2468', newPIN: '' });

    /* ---- Slider: Level mode with two fixture channels, DMX ---- */
    console.log('\n[Slider]');
    const slider = await placeWidget(page, api, 'Slider', 520, 40, 'Slider');
    cfg = (await api.widget(slider.id)).typeConfig;
    check(Array.isArray(cfg.levelChannels) && 'monitorEnabled' in cfg && 'clickAndGoType' in cfg && 'grandMasterValueMode' in cfg && 'controlledFunction' in cfg && 'adjustFlashEnabled' in cfg, 'slider typeConfig exposes the full VcSliderConfig', cfg);
    // A new VCSlider starts in Adjust mode: exercise Function Control there first
    const adjustScene = scenes[1];
    await clickCheck(page, 'Adjust');
    await until(async () => (await api.widget(slider.id)).typeConfig.sliderMode === 'Adjust', 'Adjust');
    await pickFunction(page, adjustScene, async () => String((await api.widget(slider.id)).typeConfig.controlledFunction) === String(adjustScene.id));
    await clickCheck(page, 'Show flash button');
    await until(async () => (await api.widget(slider.id)).typeConfig.adjustFlashEnabled === true, 'adjustFlashEnabled');
    check(true, 'Adjust mode: controlled function "' + adjustScene.name + '" + flash button');
    check((await api.call('vc.slider.flash', { widgetId: String(slider.id), on: true })) !== null, 'vc.slider.flash accepted on an Adjust slider with the flash button');
    await api.call('vc.slider.flash', { widgetId: String(slider.id), on: false });
    await clickCheck(page, 'Level');
    await until(async () => (await api.widget(slider.id)).typeConfig.sliderMode === 'Level', 'Level');
    check(true, 'switched to Level mode');
    await clickFn(page, q('[data-vc-levelpick]'), 'Pick channels');
    await page.waitFor(`document.querySelectorAll('[data-vc-levelfx]').length > 0`);
    const fx = fixtures.find(f => Number(f.channels) >= 2) || fixtures[0];
    const fxRow = `document.querySelector('[data-vc-levelfx="${fx.id}"]')`;
    await page.eval(`(function(){ const el = ${fxRow}; if (el) el.scrollIntoView(); })()`);
    await clickFn(page, `${fxRow}.querySelector('button')`, 'expand fixture ' + fx.name);
    await page.waitFor(`document.querySelectorAll('[data-vc-levelch^="${fx.id}:"]').length >= 2`);
    await clickFn(page, q(`[data-vc-levelch="${fx.id}:0"]`), 'channel 1');
    await until(async () => (await api.widget(slider.id)).typeConfig.levelChannels.length === 1, 'one level channel');
    await clickFn(page, q(`[data-vc-levelch="${fx.id}:1"]`), 'channel 2');
    await until(async () => (await api.widget(slider.id)).typeConfig.levelChannels.length === 2, 'two level channels');
    cfg = (await api.widget(slider.id)).typeConfig;
    check(cfg.levelChannels.every(c => String(c.fixtureId) === String(fx.id)) && cfg.levelChannels.map(c => c.channel).sort().join() === '0,1', 'two channels of ' + fx.name + ' picked', cfg.levelChannels);
    // Switching modes leaves monitoring off (VCSlider::setSliderMode): on -> off round trip
    await clickCheck(page, 'Monitor channel levels', false);
    await until(async () => (await api.widget(slider.id)).typeConfig.monitorEnabled === true, 'monitorEnabled true');
    check(true, 'Monitor channel levels toggled on');
    await clickCheck(page, 'Monitor channel levels', true);
    await until(async () => (await api.widget(slider.id)).typeConfig.monitorEnabled === false, 'monitorEnabled false');
    await clickCheck(page, 'Monitor channel levels', false);
    await until(async () => (await api.widget(slider.id)).typeConfig.monitorEnabled === true, 'monitorEnabled true again');
    // "External input" starts collapsed (like "Values range"): open the section first
    await clickFn(page, `[...document.querySelectorAll('div, span')].find(e => e.children.length <= 3 && e.textContent.trim() === 'External input')`, 'External input section header');
    await clickCheck(page, 'Catch up with the external controller input value');
    await until(async () => (await api.widget(slider.id)).typeConfig.catchValues === true, 'catchValues');
    check(true, 'Catch up with the external controller input value -> catchValues true');
    // leave edit mode, move the fader, read DMX
    await clickFn(page, `document.querySelector('button img[src$="/edit.svg"]').closest('button')`, 'edit button');
    await page.waitFor(`!document.querySelector('[data-vc-selected]') && !document.body.textContent.includes('Pick a widget')`);
    const fr = await page.rectOf('[data-vc-widget="' + slider.id + '"] [data-vc-fader]');
    await page.drag(fr.x + fr.w / 2, fr.y + fr.h - 4, fr.x + fr.w / 2, fr.y + 4, 12);
    const fxDetail = await api.call('fixtures.get', { fixtureId: String(fx.id) });
    const dmx = await until(async () => {
      const u = await api.call('io.dmx.universe.get', { universeId: Number(fxDetail.universe) });
      const a = Number(fxDetail.address);
      return u.values[a] > 200 && u.values[a + 1] > 200 ? [u.values[a], u.values[a + 1]] : null;
    }, 'DMX on both channels');
    check(true, 'fader drag drives DMX of both level channels (universe ' + (Number(fxDetail.universe) + 1) + ' addr ' + (Number(fxDetail.address) + 1) + ')', dmx);
    await api.call('vc.slider.setValue', { widgetId: String(slider.id), value: 0 });
    await clickFn(page, `document.querySelector('button img[src$="/edit.svg"]').closest('button')`, 'edit button');
    await page.waitFor(`document.body.textContent.includes('Pick a widget')`);

    /* ---- Button: every option ---- */
    console.log('\n[Button]');
    const button = await placeWidget(page, api, 'Button', 640, 40, 'Button');
    const scene = scenes[0];
    await pickFunction(page, scene, async () => String((await api.widget(button.id)).typeConfig.functionID) === String(scene.id));
    check(true, 'attached function "' + scene.name + '"');
    await clickCheck(page, 'Flash Function (only for Scenes)');
    await until(async () => (await api.widget(button.id)).typeConfig.actionType === 'Flash', 'Flash');
    await clickCheck(page, 'Override priority');
    await until(async () => (await api.widget(button.id)).typeConfig.flashOverrides === true, 'flashOverrides');
    await clickCheck(page, 'Force LTP');
    await until(async () => (await api.widget(button.id)).typeConfig.flashForceLTP === true, 'flashForceLTP');
    check(true, 'Flash + Override priority + Force LTP');
    await clickCheck(page, 'Enable');
    await until(async () => (await api.widget(button.id)).typeConfig.startupIntensityEnabled === true, 'startupIntensityEnabled');
    await typeInto(page, `[...document.querySelectorAll('input')].find(i => /%$/.test(i.value))`, '50', 'startup intensity');
    await until(async () => Math.abs((await api.widget(button.id)).typeConfig.startupIntensity - 0.5) < 0.001, 'startupIntensity 0.5');
    check(true, 'startup intensity enabled at 50%');
    await clickCheck(page, 'Stop all Functions');
    await until(async () => (await api.widget(button.id)).typeConfig.actionType === 'StopAll', 'StopAll');
    await typeInto(page, `[...document.querySelectorAll('input')].find(i => / ms$/.test(i.value))`, '1500', 'stop all fade out');
    await until(async () => (await api.widget(button.id)).typeConfig.stopAllFadeOutTime === 1500, 'stopAllFadeOutTime');
    check(true, 'Stop all Functions with a 1500 ms fade out');
    await clickCheck(page, 'Toggle Blackout');
    await until(async () => (await api.widget(button.id)).typeConfig.actionType === 'Blackout', 'Blackout');
    await clickCheck(page, 'Toggle Function on/off');
    await until(async () => (await api.widget(button.id)).typeConfig.actionType === 'Toggle', 'Toggle');
    check(true, 'Blackout and Toggle actions');
    cfg = (await api.widget(button.id)).typeConfig;
    check(String(cfg.functionID) === String(scene.id) && cfg.flashOverrides && cfg.flashForceLTP && cfg.startupIntensityEnabled && cfg.stopAllFadeOutTime === 1500, 'button config persisted as a whole', cfg);
    // Usage popup from the toolbar for this button's function
    await clickFn(page, toolbarTool('usage'), 'usage tool');
    await page.waitFor(`document.querySelectorAll('[data-vc-usage-row]').length >= 1`);
    const usageRows = await page.eval(`[...document.querySelectorAll('[data-vc-usage-row]')].map(e => e.getAttribute('data-vc-usage-row'))`);
    check(usageRows.indexOf(String(button.id)) !== -1, 'usage popup lists the button among the users of "' + scene.name + '"', usageRows);
    await clickText(page, 'button', 'Close');
    await page.waitFor(`!document.querySelector('[data-vc-usage]')`);

    /* ---- Align / distribute / bulk style ---- */
    console.log('\n[Align / distribute / bulk style]');
    const mk = (x, y) => api.structural('vc.widget.create', { widgetType: 'Button', page: scratch, geometry: { x, y, width: 40, height: 40 }, style: { caption: 'A' } }).then(r => r.widgetId);
    const a1 = await mk(760, 120), a2 = await mk(820, 200), a3 = await mk(1000, 60);
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${a3}"]')`);
    await page.click(`[data-vc-widget="${a1}"]`);
    await page.click(`[data-vc-widget="${a2}"]`, { modifiers: 2 });
    await page.click(`[data-vc-widget="${a3}"]`, { modifiers: 2 });
    await page.waitFor(`document.querySelectorAll('[data-vc-selected]').length === 3`);
    await clickFn(page, toolbarTool('align-top'), 'align top');
    await until(async () => (await api.widget(a2)).geometry.y === 120 && (await api.widget(a3)).geometry.y === 120, 'aligned top');
    check((await api.widget(a1)).geometry.y === 120, 'Align top moves the others to the first selected widget');
    await clickFn(page, toolbarTool('distribute-x'), 'distribute horizontally');
    await until(async () => (await api.widget(a2)).geometry.x === 880, 'distributed');
    check((await api.widget(a1)).geometry.x === 760 && (await api.widget(a3)).geometry.x === 1000, 'Distribute horizontally spaces the middle widget evenly (760 / 880 / 1000)');
    await clickFn(page, toolbarTool('align-left'), 'align left');
    await until(async () => (await api.widget(a3)).geometry.x === 760, 'aligned left');
    check(true, 'Align left');
    await typeInto(page, `[...document.querySelectorAll('input')].find(i => i.closest('span') && i.value === 'A')`, 'Bulk', 'bulk caption');
    await until(async () => (await api.widget(a3)).style.caption === 'Bulk' && (await api.widget(a1)).style.caption === 'Bulk', 'bulk caption');
    check((await api.widget(a2)).style.caption === 'Bulk', 'caption of a 3-widget selection set through vc.widget.bulkStyle');
    const bulkEvent = api.events.find(e => e.topic === 'vc.widget.bulkUpdated' && e.data.widgets.length === 3 && e.data.widgets[0].style.caption === 'Bulk');
    check(!!bulkEvent, 'vc.widget.bulkUpdated carried the three restyled widgets');

    /* ---- Add widgets from functions ---- */
    console.log('\n[Create from functions]');
    await page.eval(`(function(){ const c = document.querySelector('div[style*="dashed"]'); c && c.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); })()`);
    await page.waitFor(`document.querySelectorAll('[data-vc-selected]').length === 0`);
    const countBefore = (await api.call('vc.widget.list', { page: scratch })).widgets.length;
    await clickFn(page, toolbarTool('from-functions'), 'from functions tool');
    await page.waitFor(`document.querySelectorAll('[data-vc-fn-row]').length >= 3`);
    for (const s of scenes.slice(0, 3)) await clickFn(page, q(`[data-vc-fn-row="${s.id}"]`), 'function row ' + s.name);
    await page.waitFor(`document.body.textContent.includes('3 selected')`);
    await clickText(page, 'button', 'Add');
    await until(async () => (await api.call('vc.widget.list', { page: scratch })).widgets.length === countBefore + 3, '3 new widgets');
    const fromFn = (await api.call('vc.widget.list', { page: scratch })).widgets.filter(w => w.widgetType === 'Button' && scenes.slice(0, 3).some(s => String(s.id) === String(w.typeConfig.functionID)) && w.id !== button.id);
    check(fromFn.length === 3 && fromFn.every(w => !w.parentId && w.style.caption), 'three Buttons created on the page root, one per Scene, captioned with the function name', fromFn.map(w => w.style.caption));
    await page.waitFor(`document.querySelectorAll('[data-vc-selected]').length === 3`);
    check(true, 'the new widgets are selected in the browser');

    /* ---- Create matrix ---- */
    console.log('\n[Create matrix]');
    await page.eval(`(function(){ const c = document.querySelector('div[style*="dashed"]'); c && c.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); })()`);
    await page.waitFor(`document.querySelectorAll('[data-vc-selected]').length === 0`);
    const countBeforeMatrix = (await api.call('vc.widget.list', { page: scratch })).widgets.length;
    await clickFn(page, toolbarTool('matrix'), 'matrix tool');
    await page.waitFor(`!!document.querySelector('[data-vc-matrix-dialog]')`);
    await clickText(page, 'button', 'Create');
    await until(async () => (await api.call('vc.widget.list', { page: scratch })).widgets.length === countBeforeMatrix + 10, 'matrix widgets');
    const all = (await api.call('vc.widget.list', { page: scratch })).widgets;
    const matrixFrame = all.filter(w => w.widgetType === 'Frame' && w.id !== frame.id).sort((a, b) => Number(b.id) - Number(a.id))[0];
    const cells = all.filter(w => w.parentId === matrixFrame.id);
    check(cells.length === 9 && cells.every(w => w.widgetType === 'Button') && matrixFrame.typeConfig.showHeader === false, '3x3 button matrix inside a new headerless Frame', { frame: matrixFrame.id, cells: cells.length });
    await page.waitFor(`document.querySelectorAll('[data-vc-widget="${matrixFrame.id}"] [data-vc-type="Button"]').length === 9`);
    check(true, 'the matrix renders in the browser');

    /* ---- Label ---- */
    console.log('\n[Label]');
    const labelSpot = await freeSpot(api, scratch);
    const label = await placeWidget(page, api, 'Label', labelSpot.x, labelSpot.y, 'Label');
    await page.waitFor(`document.body.textContent.includes('A label has no settings beyond')`);
    check(JSON.stringify((await api.widget(label.id)).typeConfig) === '{}', 'Label has an empty typeConfig and its own explanatory panel');
    await typeInto(page, `[...document.querySelectorAll('input')].find(i => i.value === 'Label')`, 'Hello', 'label caption');
    await until(async () => (await api.widget(label.id)).style.caption === 'Hello', 'label caption');
    check(true, 'Label caption edited through Basic properties');

    /* ---- SoloFrame ---- */
    console.log('\n[SoloFrame]');
    const soloSpot = await freeSpot(api, scratch, 300);
    const solo = await placeWidget(page, api, 'Solo Frame', soloSpot.x, soloSpot.y, 'SoloFrame');
    await clickCheck(page, 'Exclude monitored functions');
    await until(async () => (await api.widget(solo.id)).typeConfig.excludeMonitoredFunctions === true, 'excludeMonitoredFunctions');
    await clickCheck(page, 'Mix (fade) between functions');
    await until(async () => (await api.widget(solo.id)).typeConfig.soloframeMixing === true, 'soloframeMixing');
    check(true, 'Solo Frame options (exclude monitored, mixing)');

    await page.screenshot(path.join(SHOTS, 'vc-layout-2-page.png'));

    /* ---- save + grep ---- */
    console.log('\n[saveAs]');
    await api.call('core.project.saveAs', { target: 'serverPath', path: OUT });
    const xml = fs.readFileSync(OUT, 'utf8');
    const has = (re, what) => check(re.test(xml), 'out.qxw contains ' + what);
    /* Spellings as VCFrame/VCSlider/VCButton/VCSoloFrame::saveXML() write them (the PIN is stored obfuscated). */
    has(/<Multipage PagesNum="3"[^>]*PagesLoop="True"/, 'a 3-page frame with PagesLoop');
    has(/<PIN>[A-Za-z0-9+\/=]{4,}<\/PIN>/, 'the (obfuscated) frame PIN');
    has(/<Shortcut Page="1" Name="Intro"\/>/, 'the page label "Intro"');
    has(/<ShowEnableButton>True<\/ShowEnableButton>/, 'ShowEnableButton');
    has(new RegExp(`<Channel Fixture="${fx.id}">1</Channel>`), 'the second level channel of fixture ' + fx.id);
    has(/<SliderMode [^>]*Monitor="true">Level<\/SliderMode>/, 'the Level slider with monitoring');
    has(/CatchValues="true"/, 'CatchValues');
    has(/<Action>Toggle<\/Action>/, 'the Toggle action');
    has(/<Intensity>50<\/Intensity>/, 'startup intensity 50');
    has(/Caption="Bulk"/, 'the bulk caption');
    has(/Caption="Hello"/, 'the label caption');
    has(/<ExcludeMonitored>True<\/ExcludeMonitored>/, 'ExcludeMonitored');
    has(/<Mixing>True<\/Mixing>/, 'solo frame Mixing');
    console.log('  frames in file: ' + (xml.match(/<Frame /g) || []).length + ', buttons: ' + (xml.match(/<Button /g) || []).length);

    /* ---- console ---- */
    check(page.consoleErrors.length === 0, 'no console errors', page.consoleErrors);
  } catch (e) {
    failures.push('EXCEPTION ' + (e && e.stack || e));
    try { await page.screenshot(path.join(SHOTS, 'vc-layout-failure.png')); } catch (x) { }
    console.log('  EXCEPTION ' + (e && e.stack || e));
    if (page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
    try { console.log('  check rows: ' + JSON.stringify(await page.eval(`[...document.querySelectorAll('button[aria-pressed]')].map(b => (b.parentElement ? b.parentElement.textContent.trim() : '?') + (b.disabled ? ' (disabled)' : ''))`))); } catch (x) { }
  } finally {
    await browser.close();
    api.close();
  }
  console.log('\n' + checks + ' checks, ' + failures.length + ' failed' + (failures.length ? ':\n  ' + failures.join('\n  ') : ''));
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
