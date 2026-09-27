// End-to-end check of the Virtual Console Cue List + Speed Dial slice (webui/vc/vc-props-cue.jsx and
// the vc.cueList.setSideFaderLevel / vc.speedDial.setFactor|apply|resetTap / vc.widget.preset.* /
// vc.speedDial.preset.update server methods) against a sandbox instance
// (dev-webui-sandbox.ps1 -Name vccue -ApiPort 9170 -WebUiPort 9171). Drives headless Chrome through
// webui/tools/cdp.js the way an operator would (clicks, typing, dragging), reads the server back over
// its own WebSocket after every step, reloads the page to see everything persisted, saves the project
// with core.project.saveAs and greps the XML.
//
//   node webui/tools/e2e/vc-cue.js            # defaults: API 9170, web UI 9171
//   E2E_API_PORT=.. E2E_WEB_PORT=.. E2E_OUT=<dir> E2E_SAVE_PATH=<qxw> node webui/tools/e2e/vc-cue.js
//
// Setup (a scratch VC page with one Cue List and one Speed widget) is done over the API; everything
// after that goes through the browser. Exit code 0 only if every assertion held and the page logged
// no console error.

const fs = require('fs'), path = require('path'), os = require('os');
const { launch, sleep } = require('../cdp.js');

const API_PORT = Number(process.env.E2E_API_PORT || 9170);
const WEB_PORT = Number(process.env.E2E_WEB_PORT || 9171);
const OUT = process.env.E2E_OUT || path.join(os.tmpdir(), 'qlc-e2e-vc-cue');
const SAVE_PATH = process.env.E2E_SAVE_PATH || 'C:\\qlcsandbox\\vccue\\out.qxw';
fs.mkdirSync(OUT, { recursive: true });
const TAG = String(Date.now()).slice(-6);
const CUE_CAPTION = 'E2E Cue ' + TAG, SPEED_CAPTION = 'E2E Speed ' + TAG;

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function eq(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }

/* ---- a second, independent API client for setup and for reading the server back -------------- */
function api(port) {
  const ws = new WebSocket('ws://127.0.0.1:' + port + '/');
  const pending = new Map(); let seq = 0, docRevision = 0;
  const events = [];
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'response' && pending.has(m.id)) {
      const { res, rej } = pending.get(m.id); pending.delete(m.id);
      if (m.ok) { if (m.result && typeof m.result.docRevision === 'number') docRevision = Math.max(docRevision, m.result.docRevision); res(m.result); }
      else rej(Object.assign(new Error(m.error.code + ': ' + m.error.message), m.error));
    } else if (m.type === 'event') { events.push(m); if (m.data && typeof m.data.docRevision === 'number') docRevision = Math.max(docRevision, m.data.docRevision); }
  };
  const call = (method, params) => new Promise((res, rej) => { const id = 'e2e-' + (++seq); pending.set(id, { res, rej }); ws.send(JSON.stringify({ type: 'request', id, method, params: params || {} })); });
  const mutate = (method, params) => call(method, Object.assign({ baseRevision: docRevision }, params));
  return {
    async connect() { await ready; const w = await call('hello', { apiVersion: '1', clientName: 'e2e-vc-cue' }); docRevision = w.docRevision; },
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
/* CustomComboBox: a root div (our data-e2e lands on it) whose button shows the current label; the popup
   entries are plain leaf elements appended later in the DOM, so the wanted label's LAST match is the entry. */
async function pickCombo(page, rootSel, currentLabel, wantedLabel) {
  await page.eval('document.querySelector(' + JSON.stringify(rootSel) + ').scrollIntoView({ block: "center" })');
  await page.click(leafByText(rootSel, currentLabel));
  await sleep(200);
  await page.click(leafByText(null, wantedLabel, true));
  await sleep(200);
}
/* CustomSpinBox: click, select all, type, Enter (it commits on every keystroke; Enter ends editing). */
async function setSpin(page, selector, value) {
  await clickSel(page, selector);
  await page.eval('(function(){const el=document.querySelector(' + JSON.stringify(selector) + '); el.focus(); el.select(); return true;})()');
  await page.type(String(value));
  await page.key('Enter');
  await sleep(150);
}
async function typeInto(page, selector, text, enter) {
  await clickSel(page, selector);
  await page.eval('(function(){const el=document.querySelector(' + JSON.stringify(selector) + '); el.focus(); el.select(); return true;})()');
  await page.type(text);
  if (enter) await page.key('Enter');
  await sleep(150);
}
async function poll(fn, timeout = 6000) { const t0 = Date.now(); for (;;) { const v = await fn(); if (v) return v; if (Date.now() - t0 > timeout) return null; await sleep(150); } }
const EDIT_BTN = 'button[title^="Enable/Disable the widgets edit mode"]';
async function waitConnected(page) {
  await page.waitFor('(function(){const b=document.querySelector(' + JSON.stringify(EDIT_BTN) + '); return !!b && !b.disabled;})()', 30000);
}
async function gotoPage(page, label) { await clickText(page, label); await sleep(400); }
async function setEdit(page, on) {
  const isOn = () => page.eval('(function(){const b=document.querySelector(' + JSON.stringify(EDIT_BTN) + '); return !!b && (b.getAttribute("aria-pressed") === "true" || !!document.querySelector("[data-vc-selected], [data-vc-resize]") || !!document.querySelector("[data-vc-widget]") && getComputedStyle(document.querySelector("[data-vc-widget]")).cursor === "move");})()');
  const cur = await isOn();
  if (cur !== on) { await page.click(EDIT_BTN); await sleep(300); }
}

(async () => {
  const srv = api(API_PORT);
  await srv.connect();
  const widgetGet = async (id) => srv.call('vc.widget.get', { widgetId: String(id) });
  const fnByName = async (name) => (await srv.call('functions.list', { typeFilter: ['Chaser'] })).functions.find(f => f.name === name);

  /* ================= setup over the API ================= */
  console.log('setup');
  const mode = await srv.call('core.mode.get');
  if (mode.mode !== 'design') await srv.call('core.mode.set', { mode: 'design' });
  const pagesBefore = (await srv.call('vc.page.list')).pages;
  await srv.mutate('vc.page.create', { index: pagesBefore.length });
  const pages = (await srv.call('vc.page.list')).pages;
  const pageIndex = pages.length - 1;
  const pageLabel = pages[pageIndex].name || ('Page ' + (pageIndex + 1));
  const cue = await srv.mutate('vc.widget.create', { widgetType: 'CueList', page: pageIndex, geometry: { x: 20, y: 20, width: 440, height: 280 }, style: { caption: CUE_CAPTION } });
  const speed = await srv.mutate('vc.widget.create', { widgetType: 'Speed', page: pageIndex, geometry: { x: 480, y: 20, width: 300, height: 320 }, style: { caption: SPEED_CAPTION } });
  const cueId = String(cue.widgetId), speedId = String(speed.widgetId);
  check(cueId && speedId, 'scratch page ' + pageIndex + ' with CueList #' + cueId + ' and Speed #' + speedId);
  const chaserA = await fnByName('Gobo Spot - Sweep L-R'), chaserB = await fnByName('Gobo Spot - Sweep F-B');
  check(chaserA && chaserB, 'SF3 chasers found (' + (chaserA && chaserA.id) + ', ' + (chaserB && chaserB.id) + ')');
  let g = await widgetGet(cueId);
  eq(g.typeConfig, { chaserID: '4294967295', nextPrevBehavior: 'DefaultRunFirst', playbackLayout: 'PlayPauseStop', sideFaderMode: 'None' }, 'fresh CueList typeConfig');
  g = await widgetGet(speedId);
  eq(g.typeConfig.visibilityMask, ['Tap', 'Multipliers', 'Beats'], 'fresh Speed visibility mask (VCSpeedDial default)');
  eq([g.factor, g.tapTimeValue, g.typeConfig.presets], ['One', 0, []], 'fresh Speed live seeds');

  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=vc');
  try {
    console.log('connect + open the scratch page');
    await waitConnected(page);
    await gotoPage(page, pageLabel);
    await page.waitFor('!!document.querySelector(\'[data-vc-widget="' + cueId + '"]\')', 10000);
    await page.click(EDIT_BTN); await sleep(300);
    check(await page.eval('!!document.querySelector("[data-vc-widget] [data-vc-widget], [data-vc-widget]") && getComputedStyle(document.querySelector(\'[data-vc-widget="' + cueId + '"]\')).cursor === "move"'), 'edit mode on');

    /* ================= Cue list properties ================= */
    console.log('cue list: properties');
    await page.click('[data-vc-widget="' + cueId + '"]'); await sleep(300);
    await page.waitFor('!!document.querySelector(\'[data-e2e-section="cueChaser"]\')', 8000);
    await typeInto(page, '[data-e2e-section="cueChaser"] input[placeholder="Search functions"]', 'Sweep L-R');
    await clickText(page, chaserA.name, '[data-e2e-section="cueChaser"]', true);
    g = await poll(async () => { const x = await widgetGet(cueId); return x.typeConfig.chaserID === String(chaserA.id) ? x : null; });
    check(!!g, 'chaser attached through the picker');
    await pickCombo(page, '[data-e2e="cue-layout"]', 'Play/Pause + Stop', 'Play/Stop + Pause');
    g = await poll(async () => { const x = await widgetGet(cueId); return x.typeConfig.playbackLayout === 'PlayStopPause' ? x : null; });
    check(!!g, 'playback layout PlayStopPause');
    await pickCombo(page, '[data-e2e="cue-nextprev"]', 'Run from first/last cue', 'Select next/previous cue');
    g = await poll(async () => { const x = await widgetGet(cueId); return x.typeConfig.nextPrevBehavior === 'Select' ? x : null; });
    check(!!g, 'next/previous behaviour Select');
    await clickSel(page, '[data-e2e="cue-fader-mode"] > div:nth-child(2) > *:first-child');
    g = await poll(async () => { const x = await widgetGet(cueId); return x.typeConfig.sideFaderMode === 'Crossfade' ? x : null; });
    check(!!g, 'side fader mode Crossfade');
    eq(g && g.sideFaderLevel, 100, 'switching to Crossfade reset the side fader level to 100 (engine behaviour)');
    /* back to the layout the transport assertions below expect */
    await pickCombo(page, '[data-e2e="cue-layout"]', 'Play/Stop + Pause', 'Play/Pause + Stop');
    g = await poll(async () => { const x = await widgetGet(cueId); return x.typeConfig.playbackLayout === 'PlayPauseStop' ? x : null; });
    check(!!g, 'playback layout back to PlayPauseStop');
    await page.screenshot(path.join(OUT, '1-cue-props.png'));

    /* ================= Cue list body: side fader, play, next ================= */
    console.log('cue list: live body');
    await page.click(EDIT_BTN); await sleep(400);
    await page.waitFor('document.querySelector(\'[data-e2e="cue-side-fader"]\') && document.querySelector(\'[data-e2e="cue-side-fader"]\').getAttribute("data-mode") === "Crossfade"', 8000);
    await page.waitFor('document.querySelectorAll(\'[data-e2e="cue-steps"] [role="row"]\').length > 0', 10000);
    const stepCount = await page.eval('document.querySelectorAll(\'[data-e2e="cue-steps"] [role="row"]\').length');
    check(stepCount > 1, 'cue list shows the chaser steps (' + stepCount + ')');
    const fr = await page.rectOf('[data-e2e="cue-side-fader"] [data-vc-fader]');
    await page.drag(fr.x + fr.w / 2, fr.y + 4, fr.x + fr.w / 2, fr.y + fr.h * 0.6, 6);
    await sleep(400);
    g = await poll(async () => { const x = await widgetGet(cueId); return x.sideFaderLevel < 90 && x.sideFaderLevel > 0 ? x : null; });
    check(!!g, 'side fader drag reached the server (level ' + (g && g.sideFaderLevel) + ')');
    eq(await page.eval('Number(document.querySelector(\'[data-e2e="cue-side-fader"]\').getAttribute("data-level"))'), g && g.sideFaderLevel, 'body shows the same level');
    check(await page.eval('document.querySelector(\'[data-e2e="cue-side-fader"]\').textContent.indexOf("%") !== -1'), 'crossfade labels are percentages');
    /* the API's own view of the event */
    check(srv.events.some(e => e.topic === 'vc.cueList.sideFaderChanged' && String(e.data.widgetId) === cueId && e.data.level === g.sideFaderLevel), 'vc.cueList.sideFaderChanged reached the second client');

    await clickSel(page, '[data-e2e="cue-play"] button');
    g = await poll(async () => { const x = await widgetGet(cueId); return x.running && x.playbackIndex === 0 ? x : null; });
    check(!!g, 'play: running at step 0');
    await clickSel(page, '[data-e2e="cue-next"] button');
    g = await poll(async () => { const x = await widgetGet(cueId); return x.playbackIndex === 1 ? x : null; });
    check(!!g, 'next: at step 1 (nextStepIndex ' + (g && g.nextStepIndex) + ', primaryTop ' + (g && g.primaryTop) + ')');
    const labels = await page.eval('Array.from(document.querySelectorAll(\'[data-e2e="cue-side-fader"] div\')).map(d => d.textContent.trim()).filter(t => /^#\\d+$/.test(t))');
    check(labels.indexOf('#2') !== -1, 'side fader step label shows the current step #2 (' + JSON.stringify(labels) + ')');
    await page.screenshot(path.join(OUT, '2-cue-live.png'));
    await clickSel(page, '[data-e2e="cue-stop"] button');
    g = await poll(async () => { const x = await widgetGet(cueId); return !x.running ? x : null; });
    check(!!g, 'stop');

    /* ================= Speed dial properties ================= */
    console.log('speed dial: properties');
    await page.click(EDIT_BTN); await sleep(400);
    await page.click('[data-vc-widget="' + speedId + '"]'); await sleep(300);
    await page.waitFor('!!document.querySelector(\'[data-e2e-section="speedFunctions"]\')', 8000);
    await typeInto(page, '[data-e2e-section="speedFunctions"] input[placeholder="Add a function…"]', 'Sweep L-R');
    await clickText(page, chaserA.name, '[data-e2e="speed-fn-matches"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.functions.length === 1 ? x : null; });
    check(!!g, 'first function added');
    await typeInto(page, '[data-e2e-section="speedFunctions"] input[placeholder="Add a function…"]', 'Sweep F-B');
    await clickText(page, chaserB.name, '[data-e2e="speed-fn-matches"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.functions.length === 2 ? x : null; });
    check(!!g, 'second function added');
    eq(g && g.typeConfig.functions.map(f => [f.functionID, f.fadeInFactor, f.fadeOutFactor, f.durationFactor]).sort(), [[String(chaserA.id), 'None', 'None', 'One'], [String(chaserB.id), 'None', 'None', 'One']].sort(), 'both with the default factors');
    await pickCombo(page, '[data-e2e="speed-fn-row"][data-fid="' + chaserA.id + '"] [data-e2e="speed-fn-duration"]', '1', '2');
    g = await poll(async () => { const x = await widgetGet(speedId); const f = x.typeConfig.functions.find(f => f.functionID === String(chaserA.id)); return f && f.durationFactor === 'Two' ? x : null; });
    check(!!g, 'duration factor of the first function is Two');
    await pickCombo(page, '[data-e2e="speed-fn-row"][data-fid="' + chaserB.id + '"] [data-e2e="speed-fn-fadein"]', '(Not sent)', '1/2');
    g = await poll(async () => { const x = await widgetGet(speedId); const f = x.typeConfig.functions.find(f => f.functionID === String(chaserB.id)); return f && f.fadeInFactor === 'Half' ? x : null; });
    check(!!g, 'fade in factor of the second function is Half');

    /* appearance: enable Dial, Apply and Seconds (rows 1, 4 and 7 of the Appearance list) */
    for (const [row, flag] of [[1, 'Dial'], [4, 'Apply'], [7, 'Seconds']]) {
      await clickSel(page, '[data-e2e="speed-visibility"] > div:nth-child(' + row + ') > *:first-child');
      g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.visibilityMask.indexOf(flag) !== -1 ? x : null; });
      check(!!g, 'visibility flag ' + flag + ' on');
    }
    eq(g && g.typeConfig.visibilityMask.slice().sort(), ['Apply', 'Beats', 'Dial', 'Multipliers', 'Seconds', 'Tap'], 'visibility mask has the new flags and kept the old ones');
    /* control properties: reset-on-dial-change appears once Dial is on; tap controls BPM */
    await clickSel(page, '[data-e2e-section="speedControl"] > div > div:nth-child(2) > *:first-child');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.resetOnDialChange === true ? x : null; });
    check(!!g, 'reset multiplier on dial change');
    await setSpin(page, 'input[data-e2e="speed-range-max"]', '20');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.timeMaximumValue === 20000 ? x : null; });
    check(!!g, 'dial time range max 20 s');

    /* presets: add two, rename + retime one, remove the other */
    await typeInto(page, 'input[data-e2e="speed-preset-name"]', 'Slow');
    await setSpin(page, 'input[data-e2e="speed-preset-time"]', '1500');
    await clickSel(page, '[data-e2e="speed-preset-add"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.presets.length === 1 ? x : null; });
    check(!!g, 'preset added');
    eq(g && [g.typeConfig.presets[0].name, g.typeConfig.presets[0].valueMs], ['Slow', 1500], 'preset Slow / 1500 ms');
    const slowId = g && g.typeConfig.presets[0].presetId;
    await typeInto(page, 'input[data-e2e="speed-preset-name"]', 'Fast');
    await setSpin(page, 'input[data-e2e="speed-preset-time"]', '250');
    await clickSel(page, '[data-e2e="speed-preset-add"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.presets.length === 2 ? x : null; });
    check(!!g, 'second preset added');
    const fastId = g && g.typeConfig.presets.find(p => p.name === 'Fast').presetId;
    await clickSel(page, '[data-e2e="speed-preset-row"][data-preset="' + slowId + '"]');
    await typeInto(page, 'input[data-e2e="speed-preset-name"]', 'Slower', true);
    g = await poll(async () => { const x = await widgetGet(speedId); const p = x.typeConfig.presets.find(p => p.presetId === slowId); return p && p.name === 'Slower' ? x : null; });
    check(!!g, 'preset renamed to Slower (vc.speedDial.preset.update)');
    await setSpin(page, 'input[data-e2e="speed-preset-time"]', '1600');
    g = await poll(async () => { const x = await widgetGet(speedId); const p = x.typeConfig.presets.find(p => p.presetId === slowId); return p && p.valueMs === 1600 ? x : null; });
    check(!!g, 'preset time changed to 1600 ms');
    await clickSel(page, '[data-e2e="speed-preset-row"][data-preset="' + fastId + '"]');
    await clickSel(page, '[data-e2e="speed-preset-remove"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.typeConfig.presets.length === 1 && x.typeConfig.presets[0].presetId === slowId ? x : null; });
    check(!!g, 'preset Fast removed');
    check(srv.events.filter(e => e.topic === 'vc.speedDial.presetsChanged' && String(e.data.widgetId) === speedId).length >= 5, 'vc.speedDial.presetsChanged broadcast for add/add/update/update/remove');
    await page.screenshot(path.join(OUT, '3-speed-props.png'));

    /* ================= Speed dial body ================= */
    console.log('speed dial: live body');
    await page.click(EDIT_BTN); await sleep(400);
    await page.waitFor('!!document.querySelector(\'[data-e2e="speed-body"] [data-e2e="speed-dial"]\') && !!document.querySelector(\'[data-e2e="speed-apply"]\')', 8000);
    await clickSel(page, '[data-e2e="speed-factor-Half"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.factor === 'Half' ? x : null; });
    check(!!g, 'factor 1/2 through the beats button');
    eq(await page.eval('document.querySelector(\'[data-e2e="speed-body"]\').getAttribute("data-factor")'), 'Half', 'body shows factor Half');
    await clickSel(page, '[data-e2e="speed-plus"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.factor === 'One' ? x : null; });
    check(!!g, '+ steps the factor to 1');
    await clickSel(page, '[data-e2e="speed-plus"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.factor === 'Two' ? x : null; });
    check(!!g, '+ again: 2');
    await clickSel(page, '[data-e2e="speed-factor-reset"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.factor === 'One' ? x : null; });
    check(!!g, 'x resets the factor to 1');
    check(srv.events.some(e => e.topic === 'vc.speedDial.factorChanged' && String(e.data.widgetId) === speedId && e.data.factor === 'Half'), 'vc.speedDial.factorChanged reached the second client');

    /* preset button -> currentTime 1600; Apply -> chaser A duration = 1600 x 1 (dial) x 2 (per-function) */
    await clickSel(page, '[data-e2e="speed-presets"] [data-e2e-preset="' + slowId + '"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.ms === 1600 ? x : null; });
    check(!!g, 'preset button applied 1600 ms (vc.widget.preset.apply)');
    check(await page.eval('document.querySelector(\'[data-e2e="speed-mult-label"]\').textContent.indexOf("1.6s") !== -1'), 'multiplier label shows 1.6s');
    await clickSel(page, '[data-e2e="speed-apply"]');
    let fa = await poll(async () => { const x = await srv.call('functions.get', { functionId: String(chaserA.id) }); return Number(x.duration != null ? x.duration : (x.speed && x.speed.duration)) === 3200 ? x : null; });
    check(!!fa, 'Apply pushed duration 3200 ms onto ' + chaserA.name);

    /* tap twice, then right-click to reset */
    await clickSel(page, '[data-e2e="speed-tap"]');
    await sleep(320);
    await clickSel(page, '[data-e2e="speed-tap"]');
    g = await poll(async () => { const x = await widgetGet(speedId); return x.tapTimeValue > 0 ? x : null; });
    check(!!g, 'two taps set a tap interval (' + (g && g.tapTimeValue) + ' ms, currentTime ' + (g && g.ms) + ')');
    check(g && g.tapTimeValue > 200 && g.tapTimeValue < 1200, 'tap interval is in a plausible range');
    eq(await page.eval('Number(document.querySelector(\'[data-e2e="speed-body"]\').getAttribute("data-tap"))'), g && g.tapTimeValue, 'body shows the tap interval');
    await page.click('[data-e2e="speed-tap"]', { button: 'right' });
    g = await poll(async () => { const x = await widgetGet(speedId); return x.tapTimeValue === 0 ? x : null; });
    check(!!g, 'right-click reset the tap (vc.speedDial.resetTap)');
    check(srv.events.some(e => e.topic === 'vc.speedDial.tapChanged' && String(e.data.widgetId) === speedId && e.data.tapTimeValue === 0), 'vc.speedDial.tapChanged (0) reached the second client');
    await page.screenshot(path.join(OUT, '4-speed-live.png'));

    /* ================= reload: everything persisted ================= */
    console.log('reload');
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=vc');
    await waitConnected(page);
    await gotoPage(page, pageLabel);
    await page.waitFor('!!document.querySelector(\'[data-e2e="cue-side-fader"]\') && !!document.querySelector(\'[data-e2e="speed-presets"]\')', 15000);
    eq(await page.eval('document.querySelector(\'[data-e2e="cue-side-fader"]\').getAttribute("data-mode")'), 'Crossfade', 'reload: cue list side fader still in Crossfade mode');
    check(await page.eval('document.querySelectorAll(\'[data-e2e="cue-steps"] [role="row"]\').length') > 1, 'reload: cue list shows the chaser steps');
    eq(await page.eval('Array.from(document.querySelectorAll(\'[data-e2e="speed-presets"] button\')).map(b => b.textContent.trim())'), ['Slower'], 'reload: speed dial preset button');
    check(await page.eval('!!document.querySelector(\'[data-e2e="speed-dial"]\') && !!document.querySelector(\'[data-e2e="speed-apply"]\') && !!document.querySelector(\'[data-e2e="speed-seconds"]\')'), 'reload: Dial/Apply/Seconds controls visible');
    await page.screenshot(path.join(OUT, '5-reload.png'));

    /* ================= save + grep ================= */
    console.log('save and grep the .qxw');
    const saved = await srv.call('core.project.saveAs', { target: 'serverPath', path: SAVE_PATH });
    check(saved && saved.filePath, 'saved to ' + (saved && saved.filePath));
    const xml = fs.readFileSync(SAVE_PATH, 'utf8');
    const cueBlock = (xml.match(new RegExp('<CueList Caption="' + CUE_CAPTION + '" ID="' + cueId + '">[\\s\\S]*?</CueList>')) || [])[0] || '';
    check(!!cueBlock, 'CueList block present in the XML');
    check(new RegExp('<Chaser>' + chaserA.id + '</Chaser>').test(cueBlock), 'XML Chaser id');
    check(/<NextPrevBehavior>2<\/NextPrevBehavior>/.test(cueBlock), 'XML NextPrevBehavior Select (2)');
    check(/<SlidersMode>Crossfade<\/SlidersMode>/.test(cueBlock), 'XML SlidersMode Crossfade');
    check(!/<PlaybackLayout>/.test(cueBlock), 'XML omits PlaybackLayout for the default PlayPauseStop');
    const speedBlock = (xml.match(new RegExp('<SpeedDial Caption="' + SPEED_CAPTION + '" ID="' + speedId + '">[\\s\\S]*?</SpeedDial>')) || [])[0] || '';
    check(!!speedBlock, 'SpeedDial block present in the XML');
    check(new RegExp('<Function FadeIn="0" FadeOut="0" Duration="7">' + chaserA.id + '</Function>').test(speedBlock), 'XML function A with duration factor Two (7)');
    check(new RegExp('<Function FadeIn="5" FadeOut="0" Duration="6">' + chaserB.id + '</Function>').test(speedBlock), 'XML function B with fade in Half (5)');
    check(/<AbsoluteValue Minimum="0" Maximum="20000"/.test(speedBlock), 'XML dial range max 20000');
    check(/<ResetFactorOnDialChange>True<\/ResetFactorOnDialChange>/.test(speedBlock), 'XML ResetFactorOnDialChange');
    const vis = Number((speedBlock.match(/<Visibility>(\d+)<\/Visibility>/) || [])[1]);
    eq(vis, 2 | 4 | 32 | 128 | 256 | 512, 'XML Visibility mask = Dial|Tap|Seconds|Multipliers|Apply|Beats');
    check(/<Preset[\s\S]*?Slower[\s\S]*?1600[\s\S]*?<\/Preset>/.test(speedBlock) && (speedBlock.match(/<Preset\b/g) || []).length === 1, 'XML has exactly one preset, Slower / 1600');
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
