// End-to-end check of the Input/Output screen extras and the server-side Simple Desk keypad against
// a sandbox instance (dev-webui-sandbox.ps1 -Name io -ApiPort 9190 -WebUiPort 9191, SF3 project,
// no IO plugins). Drives headless Chrome through webui/tools/cdp.js like an operator would and reads
// every change back over a second API connection (io.universe.get, io.grandMaster.get,
// io.inputProfile.list/get, io.dmx.universe.get, io.simpleDesk.get), the profile file on disk and a
// core.project.saveAs into the sandbox directory.
//
//   node webui/tools/e2e/io.js [--api 9190] [--web 9191] [--out <dir>] [--sandbox C:\qlcsandbox\io]
//
// Exit code 0 = every assertion held and the page logged no console errors. Screenshots land in --out
// (default: the OS temp dir). Only ever run against a sandbox: it writes an input profile into the
// sandbox's InputProfiles folder (QLCPLUS_USER_INPUTPROFILE_DIR), changes the Grand Master modes and
// writes <sandbox>\out.qxw. It never changes the host's audio device (that is a real QSettings write).
//
// Not covered here, by design of the sandbox: everything that needs a real IO plugin line (patch
// parameters, per-output pause/blackout, additional outputs, profile assignment, learn) - those paths
// are unit-tested against the plugin stub in controlapi/test/apiioconfigdomain.

const path = require('path'), fs = require('fs'), os = require('os');
const { launch } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', 9190)), WEB_PORT = Number(opt('web', 9191));
const OUT = opt('out', path.join(os.tmpdir(), 'qlc-e2e-io'));
const SANDBOX = opt('sandbox', 'C:\\qlcsandbox\\io');
fs.mkdirSync(OUT, { recursive: true });

const PROFILE_MANUFACTURER = 'E2E', PROFILE_MODEL = 'Webdesk', PROFILE_NAME = PROFILE_MANUFACTURER + ' ' + PROFILE_MODEL;
const PROFILE_FILE = path.join(SANDBOX, 'InputProfiles', PROFILE_MANUFACTURER + '-' + PROFILE_MODEL + '.qxi');
const REAL_USER_FILE = path.join(process.env.USERPROFILE || '', 'QLC+', 'InputProfiles', PROFILE_MANUFACTURER + '-' + PROFILE_MODEL + '.qxi');

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

/* ---- a second API client for read-back --------------------------------------------------------- */
class Api {
  constructor(port) { this.port = port; this.id = 0; this.pending = new Map(); this.events = []; }
  async open() {
    this.ws = new WebSocket('ws://127.0.0.1:' + this.port + '/');
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.type === 'response' && this.pending.has(m.id)) { const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id); m.ok ? res(m.result) : rej(Object.assign(new Error(m.error.code + ': ' + m.error.message), m.error)); }
      else if (m.type === 'event') this.events.push(m);
    };
    await this.call('hello', { apiVersion: '1', clientName: 'io e2e' });
  }
  call(method, params = {}) {
    const id = 'e2e-' + (++this.id);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  close() { try { this.ws.close(); } catch (e) { } }
}

/* ---- page helpers ------------------------------------------------------------------------------ */
// cdp.js stringifies finder functions, so values are inlined into their source rather than closed over.
// "Leaf" = an element with no element children other than icons (combo entries are <div><img/>text</div>).
function leafByText(scope, text, last) {
  return new Function('const root = document.querySelector(' + JSON.stringify(scope) + '); if (!root) return null;'
    + ' const all = Array.from(root.querySelectorAll("*")).filter(el => Array.from(el.children).every(c => c.tagName === "IMG") && el.textContent.trim() === ' + JSON.stringify(text) + ');'
    + ' return ' + (last ? 'all[all.length - 1]' : 'all[0]') + ';');
}
/** The design-system CustomCheckBox is a <button> right before its RobotoText label. */
function checkboxByLabel(scope, label) {
  return new Function('const root = document.querySelector(' + JSON.stringify(scope) + '); if (!root) return null;'
    + ' const leaf = Array.from(root.querySelectorAll("*")).find(el => el.children.length === 0 && el.textContent.trim() === ' + JSON.stringify(label) + ');'
    + ' let el = leaf; while (el && el !== root) { const prev = el.previousElementSibling; if (prev && prev.tagName === "BUTTON") return prev; el = el.parentElement; } return null;');
}
/** The <input> in the "Label | control" Row whose label text is `label` (io/io-shared.jsx Row). */
function rowInput(scope, label) {
  return new Function('const root = document.querySelector(' + JSON.stringify(scope) + '); if (!root) return null;'
    + ' const rows = Array.from(root.querySelectorAll("div")).filter(d => d.children.length >= 2 && d.children[0].textContent.trim() === ' + JSON.stringify(label) + ');'
    + ' for (const r of rows) { const i = r.querySelector("input"); if (i) return i; } return null;');
}
/** Scroll the target into view first: cdp.js clicks at the element's on-screen centre, and the right
    panel / dialogs scroll. */
async function clickEl(page, finder) {
  const src = typeof finder === 'function' ? '(' + finder.toString() + ')()' : 'document.querySelector(' + JSON.stringify(finder) + ')';
  await page.eval('(function(){ const el = ' + src + '; if (!el) throw new Error("element not found: ' + String(finder).replace(/["\\\n]/g, ' ').slice(0, 80) + '"); el.scrollIntoView({ block: "center", inline: "nearest" }); })()');
  await sleep(80);
  await page.click(finder);
}
async function pickCombo(page, scope, currentLabel, wantedLabel) {
  await clickEl(page, leafByText(scope, currentLabel));
  await sleep(200);
  await page.eval('(' + leafByText(scope, wantedLabel, true).toString() + ')().scrollIntoView({ block: "nearest" })');
  await sleep(100);
  await clickEl(page, leafByText(scope, wantedLabel, true));
  await sleep(250);
}
// React-controlled inputs: set through the native setter and fire the event React listens to.
async function setInputEl(page, finder, value, commitWithEnter) {
  await page.eval('(function(){ const el = (' + finder.toString() + ')(); if (!el) throw new Error("input not found"); el.focus();'
    + ' Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, ' + JSON.stringify(value) + ');'
    + ' el.dispatchEvent(new Event("input", { bubbles: true })); })()');
  if (commitWithEnter) await page.key('Enter');
  await sleep(150);
}
async function textOf(page, selector) { return page.eval('(document.querySelector(' + JSON.stringify(selector) + ') || {}).textContent || ""'); }
async function exists(page, selector) { return page.eval('!!document.querySelector(' + JSON.stringify(selector) + ')'); }
async function shot(page, name) { const f = path.join(OUT, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }
async function waitUntil(fn, timeout = 5000, interval = 150) {
  const t0 = Date.now();
  for (;;) { let v = false; try { v = await fn(); } catch (e) { } if (v) return v; if (Date.now() - t0 > timeout) return false; await sleep(interval); }
}
const keypadInput = () => Array.from(document.querySelectorAll('input')).find(i => i.parentElement && Array.from(i.parentElement.children).some(c => (c.textContent || '').trim() === 'ENTER'));

(async () => {
  const api = new Api(API_PORT);
  await api.open();
  const project = await api.call('core.project.get').catch(() => ({}));
  console.log('project:', project.fileName || project.filePath || '(untitled)');
  const plugins = await api.call('io.plugin.list');
  console.log('plugins on this instance:', (plugins.plugins || []).map(p => p.name).join(', ') || '(none - sandbox)');
  /* start from a known state (a previous run may have been interrupted) */
  if (fs.existsSync(PROFILE_FILE)) fs.unlinkSync(PROFILE_FILE);
  try { await api.call('io.inputProfile.delete', { name: PROFILE_NAME }); } catch (e) { }
  await api.call('io.universe.setMonitor', { universeId: 0, monitor: false });
  await api.call('io.grandMaster.setMode', { channelMode: 'Intensity', valueMode: 'Reduce' });
  api.events.length = 0;

  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=io');
  try {
    await page.waitFor('!!document.querySelector("[data-universe=\\"0\\"]") && !!document.querySelector("[data-role=\\"grand-master\\"]")', 30000);
    console.log('connected, I/O screen rendered');
    await sleep(600);

    /* ---------------- 1. universe monitor toggle ---------------- */
    console.log('Universe monitor');
    const before = await api.call('io.universe.get', { universeId: 0 });
    check(before.monitor === false, 'universe 0 starts with monitor off');
    const monitorPressed = () => page.eval('(document.querySelector("[data-universe=\\"0\\"] [data-role=\\"monitor\\"]") || {}).getAttribute("aria-pressed")');
    check((await monitorPressed()) === 'false', 'Monitor checkbox of universe 1 renders unchecked (data-role="monitor", aria-pressed)');
    await clickEl(page, checkboxByLabel('[data-universe="0"]', 'Monitor'));
    check(await waitUntil(async () => (await api.call('io.universe.get', { universeId: 0 })).monitor === true), 'monitor on after clicking the checkbox (io.universe.get)');
    const monEvent = api.events.find(e => e.topic === 'io.universe.monitorChanged' && e.data.universeId === 0 && e.data.monitor === true);
    check(!!monEvent, 'io.universe.monitorChanged event received by the second client');
    check(await waitUntil(async () => (await monitorPressed()) === 'true'), 'checkbox re-rendered as checked from the read-back');
    await clickEl(page, checkboxByLabel('[data-universe="0"]', 'Monitor'));
    check(await waitUntil(async () => (await api.call('io.universe.get', { universeId: 0 })).monitor === false), 'monitor off again');
    check(await waitUntil(async () => (await monitorPressed()) === 'false'), 'checkbox unchecked again');

    /* ---------------- 2. Grand Master modes ---------------- */
    console.log('Grand Master modes');
    const gm0 = await api.call('io.grandMaster.get');
    check(gm0.channelMode === 'Intensity' && gm0.valueMode === 'Reduce', 'GM starts as Intensity / Reduce (engine defaults)');
    await pickCombo(page, '[data-role="grand-master"]', 'Intensity', 'All channels');
    check(await waitUntil(async () => (await api.call('io.grandMaster.get')).channelMode === 'AllChannels'), 'channel mode AllChannels after the combo (io.grandMaster.get)');
    await pickCombo(page, '[data-role="grand-master"]', 'Reduce', 'Limit');
    check(await waitUntil(async () => (await api.call('io.grandMaster.get')).valueMode === 'Limit'), 'value mode Limit after the combo');
    check(api.events.some(e => e.topic === 'io.grandMaster.changed' && e.data.valueMode === 'Limit'), 'io.grandMaster.changed carried the new mode');
    await pickCombo(page, '[data-role="grand-master"]', 'All channels', 'Intensity');
    await pickCombo(page, '[data-role="grand-master"]', 'Limit', 'Reduce');
    check(await waitUntil(async () => { const g = await api.call('io.grandMaster.get'); return g.channelMode === 'Intensity' && g.valueMode === 'Reduce'; }), 'modes restored to Intensity / Reduce');

    /* ---------------- 3. audio devices render ---------------- */
    console.log('Audio devices');
    const audio = await api.call('io.audio.listDevices');
    check(Array.isArray(audio.inputs) && audio.inputs[0].privateName === '__qlcplusdefault__', 'io.audio.listDevices lists the default input first (' + (audio.inputs.length - 1) + ' host inputs)');
    const audioText = await textOf(page, '[data-role="audio-devices"]');
    check(audioText.indexOf('Default device') !== -1 || audioText.indexOf(audio.inputs.find(d => d.privateName === audio.inputDevice) ? audio.inputs.find(d => d.privateName === audio.inputDevice).name : 'Default device') !== -1, 'Audio section shows the selected devices: ' + audioText.replace(/\s+/g, ' ').slice(0, 120));
    console.log('  (device selection deliberately not exercised: it would change the host\'s real audio setting)');

    /* ---------------- 4. input profile: create, save, reopen, delete ---------------- */
    console.log('Input profile editor');
    const listBefore = await api.call('io.inputProfile.list');
    check(!listBefore.profiles.some(p => p.name === PROFILE_NAME), 'profile "' + PROFILE_NAME + '" does not exist yet');
    await clickEl(page, leafByText('body', 'New'));
    await page.waitFor('!!document.querySelector("[data-role=\\"profile-editor\\"]")', 5000);
    await setInputEl(page, rowInput('[data-role="profile-editor"]', 'Manufacturer'), PROFILE_MANUFACTURER, true);
    await setInputEl(page, rowInput('[data-role="profile-editor"]', 'Model'), PROFILE_MODEL, true);
    await pickCombo(page, '[data-role="profile-editor"]', 'MIDI', 'OSC');
    /* a channel */
    await clickEl(page, leafByText('[data-role="profile-editor"]', 'Add channel'));
    await page.waitFor('!!document.querySelector("[data-role=\\"channel-editor\\"]")', 5000);
    await setInputEl(page, rowInput('[data-role="channel-editor"]', 'Number'), '10', true);
    await setInputEl(page, rowInput('[data-role="channel-editor"]', 'Name'), 'Fader 10', true);
    await pickCombo(page, '[data-role="channel-editor"]', 'Button', 'Slider');
    await pickCombo(page, '[data-role="channel-editor"]', 'Absolute', 'Relative');
    await clickEl(page, leafByText('body', 'Ok', true));
    check(await waitUntil(() => exists(page, '[data-role="profile-editor"] [data-channel="9"]')), 'channel row 10 (key 9) appears in the table');
    /* a colour + a MIDI channel label */
    await clickEl(page, leafByText('[data-role="profile-editor"]', 'Colors'));
    await setInputEl(page, rowInput('[data-role="profile-editor"]', 'Value'), '3', true).catch(() => {});
    await setInputEl(page, () => document.querySelector('[data-role="profile-editor"] input[placeholder="e.g. Red"]'), 'Amber', true);
    await clickEl(page, leafByText('[data-role="profile-editor"]', 'Add / update'));
    check(await waitUntil(() => exists(page, '[data-role="profile-editor"] [data-color]')), 'colour row added');
    await clickEl(page, leafByText('[data-role="profile-editor"]', 'MIDI Channels'));
    await setInputEl(page, () => document.querySelector('[data-role="profile-editor"] input[placeholder="e.g. Faders"]'), 'Main', true);
    await clickEl(page, leafByText('[data-role="profile-editor"]', 'Add / update'));
    check(await waitUntil(() => exists(page, '[data-role="profile-editor"] [data-midi-channel="0"]')), 'MIDI channel label row added');
    await clickEl(page, leafByText('[data-role="profile-editor"]', 'Input Mapping'));
    await shot(page, 'io-profile-editor');
    /* save */
    await clickEl(page, leafByText('body', 'Save', true));
    check(await waitUntil(async () => (await api.call('io.inputProfile.list')).profiles.some(p => p.name === PROFILE_NAME)), 'io.inputProfile.list contains the new profile after Save');
    check(await waitUntil(async () => /Saved E2E Webdesk/.test(await textOf(page, '[data-role="profile-editor"]'))), 'editor reports the save (status line)');
    check(await waitUntil(() => page.eval('(' + leafByText('body', 'Close', true).toString() + ')() != null')), 'footer button turns from Discard into Close once saved');
    check(fs.existsSync(PROFILE_FILE), 'profile file written into the sandbox: ' + PROFILE_FILE);
    check(!fs.existsSync(REAL_USER_FILE), 'nothing written into the real user profile folder (' + REAL_USER_FILE + ')');
    const saved = await api.call('io.inputProfile.get', { name: PROFILE_NAME });
    check(saved.profile.type === 'OSC', 'saved type is OSC');
    const ch = saved.profile.channels.find(c => c.number === 9);
    check(!!ch && ch.name === 'Fader 10' && ch.type === 'Slider' && ch.movementType === 'Relative', 'saved channel 10: Slider, Relative, named "Fader 10"');
    check(saved.profile.colorTable.some(c => c.label === 'Amber'), 'saved colour table has "Amber"');
    check(saved.profile.midiChannelTable.some(m => m.channel === 0 && m.label === 'Main'), 'saved MIDI channel table has channel 1 = "Main"');
    check(saved.profile.isUser === true && saved.profile.path.replace(/\//g, '\\').toLowerCase() === PROFILE_FILE.toLowerCase(), 'io.inputProfile.get reports the sandbox path and isUser');
    const xml = fs.readFileSync(PROFILE_FILE, 'utf8');
    check(/<Manufacturer>E2E<\/Manufacturer>/.test(xml) && /<Type>OSC<\/Type>/.test(xml) && /Number="9"/.test(xml), 'the .qxi on disk has manufacturer, type and the channel');
    check(api.events.some(e => e.topic === 'io.inputProfile.changed' && e.data.profile && e.data.profile.name === PROFILE_NAME), 'io.inputProfile.changed broadcast to the other client');
    /* close, reopen through the list */
    await clickEl(page, leafByText('body', 'Close', true));
    await sleep(300);
    check(!(await exists(page, '[data-role="profile-editor"]')), 'editor closed');
    check(await waitUntil(() => exists(page, '[data-profile="' + PROFILE_NAME + '"]')), 'profile listed in the Input profiles panel');
    await clickEl(page, '[data-profile="' + PROFILE_NAME + '"]');
    await sleep(150);
    await clickEl(page, leafByText('body', 'Edit'));
    await page.waitFor('!!document.querySelector("[data-role=\\"profile-editor\\"] [data-channel=\\"9\\"]")', 5000);
    const reopened = await textOf(page, '[data-role="profile-editor"] [data-channel="9"]');
    check(/Fader 10/.test(reopened) && /Slider/.test(reopened), 'reopened editor shows the saved channel: ' + reopened.replace(/\s+/g, ' ').trim());
    const manufacturerValue = await page.eval('(' + rowInput('[data-role="profile-editor"]', 'Manufacturer').toString() + ')().value');
    check(manufacturerValue === PROFILE_MANUFACTURER, 'reopened editor shows the manufacturer');
    await shot(page, 'io-profile-reopened');
    /* delete */
    await clickEl(page, leafByText('body', 'Delete profile'));
    await sleep(200);
    await clickEl(page, leafByText('body', 'Delete', true));
    check(await waitUntil(async () => !(await api.call('io.inputProfile.list')).profiles.some(p => p.name === PROFILE_NAME)), 'profile gone from io.inputProfile.list after Delete');
    check(!fs.existsSync(PROFILE_FILE), 'profile file removed from the sandbox');
    check(api.events.some(e => e.topic === 'io.inputProfile.deleted' && e.data.name === PROFILE_NAME), 'io.inputProfile.deleted broadcast');
    check(await waitUntil(async () => !(await exists(page, '[data-profile="' + PROFILE_NAME + '"]'))), 'profile row disappeared from the panel');
    const rev = await api.call('io.inputProfile.list');
    check(rev.profilesRevision >= 2, 'profilesRevision advanced to ' + rev.profilesRevision);

    /* ---------------- 5. plugin panel: no plugins in the sandbox ---------------- */
    const pluginText = await textOf(page, 'body');
    check(/No IO plugins are loaded/.test(pluginText) || (plugins.plugins || []).length > 0, 'plugins panel explains the plugin-less sandbox');
    await shot(page, 'io-screen');

    /* ---------------- 6. Simple Desk: keypad through the server + shared history ---------------- */
    console.log('Simple Desk keypad (server path)');
    await api.call('io.simpleDesk.resetUniverse', { universeId: 0 });
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=sd');
    await page.waitFor('!!document.querySelector("[data-channel=\\"3\\"]") && !!document.querySelector("[data-role=\\"history-source\\"]")', 30000);
    await sleep(800);
    check((await textOf(page, '[data-role="history-source"]')).trim() === 'server', 'history is the server\'s (io.simpleDesk.sendKeypadCommand supported)');
    await setInputEl(page, keypadInput, '1 thru 4 @ 50', true);
    check(await waitUntil(async () => { const d = await api.call('io.dmx.universe.get', { universeId: 0 }); return d.values[0] === 50 && d.values[3] === 50 && d.values[4] === 0; }, 5000), 'channels 1-4 of universe 1 are 50 in io.dmx.universe.get, channel 5 untouched');
    const desk = await api.call('io.simpleDesk.get', { universeId: 0 });
    check(desk.commandHistory[0] === '1 THRU 4 AT 50', 'server command history starts with "1 THRU 4 AT 50" (got ' + JSON.stringify(desk.commandHistory[0]) + ')');
    check(await waitUntil(async () => (await textOf(page, '[data-history="0"]')).trim() === '1 THRU 4 AT 50'), 'page history list shows the server entry');
    check(api.events.some(e => e.topic === 'io.simpleDesk.commandHistoryChanged'), 'io.simpleDesk.commandHistoryChanged reached the other client');
    /* the remembered selection + a relative step, then a second command from the "other" client shows up here too */
    await setInputEl(page, keypadInput, 'FULL', true);
    check(await waitUntil(async () => { const d = await api.call('io.dmx.universe.get', { universeId: 0 }); return d.values[0] === 255 && d.values[3] === 255; }, 5000), 'bare "FULL" applies to the remembered channels 1-4');
    await api.call('io.simpleDesk.sendKeypadCommand', { command: '6 AT 20', universeId: 0 });
    check(await waitUntil(async () => (await textOf(page, '[data-history="0"]')).trim() === '6 AT 20'), 'a command sent by another client appears in this tab\'s history');
    const stripValue = await waitUntil(() => page.eval('(function(){ const s = document.querySelector("[data-channel=\\"0\\"]"); if (!s) return false;'
      + ' const inputs = Array.from(s.querySelectorAll("input")).map(i => i.value); return /(^|[^0-9])255([^0-9]|$)/.test(s.textContent) || inputs.indexOf("255") !== -1; })()'), 3000);
    check(!!stripValue, 'channel strip 1 shows 255 (channelChanged events applied)');
    await shot(page, 'sd-keypad-server');
    await api.call('io.simpleDesk.resetUniverse', { universeId: 0 });

    /* ---------------- 7. save into the sandbox ---------------- */
    const outPath = path.join(SANDBOX, 'out.qxw');
    const savedProject = await api.call('core.project.saveAs', { target: 'serverPath', path: outPath, baseRevision: (await api.call('io.universe.list')).docRevision });
    check(!!savedProject && fs.existsSync(outPath), 'core.project.saveAs wrote ' + outPath);
    const qxw = fs.readFileSync(outPath, 'utf8');
    check(/<Universe Name="Universe 1" ID="0"\/>/.test(qxw) && /<Universe Name="Universe 6" ID="5"\/>/.test(qxw), 'saved .qxw carries the six universes');

    check(page.consoleErrors.length === 0, 'no console errors' + (page.consoleErrors.length ? ': ' + page.consoleErrors.join(' | ') : ''));
  } catch (e) {
    failures++;
    console.log('  FAIL exception: ' + (e.stack || e));
    try { await shot(page, 'io-failure'); } catch (e2) { }
    if (page.consoleErrors.length) console.log('  console errors: ' + page.consoleErrors.join(' | '));
  } finally {
    await b.close();
    api.close();
  }
  console.log(failures ? failures + ' failure(s)' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})();
