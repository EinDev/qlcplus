/**
 * End-to-end driver for the Virtual Console external controls slice (vc/vc-external.jsx): manual
 * input sources, custom feedback, key combinations recorded in the browser, the auto-detect arm /
 * cancel path, reload persistence, and the Operate-mode key bindings, against a sandbox instance
 * (dev-webui-sandbox.ps1 -Name vcinput -ApiPort 9220 -WebUiPort 9221) in headless Chrome.
 *
 *   node webui/tools/e2e/vc-input.js
 *   env: QLC_API (ws://127.0.0.1:9220/), QLC_WEB (http://localhost:9221/), QLC_OUT (saveAs path,
 *        C:\qlcsandbox\vcinput\out.qxw), QLC_SHOTS (screenshot dir)
 *
 * The sandbox has no IO plugins, so no controller signal can arrive: auto-detection is exercised up
 * to arming / refusing a second client / cancelling (the binding itself is covered by
 * controlapi/test/apivcinputdomain against engine/test/iopluginstub). Every UI gesture is read back
 * over a second, raw API connection; the run ends with core.project.saveAs + a grep of the .qxw.
 */
const fs = require('fs');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const API = process.env.QLC_API || 'ws://127.0.0.1:9220/';
const WEB = process.env.QLC_WEB || 'http://localhost:9221/';
const OUT = process.env.QLC_OUT || 'C:\\qlcsandbox\\vcinput\\out.qxw';
const SHOTS = process.env.QLC_SHOTS || path.join(process.cwd(), 'e2e-shots');

/* ---------------------------------------------------------------- raw API client */
class Api {
  constructor(url, name) { this.url = url; this.name = name || 'vc-input e2e'; this.rev = 0; this.next = 1; this.pending = new Map(); this.events = []; }
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
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: this.name }).then(r => { this.rev = r.docRevision; this.clientId = r.clientId; res(r); }, rej);
    });
  }
  call(method, params = {}) {
    const id = 'e-' + (this.next++);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  structural(method, params = {}) {
    const go = () => this.call(method, Object.assign({}, params, { baseRevision: this.rev }));
    return go().catch(e => { if (e.code === 'CONFLICT') return go(); throw e; });
  }
  widget(id) { return this.call('vc.widget.get', { widgetId: String(id) }); }
  eventsOf(topic, since = 0) { return this.events.slice(since).filter(e => e.topic === topic); }
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
const byText = (tag, text) => `[...document.querySelectorAll(${JSON.stringify(tag)})].reverse().find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
async function clickFn(page, fnBody, what) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest', inline: 'nearest' }); })()`);
  await page.click(new Function('return ' + fnBody));
}
async function typeInto(page, fnBody, text, what) {
  await clickFn(page, fnBody, what);
  await page.eval(`(function(){ const el = document.activeElement; if (el && el.select) el.select(); })()`);
  await page.type(text);
  await page.key('Enter');
}
/** A real key press through CDP: $key is KeyboardEvent.key, $mods an array of 'Ctrl'/'Shift'/'Alt'. */
async function press(page, key, code, vk, mods = [], phase = 'both') {
  const bits = { Alt: 1, Ctrl: 2, Meta: 4, Shift: 8 };
  const modifiers = mods.reduce((m, x) => m | bits[x], 0);
  const base = { key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers };
  if (phase !== 'up') await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: mods.some(m => m !== 'Shift') ? 'rawKeyDown' : 'keyDown' }, base, mods.some(m => m !== 'Shift') ? {} : { text: key }));
  if (phase !== 'down') await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, base));
}
async function selectWidget(page, id) {
  /* Near the top-left corner, not the centre: a slider's fader / a frame's body would take a centre click. */
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${id}"]')`);
  const r = await page.rectOf(`[data-vc-widget="${id}"]`);
  await page.mouse('mouseMoved', r.x + 5, r.y + 5);
  await page.mouse('mousePressed', r.x + 5, r.y + 5); await page.mouse('mouseReleased', r.x + 5, r.y + 5);
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${id}"][data-vc-selected]')`, 4000).catch(async (e) => {
    const hit = await page.eval(`(function(){ const el = document.elementFromPoint(${r.x + 5}, ${r.y + 5}); return el ? el.outerHTML.slice(0, 120) : 'none'; })()`);
    const sel = await page.eval(`JSON.stringify([...document.querySelectorAll('[data-vc-selected]')].map(e => e.getAttribute('data-vc-widget')))`) + ' ev ' + await page.eval('JSON.stringify((window.__ev || []).slice(-12))');
    throw new Error('selecting widget ' + id + ' failed; element at the click point: ' + hit + '; selected: ' + sel + '; rect ' + JSON.stringify(r));
  });
  await page.waitFor(`!!document.querySelector('[data-vcx-panel="${id}"]')`, 8000);
  await sleep(250);
}
const panel = (id, sel) => `document.querySelector('[data-vcx-panel="${id}"] ${sel}')`;
/** Adds a universe 1 / channel $ch source through the manual input source dialog. */
async function addManual(page, id, ch) {
  await clickFn(page, panel(id, '[data-vcx-add-manual]'), 'manual source button');
  await page.waitFor(`!!document.querySelector('[data-vcx-manual]')`);
  await sleep(300); // io.universe.list round trip picks the mode
  await clickFn(page, q('[data-vcx-manual-mode="manual"]'), 'manual mode');
  await typeInto(page, q('input[data-vcx-manual-channel]'), String(ch), 'manual channel');
  await clickFn(page, byText('button', 'Ok'), 'Ok');
  await page.waitFor(`!document.querySelector('[data-vcx-manual]')`);
}

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  const other = new Api(API, 'vc-input bystander');
  await other.connect();
  console.log('API connected, docRevision ' + api.rev);

  if ((await api.call('core.mode.get')).mode !== 'design') await api.call('core.mode.set', { mode: 'design' });
  const scenes = (await api.call('functions.list')).functions.filter(f => !f.hidden && f.type === 'Scene');
  check(scenes.length > 0, 'sandbox project has a Scene for the button');

  /* A scratch page holding the four widgets (created over the API: this slice is about their external controls). */
  const pagesBefore = (await api.call('vc.page.list')).pages;
  await api.structural('vc.page.create', { index: pagesBefore.length });
  const scratch = pagesBefore.length;
  const mk = async (widgetType, x, caption, geometry) => (await api.structural('vc.widget.create', { widgetType, page: scratch,
    geometry: Object.assign({ x, y: 20, width: 90, height: 90 }, geometry || {}), style: { caption } })).widgetId;
  const button = await mk('Button', 20, 'KeyBtn');
  await api.structural('vc.widget.setConfig', { widgetId: String(button), config: { functionID: String(scenes[0].id) } });
  const slider = await mk('Slider', 140, 'Fader', { height: 160, width: 70 });
  const cue = await mk('CueList', 240, 'Cues', { width: 300, height: 200 });
  const frame = await mk('Frame', 580, 'Pager', { width: 260, height: 200 });
  await api.structural('vc.widget.setConfig', { widgetId: String(frame), config: { multiPageMode: true, totalPagesNumber: 3 } });
  console.log('scratch page #' + (scratch + 1) + ': button ' + button + ', slider ' + slider + ', cue list ' + cue + ', frame ' + frame);

  const browser = await launch({ headless: true });
  const page = await browser.open(WEB + '?ctx=vc', { width: 1700, height: 1000 });
  const openScratch = async () => {
    await page.waitFor(`document.querySelectorAll('[title^="Page "]').length >= ${scratch + 1}`, 30000);
    await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${scratch + 1}'))`, 'scratch page tab');
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${frame}"]')`);
  };
  const editToggle = `document.querySelector('button img[src$="/edit.svg"]').closest('button')`;
  try {
    await openScratch();
    await page.eval(`(function(){ window.__ev = []; ['pointerdown','pointerup','keydown'].forEach(t => window.addEventListener(t, e => window.__ev.push(t + ':' + (e.key || '') + (e.target.getAttribute && (e.target.getAttribute('data-vc-widget') || e.target.tagName))), true)); })()`);
    await clickFn(page, editToggle, 'edit button');
    await page.waitFor(`document.body.textContent.includes('Pick a widget')`);
    await sleep(500);

    /* ---- snapshot: external controls table ---- */
    console.log('\n[external controls table]');
    const cueDetail = await api.widget(cue);
    check(cueDetail.externalControls.map(c => c.name).join('|') === 'Next Cue|Previous Cue|Play/Stop/Pause|Stop/Pause|Side Fader', 'cue list exposes its five controls', cueDetail.externalControls);
    check((await api.widget(frame)).externalControls.filter(c => c.controlId >= 20).length === 3, 'multipage frame exposes one shortcut control per page');

    /* ---- manual input sources ---- */
    console.log('\n[manual input sources]');
    await selectWidget(page, button);
    await addManual(page, button, 5);
    let src = await until(async () => (await api.widget(button)).inputSources[0], 'button source');
    check(src.controlId === 0 && src.universe === 0 && src.channel === 4, 'button: Pressure <- universe 1 / channel 5 (wire 0 / 4)', src);
    await page.waitFor(`!!document.querySelector('[data-vcx-source="0:0:4"]')`);
    check(true, 'button panel lists the new source row');

    console.log('\n[custom feedback]');
    await clickFn(page, panel(button, '[data-vcx-source="0:0:4"] [data-vcx-source-feedback]'), 'custom feedback button');
    await page.waitFor(`!!document.querySelector('[data-vcx-feedback]')`);
    await typeInto(page, q('input[data-vcx-fb="lowerValue"]'), '10', 'lower');
    await typeInto(page, q('input[data-vcx-fb="upperValue"]'), '200', 'upper');
    await typeInto(page, q('input[data-vcx-fb="monitorValue"]'), '100', 'monitor');
    await page.screenshot(path.join(SHOTS, 'vc-input-1-feedback.png'));
    await clickFn(page, byText('button', 'Ok'), 'Ok');
    await page.waitFor(`!document.querySelector('[data-vcx-feedback]')`); // closes once the server acked
    src = await until(async () => { const s = (await api.widget(button)).inputSources[0]; return s && s.lowerValue === 10 ? s : null; }, 'feedback values');
    check(src.lowerValue === 10 && src.upperValue === 200 && src.monitorValue === 100, 'custom feedback 10 / 200 / 100 stored', src);
    check((await api.widget(button)).inputSources.length === 1, 'feedback edit updated the source in place');

    await selectWidget(page, slider);
    await addManual(page, slider, 5);
    src = await until(async () => (await api.widget(slider)).inputSources[0], 'slider source');
    check(src.controlId === 0 && src.channel === 4, 'slider: Slider Control <- universe 1 / channel 5', src);

    await selectWidget(page, cue);
    await addManual(page, cue, 5);
    src = await until(async () => (await api.widget(cue)).inputSources[0], 'cue source');
    check(src.controlId === 0 && src.channel === 4, 'cue list: Next Cue <- universe 1 / channel 5', src);

    /* ---- key combinations ---- */
    console.log('\n[key combinations]');
    await selectWidget(page, button);
    await clickFn(page, panel(button, '[data-vcx-add-key]'), 'add key button');
    await page.waitFor(`!!document.querySelector('[data-vcx-pending-key]')`);
    await press(page, 'K', 'KeyK', 75, ['Ctrl', 'Shift']);
    let keys = await until(async () => { const k = (await api.widget(button)).keySequences; return k.length ? k : null; }, 'button key');
    check(keys.length === 1 && keys[0].keySequence === 'Ctrl+Shift+K' && keys[0].controlId === 0, 'button: Ctrl+Shift+K -> Pressure', keys);
    await page.waitFor(`!!document.querySelector('[data-vcx-key="Ctrl+Shift+K"]')`);

    await selectWidget(page, frame);
    await clickFn(page, panel(frame, '[data-vcx-add-key]'), 'add key button');
    await page.waitFor(`!!document.querySelector('[data-vcx-pending-key]')`);
    await press(page, 'f', 'KeyF', 70);
    keys = await until(async () => { const k = (await api.widget(frame)).keySequences; return k.length ? k : null; }, 'frame key');
    check(keys.length === 1 && keys[0].keySequence === 'F' && keys[0].controlId === 0, 'frame: F -> Next Page', keys);

    /* Re-record the frame key (G), then back to F: exactly one entry each time (the 2026-09-26 duplicate fix). */
    await clickFn(page, panel(frame, '[data-vcx-key="F"] img[src$="/keybinding.svg"]'), 're-record key');
    await press(page, 'g', 'KeyG', 71);
    keys = await until(async () => { const k = (await api.widget(frame)).keySequences; return k.length === 1 && k[0].keySequence === 'G' ? k : null; }, 'frame key G');
    check(true, 're-recording replaces F with G (one entry)');
    await page.waitFor(`!!document.querySelector('[data-vcx-key="G"]')`);
    await clickFn(page, panel(frame, '[data-vcx-key="G"] img[src$="/keybinding.svg"]'), 're-record key');
    await press(page, 'f', 'KeyF', 70);
    keys = await until(async () => { const k = (await api.widget(frame)).keySequences; return k.length === 1 && k[0].keySequence === 'F' ? k : null; }, 'frame key F again');
    check(true, 're-recording back to F (one entry)');

    /* ---- auto-detection (arm / global slot / cancel) ---- */
    console.log('\n[auto-detection]');
    await selectWidget(page, slider);
    await clickFn(page, panel(slider, '[data-vcx-add-detect]'), 'auto-detect button');
    await page.waitFor(`!!document.querySelector('[data-vcx-pending-detect]')`);
    let refused = null;
    try { await other.call('vc.widget.inputDetect.start', { widgetId: String(cue), controlId: 0 }); } catch (e) { refused = e; }
    check(refused && refused.code === 'INVALID_STATE', 'while the browser is detecting, another client is refused (single global slot)', refused && refused.code);
    await page.screenshot(path.join(SHOTS, 'vc-input-2-detecting.png'));
    await clickFn(page, `document.querySelector('[data-vcx-detect-cancel]')`, 'cancel detection');
    await page.waitFor(`!document.querySelector('[data-vcx-pending-detect]')`);
    await sleep(300);
    let armed = await other.call('vc.widget.inputDetect.start', { widgetId: String(cue), controlId: 0 }).then(() => true, () => false);
    check(armed, 'cancelling released the slot (another client can arm)');
    await other.call('vc.widget.inputDetect.stop', {});

    /* ---- remove (a second slider source, added and removed again) ---- */
    await addManual(page, slider, 9);
    await until(async () => (await api.widget(slider)).inputSources.length === 2, 'second slider source');
    await clickFn(page, panel(slider, '[data-vcx-source="0:0:8"] [data-vcx-source-remove]'), 'remove source');
    await until(async () => (await api.widget(slider)).inputSources.length === 1, 'source removed');
    check(true, 'remove button drops the source (vc.widget.inputSource.remove)');
    const overflow = await page.eval(`(function(){ const p = document.querySelector('[data-vcx-panel="${slider}"]'); return p ? p.scrollWidth - p.clientWidth : -1; })()`);
    check(overflow >= 0 && overflow <= 1, 'external controls section fits the property panel (no horizontal overflow)', overflow);
    await page.screenshot(path.join(SHOTS, 'vc-input-3-panel.png'));

    /* ---- reload persistence ---- */
    console.log('\n[reload]');
    await page.goto(WEB + '?ctx=vc');
    await openScratch();
    await clickFn(page, editToggle, 'edit button');
    await sleep(500);
    await selectWidget(page, button);
    await page.waitFor(`!!document.querySelector('[data-vcx-panel="${button}"] [data-vcx-source="0:0:4"]') && !!document.querySelector('[data-vcx-panel="${button}"] [data-vcx-key="Ctrl+Shift+K"]')`);
    check(true, 'after a reload the button panel shows its source and key again');
    const bd = await api.widget(button);
    check(bd.inputSources.length === 1 && bd.keySequences.length === 1, 'vc.widget.get after reload', { s: bd.inputSources, k: bd.keySequences });

    /* ---- Operate mode: keys trigger widgets ---- */
    console.log('\n[operate mode keys]');
    await clickFn(page, editToggle, 'edit button (off)');
    await api.call('core.mode.set', { mode: 'operate' });
    await sleep(600);
    await page.eval(`(function(){ if (document.activeElement && document.activeElement.blur) document.activeElement.blur(); })()`);
    let mark = api.events.length;
    await press(page, 'K', 'KeyK', 75, ['Ctrl', 'Shift']);
    let ev = await until(() => api.eventsOf('vc.button.stateChanged', mark).find(e => String(e.data.widgetId) === String(button)), 'button stateChanged');
    check(ev.data.state === 'active', 'Ctrl+Shift+K toggles the button on (vc.button.stateChanged active)', ev.data);
    await page.waitFor(`!!document.querySelector('[data-vcx-keycast]')`);
    await page.screenshot(path.join(SHOTS, 'vc-input-4-operate.png'));
    mark = api.events.length;
    await press(page, 'K', 'KeyK', 75, ['Ctrl', 'Shift']);
    ev = await until(() => api.eventsOf('vc.button.stateChanged', mark).find(e => String(e.data.widgetId) === String(button)), 'button stateChanged off');
    check(ev.data.state === 'inactive', 'Ctrl+Shift+K again toggles it off', ev.data);

    const before = (await api.call('vc.frame.get', { widgetId: String(frame) })).currentPage;
    mark = api.events.length;
    await press(page, 'f', 'KeyF', 70);
    ev = await until(() => api.eventsOf('vc.frame.pageChanged', mark).find(e => String(e.data.widgetId) === String(frame)), 'frame pageChanged');
    check(ev.data.page === before + 1, 'F flips the frame to the next page', { before, after: ev.data.page });

    /* A key typed into a text field is text, not a binding. */
    await page.eval(`(function(){ const i = document.createElement('input'); i.id = 'vcx-probe'; document.body.appendChild(i); i.focus(); })()`);
    mark = api.events.length;
    await press(page, 'f', 'KeyF', 70);
    await sleep(500);
    check(!api.eventsOf('vc.frame.pageChanged', mark).length, 'F typed into a focused text field does not flip the frame');
    await page.eval(`(function(){ const i = document.getElementById('vcx-probe'); if (i) i.remove(); })()`);

    /* ---- save + grep ---- */
    console.log('\n[saveAs]');
    await api.call('core.mode.set', { mode: 'design' });
    await api.call('core.project.saveAs', { target: 'serverPath', path: OUT });
    const xml = fs.readFileSync(OUT, 'utf8');
    const section = (tag, id) => { const m = xml.match(new RegExp(`<${tag} Caption="[^"]*" ID="${id}"[\\s\\S]*?</${tag}>`)); return m ? m[0] : ''; };
    const bx = section('Button', button), sx = section('Slider', slider), cx = section('CueList', cue), fxm = section('Frame', frame);
    check(/<Input ID="0" Universe="0" Channel="4" LowerValue="10" UpperValue="200" MonitorValue="100"\/>/i.test(bx) || /<Input [^>]*Universe="0" Channel="4" LowerValue="10" UpperValue="200" MonitorValue="100"/.test(bx), 'Button XML: <Input ... Universe="0" Channel="4" LowerValue="10" UpperValue="200" MonitorValue="100"/>', bx.match(/<Input[^>]*>/g));
    check(/<Key>Ctrl\+Shift\+K<\/Key>/.test(bx), 'Button XML: <Key>Ctrl+Shift+K</Key>', bx.match(/<Key>[^<]*<\/Key>/g));
    check(/<Input [^>]*Universe="0" Channel="4"/.test(sx), 'Slider XML: <Input ... Universe="0" Channel="4"/>', sx.match(/<Input[^>]*>/g));
    check(/<Next>\s*<Input [^>]*Universe="0" Channel="4"/.test(cx), 'CueList XML: <Next><Input ... Channel="4"/></Next>', cx.match(/<Next>[\s\S]*?<\/Next>/));
    check(/<Next>\s*<Key>F<\/Key>\s*<\/Next>/.test(fxm), 'Frame XML: <Next><Key>F</Key></Next>', fxm.match(/<Next>[\s\S]*?<\/Next>/));
    if (!bx || !fxm) console.log('  (widget sections not found: button ' + !!bx + ', frame ' + !!fxm + ')');
    console.log('  button XML: ' + (bx.match(/<Input[^>]*>|<Key>[^<]*<\/Key>/g) || []).join(' '));

    check(page.consoleErrors.length === 0, 'no console errors', page.consoleErrors);
  } catch (e) {
    failures.push('EXCEPTION ' + (e && e.stack || e));
    try { await page.screenshot(path.join(SHOTS, 'vc-input-failure.png')); } catch (x) { }
    console.log('  EXCEPTION ' + (e && e.stack || e));
    if (page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
  } finally {
    await browser.close();
    api.close(); other.close();
  }
  console.log('\n' + checks + ' checks, ' + failures.length + ' failed' + (failures.length ? ':\n  ' + failures.join('\n  ') : ''));
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
