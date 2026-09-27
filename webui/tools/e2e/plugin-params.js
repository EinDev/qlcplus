/**
 * End-to-end driver for per-patch plugin line parameters (webui/io/PatchProperties.jsx ->
 * io.patch.setParameters -> QLCIOPlugin::setParameter / getParameters -> <PluginParameters> in the .qxw)
 * against a sandbox whose only IO plugin is the engine tests' I/O stub:
 *
 *   .\dev-webui-sandbox.ps1 -Name plugparams -BuildDir .\build -WebUiRoot .\webui -ApiPort 9390 -WebUiPort 9391 -Plugins iopluginstub
 *   node webui/tools/e2e/plugin-params.js --sandbox C:\qlcsandbox\plugparams [--api 9390] [--web 9391] [--shots <dir>]
 *
 * Why the stub and not ArtNet / E1.31 / OSC: those send UDP (and the machine running these tests may have
 * listeners for them on 127.0.0.1), so they never go into a sandbox. The parameter path is generic: the
 * stub stores any key per universe / line through QLCIOPlugin's own parameter map, exactly like the
 * network plugins do after validating their keys (their key lists are checked against the plugin
 * sources in the editor itself, see PatchProperties.jsx's SPEC).
 *
 * What it drives, every step read back over a second raw API connection:
 *   patch a stub output and a stub input on one universe through the I/O screen's pickers; open the
 *   output's parameter editor; add a string, a number and a boolean key and an extra key; edit each of
 *   them (string, number, boolean checkbox); remove one (null = unset); close and reopen the editor
 *   and see the values; set a key on the input patch; core.project.saveAs into the sandbox and find
 *   the values in the <Output>/<Input> <PluginParameters> elements; clear the live values; reopen the
 *   saved copy with core.project.open and see them come back in the editor. The .qxw stores every
 *   parameter as text, so after the reopen numbers and booleans come back as strings (engine
 *   behaviour, Universe::loadXMLPluginParameters): compared as String(value).
 *
 * Exit code 1 when a check fails or the page logged a console error.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', process.env.E2E_API_PORT || 9390)), WEB_PORT = Number(opt('web', process.env.E2E_WEB_PORT || 9391));
const HOST = process.env.E2E_HOST || '[::1]';
const API = 'ws://' + HOST + ':' + API_PORT + '/';
const WEB = 'http://localhost:' + WEB_PORT + '/';
const SANDBOX = opt('sandbox', process.env.QLC_SANDBOX || '');
const SHOTS = opt('shots', process.env.QLC_SHOTS || path.join(os.tmpdir(), 'qlc-e2e-plugin-params'));
if (!SANDBOX || !/qlcsandbox/i.test(SANDBOX)) {
  console.error('--sandbox <dir> is required and must be a dev-webui-sandbox.ps1 folder (…\\qlcsandbox\\<name>)');
  process.exit(2);
}

const STUB = 'I/O Plugin Stub';
const U = 1;              // universe id the run patches ("Universe 2" of the test project)
const OUT_LINE = 1;       // stub output line "2: Stub 2"
const IN_LINE = 2;        // stub input line "3: Stub 3"
const ENGINE_KEY = 'UniverseChannels'; // set by the engine itself on every output line (Universe::dumpOutput)

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
          if (!f.ok && f.error && f.error.details && f.error.details.docRevision != null) this.rev = f.error.details.docRevision;
          if (p) f.ok ? p.res(f.result) : p.rej(Object.assign(new Error(f.error.message), f.error));
        } else if (f.type === 'event' && f.data && f.data.docRevision != null) this.rev = f.data.docRevision;
      };
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'plugin-params e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
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
async function waitCheck(fn, msg, timeout = 8000, extraFn) {
  let ok = false;
  try { ok = !!(await until(fn, msg, timeout)); } catch (e) { ok = false; }
  let extra; if (!ok && extraFn) { try { extra = await extraFn(); } catch (e) { extra = String(e); } }
  return check(ok, msg, extra);
}

/* ---------------------------------------------------------------- page helpers */
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const DLG = '[data-role="patch-properties"]';
const inDlg = (sel) => q(DLG + ' ' + sel);
async function clickFn(page, fnBody, what) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest', inline: 'nearest' }); })()`);
  await sleep(60);
  await page.click(new Function('return ' + fnBody));
}
async function typeInto(page, fnBody, text, what) {
  await clickFn(page, fnBody, what);
  await page.eval(`(function(){ const el = document.activeElement; if (el && el.select) el.select(); })()`);
  const k = { key: 'a', code: 'KeyA', windowsVirtualKeyCode: 65, modifiers: 2 };
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'rawKeyDown' }, k));
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, k));
  await page.s.send('Input.insertText', { text: String(text) });
  await page.key('Enter');
}
async function pickCombo(page, rootFn, wantedText, what) {
  const root = `(${rootFn})`;
  await clickFn(page, `${root} && ${root}.querySelector('button')`, what + ' (open)');
  await sleep(200);
  const opt = `(function(){ const r = ${root}; if (!r) return null; const bs = [...r.querySelectorAll('div > button')].filter(b => b !== r.firstElementChild); return bs.find(b => b.textContent.trim().startsWith(${JSON.stringify(wantedText)})); })()`;
  await clickFn(page, opt, what + ' -> ' + wantedText);
  await sleep(250);
}
async function shot(page, name) { const f = path.join(SHOTS, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); return f; }

/** The editor's current rows: {key: value shown} (text inputs by value, checkboxes as booleans). */
const rowsExpr = `(function(){ const d = ${q(DLG)}; if (!d) return null; const o = {};
  d.querySelectorAll('[data-param]').forEach(e => { o[e.getAttribute('data-param')] = e.tagName === 'INPUT' ? e.value : e.getAttribute('aria-pressed') === 'true'; });
  return o; })()`;

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev + ', sandbox ' + SANDBOX);
  const plugins = (await api.call('io.plugin.list')).plugins || [];
  if (!check(plugins.length === 1 && plugins[0].name === STUB, 'the sandbox has the I/O plugin stub and no other IO plugin', plugins.map(p => p.name))) { api.close(); process.exit(1); }

  const get = () => api.call('io.universe.get', { universeId: U });
  const outParams = async () => (((await get()).outputPatches || [])[0] || {}).parameters || {};
  const inParams = async () => ((await get()).inputPatch || {}).parameters || {};

  /* a known start: nothing patched on the universe this run uses, no parameters left on the stub lines
     (the stub keeps its per-universe map when a line is unpatched, so clear the keys first) */
  {
    const d = await get();
    for (const p of d.outputPatches || []) if (p.pluginName === STUB && Object.keys(p.parameters || {}).some(k => k !== ENGINE_KEY)) {
      const nul = {}; Object.keys(p.parameters).filter(k => k !== ENGINE_KEY).forEach(k => { nul[k] = null; });
      await api.structural('io.patch.setParameters', { universeId: U, patchType: 'output', index: p.index, parameters: nul });
    }
    if (d.inputPatch && d.inputPatch.pluginName === STUB && Object.keys(d.inputPatch.parameters || {}).length) {
      const nul = {}; Object.keys(d.inputPatch.parameters).forEach(k => { nul[k] = null; });
      await api.structural('io.patch.setParameters', { universeId: U, patchType: 'input', parameters: nul });
    }
    for (let i = (d.outputPatches || []).length - 1; i >= 0; i--) await api.structural('io.patch.remove', { universeId: U, direction: 'output', index: i });
    if (d.inputPatch) await api.structural('io.patch.remove', { universeId: U, direction: 'input' });
  }

  const browser = await launch({ headless: true });
  let page = await browser.open(WEB + '?ctx=io', { width: 1600, height: 1100 });
  const uni = (sel) => `document.querySelector('[data-universe="${U}"] ${sel}')`;
  const openEditor = async (direction, what) => {
    const btn = direction === 'input' ? uni('[data-role="input-properties"]') : uni('[data-output="0"] [data-role="output-properties"]');
    await clickFn(page, btn, what);
    await page.waitFor(`!!${q(DLG)}`, 5000);
    await sleep(200);
  };
  const closeEditor = async () => {
    await clickFn(page, `[...document.querySelectorAll('button')].reverse().find(b => b.textContent.trim() === 'Close')`, 'Close');
    await page.waitFor(`!${q(DLG)}`, 5000);
  };
  const addParam = async (key, value) => {
    await typeInto(page, inDlg('input[data-role="param-key"]'), key, 'parameter key');
    await typeInto(page, inDlg('input[data-role="param-value"]'), value, 'parameter value');
    await clickFn(page, inDlg('[data-role="param-add"]'), 'Set ' + key);
    /* the add row empties once the server answered: what an operator waits for before the next one */
    await page.waitFor(`(${inDlg('input[data-role="param-key"]')} || {}).value === ''`, 5000).catch(() => { throw new Error('the add row did not clear after Set ' + key); });
  };
  const editParam = (key, value) => typeInto(page, inDlg(`input[data-param="${key}"]`), value, 'edit ' + key);

  try {
    await page.waitFor(`!!${uni('[data-role="input-picker"]')}`, 30000);
    await sleep(500);

    /* ---- patch a stub output and a stub input through the pickers ---- */
    console.log('\n[patch the stub lines on universe ' + (U + 1) + ']');
    await pickCombo(page, uni('[data-output="add"] [data-role="output-add-picker"]'), STUB + ' — ' + (OUT_LINE + 1) + ':', 'output picker');
    await waitCheck(async () => { const p = ((await get()).outputPatches || [])[0]; return p && p.pluginName === STUB && p.output === OUT_LINE; },
      'output picker -> universe ' + (U + 1) + ' outputs to stub line ' + (OUT_LINE + 1), 8000, get);
    await pickCombo(page, uni('[data-role="input-picker"]'), STUB + ' — ' + (IN_LINE + 1) + ':', 'input picker');
    await waitCheck(async () => { const p = (await get()).inputPatch; return p && p.pluginName === STUB && p.input === IN_LINE; },
      'input picker -> universe ' + (U + 1) + ' reads stub line ' + (IN_LINE + 1), 8000, get);

    /* ---- output parameters: add one of each type ---- */
    console.log('\n[output line parameters]');
    await openEditor('output', 'output properties');
    check(await page.eval(`/No parameters set/.test(${q(DLG)}.textContent)`), 'a fresh line shows "No parameters set"');
    await waitCheck(() => page.eval(`(function(){ const e = ${inDlg('[data-engine-param="UniverseChannels"]')}; return !!e && /^[0-9]+$/.test(e.textContent.trim()) && !${inDlg('[data-remove-param="UniverseChannels"]')} && !${inDlg('[data-param="UniverseChannels"]')}; })()`),
      'the engine-managed UniverseChannels is shown read-only (no field, no trash button)');
    check(await page.eval(`/I\\/O Plugin Stub/.test(${q(DLG)}.textContent) && /Output 1 patch properties/.test(document.body.textContent)`), 'the editor names the plugin and the patch');
    await addParam('outputIP', '10.20.30.40');
    await waitCheck(async () => (await outParams()).outputIP === '10.20.30.40', 'Set outputIP = "10.20.30.40" (string) reaches the line', 5000, outParams);
    await addParam('port', '6454');
    await waitCheck(async () => (await outParams()).port === 6454, 'Set port = 6454 is stored as a number', 5000, outParams);
    await addParam('enabled', 'true');
    await waitCheck(async () => (await outParams()).enabled === true, 'Set enabled = true is stored as a boolean', 5000, outParams);
    await addParam('transmitMode', 'Full');
    await waitCheck(async () => (await outParams()).transmitMode === 'Full', 'Set transmitMode = "Full"', 5000, outParams);
    await waitCheck(async () => { const r = await page.eval(rowsExpr); return r && r.outputIP === '10.20.30.40' && r.port === '6454' && r.enabled === true && r.transmitMode === 'Full'; },
      'the editor lists the four keys with their values (boolean as a checkbox)', 5000, () => page.eval(rowsExpr));
    await waitCheck(() => page.eval(`/transmitMode set to "Full"/.test((${inDlg('[data-role="patch-status"]')} || {}).textContent || '')`), 'the status line confirms the last set (the stub kept the key)');
    await shot(page, 'plugin-params-added');

    /* ---- edit each type ---- */
    await editParam('outputIP', '10.20.30.41');
    await waitCheck(async () => (await outParams()).outputIP === '10.20.30.41', 'editing the string field -> outputIP = "10.20.30.41"', 5000, outParams);
    await editParam('port', '7000');
    await waitCheck(async () => (await outParams()).port === 7000, 'editing the number field keeps it a number -> port = 7000', 5000, outParams);
    await clickFn(page, inDlg('[data-param="enabled"]'), 'enabled checkbox');
    await waitCheck(async () => (await outParams()).enabled === false, 'the checkbox toggles the boolean -> enabled = false', 5000, outParams);
    await editParam('transmitMode', 'Partial');
    await waitCheck(async () => (await outParams()).transmitMode === 'Partial', 'editing -> transmitMode = "Partial"', 5000, outParams);

    /* ---- remove one (null = back to the plugin default) ---- */
    await clickFn(page, inDlg('[data-remove-param="transmitMode"]'), 'remove transmitMode');
    await waitCheck(async () => !('transmitMode' in (await outParams())), 'the trash button unsets transmitMode (io.patch.setParameters null)', 5000, outParams);
    await waitCheck(async () => { const r = await page.eval(rowsExpr); return r && !('transmitMode' in r) && Object.keys(r).length === 3; }, 'its row is gone', 5000, () => page.eval(rowsExpr));

    /* ---- close / reopen ---- */
    await closeEditor();
    await openEditor('output', 'reopen output properties');
    await waitCheck(async () => { const r = await page.eval(rowsExpr); return r && r.outputIP === '10.20.30.41' && r.port === '7000' && r.enabled === false && Object.keys(r).length === 3; },
      'reopened: outputIP 10.20.30.41, port 7000, enabled unchecked', 5000, () => page.eval(rowsExpr));
    await shot(page, 'plugin-params-reopened');
    await closeEditor();

    /* ---- input parameters ---- */
    console.log('\n[input line parameters]');
    await openEditor('input', 'input properties');
    check(await page.eval(`/Input patch properties/.test(document.body.textContent)`), 'the input editor opens');
    await addParam('inputUni', '3');
    await waitCheck(async () => (await inParams()).inputUni === 3, 'Set inputUni = 3 on the input line', 5000, inParams);
    await closeEditor();
    check(!('inputUni' in (await outParams())), 'input and output parameters stay separate');

    /* ---- save ---- */
    console.log('\n[saveAs into the sandbox]');
    const out = path.join(SANDBOX, 'plugin-params.qxw');
    await api.call('core.project.saveAs', { target: 'serverPath', path: out });
    const xml = fs.readFileSync(out, 'utf8');
    const uniXml = (xml.match(new RegExp('<Universe [^>]*ID="' + U + '"[^>]*>[\\s\\S]*?</Universe>')) || [''])[0];
    const outXml = (uniXml.match(/<Output [^>]*>[\s\S]*?<\/Output>/) || [''])[0];
    const inXml = (uniXml.match(/<Input [^>]*>[\s\S]*?<\/Input>/) || [''])[0];
    console.log('  ' + outXml.replace(/\s+/g, ' '));
    console.log('  ' + inXml.replace(/\s+/g, ' '));
    check(/Plugin="I\/O Plugin Stub"/.test(outXml) && /<PluginParameters [^>]*outputIP="10\.20\.30\.41"/.test(outXml) && /port="7000"/.test(outXml) && /enabled="false"/.test(outXml),
      'saved: <Output Plugin="I/O Plugin Stub"> carries <PluginParameters outputIP="10.20.30.41" port="7000" enabled="false">', outXml);
    check(!/transmitMode/.test(outXml), 'saved: the removed transmitMode is not in the file');
    check(/Plugin="I\/O Plugin Stub"/.test(inXml) && /<PluginParameters [^>]*inputUni="3"/.test(inXml), 'saved: <Input> carries <PluginParameters inputUni="3">', inXml);

    /* ---- clear the live values, then reopen the saved copy: whatever comes back is from the file ---- */
    console.log('\n[reopen the saved copy]');
    await api.structural('io.patch.setParameters', { universeId: U, patchType: 'output', index: 0, parameters: { outputIP: null, port: null, enabled: null } });
    await api.structural('io.patch.setParameters', { universeId: U, patchType: 'input', parameters: { inputUni: null } });
    check(Object.keys(await outParams()).filter(k => k !== ENGINE_KEY).length === 0 && Object.keys(await inParams()).length === 0, 'live parameters cleared before the reopen', [await outParams(), await inParams()]);
    await api.call('core.project.open', { source: 'path', path: out });
    await sleep(1500);
    await api.call('hello', { apiVersion: '1' }).then(r => { api.rev = r.docRevision; });
    await waitCheck(async () => { const p = await outParams(); return p.outputIP === '10.20.30.41' && String(p.port) === '7000' && String(p.enabled) === 'false'; },
      'core.project.open restores the output parameters from the file', 8000, outParams);
    await waitCheck(async () => String((await inParams()).inputUni) === '3', 'and the input parameter', 8000, inParams);

    await page.goto(WEB + '?ctx=io');
    await page.waitFor(`!!${uni('[data-output="0"] [data-role="output-properties"]')}`, 30000);
    await sleep(500);
    await openEditor('output', 'output properties after the reopen');
    await waitCheck(async () => { const r = await page.eval(rowsExpr); return r && r.outputIP === '10.20.30.41' && r.port === '7000' && String(r.enabled) === 'false' && Object.keys(r).length === 3; },
      'the editor shows the reloaded values (numbers / booleans come back as text from the .qxw)', 5000, () => page.eval(rowsExpr));
    await shot(page, 'plugin-params-after-reopen');
    await closeEditor();
    await openEditor('input', 'input properties after the reopen');
    await waitCheck(async () => { const r = await page.eval(rowsExpr); return r && r.inputUni === '3'; }, 'the input editor shows inputUni 3', 5000, () => page.eval(rowsExpr));
    await closeEditor();
    check(page.consoleErrors.length === 0, 'no console errors', page.consoleErrors);
  } catch (e) {
    failures.push('EXCEPTION ' + (e && e.stack || e));
    console.log('  EXCEPTION ' + (e && e.stack || e));
    if (page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
    try { await shot(page, 'plugin-params-failure'); } catch (x) { }
  } finally {
    await browser.close();
    api.close();
  }
  console.log('\n' + checks + ' checks, ' + failures.length + ' failed' + (failures.length ? ':\n  ' + failures.join('\n  ') : ''));
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error(e); process.exit(1); });
