/**
 * End-to-end driver for the "partial rows" slice (Virtual Console, Show Manager, Input / Output) against a
 * sandbox instance with the internal Loopback plugin in headless Chrome:
 *
 *   .\dev-webui-sandbox.ps1 -Name partialsvc -BuildDir .\build -WebUiRoot .\webui -ApiPort 9330 -WebUiPort 9331 -Plugins loopback
 *   node webui/tools/e2e/partials-vc.js --sandbox C:\qlcsandbox\partialsvc [--api 9330] [--web 9331] [--shots <dir>] [--only io,vc,show]
 *
 * Loopback's output lines feed its own input lines on the same machine (nothing leaves the host), which
 * lets this driver exercise real I/O paths the plugin-less sandboxes could not: output / input /
 * feedback patches, several outputs per universe, per-output pause and blackout, input profile
 * assignment and learn, VC external-control auto-detection from a real input signal and feedback
 * values reaching a line. The universes it patches (4, 5 and 6 of the test project, plus 3 for the
 * second output) are read back through Passthrough: an input patched on a passthrough universe copies
 * what arrives into that universe's output, which io.dmx.universe.get reports.
 *
 * <sandbox dir> must be the dev-webui-sandbox.ps1 folder: core.project.saveAs writes <sandbox>\partials.qxw
 * and the audio settings check reads <sandbox>\Settings (QLCPLUS_SETTINGS_DIR). Every UI gesture is read
 * back over a second, raw API connection; exit code 1 when a check fails or the page logged a console error.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', process.env.E2E_API_PORT || 9330)), WEB_PORT = Number(opt('web', process.env.E2E_WEB_PORT || 9331));
const HOST = process.env.E2E_HOST || '[::1]';
const API = 'ws://' + HOST + ':' + API_PORT + '/';
const WEB = 'http://localhost:' + WEB_PORT + '/';
const SANDBOX = opt('sandbox', process.env.QLC_SANDBOX || '');
const SHOTS = opt('shots', process.env.QLC_SHOTS || path.join(os.tmpdir(), 'qlc-e2e-partials'));
const ONLY = (opt('only', '') || '').split(',').filter(Boolean);
const run = (name) => !ONLY.length || ONLY.includes(name);
if (!SANDBOX || !/qlcsandbox/i.test(SANDBOX)) {
  console.error('--sandbox <dir> is required and must be a dev-webui-sandbox.ps1 folder (…\\qlcsandbox\\<name>)');
  process.exit(2);
}

/* Universe ids used by the loopback round trips (the test project has six; a seventh is added through
   the UI, which also proves io.universe.create starts the new universe). Passthrough merges HTP, so the
   reading universes are read on a channel no fixture of theirs uses. */
const U_OUT = 3;      // "Universe 4": output 1 -> Loopback 1, output 2 -> Loopback 3
const U_IN = 4;       // "Universe 5": input <- Loopback 1 (passthrough), feedback -> Loopback 2, the profile
const U_FB = 5;       // "Universe 6": input <- Loopback 2 (passthrough): shows what feedback sent
const CH = 511;       // 0-based channel of the round trips (512 on screen); universes 5 / 6 patch fewer channels
const PROFILE = 'Akai APC Mini mk2'; // bundled MIDI profile with a colour table and a MIDI channel table

/* ---------------------------------------------------------------- raw API client */
class Api {
  constructor(url, name) { this.url = url; this.name = name || 'partials e2e'; this.rev = 0; this.next = 1; this.pending = new Map(); this.events = []; }
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
  dmx(universeId) { return this.call('io.dmx.universe.get', { universeId }).then(r => r.values || []); }
  /** Simple Desk write of one channel (absolute address). */
  sd(universeId, ch, value) { return this.call('io.simpleDesk.setChannels', { channels: [{ address: universeId * 512 + ch, value }] }); }
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
/** Like until() but reports instead of throwing. */
async function waitCheck(fn, msg, timeout = 8000, extraFn) {
  let ok = false;
  try { ok = !!(await until(fn, msg, timeout)); } catch (e) { ok = false; }
  let extra; if (!ok && extraFn) { try { extra = await extraFn(); } catch (e) { extra = String(e); } }
  return check(ok, msg, extra);
}

/* ---------------------------------------------------------------- page helpers */
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const byText = (tag, text, root) => `[...(${root || 'document'}).querySelectorAll(${JSON.stringify(tag)})].reverse().find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
/** Innermost element (children only icons) with exactly this text under scope; the last one = the open popup's. */
const leaf = (scope, text, first) => `(function(){ const a = [...document.querySelectorAll(${JSON.stringify(scope + ' *')})].filter(e => [...e.children].every(c => c.tagName === 'IMG') && e.textContent.trim() === ${JSON.stringify(text)}); return ${first ? 'a[0]' : 'a[a.length - 1]'}; })()`;
const leafStarts = (scope, text) => `(function(){ const a = [...document.querySelectorAll(${JSON.stringify(scope + ' *')})].filter(e => [...e.children].every(c => c.tagName === 'IMG') && e.textContent.trim().startsWith(${JSON.stringify(text)})); return a[a.length - 1]; })()`;
async function clickFn(page, fnBody, what, opts) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest', inline: 'nearest' }); })()`);
  await sleep(60);
  await page.click(new Function('return ' + fnBody), opts || {});
}
async function selectAll(page) {
  const k = { key: 'a', code: 'KeyA', windowsVirtualKeyCode: 65, modifiers: 2 };
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'rawKeyDown' }, k));
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, k));
}
async function typeInto(page, fnBody, text, what) {
  await clickFn(page, fnBody, what);
  await page.eval(`(function(){ const el = document.activeElement; if (el && el.select) el.select(); })()`);
  await selectAll(page);
  await page.s.send('Input.insertText', { text: String(text) });
  await page.key('Enter');
}
/** Open the design-system CustomComboBox whose root element `rootFn` returns (its first <button> opens
    it) and pick the option whose text is `wantedText` (or starts with it) from that combo's own popup. */
async function pickCombo(page, rootFn, wantedText, what, startsWith) {
  const root = `(${rootFn})`;
  await clickFn(page, `${root} && ${root}.querySelector('button')`, what + ' (open)');
  await sleep(200);
  const match = startsWith ? `b.textContent.trim().startsWith(${JSON.stringify(wantedText)})` : `b.textContent.trim() === ${JSON.stringify(wantedText)}`;
  const opt = `(function(){ const r = ${root}; if (!r) return null; const bs = [...r.querySelectorAll('div > button')].filter(b => b !== r.firstElementChild); return bs.find(b => ${match}); })()`;
  await clickFn(page, opt, what + ' -> ' + wantedText);
  await sleep(250);
}
/** A real key press through CDP: $key is KeyboardEvent.key, $mods 'Ctrl'/'Shift'/'Alt'. */
async function press(page, key, code, vk, mods = [], phase = 'both') {
  const bits = { Alt: 1, Ctrl: 2, Meta: 4, Shift: 8 };
  const modifiers = mods.reduce((m, x) => m | bits[x], 0);
  const base = { key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers };
  const raw = mods.some(m => m !== 'Shift');
  if (phase !== 'up') await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: raw ? 'rawKeyDown' : 'keyDown' }, base, raw ? {} : { text: key }));
  if (phase !== 'down') await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, base));
}
async function shot(page, name) { const f = path.join(SHOTS, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); return f; }
function sectionError(name, e, page) {
  failures.push(name + ' EXCEPTION ' + (e && e.stack || e));
  console.log('  EXCEPTION ' + (e && e.stack || e));
  if (page && page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
}

/* ================================================================ Input / Output */

async function ioSection(browser, api) {
  console.log('\n[Input / Output with the Loopback plugin]');
  const plugins = (await api.call('io.plugin.list')).plugins || [];
  const loop = plugins.find(p => p.name === 'Loopback');
  if (!check(!!loop && loop.outputLines.length >= 3 && loop.inputLines.length >= 3, 'the sandbox has the Loopback plugin (and only it)', plugins.map(p => p.name))) return {};
  check(plugins.length === 1, 'no other IO plugin is loaded', plugins.map(p => p.name));
  /* a known start: the project's six universes, nothing patched on the ones this section uses */
  let list = (await api.call('io.universe.list')).universes;
  while (list.length > 6) { await api.structural('io.universe.delete', { universeId: list[list.length - 1].id }); list = (await api.call('io.universe.list')).universes; }
  for (const u of [U_OUT, U_IN, U_FB]) {
    const d = await api.call('io.universe.get', { universeId: u });
    for (let i = (d.outputPatches || []).length - 1; i >= 0; i--) await api.structural('io.patch.remove', { universeId: u, direction: 'output', index: i });
    if (d.feedbackPatch) await api.structural('io.patch.remove', { universeId: u, direction: 'feedback' });
    if (d.inputPatch) await api.structural('io.patch.remove', { universeId: u, direction: 'input' });
    if (d.passthrough) await api.structural('io.universe.update', { universeId: u, passthrough: false });
  }

  const page = await browser.open(WEB + '?ctx=io', { width: 1600, height: 1100 });
  const uni = (id, sel) => `document.querySelector('[data-universe="${id}"] ${sel}')`;
  const get = (id) => api.call('io.universe.get', { universeId: id });
  try {
    await page.waitFor(`!!document.querySelector('[data-universe="${U_FB}"] [data-role="input-picker"]')`, 30000);
    await sleep(500);

    /* ---- a seventh universe from the toolbar: it reads the second output below, which only works
       when io.universe.create started its thread (it used to wait for a project reload) ---- */
    await clickFn(page, q('[data-role="add-universe"]'), 'add universe');
    await page.waitFor(`document.body.textContent.includes('Add universe')`);
    await page.s.send('Input.insertText', { text: 'Loopback check' });
    await clickFn(page, byText('button', 'Add'), 'Add');
    const created = await until(async () => (await api.call('io.universe.list')).universes.find(u => u.name === 'Loopback check'), 'the new universe');
    const U_OUT2 = created.id;
    check(U_OUT2 === 6, 'Add universe -> "Loopback check" is universe 7 (io.universe.list)', created);
    await page.waitFor(`!!document.querySelector('[data-universe="${U_OUT2}"] [data-role="input-picker"]')`, 8000);

    /* ---- output patch + a second output on the same universe ---- */
    await pickCombo(page, uni(U_OUT, '[data-output="add"] [data-role="output-add-picker"]'), 'Loopback — Loopback 1', 'output picker of universe 4', true);
    await waitCheck(async () => { const d = await get(U_OUT); return d.outputPatches.length === 1 && d.outputPatches[0].pluginName === 'Loopback' && d.outputPatches[0].output === 0; },
      'output picker -> universe 4 output 1 is Loopback line 1 (io.universe.get)');
    await pickCombo(page, uni(U_OUT, '[data-role="output-add-picker"]'), 'Loopback — Loopback 3', '"+ output" picker of universe 4', true);
    await waitCheck(async () => { const d = await get(U_OUT); return d.outputPatches.length === 2 && d.outputPatches[1].output === 2; },
      '"+ output" -> universe 4 has a second output patch on Loopback line 3', 8000, () => get(U_OUT));

    /* ---- input patches (+ passthrough) on the reading universes ---- */
    for (const [u, lineName] of [[U_IN, 'Loopback 1'], [U_OUT2, 'Loopback 3']]) {
      await pickCombo(page, uni(u, '[data-role="input-picker"]'), 'Loopback — ' + lineName, 'input picker of universe ' + (u + 1), true);
      await waitCheck(async () => { const d = await get(u); return d.inputPatch && d.inputPatch.pluginName === 'Loopback' && 'Loopback ' + (d.inputPatch.input + 1) === lineName; },
        'input picker -> universe ' + (u + 1) + ' input is ' + lineName);
      await clickFn(page, uni(u, '[data-role="passthrough"]'), 'passthrough of universe ' + (u + 1));
      await waitCheck(async () => (await get(u)).passthrough === true, 'Passthrough checkbox -> universe ' + (u + 1) + ' passthrough on');
    }

    /* ---- a value on universe 4 goes out of BOTH outputs and comes back on universes 5 and 7 ---- */
    await api.sd(U_OUT, CH, 77);
    await waitCheck(async () => (await api.dmx(U_IN))[CH] === 77, 'Simple Desk 77 on universe 4 arrives on universe 5 (output 1 -> Loopback 1 -> input -> passthrough)', 8000, async () => (await api.dmx(U_IN))[CH]);
    await waitCheck(async () => (await api.dmx(U_OUT2))[CH] === 77, '...and on the new universe 7 through the second output (Loopback 3): several outputs per universe work, and the created universe runs');

    /* ---- per-output pause / blackout ---- */
    const outRow = (index, sel) => `document.querySelector('[data-universe="${U_OUT}"] [data-output="${index}"] ${sel}')`;
    await clickFn(page, outRow(0, '[data-role="output-pause"]'), 'pause output 1');
    await waitCheck(async () => (await get(U_OUT)).outputPatches[0].paused === true, 'pause button -> output 1 paused (io.universe.get)');
    /* the held frame is the first one output after the pause (OutputPatch::dump): let a few universe
       ticks (20 ms) pass before changing the value, or the new value can be the frozen one */
    await sleep(200);
    await api.sd(U_OUT, CH, 90);
    await waitCheck(async () => (await api.dmx(U_OUT2))[CH] === 90, 'while output 1 is paused, output 2 still sends the new value 90 (universe 7)');
    await sleep(600);
    check((await api.dmx(U_IN))[CH] === 77, 'the paused output 1 held its last frame (universe 5 still 77)', (await api.dmx(U_IN))[CH]);
    await clickFn(page, outRow(0, '[data-role="output-pause"]'), 'resume output 1');
    await waitCheck(async () => (await api.dmx(U_IN))[CH] === 90, 'resume -> output 1 sends 90 again');
    /* Blackout zeroes intensity channels only (Universe::m_blackoutValues keeps every LTP channel, like
       the global blackout), so it is read on a fixture's dimmer channel of universe 4. */
    let dim = null;
    for (const f of (await api.call('fixtures.list')).fixtures.filter(x => x.universe === U_OUT)) {
      const d = await api.call('fixtures.get', { fixtureId: String(f.id) });
      const c = (d.channelList || []).find(x => x.group === 'Intensity' && !x.colour);
      if (c) { dim = f.address + c.index; break; }
    }
    if (check(dim != null, 'universe 4 has a fixture dimmer channel for the blackout check')) {
      await api.sd(U_OUT, dim, 200);
      await waitCheck(async () => (await api.dmx(U_OUT2))[dim] === 200, 'dimmer channel ' + (dim + 1) + ' at 200 reaches universe 7');
      await clickFn(page, outRow(1, '[data-role="output-blackout"]'), 'blackout output 2');
      await waitCheck(async () => (await get(U_OUT)).outputPatches[1].blackout === true, 'blackout button -> output 2 blackout (io.universe.get)');
      await waitCheck(async () => (await api.dmx(U_OUT2))[dim] === 0, 'output 2 blacked out: universe 7 receives 0 on the dimmer', 8000, async () => (await api.dmx(U_OUT2))[dim]);
      check((await api.dmx(U_OUT2))[CH] === 90, 'the blacked-out output keeps its non-intensity channel (90), as blackout does', (await api.dmx(U_OUT2))[CH]);
      check((await api.dmx(U_IN))[dim] >= 200, 'output 1 is not affected by output 2\'s blackout (universe 5 still gets the dimmer value)', (await api.dmx(U_IN))[dim]);
      await clickFn(page, outRow(1, '[data-role="output-blackout"]'), 'restore output 2');
      await waitCheck(async () => (await api.dmx(U_OUT2))[dim] === 200, 'blackout off -> universe 7 receives the dimmer value again');
      await api.call('io.simpleDesk.resetUniverse', { universeId: U_OUT });
      await api.sd(U_OUT, CH, 90);
    }

    /* ---- feedback patch (verified end to end in the VC section) and the reading universe for it ---- */
    await pickCombo(page, uni(U_IN, '[data-role="feedback-picker"]'), 'Loopback — Loopback 2', 'feedback picker of universe 5', true);
    await waitCheck(async () => { const d = await get(U_IN); return d.feedbackPatch && d.feedbackPatch.output === 1; }, 'feedback picker -> universe 5 feedback on Loopback line 2');
    await pickCombo(page, uni(U_FB, '[data-role="input-picker"]'), 'Loopback — Loopback 2', 'input picker of universe 6', true);
    await clickFn(page, uni(U_FB, '[data-role="passthrough"]'), 'passthrough of universe 6');
    await waitCheck(async () => { const d = await get(U_FB); return d.inputPatch && d.inputPatch.input === 1 && d.passthrough; }, 'universe 6 reads Loopback line 2 with passthrough');

    /* ---- input profile assignment ---- */
    await pickCombo(page, uni(U_IN, '[data-role="profile-picker"]'), PROFILE, 'profile picker of universe 5');
    await waitCheck(async () => (await get(U_IN)).inputPatch.profileName === PROFILE, 'profile picker -> universe 5 input profile "' + PROFILE + '"');
    await shot(page, 'partials-io-patched');

    /* ---- input profile learn: the Loopback output drives the input ---- */
    await clickFn(page, q('[data-role="profile-new"]'), 'New profile');
    await page.waitFor(`!!document.querySelector('[data-role="profile-editor"]')`);
    await pickCombo(page, q('[data-role="detect-universe"]'), 'Universe 5 (Loopback)', 'detect universe');
    await clickFn(page, q('[data-role="detect-toggle"]'), 'Detect channels');
    await page.waitFor(`document.querySelector('[data-role="detect-toggle"]').textContent.includes('Stop detection')`, 5000);
    for (const v of [10, 60, 120]) { await api.sd(U_OUT, 41, v); await sleep(250); }
    await waitCheck(() => page.eval(`!!document.querySelector('[data-role="profile-editor"] [data-channel="41"]')`), 'learn: moving channel 42 on universe 4 adds channel 42 to the profile being edited (io.inputProfile.learn.signal)', 8000,
      () => page.eval(`[...document.querySelectorAll('[data-role="profile-editor"] [data-channel]')].map(e => e.getAttribute('data-channel'))`));
    await waitCheck(() => page.eval(`/Slider/.test((document.querySelector('[data-role="profile-editor"] [data-channel="41"]') || {}).textContent || '')`), 'learn: a sweeping channel is promoted to a Slider');
    await shot(page, 'partials-io-learn');
    await clickFn(page, q('[data-role="detect-toggle"]'), 'Stop detection');
    await sleep(300);
    await clickFn(page, leaf('[data-role="profile-editor"]', 'Discard'), 'Discard').catch(async () => { await page.key('Escape'); });
    await sleep(400);
    check(!(await page.eval(`!!document.querySelector('[data-role="profile-editor"]')`)), 'profile editor closed without saving');

    /* ---- plugin rescan / lines ---- */
    await clickFn(page, q('[data-plugin="Loopback"] [data-role="plugin-rescan"]'), 'Loopback rescan');
    await waitCheck(() => page.eval(`!!document.querySelector('[data-plugin="Loopback"] [data-role="plugin-note"]')`), 'Rescan answers in the plugin panel: ' + 'note shown');
    const note = await page.eval(`(document.querySelector('[data-plugin="Loopback"] [data-role="plugin-note"]') || {}).textContent`);
    check(/rescanned|no rescan hook/.test(note || ''), 'Loopback rescan result is reported (' + note + ')');
    const lines = await api.call('io.plugin.getLines', { pluginName: 'Loopback' });
    check(lines.inputs.length === loop.inputLines.length && lines.outputs.length === loop.outputLines.length, 'io.plugin.getLines lists the Loopback lines (' + lines.inputs.length + ' in / ' + lines.outputs.length + ' out)');

    /* ---- audio input format / output buffer (sandbox Settings, not the registry) ---- */
    const audio0 = await api.call('io.audio.listDevices');
    check(audio0.inputSampleRate === 44100 && audio0.inputChannels === 1 && audio0.outputBufferMs === 100, 'io.audio.listDevices reports the defaults 44100 Hz / mono / 100 ms', audio0.inputSampleRate + '/' + audio0.inputChannels + '/' + audio0.outputBufferMs);
    await pickCombo(page, q('[data-role="audio-samplerate"]'), '48000 Hz', 'sample rate');
    await waitCheck(async () => (await api.call('io.audio.listDevices')).inputSampleRate === 48000, 'Sample rate combo -> 48000 Hz (io.audio.listDevices)');
    await pickCombo(page, q('[data-role="audio-channels"]'), 'Stereo', 'channels');
    await waitCheck(async () => (await api.call('io.audio.listDevices')).inputChannels === 2, 'Channels combo -> stereo');
    await typeInto(page, q('[data-role="audio-buffer"] input') + ' || ' + q('input[data-role="audio-buffer"]'), '250', 'buffer size');
    await waitCheck(async () => (await api.call('io.audio.listDevices')).outputBufferMs === 250, 'Buffer size spin box -> 250 ms', 8000, async () => (await api.call('io.audio.listDevices')).outputBufferMs);
    const ini = path.join(SANDBOX, 'Settings', 'qlcplus', 'Q Light Controller Plus.ini');
    const iniText = fs.existsSync(ini) ? fs.readFileSync(ini, 'utf8') : '';
    check(/samplerate=48000/.test(iniText) && /channels=2/.test(iniText) && /outputBufferMs=250/.test(iniText), 'written to the sandbox\'s own settings file (QLCPLUS_SETTINGS_DIR), not the registry', iniText.slice(0, 200));
    /* device selection: also a settings write, safe now that it lands in the sandbox's own file */
    const devIn = audio0.inputs.find(d => d.privateName !== '__qlcplusdefault__');
    if (devIn) {
      await pickCombo(page, q('[data-role="audio-input"]'), devIn.name, 'audio input device');
      await waitCheck(async () => (await api.call('io.audio.listDevices')).inputDevice === devIn.privateName, 'Input device combo -> "' + devIn.name + '" (io.audio.listDevices)');
      check(/input=/.test(fs.readFileSync(ini, 'utf8')), 'the device choice is in the sandbox settings file too');
    }
    if (devIn) await api.call('io.audio.setDevice', { direction: 'input', privateName: '__qlcplusdefault__' });
    /* input level check: the host's capture level, only while this client previews */
    await clickFn(page, q('[data-role="audio-level-toggle"]'), 'start the level check');
    const levels = [];
    const t0 = Date.now();
    while (Date.now() - t0 < 3000) { levels.push(Number(await page.eval(`(document.querySelector('[data-role="audio-level"]') || {}).getAttribute ? document.querySelector('[data-role="audio-level"]').getAttribute('data-level') : -1`))); await sleep(150); }
    const other = new Api(API, 'partials bystander');
    await other.connect();
    await sleep(800);
    check(!other.events.some(e => e.topic === 'io.audio.inputLevel'), 'io.audio.inputLevel is not sent to a client that does not preview');
    const pv = await other.call('io.audio.inputPreview.set', { enabled: true });
    await sleep(1500);
    const got = other.events.filter(e => e.topic === 'io.audio.inputLevel');
    check(pv.capturing === true && got.length >= 5, 'a previewing client receives io.audio.inputLevel from the host capture (' + got.length + ' in 1.5 s, capturing ' + pv.capturing + ')');
    other.close();
    const maxLevel = Math.max.apply(null, levels);
    console.log('  info level samples over 3 s: max ' + maxLevel + ', ' + levels.filter(l => l > 0).length + '/' + levels.length + ' non-zero');
    check(levels.every(l => l >= 0 && l <= 32767), 'level check running: the meter shows the host\'s input level (0..32767)');
    await shot(page, 'partials-io-audio');
    await clickFn(page, q('[data-role="audio-level-toggle"]'), 'stop the level check');
    await waitCheck(() => page.eval(`document.querySelector('[data-role="audio-level"]').getAttribute('data-level') === '0'`), 'stopping the check resets the meter');
    await api.call('io.audio.setConfig', { inputSampleRate: 44100, inputChannels: 1, outputBufferMs: 100 });
    await waitCheck(() => page.eval(`/44100 Hz/.test((document.querySelector('[data-role="audio-samplerate"]') || {}).textContent || '')`), 'another client\'s io.audio.setConfig shows up (io.audio.configChanged)');
    check(page.consoleErrors.length === 0, 'I/O: no console errors', page.consoleErrors);
    return { patched: true };
  } catch (e) {
    sectionError('IO', e, page);
    try { await shot(page, 'partials-io-failure'); } catch (x) { }
    return {};
  }
}

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev + ', sandbox ' + SANDBOX);
  const browser = await launch({ headless: true });
  const ctx = {};
  try {
    if (run('io') || run('vc')) ctx.io = await ioSection(browser, api);
  } catch (e) {
    failures.push('EXCEPTION ' + (e && e.stack || e));
    console.log('  EXCEPTION ' + (e && e.stack || e));
  } finally {
    await browser.close();
    api.close();
  }
  console.log('\n' + checks + ' checks, ' + failures.length + ' failed' + (failures.length ? ':\n  ' + failures.join('\n  ') : ''));
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
