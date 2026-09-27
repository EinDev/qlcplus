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

    /* ---- feedback patch: Loopback hands feedback back to the input line of the same number, and only
       for the universe that sent it, so like a real controller (one device, in and out) universe 5
       gets its feedback on Loopback 1 - it arrives as input there and passthrough shows it (checked
       end to end in the VC section) ---- */
    await pickCombo(page, uni(U_IN, '[data-role="feedback-picker"]'), 'Loopback — Loopback 1', 'feedback picker of universe 5', true);
    await waitCheck(async () => { const d = await get(U_IN); return d.feedbackPatch && d.feedbackPatch.output === 0; }, 'feedback picker -> universe 5 feedback on Loopback line 1');

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

/* ================================================================ Virtual Console */

const EDIT_TOGGLE = `document.querySelector('button img[src$="/edit.svg"]').closest('button')`;
/** Select one widget in edit mode (near its top-left corner: a fader / pad would take a centre click). */
async function selectWidget(page, id, add) {
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${id}"]')`);
  await page.eval(`document.querySelector('[data-vc-widget="${id}"]').scrollIntoView({ block: 'nearest', inline: 'nearest' })`);
  const r = await page.rectOf(`[data-vc-widget="${id}"]`);
  const opts = add ? { modifiers: 2 } : {};
  await page.mouse('mouseMoved', r.x + 5, r.y + 5);
  await page.mouse('mousePressed', r.x + 5, r.y + 5, opts); await page.mouse('mouseReleased', r.x + 5, r.y + 5, opts);
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${id}"][data-vc-selected]')`, 5000);
  await sleep(250);
}
async function deselectAll(page) {
  await page.eval(`(function(){ const c = document.querySelector('div[style*="dashed"]'); c && c.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true })); })()`);
  await page.waitFor(`document.querySelectorAll('[data-vc-selected]').length === 0`, 4000).catch(() => {});
}
async function setEdit(page, on) {
  const isOn = await page.eval(`document.body.textContent.includes('Pick a widget') || !!document.querySelector('[data-vc-selected]') || !!document.querySelector('[data-vc-page-props]')`);
  if (!!isOn !== !!on) { await clickFn(page, EDIT_TOGGLE, 'edit toggle'); await sleep(500); }
}
async function openProps(page) {
  if (await page.eval(`!!document.querySelector('button[title="Widget properties"]')`)) {
    const shown = await page.eval(`document.body.textContent.includes('Basic properties') || !!document.querySelector('[data-vc-page-props]')`);
    if (!shown) { await clickFn(page, q('button[title="Widget properties"]'), 'properties rail button'); await sleep(400); }
  }
}
/** Set a native <input type=color> (headless Chrome cannot open the picker) the way React sees it. */
async function setColorInput(page, fnBody, hex) {
  await page.eval(`(function(){ const i = (${fnBody}); const setter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set; setter.call(i, ${JSON.stringify(hex)}); i.dispatchEvent(new Event('input', { bubbles: true })); i.dispatchEvent(new Event('change', { bubbles: true })); })()`);
}
const KEY = { F7: ['F7', 'F7', 118], F8: ['F8', 'F8', 119], F9: ['F9', 'F9', 120], F10: ['F10', 'F10', 121], J: ['j', 'KeyJ', 74], K: ['k', 'KeyK', 75], L: ['l', 'KeyL', 76], M: ['m', 'KeyM', 77], N: ['n', 'KeyN', 78], U: ['u', 'KeyU', 85], Y: ['y', 'KeyY', 89] };
const pressKey = (page, k, phase) => press(page, KEY[k][0], KEY[k][1], KEY[k][2], [], phase);

async function vcSection(browser, api, io) {
  console.log('\n[Virtual Console]');
  if ((await api.call('core.mode.get')).mode !== 'design') await api.call('core.mode.set', { mode: 'design' });
  const fns = (await api.call('functions.list')).functions.filter(f => !f.hidden);
  const scene = fns.find(f => f.type === 'Scene');
  const matrix = fns.find(f => f.type === 'RGBMatrix');
  /* a chaser of our own with three steps, so next / previous are observable */
  const ch = await api.structural('functions.create', { type: 'Chaser', name: 'E2E partials chaser' });
  const chaserId = String(ch.functionId);
  for (const s of fns.filter(f => f.type === 'Scene').slice(0, 3))
    await api.structural('functions.steps.addStep', { functionId: chaserId, step: { targetFunctionId: String(s.id), fadeIn: 0, hold: 60000, fadeOut: 0, duration: 60000 } });
  const chDetail = (await api.call('functions.get', { functionId: chaserId })).typeDetail || {};
  const chSteps = (chDetail.config && chDetail.config.steps) || chDetail.steps || [];
  check(!!scene && !!matrix && chSteps.length === 3, 'the project has a Scene and an RGB Matrix; a 3-step test chaser was created', { scene: !!scene, matrix: !!matrix, steps: chSteps.length });

  /* fixtures: a dimmer on universe 4 (the monitored slider), a moving head (XY pad), a channel with
     capabilities (Click & Go preset) and an RGB set (Click & Go colours) */
  const fixtures = (await api.call('fixtures.list')).fixtures;
  let dimmer = null, mover = null, goboCh = null, rgb = null;
  for (const f of fixtures) {
    if (dimmer && mover && goboCh && rgb) break;
    const d = await api.call('fixtures.get', { fixtureId: String(f.id) });
    const cl = d.channelList || [];
    const dim = cl.find(c => c.group === 'Intensity' && !c.colour);
    if (!dimmer && dim && f.universe === U_OUT) dimmer = { f, ch: dim.index };
    if (!mover && cl.some(c => c.group === 'Pan') && cl.some(c => c.group === 'Tilt')) mover = f;
    const gobo = cl.find(c => c.group === 'Gobo');
    if (!goboCh && gobo) goboCh = { f, ch: gobo.index };
    const r = cl.find(c => c.colour === 'Red'), g = cl.find(c => c.colour === 'Green'), b = cl.find(c => c.colour === 'Blue');
    if (!rgb && r && g && b) rgb = { f, chs: [r.index, g.index, b.index] };
  }
  check(!!dimmer && !!mover && !!goboCh && !!rgb, 'test fixtures found (dimmer on universe 4, moving head, gobo channel, RGB)', { dimmer: !!dimmer, mover: !!mover, gobo: !!goboCh, rgb: !!rgb });

  const pagesBefore = (await api.call('vc.page.list')).pages;
  await api.structural('vc.page.create', { index: pagesBefore.length });
  const scratch = pagesBefore.length;
  const mk = async (widgetType, geometry, caption, typeConfig) => String((await api.structural('vc.widget.create', Object.assign({ widgetType, page: scratch, geometry, style: { caption } }, typeConfig ? { typeConfig } : {}))).widgetId);
  const W = {};
  W.btn = await mk('Button', { x: 10, y: 10, width: 90, height: 70 }, 'FB flash', { functionID: String(scene.id), actionType: 'Flash' });
  W.cue = await mk('CueList', { x: 110, y: 10, width: 330, height: 200 }, 'Keys cue', { chaserID: chaserId });
  W.speed = await mk('Speed', { x: 450, y: 10, width: 200, height: 175 }, 'Keys speed');
  W.flash = await mk('Slider', { x: 660, y: 10, width: 70, height: 200 }, 'Flash', { sliderMode: 'Adjust', controlledFunction: String(scene.id), adjustFlashEnabled: true });
  W.anim = await mk('Animation', { x: 740, y: 10, width: 260, height: 200 }, 'Keys anim', { functionID: String(matrix.id) });
  const knobs = await api.structural('vc.widget.preset.add', { widgetId: W.anim, preset: { presetType: 'colorKnobs', colorIndex: 0 } }).catch(e => ({ error: e.message }));
  const colorPreset = await api.structural('vc.widget.preset.add', { widgetId: W.anim, preset: { presetType: 'color', colorIndex: 1, color: '#ff00ff' } }).catch(e => ({ error: e.message }));
  console.log('  info animation presets: ' + JSON.stringify([knobs, colorPreset]).slice(0, 200));
  W.mon = await mk('Slider', { x: 10, y: 220, width: 80, height: 220 }, 'Monitor', { sliderMode: 'Level', monitorEnabled: true, levelChannels: [{ fixtureId: String(dimmer.f.id), channel: dimmer.ch }] });
  W.cngP = await mk('Slider', { x: 100, y: 220, width: 80, height: 220 }, 'Gobo CnG', { sliderMode: 'Level', clickAndGoType: 'Preset', levelChannels: [{ fixtureId: String(goboCh.f.id), channel: goboCh.ch }] });
  W.cngC = await mk('Slider', { x: 190, y: 220, width: 80, height: 220 }, 'RGB CnG', { sliderMode: 'Level', clickAndGoType: 'Colors', levelChannels: rgb.chs.map(c => ({ fixtureId: String(rgb.f.id), channel: c })) });
  W.props = await mk('Slider', { x: 280, y: 220, width: 80, height: 220 }, 'Props');
  W.frame = await mk('Frame', { x: 370, y: 220, width: 260, height: 200 }, 'Collapse me', { showHeader: true });
  W.pad = await mk('XYPad', { x: 640, y: 220, width: 260, height: 260 }, 'Pad', { invertedAppearance: true });
  W.at = await mk('AudioTriggers', { x: 910, y: 220, width: 200, height: 200 }, 'Audio');
  W.a1 = await mk('Button', { x: 20, y: 500, width: 60, height: 40 }, 'A1');
  W.a2 = await mk('Button', { x: 120, y: 540, width: 80, height: 60 }, 'A2');
  W.a3 = await mk('Button', { x: 260, y: 620, width: 50, height: 50 }, 'A3');
  console.log('  scratch page ' + (scratch + 1) + ': ' + JSON.stringify(W));

  const page = await browser.open(WEB + '?ctx=vc', { width: 1700, height: 1100 });
  const ctx = { page, W, scratch, scene, matrix, chaserId, dimmer, mover, goboCh, rgb };
  try {
    await page.waitFor(`document.querySelectorAll('[title^="Page "]').length >= ${scratch + 1}`, 30000);
    await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${scratch + 1}'))`, 'scratch page tab');
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${W.a3}"]')`);
    await sleep(500);
    await vcExternal(api, ctx, io);
    await vcKeys(api, ctx);
    check(page.consoleErrors.length === 0, 'VC: no console errors', page.consoleErrors);
  } catch (e) {
    sectionError('VC', e, page);
    try { await shot(page, 'partials-vc-failure'); } catch (x) { }
  }
  return ctx;
}

/* ---- external controls: auto-detect from a real input signal, custom feedback, feedback on the line */
async function vcExternal(api, ctx, io) {
  const { page, W } = ctx;
  console.log('\n[VC external controls over Loopback]');
  if (!io || !io.patched) { check(false, 'the I/O section patched the Loopback lines (run it first)'); return; }
  const FB = 40; // 0-based input channel of the button's source on universe 5
  /* only one universe may see the signal: drop universe 4's second output (it feeds universe 7) */
  const outs = (await api.call('io.universe.get', { universeId: U_OUT })).outputPatches || [];
  for (let i = outs.length - 1; i >= 1; i--) await api.structural('io.patch.remove', { universeId: U_OUT, direction: 'output', index: i });
  await api.sd(U_OUT, FB, 0);
  await setEdit(page, true);
  await openProps(page);
  await selectWidget(page, W.btn);
  await page.waitFor(`!!document.querySelector('[data-vcx-panel="${W.btn}"]')`, 8000);
  await clickFn(page, q(`[data-vcx-panel="${W.btn}"] [data-vcx-add-detect]`), 'auto-detect button');
  await page.waitFor(`!!document.querySelector('[data-vcx-pending-detect]')`);
  /* move "a control on the controller": universe 4's output on Loopback 1 is universe 5's input */
  await api.sd(U_OUT, FB, 255);
  await sleep(150);
  await api.sd(U_OUT, FB, 0);
  const src = await until(async () => (await api.widget(W.btn)).inputSources.find(s => Number(s.universe) === U_IN && (Number(s.channel) & 0xffff) === FB), 'auto-detected source', 8000).catch(() => null);
  check(!!src, 'auto-detect: a real input signal on universe 5 channel ' + (FB + 1) + ' bound the button (vc.widget.get inputSources)', (await api.widget(W.btn)).inputSources);
  await waitCheck(() => page.eval(`!document.querySelector('[data-vcx-pending-detect]')`), 'the pending detection row closes when the source lands');
  if (!src) return;
  const srcSel = `[data-vcx-panel="${W.btn}"] [data-vcx-source="${src.controlId}:${src.universe}:${src.channel}"]`;
  await page.waitFor(`!!document.querySelector('${srcSel}')`, 5000);
  check(/APC|Channel|Universe/.test(await page.eval(`document.querySelector('${srcSel}').textContent`)), 'the new source row names universe / channel');

  /* custom feedback: the profile (Akai APC Mini mk2) brings a colour table and MIDI channel routing */
  await clickFn(page, q(`${srcSel} [data-vcx-source-feedback]`), 'custom feedback button');
  await page.waitFor(`!!document.querySelector('[data-vcx-feedback]')`);
  const hasColors = await until(() => page.eval(`document.querySelectorAll('[data-vcx-feedback] div[role="button"]').length >= 3`), 'colour swatches', 5000).catch(() => false);
  check(!!hasColors, 'custom feedback shows the profile colour table swatches (lower / upper / monitor)');
  const hasRouting = await page.eval(`document.querySelector('[data-vcx-feedback]').textContent.includes('MIDI Channel')`);
  check(hasRouting, 'custom feedback shows the MIDI channel routing of the profile');
  /* upper value from the colour table: click the upper swatch, then a table row */
  await clickFn(page, `[...document.querySelectorAll('[data-vcx-feedback] div[role="button"]')][1]`, 'upper swatch');
  const rowPick = await page.eval(`(function(){ const rows = [...document.querySelectorAll('[data-vcx-feedback] div[role="button"]')].slice(3); const r = rows.find(x => /\\b(21|22|5|9)\\b/.test(x.textContent)) || rows[5] || rows[1]; if (!r) return null; r.setAttribute('data-e2e-pick', '1'); return r.textContent; })()`);
  await clickFn(page, q('[data-e2e-pick="1"]'), 'colour table row');
  const upperPicked = Number(await page.eval(`document.querySelector('input[data-vcx-fb="upperValue"]').value`));
  check(upperPicked > 0 && upperPicked < 255, 'picking a colour table row sets the upper value (' + rowPick + ' -> ' + upperPicked + ')');
  await typeInto(page, q('input[data-vcx-fb="lowerValue"]'), '3', 'lower');
  /* route the upper value to the second MIDI channel label */
  const midiTable = ((await api.call('io.inputProfile.get', { name: PROFILE })).profile || {}).midiChannelTable || [];
  const route = midiTable[1] || midiTable[0];
  const routeRow = `[...document.querySelectorAll('[data-vcx-feedback] div')].find(d => d.children.length >= 2 && d.children[0].textContent.trim() === 'Upper Channel')`;
  if (check(!!route, 'the profile has MIDI channel labels (' + midiTable.map(m => m.label).join(', ') + ')'))
    await pickCombo(page, `(${routeRow}) && (${routeRow}).lastElementChild`, route.label, 'upper MIDI channel');
  await shot(page, 'partials-vc-feedback');
  await clickFn(page, byText('button', 'Ok'), 'Ok');
  await page.waitFor(`!document.querySelector('[data-vcx-feedback]')`, 5000);
  const fbSrc = await until(async () => (await api.widget(W.btn)).inputSources.find(s => s.upperValue === upperPicked && s.lowerValue === 3), 'feedback saved', 8000).catch(() => null);
  check(!!fbSrc, 'custom feedback values saved (lower 3, upper ' + upperPicked + ')', (await api.widget(W.btn)).inputSources);
  if (fbSrc) check(fbSrc.upperChannel > 0, 'upper value routed to a MIDI channel of the profile (upperChannel ' + fbSrc.upperChannel + ')', fbSrc);

  /* the feedback really reaches the line: press the button in operate mode; universe 5 gets the
     feedback on its input (Loopback 1 back to itself) and passthrough shows it */
  await setEdit(page, false);
  const r = await page.rectOf(`[data-vc-widget="${W.btn}"]`);
  await page.mouse('mouseMoved', r.x + r.w / 2, r.y + r.h / 2);
  await page.mouse('mousePressed', r.x + r.w / 2, r.y + r.h / 2);
  await waitCheck(async () => (await api.dmx(U_IN))[FB] === upperPicked, 'pressing the flash button sends its upper feedback value ' + upperPicked + ' out of the feedback line (read back on universe 5)', 5000, async () => (await api.dmx(U_IN))[FB]);
  await page.mouse('mouseReleased', r.x + r.w / 2, r.y + r.h / 2);
  await waitCheck(async () => (await api.dmx(U_IN))[FB] === 3, 'releasing it sends the lower value 3', 5000, async () => (await api.dmx(U_IN))[FB]);
  /* feedback off: the same press sends nothing */
  await api.structural('io.patch.remove', { universeId: U_IN, direction: 'feedback' });
  await page.mouse('mousePressed', r.x + r.w / 2, r.y + r.h / 2);
  await sleep(700);
  check((await api.dmx(U_IN))[FB] === 3, 'with the feedback patch removed a press sends nothing to the line', (await api.dmx(U_IN))[FB]);
  await page.mouse('mouseReleased', r.x + r.w / 2, r.y + r.h / 2);
}

/* ---- Operate-mode key bindings of cue list, speed dial, slider flash and animation */
async function vcKeys(api, ctx) {
  const { page, W } = ctx;
  console.log('\n[VC key bindings in Operate mode]');
  const bind = async (widgetId, name, key) => {
    const d = await api.widget(widgetId);
    const c = (d.externalControls || []).find(x => x.name === name);
    if (!check(!!c && c.allowKeyboard, name + ' is a keyboard control of widget ' + widgetId, d.externalControls)) return null;
    await api.structural('vc.widget.keySequence.set', { widgetId: String(widgetId), controlId: Number(c.controlId), keySequence: key });
    return c.controlId;
  };
  await bind(W.cue, 'Play/Stop/Pause', 'F7');
  await bind(W.cue, 'Next Cue', 'F8');
  await bind(W.cue, 'Previous Cue', 'F9');
  await bind(W.cue, 'Stop/Pause', 'F10');
  await bind(W.speed, 'Tap Button', 'J');
  await bind(W.speed, 'Multiply Button', 'K');
  await bind(W.speed, 'Divide Button', 'L');
  await bind(W.speed, 'Reset Button', 'M');
  await bind(W.flash, 'Flash Control', 'N');
  await bind(W.anim, 'Intensity', 'U');
  const animPresets = (await api.widget(W.anim)).typeConfig.presets || [];
  const colorPreset = animPresets.find(p => p.presetType === 'color');
  const colorCtl = colorPreset ? ((await api.widget(W.anim)).externalControls || []).find(c => Number(c.controlId) === Number(colorPreset.presetId)) : null;
  check(!!colorCtl, 'the animation colour preset is external control ' + (colorPreset && colorPreset.presetId) + ' (its preset id - not 30 + id like the XY pad)', { presets: animPresets.map(p => p.presetId), controls: ((await api.widget(W.anim)).externalControls || []).map(c => c.controlId) });
  if (colorCtl) await api.structural('vc.widget.keySequence.set', { widgetId: W.anim, controlId: Number(colorCtl.controlId), keySequence: 'Y' });

  await setEdit(page, false);
  await page.goto(WEB + '?ctx=vc');
  await page.waitFor(`document.querySelectorAll('[title^="Page "]').length >= ${ctx.scratch + 1}`, 30000);
  await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${ctx.scratch + 1}'))`, 'scratch page tab');
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${W.cue}"]')`);
  await sleep(800);
  await page.eval(`document.activeElement && document.activeElement.blur && document.activeElement.blur()`);
  const cue = async () => { const d = await api.widget(W.cue); return d; };
  await pressKey(page, 'F7');
  await waitCheck(async () => (await cue()).running === true, 'F7 (Play/Stop/Pause) starts the cue list');
  const i0 = (await cue()).playbackIndex;
  await pressKey(page, 'F8');
  await waitCheck(async () => (await cue()).playbackIndex === (i0 + 1) % 3, 'F8 (Next Cue) advances to the next step', 5000, async () => (await cue()).playbackIndex);
  await pressKey(page, 'F9');
  await waitCheck(async () => (await cue()).playbackIndex === i0, 'F9 (Previous Cue) goes back');
  await pressKey(page, 'F10');
  await waitCheck(async () => { const d = await cue(); return d.running === false || d.paused === true; }, 'F10 (Stop/Pause) stops or pauses it');
  await api.call('vc.cueList.stop', { widgetId: W.cue }).catch(() => null);

  const sp = () => api.widget(W.speed);
  const ms0 = (await sp()).ms;
  await pressKey(page, 'J'); await sleep(400); await pressKey(page, 'J'); await sleep(400); await pressKey(page, 'J');
  await waitCheck(async () => { const d = await sp(); return d.ms !== ms0 && d.ms > 200 && d.ms < 900; }, 'J (Tap Button) x3 at ~400 ms sets the dial by tap tempo', 5000, async () => (await sp()).ms);
  await pressKey(page, 'K');
  await waitCheck(async () => (await sp()).factor === 'Two', 'K (Multiply Button) doubles the factor (One -> Two)', 5000, async () => (await sp()).factor);
  await pressKey(page, 'L'); await pressKey(page, 'L');
  await waitCheck(async () => (await sp()).factor === 'Half', 'L (Divide Button) twice -> Half', 5000, async () => (await sp()).factor);
  await pressKey(page, 'M');
  await waitCheck(async () => (await sp()).factor === 'One', 'M (Reset Button) -> One');

  /* slider flash (Adjust + flash button): the Scene runs while the key is held */
  const running = async () => ((await api.call('functions.get', { functionId: String(ctx.scene.id) })).running || (await api.call('functions.get', { functionId: String(ctx.scene.id) })).isRunning);
  await pressKey(page, 'N', 'down');
  const flashOn = await until(async () => (await api.widget(W.flash)).typeConfig && ((await api.call('functions.get', { functionId: String(ctx.scene.id) })).running === true), 'flash on', 3000).catch(() => false);
  check(!!flashOn, 'N held (Flash Control) flashes the slider\'s Scene (functions.get running)');
  await pressKey(page, 'N', 'up');
  await waitCheck(async () => (await api.call('functions.get', { functionId: String(ctx.scene.id) })).running === false, 'releasing N ends the flash');

  /* animation: Intensity follows the key, the colour preset key applies that preset */
  await pressKey(page, 'U', 'down');
  await waitCheck(async () => (await api.widget(W.anim)).faderLevel === 255, 'U held (Intensity) puts the animation fader at 255');
  await pressKey(page, 'U', 'up');
  await waitCheck(async () => (await api.widget(W.anim)).faderLevel === 0, 'releasing U puts it back to 0');
  if (colorPreset) {
    await pressKey(page, 'Y');
    await waitCheck(async () => Number((await api.widget(W.anim)).activePresetId) === Number(colorPreset.presetId), 'Y applies the bound colour preset (activePresetId ' + colorPreset.presetId + ')', 5000, async () => (await api.widget(W.anim)).activePresetId);
  }
  await shot(page, 'partials-vc-keys');
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
    /* the VC section's external-control checks need the Loopback patches the I/O section makes */
    if (run('io') || run('vc')) ctx.io = await ioSection(browser, api);
    if (run('vc')) ctx.vc = await vcSection(browser, api, ctx.io);
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
