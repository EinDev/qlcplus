// End-to-end check of the Fixture Editor screen (webui/FixtureEditor.jsx + webui/fixtureeditor/*)
// against a sandbox instance whose user fixture folder is redirected:
//
//   .\dev-webui-sandbox.ps1 -Name fixdefs -BuildDir .\build -WebUiRoot .\webui -ApiPort 9200 -WebUiPort 9201 `
//       -UserFixtureDir C:\qlcsandbox\fixdefs\UserFixtures
//   node webui/tools/e2e/fixture-editor.js [--api 9200] [--web 9201] [--out <dir>] [--userdir C:\qlcsandbox\fixdefs\UserFixtures]
//
// Drives headless Chrome through webui/tools/cdp.js like an operator (real clicks, typed values,
// the combo boxes, the wizards, drag-free up/down ordering, a real file upload and download), and
// reads every step back over a second API connection (fixturedefs.session.get / list / get) and
// from the .qxf files on disk. Covers: New -> General -> 4 channels by the channel wizard -> one
// custom channel with 3 capabilities by the capability wizard + colour preset + an alias -> a mode
// with every channel, reordered, acts-on, 2 emitters and a physical override -> global physical ->
// Validate -> Save; reload survival; Open picker reopen and a field-by-field check; a foreign edit
// forcing a CONFLICT rebase; Export (download) + Import (upload); fork a bundled definition to a
// user copy, edit, save, delete; delete the E2E definition; the Add Fixtures dialog entry point.
// Also asserts that %UserProfile%\QLC+\Fixtures is untouched (file count + sizes + mtimes).
//
// Exit code 0 = every assertion held and the page logged no console errors. Only ever run against
// a sandbox whose QLCPLUS_USER_FIXTURE_DIR points into C:\qlcsandbox (the script refuses otherwise).

const path = require('path'), fs = require('fs'), os = require('os');
const { launch } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', 9200)), WEB_PORT = Number(opt('web', 9201));
const OUT = opt('out', path.join(os.tmpdir(), 'qlc-e2e-fixture-editor'));
const USER_DIR = opt('userdir', 'C:\\qlcsandbox\\fixdefs\\UserFixtures');
const REAL_DIR = path.join(process.env.USERPROFILE || '', 'QLC+', 'Fixtures');
fs.mkdirSync(OUT, { recursive: true });
const MAN = 'E2E', MODEL = 'Web Spot', FORK_MAN = 'Generic', FORK_MODEL = 'Generic Smoke';

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
function snapshotDir(dir) {
  if (!fs.existsSync(dir)) return [];
  const out = [];
  (function walk(d) { for (const e of fs.readdirSync(d, { withFileTypes: true })) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else { const st = fs.statSync(p); out.push(p + '|' + st.size + '|' + st.mtimeMs); } } })(dir);
  return out.sort();
}

/* ---- second API client for read-back ------------------------------------------------------------ */
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
    await this.call('hello', { apiVersion: '1', clientName: 'fixture-editor e2e' });
  }
  call(method, params = {}) { const id = 'e2e-' + (++this.id); return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); }); }
  close() { try { this.ws.close(); } catch (e) { } }
}

/* ---- page helpers (cdp.js stringifies finders, so values are inlined into their source) ---------- */
const q = (sel) => new Function('return document.querySelector(' + JSON.stringify(sel) + ');');
function buttonByText(scope, text) {
  return new Function('const root = document.querySelector(' + JSON.stringify(scope) + '); if (!root) return null;'
    + ' return Array.from(root.querySelectorAll("button")).find(b => b.textContent.trim() === ' + JSON.stringify(text) + ') || null;');
}
async function clickEl(page, finder) {
  const src = typeof finder === 'function' ? '(' + finder.toString() + ')()' : 'document.querySelector(' + JSON.stringify(finder) + ')';
  await page.eval('(function(){ const el = ' + src + '; if (!el) throw new Error("element not found: ' + String(finder).replace(/["\\\n]/g, ' ').slice(0, 100) + '"); el.scrollIntoView({ block: "center", inline: "nearest" }); })()');
  await sleep(60);
  await page.click(finder);
  await sleep(120);
}
/** Design-system CustomComboBox: `sel` is its wrapper (data-fe lands there); open it, pick by label. */
async function pickCombo(page, sel, wanted) {
  await clickEl(page, sel + ' > button');
  await sleep(150);
  const item = new Function('const w = document.querySelector(' + JSON.stringify(sel) + '); if (!w) return null;'
    + ' const list = w.querySelectorAll(":scope > div button"); return Array.from(list).find(b => b.textContent.trim() === ' + JSON.stringify(wanted) + ') || null;');
  await clickEl(page, item);
  await sleep(200);
}
/** React-controlled <input>: native setter + input event, then blur (FE.Text commits on blur) or Enter. */
async function typeInto(page, sel, value, how = 'blur') {
  await page.eval('(function(){ const el = document.querySelector(' + JSON.stringify(sel) + '); if (!el) throw new Error("input not found: ' + sel.replace(/"/g, "'") + '");'
    + ' el.scrollIntoView({ block: "center" }); el.focus(); Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, ' + JSON.stringify(String(value)) + ');'
    + ' el.dispatchEvent(new Event("input", { bubbles: true })); ' + (how === 'blur' ? 'el.blur();' : '') + ' })()');
  if (how === 'enter') await page.key('Enter');
  await sleep(250);
}
/** Design-system CustomSpinBox: data-* attributes land on its <input>, which commits on every change. */
async function spin(page, sel, value) { await typeInto(page, sel, value, 'enter'); await page.eval('document.activeElement && document.activeElement.blur()'); await sleep(200); }
async function valueOf(page, sel) { return page.eval('(document.querySelector(' + JSON.stringify(sel) + ') || {}).value'); }
async function textOf(page, sel) { return page.eval('(document.querySelector(' + JSON.stringify(sel) + ') || {}).textContent || ""'); }
async function exists(page, sel) { return page.eval('!!document.querySelector(' + JSON.stringify(sel) + ')'); }
async function shot(page, name) { const f = path.join(OUT, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }
async function waitUntil(fn, timeout = 6000, interval = 150) { const t0 = Date.now(); for (;;) { let v = false; try { v = await fn(); } catch (e) { } if (v) return v; if (Date.now() - t0 > timeout) return false; await sleep(interval); } }
async function tab(page, id) { await clickEl(page, '[data-fe-tab="' + id + '"]'); await sleep(250); }
async function dialogButton(page, text) { await clickEl(page, new Function('const b = Array.from(document.querySelectorAll("button")).filter(x => x.textContent.trim() === ' + JSON.stringify(text) + '); return b[b.length - 1] || null;')); }

(async () => {
  if (!/qlcsandbox/i.test(USER_DIR)) { console.log('refusing: --userdir must be inside C:\\qlcsandbox'); process.exit(2); }
  const realBefore = snapshotDir(REAL_DIR);
  console.log('real user fixture folder ' + REAL_DIR + ': ' + realBefore.length + ' files');
  const api = new Api(API_PORT);
  await api.open();
  const sessionOf = async (sid) => api.call('fixturedefs.session.get', { sessionId: sid });
  const findSession = async (man, model) => { const l = await api.call('fixturedefs.session.list'); return l.sessions.find(s => s.manufacturer === man && s.model === model) || null; };

  /* ---- known start state ---- */
  for (const s of (await api.call('fixturedefs.session.list')).sessions) await api.call('fixturedefs.session.close', { sessionId: s.sessionId });
  for (const [man, model] of [[MAN, MODEL], [FORK_MAN, FORK_MODEL]]) {
    const l = await api.call('fixturedefs.list', { manufacturer: man });
    const e = l.entries.find(x => x.model === model && x.isUser);
    if (e) { await api.call('fixturedefs.delete', { manufacturer: man, model, baseRevision: e.defRevision }); console.log('  (removed a leftover user ' + man + ' - ' + model + ')'); }
  }
  const e2eFile = path.join(USER_DIR, 'E2E-Web-Spot.qxf');
  check(!fs.existsSync(e2eFile), 'no E2E-Web-Spot.qxf in the sandbox user folder at start');
  check((await api.call('fixturedefs.list', { manufacturer: MAN })).entries.length === 0, 'fixturedefs.list({manufacturer:"E2E"}) is empty at start');

  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=fxeditor');
  await page.s.send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: OUT }).catch(() => page.s.send('Page.setDownloadBehavior', { behavior: 'allow', downloadPath: OUT }));
  let sid = null;
  try {
    await page.waitFor('!!document.querySelector("[data-fe=\\"screen\\"]") && !!document.querySelector("[data-fe=\\"tb-new\\"]")', 30000);
    await sleep(800);
    console.log('connected, Fixture Editor rendered');

    /* ================= 1. New + General ================= */
    console.log('New definition + General');
    await clickEl(page, '[data-fe="tb-new"]');
    check(await waitUntil(() => exists(page, '[data-fe="general-tab"]')), 'New opens a session on the General tab');
    const created = (await api.call('fixturedefs.session.list')).sessions;
    check(created.length === 1, 'server has exactly one session');
    sid = created[0].sessionId;
    await typeInto(page, '[data-fe="general-manufacturer"]', MAN);
    await typeInto(page, '[data-fe="general-model"]', MODEL);
    await typeInto(page, '[data-fe="general-author"]', 'Web UI e2e');
    await clickEl(page, '[data-fe="general-type"] [data-type="Moving Head"]');
    check(await waitUntil(async () => { const d = (await sessionOf(sid)).definition; return d.manufacturer === MAN && d.model === MODEL && d.author === 'Web UI e2e' && d.type === 'Moving Head'; }),
      'manufacturer / model / author / type read back through fixturedefs.session.get');
    check(await waitUntil(async () => /E2E - Web Spot/.test(await textOf(page, '[data-fe="session-tabs"]'))), 'session tab is labelled "E2E - Web Spot"');
    check(await waitUntil(() => page.eval('!!document.querySelector("[data-session-label=\\"E2E - Web Spot\\"] img")')), 'modified marker shown on the tab');
    await shot(page, 'fe-general');

    /* ================= 2. Channel wizard: 4 channels ================= */
    console.log('Channels: channel wizard (RGBW = 4 channels)');
    await tab(page, 'channels');
    await clickEl(page, '[data-fe="channel-wizard"]');
    await page.waitFor('!!document.querySelector("[data-fe=\\"channel-wizard-dialog\\"]")', 5000);
    await pickCombo(page, '[data-fe="wizard-type"]', 'RGBW');
    check(/Red 1.*Green 1.*Blue 1.*White 1/.test(await textOf(page, '[data-fe="wizard-preview"]')), 'wizard preview lists Red 1, Green 1, Blue 1, White 1');
    await shot(page, 'fe-channel-wizard');
    await dialogButton(page, 'Ok');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.channels.length === 4), 'fixturedefs.channel.wizard created 4 channels');
    let def = (await sessionOf(sid)).definition;
    check(def.channels.map(c => c.name).join(',') === 'Red 1,Green 1,Blue 1,White 1' && def.channels.every(c => c.group === 'Intensity'), 'names and Intensity group: ' + def.channels.map(c => c.name + '/' + c.colour).join(', '));

    /* ================= 3. Custom channel + capability wizard ================= */
    console.log('Channels: custom channel with 3 capabilities');
    await clickEl(page, '[data-fe="add-channel"]');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.channels.length === 5), 'channel.add created a 5th (Custom) channel');
    await page.waitFor('!!document.querySelector("[data-fe=\\"channel-editor\\"]")', 5000);
    await typeInto(page, '[data-fe="channel-name"]', 'Gobo');
    await pickCombo(page, '[data-fe="channel-group"]', 'Gobo');
    await spin(page, '[data-fe="channel-default"]', 10);
    const gobo = () => sessionOf(sid).then(r => r.definition.channels.find(c => c.name === 'Gobo'));
    check(await waitUntil(async () => { const g = await gobo(); return g && g.group === 'Gobo' && g.defaultValue === 10; }), 'channel renamed "Gobo", group Gobo, default value 10');
    /* a new custom channel carries one 0-255 capability: remove it so the wizard has room */
    await clickEl(page, '[data-cap-index="0"]');
    await clickEl(page, '[data-fe="remove-capability"]');
    check(await waitUntil(async () => (await gobo()).capabilities.length === 0), 'default capability removed');
    await clickEl(page, '[data-fe="capability-wizard"]');
    await page.waitFor('!!document.querySelector("[data-fe=\\"capability-wizard-dialog\\"]")', 5000);
    await spin(page, '[data-fe="wizard-width"]', 20);
    await spin(page, '[data-fe="wizard-amount"]', 3);
    await typeInto(page, '[data-fe="wizard-label"]', 'Gobo #', 'none');
    check(/\[40 - 59\] Gobo 3/.test(await textOf(page, '[data-fe="wizard-preview"]')), 'capability wizard preview shows [40 - 59] Gobo 3');
    await shot(page, 'fe-capability-wizard');
    await dialogButton(page, 'Ok');
    check(await waitUntil(async () => { const g = await gobo(); return g.capabilities.length === 3; }), 'fixturedefs.channel.capability.wizard created 3 capabilities');
    let g = await gobo();
    check(g.capabilities.map(c => c.min + '-' + c.max + ' ' + c.name).join('|') === '0-19 Gobo 1|20-39 Gobo 2|40-59 Gobo 3', 'ranges and names: ' + g.capabilities.map(c => c.min + '-' + c.max + ' ' + c.name).join(' | '));
    /* edit a capability inline + a colour preset on the first one */
    await typeInto(page, '[data-cap-index="1"] [data-fe="cap-name"]', 'Gobo 2 (star)');
    await clickEl(page, '[data-cap-index="0"] [data-fe="cap-name"]');
    await page.waitFor('!!document.querySelector("[data-fe=\\"capability-detail\\"]")', 5000);
    await pickCombo(page, '[data-fe="cap-preset"]', 'Color Macro');
    await page.waitFor('!!document.querySelector("[data-fe=\\"cap-color1\\"]")', 5000);
    await typeInto(page, '[data-fe="cap-color1"]', '#ff8000', 'none');
    check(await waitUntil(async () => { const c = (await gobo()).capabilities; return c[1].name === 'Gobo 2 (star)' && c[0].preset === 'ColorMacro' && String(c[0].resources[0]).toLowerCase() === '#ff8000'; }),
      'capability 2 renamed inline; capability 1 is ColorMacro #ff8000');

    /* ================= 4. Modes ================= */
    console.log('Modes: one mode with every channel, 2 emitters, physical override');
    await tab(page, 'modes');
    await clickEl(page, '[data-fe="add-mode"]');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.modes.length === 1), 'mode.add created a mode');
    await page.waitFor('!!document.querySelector("[data-fe=\\"mode-editor\\"]")', 5000);
    await typeInto(page, '[data-fe="mode-name"]', 'Standard');
    await clickEl(page, '[data-fe="mode-add-all"]');
    const modeOf = async () => (await sessionOf(sid)).definition.modes[0];
    const slotNames = async () => { const d = (await sessionOf(sid)).definition; return d.modes[0].channels.map(sl => d.channels.find(c => c.channelId === sl.channelId).name); };
    check(await waitUntil(async () => { const m = await modeOf(); return m.name === 'Standard' && m.channels.length === 5; }), 'mode renamed "Standard" and holds all 5 channels');
    /* move Gobo (last) to the top with the up arrows */
    for (let i = 4; i > 0; i--) { await clickEl(page, '[data-slot-index="' + i + '"] [data-fe="slot-up"]'); await waitUntil(async () => (await slotNames()).indexOf('Gobo') === i - 1); }
    check((await slotNames()).join(',') === 'Gobo,Red 1,Green 1,Blue 1,White 1', 'slot order after 4x up: ' + (await slotNames()).join(', '));
    await clickEl(page, '[data-slot-index="0"] [data-fe="slot-down"]');
    check(await waitUntil(async () => (await slotNames()).join(',') === 'Red 1,Gobo,Green 1,Blue 1,White 1'), 'down arrow moves Gobo to slot 2');
    /* acts-on */
    await pickCombo(page, '[data-slot-index="4"] [data-fe="slot-acts-on"]', 'Red 1');
    check(await waitUntil(async () => { const d = (await sessionOf(sid)).definition, m = d.modes[0]; const red = d.channels.find(c => c.name === 'Red 1'); return m.channels[4].actsOnChannelId === red.channelId; }), 'White 1 acts on Red 1');
    await pickCombo(page, '[data-slot-index="4"] [data-fe="slot-acts-on"]', 'None');
    check(await waitUntil(async () => (await modeOf()).channels[4].actsOnChannelId === null), 'acts-on cleared again');
    /* two emitters: (Red 1, Green 1) and (Blue 1, White 1) */
    await clickEl(page, '[data-slot-index="0"] [data-fe="slot-check"]');
    await clickEl(page, '[data-slot-index="2"] [data-fe="slot-check"]');
    await clickEl(page, '[data-fe="mode-create-head"]');
    check(await waitUntil(async () => (await modeOf()).heads.length === 1), 'first emitter created');
    await clickEl(page, '[data-slot-index="3"] [data-fe="slot-check"]');
    await clickEl(page, '[data-slot-index="4"] [data-fe="slot-check"]');
    await clickEl(page, '[data-fe="mode-create-head"]');
    check(await waitUntil(async () => (await modeOf()).heads.length === 2), 'second emitter created');
    let d2 = (await sessionOf(sid)).definition;
    const nameOf = (id) => d2.channels.find(c => c.channelId === id).name;
    check(d2.modes[0].heads.map(h => h.channelIds.map(nameOf).join('+')).join(' / ') === 'Red 1+Green 1 / Blue 1+White 1', 'heads: ' + d2.modes[0].heads.map(h => h.channelIds.map(nameOf).join('+')).join(' / '));
    /* per-mode physical override */
    await clickEl(page, '[data-fe="mode-phy-override"]');
    check(await waitUntil(async () => (await modeOf()).useGlobalPhysical === false), 'mode switched to "Override global settings"');
    await typeInto(page, '[data-fe="mode-editor"] [data-fe="phy-powerConsumption"]', '150');
    await typeInto(page, '[data-fe="mode-editor"] [data-fe="phy-weight"]', '6.5');
    check(await waitUntil(async () => { const m = await modeOf(); return m.physical && m.physical.powerConsumption === 150 && Math.abs(m.physical.weight - 6.5) < 1e-6; }), 'mode override: 150 W, 6.5 kg (both kept)');
    await shot(page, 'fe-modes');

    /* ================= 5. Alias on Gobo capability 3 ================= */
    console.log('Alias');
    await tab(page, 'channels');
    await clickEl(page, '[data-channel-name="Gobo"]');
    await clickEl(page, '[data-cap-index="2"] [data-fe="cap-name"]');
    await pickCombo(page, '[data-fe="cap-preset"]', 'Alias');
    await page.waitFor('!!document.querySelector("[data-fe=\\"alias-editor\\"]")', 5000);
    await clickEl(page, '[data-fe="alias-add"]');
    check(await waitUntil(async () => ((await gobo()).capabilities[2].aliases || []).length === 1), 'alias.add created an alias');
    await pickCombo(page, '[data-alias-index="0"] [data-fe="alias-target"]', 'White 1');
    check(await waitUntil(async () => { const a = (await gobo()).capabilities[2].aliases[0]; return a && a.targetMode === 'Standard' && a.targetChannel === 'White 1'; }), 'alias: in Standard replace Gobo with White 1');
    await shot(page, 'fe-channels');
    await tab(page, 'aliases');
    check(/Gobo \[40 - 59\] Gobo 3 \(1\)/.test(await textOf(page, '[data-fe="aliases-tab"]')), 'Aliases tab lists "Gobo [40 - 59] Gobo 3 (1)"');
    await shot(page, 'fe-aliases');

    /* ================= 5b. the remaining channel / capability / alias / mode controls ================= */
    console.log('Channel preset add + remove, preset / role / colour, capability presets, auto colours');
    const chan = (name) => sessionOf(sid).then(r => r.definition.channels.find(c => c.name === name));
    await tab(page, 'channels');
    await pickCombo(page, '[data-fe="add-channel-preset"]', 'Intensity Dimmer');
    await clickEl(page, '[data-fe="add-channel"]');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.channels.some(c => c.preset === 'IntensityDimmer')), 'add from the preset combo: an IntensityDimmer channel');
    const dimName = (await sessionOf(sid)).definition.channels.find(c => c.preset === 'IntensityDimmer').name;
    await clickEl(page, '[data-channel-name="' + dimName + '"]');
    await clickEl(page, '[data-fe="remove-channels"]');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.channels.length === 5), 'remove channel: back to 5 channels');
    /* Red 1: preset -> Custom unlocks type / role / colour */
    await clickEl(page, '[data-channel-name="Red 1"]');
    await pickCombo(page, '[data-fe="channel-preset"]', 'Custom');
    check(await waitUntil(async () => (await chan('Red 1')).preset === 'Custom'), 'Red 1 preset set to Custom');
    await clickEl(page, '[data-fe="role-lsb"]');
    check(await waitUntil(async () => (await chan('Red 1')).controlByte === 'LSB'), 'role Fine (LSB)');
    await clickEl(page, '[data-fe="role-msb"]');
    check(await waitUntil(async () => (await chan('Red 1')).controlByte === 'MSB'), 'role Coarse (MSB) again');
    await pickCombo(page, '[data-fe="channel-colour"]', 'Amber');
    check(await waitUntil(async () => (await chan('Red 1')).colour === 'Amber'), 'colour Amber');
    await pickCombo(page, '[data-fe="channel-colour"]', 'Red');
    await pickCombo(page, '[data-fe="channel-preset"]', 'Intensity Red');
    check(await waitUntil(async () => { const c = await chan('Red 1'); return c.preset === 'IntensityRed' && c.colour === 'Red' && c.capabilities.length === 1; }), 'preset Intensity Red restored (capability regenerated)');
    /* Gobo: "+" capability, then value / range / picture presets */
    await clickEl(page, '[data-channel-name="Gobo"]');
    await clickEl(page, '[data-fe="add-capability"]');
    check(await waitUntil(async () => { const c = (await gobo()).capabilities; return c.length === 4 && c[3].min === 60 && c[3].max === 255; }), '"+" added a capability on the next free range 60-255');
    await clickEl(page, '[data-cap-index="3"] [data-fe="cap-name"]');
    await pickCombo(page, '[data-fe="cap-preset"]', 'Strobe Frequency');
    await typeInto(page, '[data-fe="cap-value1"]', '5');
    check(await waitUntil(async () => { const c = (await gobo()).capabilities[3]; return c.preset === 'StrobeFrequency' && Number(c.resources[0]) === 5; }), 'Strobe Frequency with 5 Hz');
    await pickCombo(page, '[data-fe="cap-preset"]', 'Strobe Freq Range');
    await typeInto(page, '[data-fe="cap-value1"]', '2');
    await typeInto(page, '[data-fe="cap-value2"]', '20');
    check(await waitUntil(async () => { const c = (await gobo()).capabilities[3]; return c.preset === 'StrobeFreqRange' && Number(c.resources[0]) === 2 && Number(c.resources[1]) === 20; }), 'Strobe Freq Range 2 - 20 Hz');
    await pickCombo(page, '[data-fe="cap-preset"]', 'Gobo Macro');
    await typeInto(page, '[data-fe="cap-picture"]', 'Gobos/Others/gobo00001.svg');
    check(await waitUntil(async () => { const c = (await gobo()).capabilities[3]; return c.preset === 'GoboMacro' && /gobo00001\.svg$/.test(String(c.resources[0])); }), 'Gobo Macro with a picture path');
    await clickEl(page, '[data-fe="remove-capability"]');
    check(await waitUntil(async () => (await gobo()).capabilities.length === 3), 'capability removed again');
    /* automatic colour assignment on a Colour channel */
    await pickCombo(page, '[data-fe="add-channel-preset"]', 'Custom');
    await clickEl(page, '[data-fe="add-channel"]');
    await waitUntil(async () => (await sessionOf(sid)).definition.channels.length === 6);
    const wheel = (await sessionOf(sid)).definition.channels[5];
    await typeInto(page, '[data-fe="channel-name"]', 'Wheel');
    await pickCombo(page, '[data-fe="channel-group"]', 'Colour');
    await typeInto(page, '[data-cap-index="0"] [data-fe="cap-max"]', '127');
    await typeInto(page, '[data-cap-index="0"] [data-fe="cap-name"]', 'red');
    await clickEl(page, '[data-fe="add-capability"]');
    await waitUntil(async () => (await chan('Wheel')).capabilities.length === 2);
    await typeInto(page, '[data-cap-index="1"] [data-fe="cap-name"]', 'blue');
    await waitUntil(async () => (await chan('Wheel')).capabilities[1].name === 'blue');
    await clickEl(page, '[data-fe="auto-colours"]');
    check(await waitUntil(async () => { const c = (await chan('Wheel')).capabilities; return c[0].preset === 'ColorMacro' && /^#ff0000$/i.test(c[0].resources[0]) && c[1].preset === 'ColorMacro' && /^#0000ff$/i.test(c[1].resources[0]) && c[0].name === 'Red'; }),
      'Auto colours: red / blue became ColorMacro #ff0000 / #0000ff and were title-cased');
    await clickEl(page, '[data-channel-name="Wheel"]');
    await clickEl(page, '[data-fe="remove-channels"]');
    check(await waitUntil(async () => !(await sessionOf(sid)).definition.channels.some(c => c.channelId === wheel.channelId)), 'Wheel channel removed again');

    console.log('Alias: apply to all modes, mode change, remove; mode / emitter removal; global <-> override');
    await tab(page, 'modes');
    await clickEl(page, '[data-fe="add-mode"]');
    await waitUntil(async () => (await sessionOf(sid)).definition.modes.length === 2);
    await typeInto(page, '[data-fe="mode-name"]', 'Compact');
    await waitUntil(async () => (await sessionOf(sid)).definition.modes.some(m => m.name === 'Compact'));
    await pickCombo(page, '[data-fe="mode-add-pick"]', 'Gobo');
    await clickEl(page, '[data-fe="mode-add-channel"]');
    const compact = async () => (await sessionOf(sid)).definition.modes.find(m => m.name === 'Compact');
    check(await waitUntil(async () => (await compact()).channels.length === 1), 'second mode "Compact" with the Gobo channel (picker + Add)');
    await tab(page, 'channels');
    await clickEl(page, '[data-channel-name="Gobo"]');
    await clickEl(page, '[data-cap-index="2"] [data-fe="cap-name"]');
    await clickEl(page, '[data-fe="alias-apply-all"]');
    check(await waitUntil(async () => (await gobo()).capabilities[2].aliases.length === 2 && (await gobo()).capabilities[2].aliases[1].targetMode === 'Compact'), 'Apply to all modes added the Compact alias');
    await pickCombo(page, '[data-alias-index="1"] [data-fe="alias-target"]', 'Blue 1');
    check(await waitUntil(async () => (await gobo()).capabilities[2].aliases[1].targetChannel === 'Blue 1'), 'second alias retargeted to Blue 1');
    await pickCombo(page, '[data-alias-index="1"] [data-fe="alias-mode"]', 'Standard');
    check(await waitUntil(async () => (await gobo()).capabilities[2].aliases[1].targetMode === 'Standard'), 'alias mode changed to Standard');
    await clickEl(page, '[data-alias-index="1"] [data-fe="alias-remove"]');
    check(await waitUntil(async () => { const a = (await gobo()).capabilities[2].aliases; return a.length === 1 && a[0].targetMode === 'Standard' && a[0].targetChannel === 'White 1'; }), 'alias removed; the first one is untouched');
    await tab(page, 'modes');
    await clickEl(page, '[data-mode-name="Compact"]');
    await clickEl(page, '[data-fe="remove-mode"]');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.modes.length === 1), 'mode Compact removed');
    await clickEl(page, '[data-mode-name="Standard"]');
    await clickEl(page, '[data-slot-channel="Gobo"] [data-fe="slot-check"]');
    await clickEl(page, '[data-fe="mode-create-head"]');
    check(await waitUntil(async () => (await modeOf()).heads.length === 3), 'third emitter (Gobo)');
    await clickEl(page, '[data-head-index="2"] [data-fe="head-check"]');
    await clickEl(page, '[data-fe="mode-remove-heads"]');
    check(await waitUntil(async () => (await modeOf()).heads.length === 2), 'emitter removed: 2 left');
    await clickEl(page, '[data-fe="mode-phy-global"]');
    check(await waitUntil(async () => (await modeOf()).useGlobalPhysical === true), '"Use global settings" drops the override');
    await clickEl(page, '[data-fe="mode-phy-override"]');
    await waitUntil(async () => (await modeOf()).useGlobalPhysical === false);
    await typeInto(page, '[data-fe="mode-editor"] [data-fe="phy-powerConsumption"]', '150');
    await typeInto(page, '[data-fe="mode-editor"] [data-fe="phy-weight"]', '6.5');
    check(await waitUntil(async () => { const m = await modeOf(); return m.physical && m.physical.powerConsumption === 150 && Math.abs(m.physical.weight - 6.5) < 1e-6; }), 'override set again: 150 W, 6.5 kg');
    check((await sessionOf(sid)).definition.channels.map(c => c.name).join(',') === 'Red 1,Green 1,Blue 1,White 1,Gobo', 'channel pool is back to the five channels');

    /* ================= 6. Global physical ================= */
    console.log('Physical');
    await tab(page, 'physical');
    const phy = { bulbType: 'LED', bulbLumens: '5000', bulbColourTemperature: '6500', lensName: 'PC', lensDegreesMin: '12.5', lensDegreesMax: '40',
      focusType: 'Head', focusPanMax: '540', focusTiltMax: '270', layoutWidth: '2', layoutHeight: '1', weight: '7.5', width: '300', height: '450', depth: '200',
      powerConsumption: '200', dmxConnector: '5-pin' };
    for (const k of Object.keys(phy)) await typeInto(page, '[data-fe="physical-tab"] [data-fe="phy-' + k + '"]', phy[k]);
    const wantPhy = (p) => Object.keys(phy).every(k => String(p[k]) === phy[k]);
    check(await waitUntil(async () => wantPhy((await sessionOf(sid)).definition.physical), 8000), 'every global physical field read back: ' + JSON.stringify((await sessionOf(sid)).definition.physical));
    await shot(page, 'fe-physical');

    /* ================= 7. foreign edit -> CONFLICT rebase ================= */
    console.log('Concurrent edit from another client');
    const before = await sessionOf(sid);
    await api.call('fixturedefs.session.update', { sessionId: sid, baseRevision: before.sessionRevision, author: 'someone else' });
    await tab(page, 'general');
    await typeInto(page, '[data-fe="general-author"]', 'Web UI e2e');
    check(await waitUntil(async () => (await sessionOf(sid)).definition.author === 'Web UI e2e'), 'UI edit after a foreign edit still lands (baseRevision rebased on CONFLICT or event)');

    /* ================= 8. Validate + Save ================= */
    console.log('Validate + Save');
    await clickEl(page, '[data-fe="tb-validate"]');
    check(await waitUntil(() => exists(page, '[data-fe="validate-dialog"]')), 'Validate opens the result dialog');
    const vText = await textOf(page, '[data-fe="validate-dialog"]');
    const serverWarnings = (await api.call('fixturedefs.session.validate', { sessionId: sid })).warnings;
    check(serverWarnings.length === 0 ? /No problems found/.test(vText) : serverWarnings.every(w => vText.indexOf(w) !== -1), 'dialog matches fixturedefs.session.validate: ' + (vText.trim() || '(empty)'));
    await dialogButton(page, 'Close');
    await clickEl(page, '[data-fe="tb-save"]');
    check(await waitUntil(async () => (await api.call('fixturedefs.list', { manufacturer: MAN })).entries.some(e => e.model === MODEL && e.isUser)), 'fixturedefs.list({manufacturer:"E2E"}) lists Web Spot as a user definition after Save');
    check(await waitUntil(() => fs.existsSync(e2eFile)), 'saved into the sandbox user folder: ' + e2eFile);
    check(await waitUntil(async () => (await sessionOf(sid)).isModified === false), 'session no longer modified after Save');
    check(await waitUntil(() => page.eval('!document.querySelector("[data-session-label=\\"E2E - Web Spot\\"] img")')), 'modified marker gone from the tab');
    const entry1 = (await api.call('fixturedefs.list', { manufacturer: MAN })).entries.find(e => e.model === MODEL);
    check(entry1.channelCount === 5 && entry1.modeCount === 1 && entry1.type === 'Moving Head', 'library entry: 5 channels, 1 mode, Moving Head (defRevision ' + entry1.defRevision + ')');
    /* save again after an edit: baseRevision = the defRevision the first save answered */
    await typeInto(page, '[data-fe="general-author"]', 'Web UI e2e 2');
    await waitUntil(async () => (await sessionOf(sid)).isModified === true);
    await clickEl(page, '[data-fe="tb-save"]');
    check(await waitUntil(async () => { const e = (await api.call('fixturedefs.list', { manufacturer: MAN })).entries.find(x => x.model === MODEL); return e && e.defRevision > entry1.defRevision && e.author === 'Web UI e2e 2'; }), 'second save accepted (defRevision advanced)');

    /* ================= 9. reload survival ================= */
    console.log('Reload');
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=fxeditor');
    check(await waitUntil(() => page.eval('!!document.querySelector("[data-session-label=\\"E2E - Web Spot\\"]")'), 20000), 'after a page reload the open session is back (session.list + session.get)');

    /* ================= 10. close + reopen from the Open picker, field by field ================= */
    console.log('Close + reopen from the Open picker');
    await clickEl(page, '[data-session-label="E2E - Web Spot"] [data-fe="session-close"]');
    check(await waitUntil(async () => (await api.call('fixturedefs.session.list')).sessions.length === 0), 'unmodified session closed without a prompt');
    await clickEl(page, '[data-fe="tb-open"]');
    await page.waitFor('!!document.querySelector("[data-fe=\\"open-dialog\\"]")', 5000);
    await typeInto(page, '[data-fe="open-search"]', 'E2E', 'none');
    await clickEl(page, '[data-manufacturer="E2E"]');
    check(await waitUntil(() => exists(page, '[data-model="Web Spot"]')), 'Open picker lists E2E / Web Spot');
    check((await textOf(page, '[data-model="Web Spot"] [data-fe="origin-badge"]')) === 'user', 'badge says "user"');
    await clickEl(page, '[data-model="Web Spot"]');
    await shot(page, 'fe-open-dialog');
    await dialogButton(page, 'Open');
    check(await waitUntil(async () => !!(await findSession(MAN, MODEL))), 'session.open from the picker');
    sid = (await findSession(MAN, MODEL)).sessionId;
    await page.waitFor('!!document.querySelector("[data-fe=\\"general-tab\\"]")', 5000);
    check(await valueOf(page, '[data-fe="general-manufacturer"]') === MAN && await valueOf(page, '[data-fe="general-model"]') === MODEL && await valueOf(page, '[data-fe="general-author"]') === 'Web UI e2e 2', 'General shows manufacturer / model / author');
    check(await page.eval('getComputedStyle(document.querySelector("[data-fe=\\"general-type\\"] [data-type=\\"Moving Head\\"]")).backgroundColor') !== await page.eval('getComputedStyle(document.querySelector("[data-fe=\\"general-type\\"] [data-type=\\"Dimmer\\"]")).backgroundColor'), 'type Moving Head highlighted');
    await tab(page, 'channels');
    check((await page.eval('Array.from(document.querySelectorAll("[data-channel-name]")).map(e => e.dataset.channelName).join(",")')) === 'Red 1,Green 1,Blue 1,White 1,Gobo', 'channel list: Red 1, Green 1, Blue 1, White 1, Gobo');
    await clickEl(page, '[data-channel-name="Gobo"]');
    check(await valueOf(page, '[data-fe="channel-name"]') === 'Gobo' && /Gobo/.test(await textOf(page, '[data-fe="channel-group"]')) && await valueOf(page, '[data-fe="channel-default"]') === '10', 'Gobo: name, group, default value 10');
    const capVals = await page.eval('Array.from(document.querySelectorAll("[data-cap-index]")).map(r => [r.querySelector("[data-fe=cap-min]").value, r.querySelector("[data-fe=cap-max]").value, r.querySelector("[data-fe=cap-name]").value].join(" ")).join("|")');
    check(capVals === '0 19 Gobo 1|20 39 Gobo 2 (star)|40 59 Gobo 3', 'capability table: ' + capVals);
    await clickEl(page, '[data-cap-index="0"] [data-fe="cap-name"]');
    check(/Color Macro/.test(await textOf(page, '[data-fe="cap-preset"]')) && (await valueOf(page, '[data-fe="cap-color1"]')) === '#ff8000', 'capability 1: Color Macro #ff8000');
    await clickEl(page, '[data-cap-index="2"] [data-fe="cap-name"]');
    check(/Standard/.test(await textOf(page, '[data-alias-index="0"] [data-fe="alias-mode"]')) && /White 1/.test(await textOf(page, '[data-alias-index="0"] [data-fe="alias-target"]')), 'capability 3: alias Standard -> White 1');
    await tab(page, 'modes');
    await clickEl(page, '[data-mode-name="Standard"]');
    check((await page.eval('Array.from(document.querySelectorAll("[data-slot-channel]")).map(e => e.dataset.slotChannel).join(",")')) === 'Red 1,Gobo,Green 1,Blue 1,White 1', 'mode slots in the saved order');
    check((await page.eval('document.querySelectorAll("[data-head-index]").length')) === 2, 'two emitters shown');
    check((await page.eval('document.querySelector("[data-fe=\\"mode-phy-override\\"]").getAttribute("aria-pressed")')) === 'true' && await valueOf(page, '[data-fe="mode-editor"] [data-fe="phy-powerConsumption"]') === '150', 'mode physical override: 150 W');
    await tab(page, 'physical');
    const phyVals = {}; for (const k of Object.keys(phy)) phyVals[k] = await valueOf(page, '[data-fe="physical-tab"] [data-fe="phy-' + k + '"]');
    check(Object.keys(phy).every(k => phyVals[k] === phy[k]), 'Physical tab shows every saved value');

    /* ================= 11. Export (download) + Import (upload) ================= */
    console.log('Export + Import');
    const exported = path.join(OUT, 'E2E-Web-Spot.qxf');
    if (fs.existsSync(exported)) fs.unlinkSync(exported);
    await clickEl(page, '[data-fe="tb-export"]');
    check(await waitUntil(() => fs.existsSync(exported) && fs.statSync(exported).size > 0, 8000), 'Export downloaded ' + exported);
    const xml = fs.existsSync(exported) ? fs.readFileSync(exported, 'utf8') : '';
    check(/<Manufacturer>E2E<\/Manufacturer>/.test(xml) && /<Model>Web Spot<\/Model>/.test(xml) && /<Type>Moving Head<\/Type>/.test(xml), 'QXF: manufacturer, model, type');
    check(/<Channel Name="Gobo"[^>]*Default="10"/.test(xml) && /Preset="ColorMacro" Res1="#ff8000"/i.test(xml) && /Gobo 2 \(star\)/.test(xml), 'QXF: Gobo channel, default 10, ColorMacro #ff8000, renamed capability');
    check(/<Alias Mode="Standard" Channel="Gobo" With="White 1"/.test(xml), 'QXF: alias');
    check(/<Mode Name="Standard">/.test(xml) && (xml.match(/<Head>/g) || []).length === 2, 'QXF: mode Standard with 2 heads');
    check(/<Bulb Type="LED" Lumens="5000" ColourTemperature="6500"/.test(xml) && /PowerConsumption="150"/.test(xml) && /PowerConsumption="200" DmxConnector="5-pin"/.test(xml), 'QXF: global physical + the mode override');
    const doc = await page.s.send('DOM.getDocument', {});
    const node = await page.s.send('DOM.querySelector', { nodeId: doc.root.nodeId, selector: '[data-fe="import-file"]' });
    await page.s.send('DOM.setFileInputFiles', { nodeId: node.nodeId, files: [exported] });
    check(await waitUntil(async () => (await api.call('fixturedefs.session.list')).sessions.filter(s => s.manufacturer === MAN && s.model === MODEL).length === 2), 'Import opened a second E2E / Web Spot session from the uploaded file');
    const imported = (await api.call('fixturedefs.session.list')).sessions.find(s => s.manufacturer === MAN && s.model === MODEL && s.sessionId !== sid);
    const impDef = (await sessionOf(imported.sessionId)).definition;
    check(impDef.channels.length === 5 && impDef.modes[0].heads.length === 2 && imported.isModified === true, 'imported session: 5 channels, 2 heads, marked modified');
    /* close it: modified -> prompt -> Discard */
    await clickEl(page, new Function('return document.querySelector(' + JSON.stringify('[data-session-id="' + imported.sessionId + '"] [data-fe="session-close"]') + ');'));
    check(await waitUntil(() => page.eval('Array.from(document.querySelectorAll("button")).some(b => b.textContent.trim() === "Discard")')), 'closing a modified session asks first (Save / Discard / Cancel)');
    await dialogButton(page, 'Discard');
    check(await waitUntil(async () => !(await api.call('fixturedefs.session.list')).sessions.some(s => s.sessionId === imported.sessionId)), 'Discard closed the imported session');

    /* ================= 12. fork a bundled definition ================= */
    console.log('Fork a bundled definition (' + FORK_MAN + ' / ' + FORK_MODEL + ')');
    await clickEl(page, '[data-fe="tb-open"]');
    await page.waitFor('!!document.querySelector("[data-fe=\\"open-dialog\\"]")', 5000);
    await typeInto(page, '[data-fe="open-search"]', FORK_MAN, 'none');
    await clickEl(page, '[data-manufacturer="' + FORK_MAN + '"]');
    await waitUntil(() => exists(page, '[data-model="' + FORK_MODEL + '"]'));
    check((await textOf(page, '[data-model="' + FORK_MODEL + '"] [data-fe="origin-badge"]')) === 'system', 'bundled model carries the "system" badge');
    await clickEl(page, '[data-model="' + FORK_MODEL + '"]');
    await dialogButton(page, 'Open');
    check(await waitUntil(() => exists(page, '[data-fe="system-banner"]')), 'bundled definition opens with the read-only banner');
    const forkSid = (await findSession(FORK_MAN, FORK_MODEL)).sessionId;
    await typeInto(page, '[data-fe="general-author"]', 'forked by e2e');
    await clickEl(page, '[data-fe="tb-save"]');
    check(await waitUntil(() => page.eval('document.body.textContent.indexOf("This is a bundled fixture definition, which cannot be overwritten") !== -1')), 'Save on a bundled definition offers the user copy instead');
    await dialogButton(page, 'Save as user copy');
    check(await waitUntil(async () => (await sessionOf(forkSid)).isUser === true), 'session.forkToUser: the session is a user copy now');
    check(await waitUntil(async () => !(await exists(page, '[data-fe="system-banner"]'))), 'banner gone');
    await clickEl(page, '[data-fe="tb-save"]');
    check(await waitUntil(async () => (await api.call('fixturedefs.list', { manufacturer: FORK_MAN })).entries.some(e => e.model === FORK_MODEL && e.isUser && e.author === 'forked by e2e')), 'fixturedefs.list shows ' + FORK_MODEL + ' as a user definition with the edited author');
    const forkFile = (await sessionOf(forkSid)).definition.sourceFile || '';
    check(forkFile.replace(/\//g, '\\').toLowerCase().startsWith(USER_DIR.toLowerCase()) && fs.existsSync(forkFile), 'user copy written into the sandbox user folder: ' + forkFile);
    await shot(page, 'fe-forked');
    /* remove the user copy again (Delete in the toolbar) */
    await clickEl(page, '[data-fe="tb-delete"]');
    await dialogButton(page, 'Delete');
    check(await waitUntil(async () => !(await api.call('fixturedefs.list', { manufacturer: FORK_MAN })).entries.some(e => e.model === FORK_MODEL && e.isUser)), 'Delete removed the user copy from the library');
    check(await waitUntil(() => !fs.existsSync(forkFile)), 'user copy file removed');
    check(await waitUntil(async () => (await api.call('fixturedefs.list', { manufacturer: FORK_MAN })).entries.some(e => e.model === FORK_MODEL && !e.isUser)), 'the bundled ' + FORK_MODEL + ' is back in the library (system)');
    await api.call('fixturedefs.session.close', { sessionId: forkSid });

    /* ================= 13. someone else saves first -> overwrite prompt; close -> Save ================= */
    console.log('Overwrite prompt + close with Save');
    const e2eEntry = async () => (await api.call('fixturedefs.list', { manufacturer: MAN })).entries.find(e => e.model === MODEL);
    await waitUntil(() => page.eval('!!document.querySelector("[data-session-label=\\"E2E - Web Spot\\"]")'));
    await clickEl(page, '[data-session-label="E2E - Web Spot"] > button');
    await tab(page, 'general'); /* each session remembers its last sub-tab */
    check(await waitUntil(() => exists(page, '[data-fe="general-tab"]')), 'back on the E2E session, General tab');
    const other = await api.call('fixturedefs.session.open', { manufacturer: MAN, model: MODEL });
    await api.call('fixturedefs.session.update', { sessionId: other.sessionId, baseRevision: 0, author: 'other client' });
    await api.call('fixturedefs.save', { sessionId: other.sessionId, baseRevision: other.baseRevision });
    await api.call('fixturedefs.session.close', { sessionId: other.sessionId });
    check((await e2eEntry()).author === 'other client', 'a second client saved the definition in between');
    await typeInto(page, '[data-fe="general-author"]', 'overwrite from UI');
    await clickEl(page, '[data-fe="tb-save"]');
    check(await waitUntil(() => page.eval('document.body.textContent.indexOf("Someone saved this definition since you opened it") !== -1')), 'Save asks before overwriting the newer library copy');
    await dialogButton(page, 'Overwrite');
    check(await waitUntil(async () => (await e2eEntry()).author === 'overwrite from UI'), 'Overwrite saved this session over it');
    await typeInto(page, '[data-fe="general-author"]', 'saved on close');
    await waitUntil(async () => (await sessionOf(sid)).isModified === true);
    await clickEl(page, '[data-session-label="E2E - Web Spot"] [data-fe="session-close"]');
    check(await waitUntil(() => page.eval('Array.from(document.querySelectorAll("button")).some(b => b.textContent.trim() === "Discard")')), 'closing the modified session asks first');
    await dialogButton(page, 'Save');
    check(await waitUntil(async () => (await e2eEntry()).author === 'saved on close'), 'Save in the close prompt saved the edit');
    check(await waitUntil(async () => !(await findSession(MAN, MODEL))), '... and closed the session');

    /* ================= 14. delete the E2E definition from the Open picker ================= */
    console.log('Delete E2E / Web Spot from the Open picker');
    await clickEl(page, '[data-fe="tb-open"]');
    await page.waitFor('!!document.querySelector("[data-fe=\\"open-dialog\\"]")', 5000);
    await typeInto(page, '[data-fe="open-search"]', 'E2E', 'none');
    await clickEl(page, '[data-manufacturer="E2E"]');
    await waitUntil(() => exists(page, '[data-model="Web Spot"]'));
    await clickEl(page, '[data-model="Web Spot"]');
    await dialogButton(page, 'Delete'); /* the picker's own Delete button (the last "Delete" in the page) */
    check(await waitUntil(() => page.eval('document.body.textContent.indexOf("Delete the user definition E2E - Web Spot?") !== -1')), 'Delete in the picker asks for confirmation');
    await dialogButton(page, 'Delete');
    check(await waitUntil(async () => (await api.call('fixturedefs.list', { manufacturer: MAN })).entries.length === 0), 'fixturedefs.list({manufacturer:"E2E"}) is empty after Delete');
    check(await waitUntil(() => !fs.existsSync(e2eFile)), 'E2E-Web-Spot.qxf removed from the sandbox user folder');
    check(api.events.some(e => e.topic === 'fixturedefs.deleted' && e.data.manufacturer === MAN), 'fixturedefs.deleted broadcast');
    check(await waitUntil(async () => (await exists(page, '[data-fe="open-dialog"]')) && !(await exists(page, '[data-model="Web Spot"]'))), 'back in the picker, Web Spot is gone from the list');
    await dialogButton(page, 'Cancel');

    /* ================= 14. entry point from the Add Fixtures dialog ================= */
    console.log('Entry point: Fixtures & Functions -> Add Fixtures -> Edit this definition');
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=fx');
    await page.waitFor('!!document.querySelector("button[aria-label=\\"Add fixtures\\"]") && !document.querySelector("button[aria-label=\\"Add fixtures\\"]").disabled', 30000);
    await clickEl(page, 'button[aria-label="Add fixtures"]');
    await page.waitFor('!!document.querySelector("[data-role=\\"fixture-editor-edit\\"]")', 8000);
    await clickEl(page, new Function('return Array.from(document.querySelectorAll("div")).find(d => d.children.length === 1 && d.textContent.trim() === "Generic" && d.style.cursor === "pointer") || null;'));
    await waitUntil(() => page.eval('Array.from(document.querySelectorAll("div")).some(d => d.textContent.trim() === "Generic RGB" && d.style.cursor === "pointer")'));
    await clickEl(page, new Function('return Array.from(document.querySelectorAll("div")).find(d => d.style.cursor === "pointer" && d.textContent.trim().indexOf("Generic RGB") === 0 && d.textContent.trim().indexOf("Generic RGBW") !== 0) || null;'));
    await sleep(300);
    await clickEl(page, '[data-role="fixture-editor-edit"]');
    check(await waitUntil(() => page.eval('!!document.querySelector("[data-session-label=\\"Generic - Generic RGB\\"]") && !!document.querySelector("[data-fe=\\"system-banner\\"]")'), 10000), '"Edit this definition" switched to the Fixture Editor with Generic - Generic RGB open');
    const rgb = await findSession('Generic', 'Generic RGB');
    if (rgb) await api.call('fixturedefs.session.close', { sessionId: rgb.sessionId });

    check(page.consoleErrors.length === 0, 'no console errors' + (page.consoleErrors.length ? ': ' + page.consoleErrors.join(' | ') : ''));
  } catch (e) {
    failures++;
    console.log('  FAIL exception: ' + (e.stack || e));
    try { await shot(page, 'fe-failure'); } catch (e2) { }
    if (page.consoleErrors.length) console.log('  console errors: ' + page.consoleErrors.join(' | '));
  } finally {
    await b.close();
    api.close();
  }
  const realAfter = snapshotDir(REAL_DIR);
  check(realAfter.length === realBefore.length && realAfter.join('\n') === realBefore.join('\n'), 'real user fixture folder untouched: ' + realAfter.length + ' files, same sizes and mtimes');
  console.log(failures ? failures + ' failure(s)' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})();
