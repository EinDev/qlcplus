// End-to-end check of the Virtual Console XY Pad / Clock / Animation / Audio Triggers slice
// (webui/vc/vc-props-live.jsx and the vc.xyPad.* / vc.clock.* / vc.animation.* / vc.audioTriggers.* +
// vc.widget.preset.* server methods) against a sandbox instance
// (dev-webui-sandbox.ps1 -Name vclive -ApiPort 9180 -WebUiPort 9181). Drives headless Chrome through
// webui/tools/cdp.js the way an operator would (clicks, typing, dragging), reads the server back over
// its own WebSocket after every step, reloads the page to see everything persisted, saves the project
// with core.project.saveAs and greps the XML.
//
//   node webui/tools/e2e/vc-live.js            # defaults: API 9180, web UI 9181
//   E2E_API_PORT=.. E2E_WEB_PORT=.. E2E_HOST=.. E2E_OUT=<dir> E2E_SAVE_PATH=<qxw> node webui/tools/e2e/vc-live.js
//
// E2E_HOST defaults to [::1]: on the machine this was written on, Logitech's lghub_updater squats
// 127.0.0.1:9180, so IPv4 loopback reaches the wrong process while the sandbox (bound to every
// address) answers on ::1. The page is opened on the same host so the web UI connects there too.
//
// Setup (a scratch VC page with the four widgets) is done over the API; everything after that goes
// through the browser. Exit code 0 only if every assertion held and the page logged no console error.

const fs = require('fs'), path = require('path'), os = require('os');
const { launch, sleep } = require('../cdp.js');

const API_PORT = Number(process.env.E2E_API_PORT || 9180);
const WEB_PORT = Number(process.env.E2E_WEB_PORT || 9181);
const HOST = process.env.E2E_HOST || '[::1]';
const OUT = process.env.E2E_OUT || path.join(os.tmpdir(), 'qlc-e2e-vc-live');
const SAVE_PATH = process.env.E2E_SAVE_PATH || 'C:\\qlcsandbox\\vclive\\out.qxw';
fs.mkdirSync(OUT, { recursive: true });
const TAG = String(Date.now()).slice(-6);
const CAP = { pad: 'E2E Pad ' + TAG, clock: 'E2E Clock ' + TAG, anim: 'E2E Anim ' + TAG, audio: 'E2E Audio ' + TAG };

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function eq(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }

/* ---- a second, independent API client for setup and for reading the server back -------------- */
function api(host, port) {
  const ws = new WebSocket('ws://' + host + ':' + port + '/');
  const pending = new Map(); let seq = 0, docRevision = 0;
  const events = [];
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'response' && pending.has(m.id)) {
      const { res, rej } = pending.get(m.id); pending.delete(m.id);
      if (m.ok) { if (m.result && typeof m.result.docRevision === 'number') docRevision = Math.max(docRevision, m.result.docRevision); res(m.result); }
      else { if (m.error.details && typeof m.error.details.docRevision === 'number') docRevision = Math.max(docRevision, m.error.details.docRevision); rej(Object.assign(new Error(m.error.code + ': ' + m.error.message), m.error)); }
    } else if (m.type === 'event') { events.push(m); if (m.data && typeof m.data.docRevision === 'number') docRevision = Math.max(docRevision, m.data.docRevision); }
  };
  const call = (method, params) => new Promise((res, rej) => { const id = 'e2e-' + (++seq); pending.set(id, { res, rej }); ws.send(JSON.stringify({ type: 'request', id, method, params: params || {} })); });
  /* Live VC actions bump docRevision too (Tardis::enqueueAction() calls Doc::setModified()), so a structural
     call right after one starts stale: retry once with the revision the CONFLICT handed back. */
  const mutate = (method, params) => call(method, Object.assign({ baseRevision: docRevision }, params)).catch(e => { if (e.code === 'CONFLICT') return call(method, Object.assign({ baseRevision: docRevision }, params)); throw e; });
  return {
    async connect() { await ready; const w = await call('hello', { apiVersion: '1', clientName: 'e2e-vc-live' }); docRevision = w.docRevision; await call('subscribe', { topics: ['vc.clock.timeChanged', 'vc.audioTriggers.levelsChanged'] }); },
    call, mutate, events, close: () => ws.close(), get docRevision() { return docRevision; }
  };
}

/* ---- page helpers ------------------------------------------------------------------------- */
function leafByText(scope, text, last) {
  return new Function('const root = ' + (scope ? 'document.querySelector(' + JSON.stringify(scope) + ')' : 'document') + '; if (!root) return null;'
    + ' const all = Array.from(root.querySelectorAll("*")).filter(el => el.children.length === 0 && el.textContent.trim() === ' + JSON.stringify(text) + ' && el.getBoundingClientRect().width > 0);'
    + ' const el = ' + (last ? 'all[all.length - 1]' : 'all[0]') + '; if (el) el.scrollIntoView({ block: "nearest" }); return el || null;');
}
async function clickText(page, text, scope, last) {
  await page.waitFor('!!(' + leafByText(scope, text, last).toString() + ')()', 8000);
  await page.click(leafByText(scope, text, last));
  await sleep(120);
}
async function clickSel(page, selector) {
  await page.waitFor('!!document.querySelector(' + JSON.stringify(selector) + ')', 8000);
  await page.eval('document.querySelector(' + JSON.stringify(selector) + ').scrollIntoView({ block: "nearest" })');
  await sleep(60);
  await page.click(selector);
  await sleep(120);
}
/** CheckRow (vc-edit.jsx): the aria-pressed button whose row text is the label. */
const checkRow = (label) => `[...document.querySelectorAll('button[aria-pressed]')].find(b => b.parentElement && b.parentElement.textContent.replace(/[\\uE000-\\uF8FF]/g, '').trim() === ${JSON.stringify(label)})`;
async function clickCheck(page, label) {
  await page.waitFor(`!!(${checkRow(label)})`, 8000);
  await page.eval(`(function(){ const b = ${checkRow(label)}; b.scrollIntoView({ block: 'nearest' }); })()`);
  await page.click(new Function('return ' + checkRow(label)));
  await sleep(150);
}
/* CustomComboBox: a root div (data-e2e lands on it) whose button shows the current label; the popup
   entries are plain leaf elements appended later in the DOM, so the wanted label's LAST match is the entry. */
async function pickCombo(page, rootSel, currentLabel, wantedLabel) {
  await page.waitFor('!!document.querySelector(' + JSON.stringify(rootSel) + ')', 8000);
  /* The popup entry only exists while the combo is open; a panel re-render right before the click can
     swallow it (e.g. the bar list rebuilt after a bar-count change), so open again once if needed. */
  const count = 'Array.from(document.querySelectorAll("*")).filter(el => el.children.length === 0 && el.textContent.trim() === ' + JSON.stringify(wantedLabel) + ' && el.getBoundingClientRect().width > 0).length';
  for (let attempt = 0; ; attempt++) {
    await page.waitFor('!!(' + leafByText(rootSel, currentLabel).toString() + ')()', 8000);
    await page.eval('document.querySelector(' + JSON.stringify(rootSel) + ').scrollIntoView({ block: "center" })');
    const before = await page.eval(count);
    await page.click(leafByText(rootSel, currentLabel));
    if (await page.waitFor(count + ' > ' + before, 2000).catch(() => false)) break;
    if (attempt >= 2) throw new Error('combo ' + rootSel + ' did not open');
    await sleep(300);
  }
  await page.click(leafByText(null, wantedLabel, true));
  await sleep(200);
}
async function typeInto(page, selector, text, enter) {
  await clickSel(page, selector);
  await page.eval('(function(){const el=document.querySelector(' + JSON.stringify(selector) + '); el.focus(); el.select(); return true;})()');
  await page.type(text);
  if (enter) await page.key('Enter');
  await sleep(150);
}
async function poll(fn, timeout = 6000) { const t0 = Date.now(); for (;;) { let v = null; try { v = await fn(); } catch (e) { } if (v) return v; if (Date.now() - t0 > timeout) return null; await sleep(150); } }
const EDIT_BTN = 'button[title^="Enable/Disable the widgets edit mode"]';
async function waitConnected(page) {
  await page.waitFor('(function(){const b=document.querySelector(' + JSON.stringify(EDIT_BTN) + '); return !!b && !b.disabled;})()', 30000);
}
async function gotoPage(page, label) { await clickText(page, label); await sleep(400); }
async function selectWidget(page, id, section) {
  await page.click('[data-vc-widget="' + id + '"]'); await sleep(300);
  await page.waitFor('!!document.querySelector(\'[data-e2e-section="' + section + '"]\')', 8000);
}
async function dragEl(page, selector, fromFrac, toFrac, horizontal) {
  const r = await page.rectOf(selector);
  const x1 = horizontal ? r.x + r.w * fromFrac : r.x + r.w / 2, x2 = horizontal ? r.x + r.w * toFrac : r.x + r.w / 2;
  const y1 = horizontal ? r.y + r.h / 2 : r.y + r.h * fromFrac, y2 = horizontal ? r.y + r.h / 2 : r.y + r.h * toFrac;
  await page.drag(x1, y1, x2, y2, 8);
  await sleep(400);
}

(async () => {
  const srv = api(HOST, API_PORT);
  await srv.connect();
  const widgetGet = async (id) => srv.call('vc.widget.get', { widgetId: String(id) });
  const cfgOf = async (id) => (await widgetGet(id)).typeConfig;

  /* ================= setup over the API ================= */
  console.log('setup');
  const mode = await srv.call('core.mode.get');
  if (mode.mode !== 'design') await srv.call('core.mode.set', { mode: 'design' });
  const pagesBefore = (await srv.call('vc.page.list')).pages;
  await srv.mutate('vc.page.create', { index: pagesBefore.length });
  const pages = (await srv.call('vc.page.list')).pages;
  const pageIndex = pages.length - 1;
  const pageLabel = pages[pageIndex].name || ('Page ' + (pageIndex + 1));
  const fixtures = (await srv.call('fixtures.list')).fixtures;
  const movers = fixtures.filter(f => f.fixtureType === 'Moving Head').slice(0, 2);
  const groups = (await srv.call('fixtures.group.list')).groups;
  const functions = (await srv.call('functions.list')).functions.filter(f => !f.hidden);
  const matrix = functions.find(f => f.type === 'RGBMatrix');
  const scene = functions.find(f => f.type === 'Scene');
  check(movers.length === 2 && groups.length && matrix && scene, 'SF3 project has 2 moving heads (' + movers.map(f => f.name).join(', ') + '), groups, an RGB Matrix (' + (matrix && matrix.name) + ') and a Scene (' + (scene && scene.name) + ')');
  const mk = (widgetType, caption, geometry) => srv.mutate('vc.widget.create', { widgetType, page: pageIndex, geometry, style: { caption } }).then(r => String(r.widgetId));
  const padId = await mk('XYPad', CAP.pad, { x: 20, y: 20, width: 320, height: 320 });
  const clockId = await mk('Clock', CAP.clock, { x: 380, y: 20, width: 260, height: 90 });
  const animId = await mk('Animation', CAP.anim, { x: 380, y: 140, width: 300, height: 220 });
  const audioId = await mk('AudioTriggers', CAP.audio, { x: 720, y: 20, width: 260, height: 220 });
  check(padId && clockId && animId && audioId, 'scratch page ' + pageIndex + ' with XYPad #' + padId + ', Clock #' + clockId + ', Animation #' + animId + ', AudioTriggers #' + audioId);
  let g = await cfgOf(padId);
  eq([g.displayMode, g.floorControl, g.fixtures, g.presets, g.horizontalRange], ['Degrees', false, [], [], { max: 255, min: 0 }], 'fresh XYPad typeConfig');
  g = await cfgOf(clockId);
  eq([g.clockType, g.enableSchedule, g.schedules, g.targetTime], ['Clock', false, [], 0], 'fresh Clock typeConfig');
  g = await cfgOf(animId);
  eq([g.functionID, g.presets, g.visibilityMask], ['4294967295', [], ['Fader', 'Label', 'PresetCombo', 'Color1', 'Color2']], 'fresh Animation typeConfig');
  g = await cfgOf(audioId);
  check(g.barsNumber >= 2 && g.bars.length === g.barsNumber && g.bars[0].type === 'None', 'fresh AudioTriggers typeConfig (' + g.barsNumber + ' bars incl. volume)');

  const b = await launch();
  const page = await b.open('http://' + HOST + ':' + WEB_PORT + '/?ctx=vc');
  try {
    console.log('connect + open the scratch page');
    await waitConnected(page);
    await gotoPage(page, pageLabel);
    await page.waitFor('!!document.querySelector(\'[data-vc-widget="' + padId + '"]\')', 10000);
    await page.click(EDIT_BTN); await sleep(300);

    /* ================= XY pad properties ================= */
    console.log('xy pad: properties');
    await selectWidget(page, padId, 'xyFixtures');
    await clickSel(page, '[data-e2e="xypad-add"]');
    await page.waitFor('!!document.querySelector(\'[data-e2e="xypad-add-dialog"]\')', 8000);
    await typeInto(page, 'input[data-e2e="xypad-add-search"]', movers[0].name);
    await clickSel(page, '[data-e2e-fixture="' + movers[0].id + '"]');
    g = await poll(async () => { const c = await cfgOf(padId); return c.fixtures.length === 1 ? c : null; });
    check(!!g && g.fixtures[0].fixtureId === String(movers[0].id), 'first mover added through the picker (' + (g && g.fixtures[0].name) + ', ' + (g && g.fixtures[0].xRangeLabel) + ')');
    await typeInto(page, 'input[data-e2e="xypad-add-search"]', movers[1].name);
    await clickSel(page, '[data-e2e-fixture="' + movers[1].id + '"]');
    g = await poll(async () => { const c = await cfgOf(padId); return c.fixtures.length === 2 ? c : null; });
    check(!!g, 'second mover added');
    await clickSel(page, '[data-e2e="xypad-add-tab-groups"]');
    await clickSel(page, '[data-e2e-group="' + groups[0].id + '"]');
    g = await poll(async () => { const c = await cfgOf(padId); return c.fixtures.length === 3 && c.presets.length === 1 ? c : null; });
    check(!!g && g.fixtures[2].fixtureGroupId === String(groups[0].id) && g.presets[0].presetType === 'fixtureGroup', 'group "' + groups[0].name + '" added as one entry and got its own preset');
    await clickText(page, 'Close', null, true);
    await page.waitFor('!document.querySelector(\'[data-e2e="xypad-add-dialog"]\')', 5000);
    check(await page.eval('document.querySelectorAll(\'[data-e2e="xypad-fixtures"] [data-e2e-entry]\').length') === 3, 'fixture list shows 3 entries');
    /* range of the first head, in degrees */
    await clickSel(page, '[data-e2e-entry="f' + movers[0].id + ':0"]');
    await clickSel(page, '[data-e2e="xypad-range"]');
    await page.waitFor('!!document.querySelector(\'[data-e2e="xypad-range-dialog"]\')', 8000);
    await typeInto(page, 'input[data-e2e="xypad-range-xMin"]', '90', true);
    await typeInto(page, 'input[data-e2e="xypad-range-xMax"]', '270', true);
    await clickSel(page, 'button[data-e2e="xypad-range-yReverse"]');
    await clickText(page, 'Apply', null, true);
    g = await poll(async () => { const c = await cfgOf(padId); const f = c.fixtures[0]; return f.xRange.min === 90 && f.xRange.max === 270 && f.yRange.reverse === true ? c : null; });
    check(!!g, 'Pan range 90..270 deg + Tilt reversed on the first head (' + (g && g.fixtures[0].xRangeLabel) + ' / ' + (g && g.fixtures[0].yRangeLabel) + ')');
    /* units combo -> the list is relabelled */
    await pickCombo(page, '[data-e2e="xypad-units"]', 'Degrees (°)', 'DMX values');
    g = await poll(async () => { const c = await cfgOf(padId); return c.displayMode === 'DMX' ? c : null; });
    check(!!g && g.fixtures[0].units === '' && g.fixtures[0].xRange.maxValue === 255, 'display mode DMX: ranges now in 0..255 (' + (g && g.fixtures[0].xRangeLabel) + ')');
    await pickCombo(page, '[data-e2e="xypad-units"]', 'DMX values', 'Degrees (°)');
    await poll(async () => (await cfgOf(padId)).displayMode === 'Degrees');
    /* Pan window (horizontal range) */
    await typeInto(page, 'input[data-e2e="xypad-hmin"]', '32', true);
    g = await poll(async () => { const c = await cfgOf(padId); return c.horizontalRange.min === 32 ? c : null; });
    check(!!g, 'Pan window minimum 32');
    /* presets: position, rename, move */
    await clickSel(page, 'button[data-e2e="xypad-preset-position"]');
    g = await poll(async () => { const c = await cfgOf(padId); return c.presets.length === 2 ? c : null; });
    check(!!g && g.presets[1].presetType === 'position', 'position preset created from the current position');
    const posPresetId = g.presets[1].presetId;
    await page.waitFor('document.querySelectorAll(\'[data-e2e="xypad-preset-list"] [data-e2e-preset-row]\').length === 2', 8000);
    await typeInto(page, 'input[data-e2e="xypad-preset-name"]', 'Centre stage', true);
    g = await poll(async () => { const c = await cfgOf(padId); return c.presets.some(p => p.name === 'Centre stage') ? c : null; });
    check(!!g, 'preset renamed to "Centre stage"');
    await clickSel(page, 'button[data-e2e="xypad-preset-up"]');
    g = await poll(async () => { const c = await cfgOf(padId); return c.presets[0].name === 'Centre stage' ? c : null; });
    check(!!g && g.presets[0].presetId !== posPresetId && g.presets[1].presetType === 'fixtureGroup', 'preset moved up (the engine swapped the ids: now #' + (g && g.presets[0].presetId) + ', was #' + posPresetId + ')');
    await clickSel(page, 'button[data-e2e="xypad-preset-fn"]');
    await typeInto(page, 'input[data-e2e="xypad-preset-fn-search"]', scene.name);
    const fnRow = await page.waitFor('!!document.querySelector(\'[data-e2e="xypad-preset-fn-search-matches"] [data-e2e-fn="' + scene.id + '"]\')', 5000).catch(() => false);
    check(!!fnRow, 'function search lists "' + scene.name + '"');
    if (fnRow) {
      await clickSel(page, '[data-e2e="xypad-preset-fn-search-matches"] [data-e2e-fn="' + scene.id + '"]');
      await sleep(400);
      g = await cfgOf(padId);
      check(g.presets.length === 3 && g.presets[2].presetType === 'function' && g.presets[2].functionID === String(scene.id), 'Scene preset "' + scene.name + '" added from the function search');
    }
    await page.screenshot(path.join(OUT, '1-xypad-props.png'));

    /* ================= XY pad live: DMX, preset, floor ================= */
    console.log('xy pad: live body');
    await page.click(EDIT_BTN); await sleep(400);
    const fx0 = await srv.call('fixtures.get', { fixtureId: String(movers[0].id) });
    const dmxBefore = (await srv.call('io.dmx.universe.get', { universeId: Number(fx0.universe) })).values.slice(Number(fx0.address), Number(fx0.address) + 4);
    await dragEl(page, '[data-vc-widget="' + padId + '"] [data-vc-pad]', 0.2, 0.85, true);
    let live = await poll(async () => { const w = await widgetGet(padId); return w.x > 0.7 ? w : null; });
    check(!!live, 'pad drag moved the cursor (x ' + (live && live.x.toFixed(2)) + ', y ' + (live && live.y.toFixed(2)) + ')');
    const dmxAfter = await poll(async () => { const v = (await srv.call('io.dmx.universe.get', { universeId: Number(fx0.universe) })).values.slice(Number(fx0.address), Number(fx0.address) + 4); return JSON.stringify(v) !== JSON.stringify(dmxBefore) ? v : null; });
    check(!!dmxAfter, 'DMX of ' + movers[0].name + ' changed (' + JSON.stringify(dmxBefore) + ' -> ' + JSON.stringify(dmxAfter) + ')');
    check(await page.eval('document.querySelectorAll(\'[data-vc-widget="' + padId + '"] [data-e2e="xypad-presets"] button\').length') >= 2, 'body shows the preset buttons');
    await clickSel(page, '[data-vc-widget="' + padId + '"] [data-e2e-preset="' + g.presets.find(p => p.name === 'Centre stage').presetId + '"]');
    live = await poll(async () => { const w = await widgetGet(padId); return w.activePresetId >= 0 ? w : null; });
    check(!!live, 'position preset applied from the body (active #' + (live && live.activePresetId) + ', x ' + (live && live.x.toFixed(2)) + ')');
    check(srv.events.some(e => e.topic === 'vc.xyPad.activePresetChanged' && String(e.data.widgetId) === padId), 'vc.xyPad.activePresetChanged reached the second client');
    /* floor control */
    await page.click(EDIT_BTN); await sleep(400);
    await selectWidget(page, padId, 'xyDisplay');
    await clickCheck(page, 'Floor control (point the fixtures at a stage floor position)');
    g = await poll(async () => { const c = await cfgOf(padId); return c.floorControl === true ? c : null; });
    check(!!g, 'floor control on');
    await page.click(EDIT_BTN); await sleep(400);
    await page.waitFor('document.querySelector(\'[data-vc-widget="' + padId + '"] [data-e2e="xypad-body"]\').getAttribute("data-e2e-floor") === "on"', 8000);
    await dragEl(page, '[data-vc-widget="' + padId + '"] [data-vc-pad]', 0.5, 0.15, true);
    live = await poll(async () => { const w = await widgetGet(padId); return w.floorPosition && w.floorPosition.x < g.floorSize.x * 0.3 ? w : null; });
    check(!!live, 'floor drag moved the target (x ' + (live && live.floorPosition.x.toFixed(1)) + 'm, z ' + (live && live.floorPosition.z.toFixed(1)) + 'm)');
    await dragEl(page, '[data-vc-widget="' + padId + '"] [data-vc-fader]', 0.9, 0.4, false);
    live = await poll(async () => { const w = await widgetGet(padId); return w.floorPosition.y > 2 ? w : null; });
    check(!!live, 'height fader lifted the target (' + (live && live.floorPosition.y.toFixed(1)) + 'm)');
    check(srv.events.some(e => e.topic === 'vc.xyPad.floorPositionChanged' && String(e.data.widgetId) === padId), 'vc.xyPad.floorPositionChanged reached the second client');
    await page.screenshot(path.join(OUT, '2-xypad-floor.png'));
    await page.click(EDIT_BTN); await sleep(400);
    await selectWidget(page, padId, 'xyDisplay');
    await clickCheck(page, 'Floor control (point the fixtures at a stage floor position)');
    await poll(async () => (await cfgOf(padId)).floorControl === false);

    /* ================= Clock ================= */
    console.log('clock: properties');
    await selectWidget(page, clockId, 'clockType');
    await clickCheck(page, 'Countdown');
    g = await poll(async () => { const c = await cfgOf(clockId); return c.clockType === 'Countdown' ? c : null; });
    check(!!g, 'clock type Countdown');
    await typeInto(page, 'input[data-e2e="clock-target-m"]', '1', true);
    await typeInto(page, 'input[data-e2e="clock-target-s"]', '30', true);
    g = await poll(async () => { const c = await cfgOf(clockId); return c.targetTime === 90000 ? c : null; });
    check(!!g, 'countdown target 00:01:30 (90000 ms)');
    await clickCheck(page, 'Enable the scheduler');
    g = await poll(async () => { const c = await cfgOf(clockId); return c.enableSchedule === true ? c : null; });
    check(!!g, 'scheduler enabled');
    await clickSel(page, 'button[data-e2e="clock-schedule-add"]');
    await typeInto(page, 'input[data-e2e="clock-schedule-fn"]', scene.name);
    await clickSel(page, '[data-e2e="clock-schedule-fn-matches"] [data-e2e-fn="' + scene.id + '"]');
    g = await poll(async () => { const c = await cfgOf(clockId); return c.schedules.length === 1 ? c : null; });
    check(!!g && g.schedules[0].functionID === String(scene.id), 'schedule for "' + scene.name + '" added');
    /* schedule times / days are a Clock-type thing: switch, edit, switch back */
    await clickCheck(page, 'Clock');
    await poll(async () => (await cfgOf(clockId)).clockType === 'Clock');
    await typeInto(page, 'input[data-e2e="clock-start-0-h"]', '20', true);
    g = await poll(async () => { const c = await cfgOf(clockId); return c.schedules[0].startTime === 20 * 3600 ? c : null; });
    check(!!g, 'start time 20:00:00');
    await clickSel(page, 'button[data-e2e="clock-stop-enable-0"]');
    g = await poll(async () => { const c = await cfgOf(clockId); return c.schedules[0].stopTime >= 0 ? c : null; });
    check(!!g, 'stop time enabled (' + (g && g.schedules[0].stopTime) + ' s)');
    await typeInto(page, 'input[data-e2e="clock-stop-0-h"]', '23', true);
    g = await poll(async () => { const c = await cfgOf(clockId); return Math.floor(c.schedules[0].stopTime / 3600) === 23 ? c : null; });
    check(!!g, 'stop time 23:xx');
    await clickSel(page, 'button[data-e2e="clock-day-0-4"]');
    await clickSel(page, 'button[data-e2e="clock-day-0-5"]');
    await clickSel(page, 'button[data-e2e="clock-repeat-0"]');
    g = await poll(async () => { const c = await cfgOf(clockId); return c.schedules[0].weekFlags === (0x10 | 0x20 | 0x80) ? c : null; });
    check(!!g, 'Fri + Sat + repeat (weekFlags 0xB0)');
    check(await page.eval('document.querySelector(\'[data-vc-widget="' + clockId + '"] [data-e2e="clock-body"]\').textContent.includes("F")'), 'clock body shows the day initials');
    await clickCheck(page, 'Countdown');
    await poll(async () => (await cfgOf(clockId)).clockType === 'Countdown');
    await page.screenshot(path.join(OUT, '3-clock-props.png'));
    console.log('clock: live body');
    await page.click(EDIT_BTN); await sleep(400);
    await clickSel(page, '[data-vc-widget="' + clockId + '"] button[data-e2e="clock-play"]');
    live = await poll(async () => { const w = await widgetGet(clockId); return w.running && w.currentTime < 90000 ? w : null; }, 4000);
    check(!!live, 'countdown running (' + (live && live.currentTime) + ' ms left)');
    await sleep(600);
    const shown = await page.eval('document.querySelector(\'[data-vc-widget="' + clockId + '"] [data-e2e="clock-time"]\').textContent');
    check(/^00:01:2\d\.\d$/.test(shown), 'body ticks down (' + shown + ')');
    check(srv.events.filter(e => e.topic === 'vc.clock.timeChanged' && String(e.data.widgetId) === clockId).length >= 3, 'vc.clock.timeChanged ticks reached the subscribed second client');
    await clickSel(page, '[data-vc-widget="' + clockId + '"] button[data-e2e="clock-play"]');
    live = await poll(async () => { const w = await widgetGet(clockId); return !w.running ? w : null; });
    check(!!live, 'paused at ' + (live && live.currentTime) + ' ms');
    await clickSel(page, '[data-vc-widget="' + clockId + '"] button[data-e2e="clock-reset"]');
    live = await poll(async () => { const w = await widgetGet(clockId); return !w.running && w.currentTime === 90000 ? w : null; });
    check(!!live, 'reset back to 90000 ms');
    await page.screenshot(path.join(OUT, '4-clock-live.png'));

    /* ================= Animation ================= */
    console.log('animation: properties');
    await page.click(EDIT_BTN); await sleep(400);
    await selectWidget(page, animId, 'animFunction');
    await typeInto(page, 'input[data-e2e="anim-fn-search"]', matrix.name);
    await clickSel(page, '[data-e2e="anim-fn-search-matches"] [data-e2e-fn="' + matrix.id + '"]');
    g = await poll(async () => { const c = await cfgOf(animId); return c.functionID === String(matrix.id) ? c : null; });
    check(!!g, 'RGB Matrix "' + matrix.name + '" attached (colorCount ' + (g && g.colorCount) + ', algorithm #' + (g && g.algorithmIndex) + ')');
    await clickCheck(page, 'Color 3 Button');
    g = await poll(async () => { const c = await cfgOf(animId); return c.visibilityMask.indexOf('Color3') !== -1 ? c : null; });
    check(!!g, 'visibility: Color 3 Button on');
    await clickCheck(page, 'Apply color and preset changes immediately');
    g = await poll(async () => { const c = await cfgOf(animId); return c.instantChanges === false ? c : null; });
    check(!!g, 'instant changes toggled off');
    await clickCheck(page, 'Apply color and preset changes immediately');
    await poll(async () => (await cfgOf(animId)).instantChanges === true);
    /* presets: colour, knobs, text, algorithm */
    await page.eval('(function(){ const i = document.querySelector(\'input[data-e2e="anim-color-pick"]\'); const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set; setter.call(i, "#00ff00"); i.dispatchEvent(new Event("input", { bubbles: true })); i.dispatchEvent(new Event("change", { bubbles: true })); })()');
    await clickSel(page, 'button[data-e2e="anim-add-color"]');
    g = await poll(async () => { const c = await cfgOf(animId); return c.presets.length === 1 ? c : null; });
    check(!!g && g.presets[0].presetType === 'color' && g.presets[0].color === '#00ff00' && g.presets[0].colorIndex === 0, 'green colour preset for slot 1');
    await pickCombo(page, '[data-e2e="anim-slot"]', 'Color 1', 'Color 2');
    await clickSel(page, 'button[data-e2e="anim-add-knobs"]');
    g = await poll(async () => { const c = await cfgOf(animId); return c.presets.length === 4 ? c : null; });
    check(!!g && g.presets.slice(1).every(p => p.isKnob && p.colorIndex === 1), 'R/G/B knob trio for slot 2');
    await typeInto(page, 'input[data-e2e="anim-text"]', 'HELLO');
    await clickSel(page, 'button[data-e2e="anim-add-text"]');
    g = await poll(async () => { const c = await cfgOf(animId); return c.presets.length === 5 ? c : null; });
    check(!!g && g.presets[4].presetType === 'text' && g.presets[4].text === 'HELLO', 'text preset HELLO');
    await clickSel(page, 'button[data-e2e="anim-add-algo"]');
    await page.waitFor('!!document.querySelector(\'[data-e2e="anim-algo-dialog"]\')', 8000);
    await page.waitFor('!!document.querySelector(\'[data-e2e="anim-algo-name"]\') && document.querySelector(\'[data-e2e="anim-algo-name"]\').textContent.trim().length > 0', 8000);
    /* The combo's button also renders its chevron (a Font Awesome private-use glyph): strip it. */
    const algoName = await page.eval('document.querySelector(\'[data-e2e="anim-algo-name"]\').textContent.replace(/[\\uE000-\\uF8FF]/g, "").trim()');
    await sleep(500);
    const rangeProp = await page.eval('(function(){ const i = document.querySelector(\'[data-e2e="anim-algo-dialog"] input[data-e2e^="anim-prop-"]\'); return i ? i.getAttribute("data-e2e").slice(10) : null; })()');
    if (rangeProp) await typeInto(page, '[data-e2e="anim-algo-dialog"] input[data-e2e="anim-prop-' + rangeProp + '"]', '42', true);
    await clickText(page, 'Add', null, true);
    g = await poll(async () => { const c = await cfgOf(animId); return c.presets.length === 6 ? c : null; });
    check(!!g && g.presets[5].presetType === 'algorithm' && g.presets[5].algorithmName === algoName, 'algorithm preset "' + algoName + '"' + (rangeProp ? ' with ' + rangeProp + '=' + (g && g.presets[5].algorithmProperties[rangeProp]) : ''));
    /* move the text preset up (ids swap) */
    await clickSel(page, '[data-e2e="anim-preset-list"] [data-e2e-preset-row="' + g.presets[4].presetId + '"]');
    await clickSel(page, 'button[data-e2e="anim-preset-up"]');
    g = await poll(async () => { const c = await cfgOf(animId); return c.presets[3].presetType === 'text' ? c : null; });
    check(!!g, 'text preset moved up');
    await page.screenshot(path.join(OUT, '5-anim-props.png'));
    console.log('animation: live body');
    await page.click(EDIT_BTN); await sleep(400);
    await dragEl(page, '[data-vc-widget="' + animId + '"] [data-vc-fader]', 0.95, 0.3, false);
    live = await poll(async () => { const w = await widgetGet(animId); return w.faderLevel > 100 ? w : null; });
    check(!!live, 'fader drag started the matrix (level ' + (live && live.faderLevel) + ')');
    const matrixRunning = async () => ((await srv.call('functions.list')).functions.find(f => String(f.id) === String(matrix.id)) || {}).running;
    check(await poll(async () => (await matrixRunning()) === true), 'the RGB Matrix is running (functions.list)');
    await clickSel(page, '[data-vc-widget="' + animId + '"] [data-e2e-preset="' + g.presets[0].presetId + '"]');
    live = await poll(async () => { const w = await widgetGet(animId); return w.activePresetId === g.presets[0].presetId ? w : null; });
    check(!!live && live.typeConfig.colors[0] === '#00ff00', 'green preset applied: active #' + (live && live.activePresetId) + ', colour 1 = ' + (live && live.typeConfig.colors[0]));
    check(srv.events.some(e => e.topic === 'vc.animation.faderLevelChanged' && String(e.data.widgetId) === animId), 'vc.animation.faderLevelChanged reached the second client');
    await page.screenshot(path.join(OUT, '6-anim-live.png'));
    await dragEl(page, '[data-vc-widget="' + animId + '"] [data-vc-fader]', 0.3, 1.0, false);
    live = await poll(async () => { const w = await widgetGet(animId); return w.faderLevel === 0 ? w : null; });
    check(!!live, 'fader back to 0');
    check(await poll(async () => (await matrixRunning()) === false), 'the RGB Matrix stopped (functions.list)');

    /* ================= Audio triggers ================= */
    console.log('audio triggers: properties');
    await page.click(EDIT_BTN); await sleep(400);
    await selectWidget(page, audioId, 'audioBars');
    await typeInto(page, 'input[data-e2e="audio-bars-number"]', '3', true);
    g = await poll(async () => { const c = await cfgOf(audioId); return c.barsNumber === 4 ? c : null; });
    check(!!g, '3 spectrum bars + volume = 4 bars');
    /* The server answers this second client before the page has re-read the widget list, so the panel
       can still show the fresh 17 bars here. Acting on that stale layout made this step flaky: pickCombo
       scrolled the panel to centre bar 1's combo in the long list, then the list shrank to 4 rows, the
       panel's scroll clamped back to the top and the click (or the popup-entry click) landed on whatever
       had moved under it - the "Spectrum Bars" header, collapsing the section. Wait for the new count. */
    await page.waitFor('document.querySelectorAll(\'[data-e2e="audio-bar-list"] [data-e2e-bar]\').length === 4', 8000);
    await pickCombo(page, '[data-e2e="audio-bar-type-1"]', 'None', 'DMX');
    g = await poll(async () => { const c = await cfgOf(audioId); return c.bars[1].type === 'DMXBar' ? c : null; });
    check(!!g, 'bar 1 is a DMX bar');
    await page.waitFor('!!document.querySelector(\'[data-e2e="audio-bar-info-1"]\')', 8000); // the edit button stays disabled until the new type renders
    await clickSel(page, 'button[data-e2e="audio-bar-edit-1"]');
    await clickSel(page, '[data-e2e="audio-bar-editor-1"] [data-vc-levelpick]');
    await page.waitFor('document.querySelectorAll(\'[data-e2e="audio-bar-editor-1"] [data-vc-levelfx]\').length > 0', 8000);
    await page.eval('document.querySelector(\'[data-e2e="audio-bar-editor-1"] [data-vc-levelfx="' + movers[0].id + '"]\').scrollIntoView()');
    await clickSel(page, '[data-e2e="audio-bar-editor-1"] [data-vc-levelfx="' + movers[0].id + '"] button');
    await clickSel(page, '[data-e2e="audio-bar-editor-1"] [data-vc-levelch="' + movers[0].id + ':0"]');
    g = await poll(async () => { const c = await cfgOf(audioId); return c.bars[1].dmxChannels && c.bars[1].dmxChannels.length === 1 ? c : null; });
    check(!!g && g.bars[1].dmxChannels[0].fixtureId === String(movers[0].id), 'DMX bar drives channel 1 of ' + movers[0].name);
    await pickCombo(page, '[data-e2e="audio-bar-type-2"]', 'None', 'Function');
    g = await poll(async () => { const c = await cfgOf(audioId); return c.bars[2].type === 'FunctionBar' ? c : null; });
    check(!!g, 'bar 2 is a Function bar');
    await page.waitFor('!!document.querySelector(\'[data-e2e="audio-bar-info-2"]\')', 8000);
    await clickSel(page, 'button[data-e2e="audio-bar-edit-2"]');
    await typeInto(page, 'input[data-e2e="audio-fn-2"]', scene.name);
    await clickSel(page, '[data-e2e="audio-fn-2-matches"] [data-e2e-fn="' + scene.id + '"]');
    g = await poll(async () => { const c = await cfgOf(audioId); return c.bars[2].functionId === String(scene.id) ? c : null; });
    check(!!g, 'function bar triggers "' + scene.name + '"');
    await typeInto(page, 'input[data-e2e="audio-max-2"]', '90', true);
    g = await poll(async () => { const c = await cfgOf(audioId); return c.bars[2].maxThreshold >= 228 && c.bars[2].maxThreshold <= 231 ? c : null; });
    check(!!g, 'activation threshold 90% (' + (g && g.bars[2].maxThreshold) + '/255)');
    await page.screenshot(path.join(OUT, '7-audio-props.png'));
    console.log('audio triggers: live body');
    await page.click(EDIT_BTN); await sleep(400);
    await clickSel(page, '[data-vc-widget="' + audioId + '"] button[data-e2e="audio-capture"]');
    live = await poll(async () => { const w = await widgetGet(audioId); return w.captureEnabled ? w : null; });
    check(!!live, 'capture enabled on the host');
    await sleep(1200);
    const levelEvents = srv.events.filter(e => e.topic === 'vc.audioTriggers.levelsChanged' && String(e.data.widgetId) === audioId).length;
    console.log('  info levelsChanged events while capturing: ' + levelEvents + (levelEvents ? '' : ' (no audio input device on this host - the meter stays at 0, which is what the desktop UI shows too)'));
    check(await page.eval('document.querySelector(\'[data-vc-widget="' + audioId + '"] [data-e2e="audio-body"]\').getAttribute("data-e2e-capture") === "on"'), 'body shows capture on');
    await clickSel(page, '[data-vc-widget="' + audioId + '"] button[data-e2e="audio-capture"]');
    live = await poll(async () => { const w = await widgetGet(audioId); return !w.captureEnabled ? w : null; });
    check(!!live, 'capture disabled again');
    await page.screenshot(path.join(OUT, '8-audio-live.png'));

    /* ================= reload: everything persisted ================= */
    console.log('reload');
    await page.goto('http://' + HOST + ':' + WEB_PORT + '/?ctx=vc');
    await waitConnected(page);
    await gotoPage(page, pageLabel);
    await page.waitFor('!!document.querySelector(\'[data-vc-widget="' + padId + '"] [data-e2e="xypad-presets"]\') && !!document.querySelector(\'[data-vc-widget="' + animId + '"] [data-e2e="animation-presets"]\')', 15000);
    check(await page.eval('document.querySelectorAll(\'[data-vc-widget="' + padId + '"] [data-e2e="xypad-presets"] button\').length') >= 2, 'reload: XY pad preset buttons');
    check(/^00:01:30/.test(await page.eval('document.querySelector(\'[data-vc-widget="' + clockId + '"] [data-e2e="clock-time"]\').textContent')), 'reload: countdown shows 00:01:30');
    eq(await page.eval('document.querySelectorAll(\'[data-vc-widget="' + animId + '"] [data-e2e="animation-presets"] button, [data-vc-widget="' + animId + '"] [data-e2e="animation-presets"] [data-e2e-knob]\').length'), 6, 'reload: 6 animation preset controls');
    eq(await page.eval('document.querySelectorAll(\'[data-vc-widget="' + audioId + '"] [data-e2e="audio-bars"] > div\').length'), 4, 'reload: 4 audio bars');
    await page.screenshot(path.join(OUT, '9-reload.png'));

    /* ================= save + grep ================= */
    console.log('save and grep the .qxw');
    const saved = await srv.call('core.project.saveAs', { target: 'serverPath', path: SAVE_PATH });
    check(saved && saved.filePath, 'saved to ' + (saved && saved.filePath));
    const xml = fs.readFileSync(SAVE_PATH, 'utf8');
    /* Attribute order differs per widget (Clock writes Enable/Type, AudioTriggers BarsNumber before Caption). */
    const block = (tag, caption, id) => (xml.match(new RegExp('<' + tag + '\\b[^>]*Caption="' + caption + '" ID="' + id + '"[\\s\\S]*?</' + tag + '>')) || [])[0] || '';
    const padBlock = block('XYPad', CAP.pad, padId);
    check(!!padBlock, 'XYPad block present in the XML');
    /* A fixture entry without a custom range is self-closing, one with a range carries <Axis> children. */
    check(new RegExp('<Fixture ID="' + movers[0].id + '" Head="0">\\s*<Axis ID="X" LowLimit="0.1666\\d*" HighLimit="0.5" Reverse="False"/>\\s*<Axis ID="Y" LowLimit="0" HighLimit="1" Reverse="True"/>').test(padBlock), 'XML first head with Pan 90/540..270/540 and Tilt reversed');
    check(new RegExp('<Fixture ID="' + movers[1].id + '" Head="0"/>').test(padBlock), 'XML second head (full range)');
    check(new RegExp('<Group ID="' + groups[0].id + '"/>').test(padBlock), 'XML the fixture group entry');
    check(/<Window hMin="32" hMax="255"/.test(padBlock), 'XML Pan window hMin 32');
    check(/<Preset ID="\d+">\s*<Type>Position<\/Type>\s*<Name>Centre stage<\/Name>/.test(padBlock), 'XML position preset "Centre stage"');
    check(new RegExp('<Type>FixtureGroup</Type>\\s*<Name>[^<]*</Name>\\s*<Group ID="' + groups[0].id + '"/>').test(padBlock), 'XML fixture-group preset');
    check(new RegExp('<Type>Scene</Type>\\s*<Name>[^<]*</Name>\\s*<FuncID>' + scene.id + '</FuncID>').test(padBlock), 'XML Scene preset');
    const clockBlock = block('Clock', CAP.clock, clockId);
    check(/Type="Countdown" Hours="0" Minutes="1" Seconds="30"/.test(clockBlock) && !/\sTime="/.test(clockBlock), 'XML countdown 00:01:30 as Hours/Minutes/Seconds');
    check(new RegExp('<Schedule Function="' + scene.id + '" StartTime="20:00:00" StopTime="23:\\d\\d:\\d\\d" WeekFlags="176"/>').test(clockBlock), 'XML schedule 20:00 - 23:xx Fri+Sat repeat');
    const animBlock = block('Matrix', CAP.anim, animId);
    check(new RegExp('<Function ID="' + matrix.id + '" InstantApply="true"/>').test(animBlock), 'XML attached matrix with instant apply');
    check(/<Visibility>(\d+)<\/Visibility>/.test(animBlock) && (Number(animBlock.match(/<Visibility>(\d+)<\/Visibility>/)[1]) & 32) === 32, 'XML visibility mask has Color3');
    check(/<Type>Color1<\/Type>\s*<Color>#00ff00<\/Color>/.test(animBlock), 'XML green Color1 preset');
    check((animBlock.match(/<Type>Color2Knob<\/Type>/g) || []).length === 3, 'XML three Color2Knob presets');
    check(/<Type>Text<\/Type>\s*<Resource>HELLO<\/Resource>/.test(animBlock), 'XML text preset');
    check(new RegExp('<Type>Animation</Type>\\s*<Resource>' + algoName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '</Resource>').test(animBlock), 'XML algorithm preset');
    const audioBlock = block('AudioTriggers', CAP.audio, audioId);
    check(/BarsNumber="3"/.test(audioBlock), 'XML 3 spectrum bars');
    check(new RegExp('<Bar Type="1"[^>]*Index="1">\\s*<DMXChannels>' + movers[0].id + ',0</DMXChannels>').test(audioBlock), 'XML DMX bar channels');
    check(new RegExp('<Bar Type="2"[^>]*MaxThreshold="2(29|30)"[^>]*Index="2" FunctionID="' + scene.id + '"').test(audioBlock), 'XML function bar with threshold');

    /* ================= reopen the saved file: the config round-trips through the engine's load ================= */
    console.log('reopen out.qxw');
    await srv.call('core.project.open', { source: 'path', path: SAVE_PATH });
    await sleep(1500);
    g = await poll(async () => { const c = await cfgOf(clockId); return c.clockType === 'Countdown' ? c : null; }, 10000);
    eq(g && [g.targetTime, g.enableSchedule, g.schedules.map(s => [s.startTime, s.weekFlags])], [90000, true, [[72000, 176]]], 'reopened: countdown target 90000 ms, scheduler, schedule 20:00 Fri+Sat repeat');
    g = await cfgOf(padId);
    eq([g.fixtures.length, g.presets.map(p => p.presetType), g.horizontalRange.min], [3, ['position', 'fixtureGroup', 'function'], 32], 'reopened: XY pad fixtures, presets, window');
    g = await cfgOf(animId);
    eq([g.functionID, g.presets.length, g.instantChanges], [String(matrix.id), 6, true], 'reopened: animation matrix, presets');
    g = await cfgOf(audioId);
    eq([g.barsNumber, g.bars[1].type, g.bars[2].functionId], [4, 'DMXBar', String(scene.id)], 'reopened: audio triggers bars');

    /* A show saved by an earlier QLC+ 5 build: the countdown was written as Time="HH:mm:ss" holding the
       milliseconds as if they were seconds (5000 ms -> "01:23:20"). It must still load as 5000 ms. */
    const oldPath = path.join(path.dirname(SAVE_PATH), 'oldtime.qxw');
    fs.writeFileSync(oldPath, xml.replace(/(<Clock\b[^>]*Type="Countdown") Hours="\d+" Minutes="\d+" Seconds="\d+"/, '$1 Time="01:23:20"'));
    await srv.call('core.project.open', { source: 'path', path: oldPath });
    /* Until the new file has loaded the widget still belongs to out.qxw (a Countdown too, 90000 ms):
       wait for that one to be replaced, or this reads the previous project. */
    g = await poll(async () => { const c = await cfgOf(clockId); return c.clockType === 'Countdown' && c.targetTime !== 90000 ? c : null; }, 10000);
    eq(g && g.targetTime, 5000, 'an old-format Time="01:23:20" countdown still loads as 5000 ms');
  } catch (e) {
    failures++;
    console.log('  EXCEPTION ' + (e && e.stack || e));
    try { await page.screenshot(path.join(OUT, 'failure.png')); } catch (e2) { }
  }
  console.log('console errors: ' + JSON.stringify(page.consoleErrors));
  if (page.consoleErrors.length) failures++;
  await b.close();
  srv.close();
  console.log(failures ? 'FAILED (' + failures + ')' : 'ALL OK');
  console.log('screenshots in ' + OUT);
  process.exit(failures ? 1 : 0);
})();
