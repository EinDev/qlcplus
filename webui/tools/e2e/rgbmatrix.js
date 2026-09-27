// End-to-end check of the RGB Matrix editor against a sandbox instance (dev-webui-sandbox.ps1).
//
//   .\dev-webui-sandbox.ps1 -Name rgb -BuildDir .\build -WebUiRoot .\webui -ApiPort 9130 -WebUiPort 9131
//   node webui/tools/e2e/rgbmatrix.js [apiPort] [webUiPort] [sandboxDir]
//
// Drives headless Chrome through webui/tools/cdp.js like an operator would (clicks, typed text,
// the design-system combo boxes) and confirms every change by reading the server back over a
// second, plain WebSocket API connection (functions.get) and finally through the .qxw written by
// core.project.saveAs into the sandbox directory. Exits non-zero on the first failed assertion or
// on any browser console error. Writes rgbmatrix-editor.png next to the sandbox log for a look.

const path = require('path'), fs = require('fs');
const { launch, sleep } = require('../cdp.js');

const API_PORT = Number(process.argv[2] || 9130);
const WEB_PORT = Number(process.argv[3] || 9131);
const SANDBOX = process.argv[4] || 'C:\\qlcsandbox\\rgb';
const FUNCTION_NAME = 'Tilt Bar Chaser REd'; // SF3: RGBMatrix, script "Waves", group 14 "Tilt bars" (42 x 1)

function assert(cond, msg) { if (!cond) throw new Error('ASSERT: ' + msg); }
function eq(a, b, msg) { if (a !== b) throw new Error('ASSERT: ' + msg + ' — got ' + JSON.stringify(a) + ', expected ' + JSON.stringify(b)); }

/* ---- a second API client, so nothing the UI does is trusted without a read-back ---- */
class Api {
  constructor(port) { this.port = port; this.id = 0; this.pending = new Map(); }
  async open() {
    this.ws = new WebSocket('ws://127.0.0.1:' + this.port + '/');
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.type === 'response' && this.pending.has(m.id)) {
        const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id);
        m.ok ? res(m.result) : rej(new Error(m.error.code + ': ' + m.error.message));
      }
    };
    await this.call('hello', { apiVersion: '1', clientName: 'rgbmatrix e2e' });
  }
  call(method, params = {}) {
    const id = 'e2e-' + (++this.id);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  close() { try { this.ws.close(); } catch (e) { } }
}

/* ---- page helpers ---- */
// The design-system CustomComboBox is a div showing the current label; clicking it opens a popup
// whose entries are plain elements with the label text. Pick one by clicking, as an operator would.
// cdp.js stringifies the finder function, so values are inlined into its source rather than closed over.
function leafByText(scope, text, last) {
  return new Function('const root = document.querySelector(' + JSON.stringify(scope) + ');'
    + ' const all = Array.from(root.querySelectorAll("*")).filter(el => el.children.length === 0 && el.textContent.trim() === ' + JSON.stringify(text) + ');'
    + ' return ' + (last ? 'all[all.length - 1]' : 'all[0]') + ';');
}
async function pickCombo(page, currentLabel, wantedLabel, scope = '[data-rgb="editor"]') {
  await page.click(leafByText(scope, currentLabel));
  await sleep(200);
  /* the popup entry comes after the combo's own label in DOM order; long lists scroll, so bring it into view first */
  await page.eval('(' + leafByText(scope, wantedLabel, true).toString() + ')().scrollIntoView({ block: "nearest" })');
  await sleep(100);
  await page.click(leafByText(scope, wantedLabel, true));
  await sleep(200);
}
// React-controlled inputs: set through the native setter and fire the event React listens to.
async function setInput(page, selector, value, commitWithEnter) {
  await page.eval('(function(){ const el = document.querySelector(' + JSON.stringify(selector) + '); el.focus();'
    + ' const proto = el.tagName === "SELECT" ? HTMLSelectElement.prototype : HTMLInputElement.prototype;'
    + ' Object.getOwnPropertyDescriptor(proto, "value").set.call(el, ' + JSON.stringify(value) + ');'
    + ' el.dispatchEvent(new Event(el.tagName === "SELECT" ? "change" : "input", { bubbles: true })); })()');
  if (commitWithEnter) await page.key('Enter');
  await sleep(120);
}
async function textOf(page, selector) { return page.eval('document.querySelector(' + JSON.stringify(selector) + ').value'); }
async function settle(page, ms = 500) { await sleep(ms); }

(async () => {
  const api = new Api(API_PORT);
  await api.open();
  const b = await launch();
  let failed = null;
  try {
    const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=fx');
    /* connected: the SF3 function list has arrived */
    await page.waitFor(() => Array.from(document.querySelectorAll('*')).some(el => el.children.length === 0 && el.textContent.trim() === 'Tilt Bar Chaser REd'), 20000);
    console.log('connected, function tree loaded');

    /* find the function id on the server side */
    const list = await api.call('functions.list');
    const fn = (list.functions || []).find(f => f.name === FUNCTION_NAME && f.type === 'RGBMatrix');
    assert(fn, 'SF3 has the RGBMatrix "' + FUNCTION_NAME + '"');
    const fid = String(fn.id);
    const get = async () => (await api.call('functions.get', { functionId: fid })).typeDetail.config;
    const original = await get();
    console.log('original config:', JSON.stringify(original));

    /* open the editor */
    await page.eval(() => { const el = Array.from(document.querySelectorAll('*')).find(el => el.children.length === 0 && el.textContent.trim() === 'Tilt Bar Chaser REd'); el.scrollIntoView({ block: 'center' }); });
    await page.click(() => Array.from(document.querySelectorAll('*')).find(el => el.children.length === 0 && el.textContent.trim() === 'Tilt Bar Chaser REd'));
    await page.waitFor('!!document.querySelector("[data-rgb=editor]")', 10000);
    await page.waitFor('!!document.querySelector("canvas[data-rgb=preview]")', 10000);
    console.log('editor open, preview canvas present');

    /* 1. the preview animates: the step index moves on its own (duration 50 ms -> every poll) */
    const steps = [];
    for (let i = 0; i < 8; i++) { steps.push(await page.eval('document.querySelector("canvas[data-rgb=preview]").getAttribute("data-step")')); await sleep(150); }
    const stepsCount = await page.eval('document.querySelector("canvas[data-rgb=preview]").getAttribute("data-steps")');
    console.log('preview steps observed:', steps.join(','), 'of', stepsCount);
    assert(new Set(steps).size >= 3, 'preview step index advances while the editor is open');
    const canvasW = await page.eval('document.querySelector("canvas[data-rgb=preview]").width');
    eq(Number(stepsCount) > 1, true, 'Waves on 42x1 has several steps');
    assert(canvasW > 100, 'preview canvas has a size');

    /* 2. colour slot 1 via the typed hex field (the native colour picker cannot be driven headless) */
    const acceptedBefore = original.algorithm.acceptedColors;
    eq(acceptedBefore, 2, 'Waves accepts two colours');
    await setInput(page, '[data-rgb="hex0"]', '00ff00', true);
    await settle(page);
    eq((await get()).colors[0], '#00ff00', 'functions.get colors[0] after typing 00ff00');
    await setInput(page, '[data-rgb="hex1"]', '0000ff', true);
    await settle(page);
    eq((await get()).colors[1], '#0000ff', 'functions.get colors[1] after typing 0000ff');
    /* reset colour 2 with its X button */
    await page.click(() => document.querySelector('[data-rgb="hex1"]').nextElementSibling);
    await settle(page);
    eq((await get()).colors[1], null, 'colors[1] is unset after the reset button');
    console.log('colour slots ok');

    /* 3. a script property (Waves "direction", a list -> combo box) */
    await page.waitFor('!!document.querySelector("[data-rgb=script-properties]")', 5000);
    const dirBefore = (await get()).algorithm.scriptProperties.find(p => p.name === 'direction').value;
    const dirWanted = dirBefore === 'Left' ? 'Right' : 'Left';
    await pickCombo(page, dirBefore, dirWanted, '[data-rgb="script-properties"]');
    await settle(page, 800);
    eq((await get()).algorithm.scriptProperties.find(p => p.name === 'direction').value, dirWanted, 'script property "direction" changed through the combo');
    /* a range property (spin box: every typed value is committed) */
    const tailInput = await page.eval(() => { const rows = document.querySelector('[data-rgb=script-properties]'); const inputs = Array.from(rows.querySelectorAll('input')); const i = inputs.findIndex(el => el.type !== 'color' && /^\d+$/.test(el.value)); if (i < 0) return null; inputs[i].setAttribute('data-rgb', 'prop-range'); return inputs[i].value; });
    assert(tailInput != null, 'Waves has a numeric (range) property field');
    await setInput(page, '[data-rgb="prop-range"]', '37', true);
    await settle(page, 800);
    eq((await get()).algorithm.scriptProperties.find(p => p.name === 'taillength').value, '37', 'script property "taillength" is 37');
    console.log('script properties ok');

    /* 4. control mode + blend mode combos */
    await pickCombo(page, 'RGB', 'Dimmer');
    await settle(page);
    eq((await get()).controlMode, 'dimmer', 'controlMode after picking Dimmer');
    await pickCombo(page, 'Default (HTP)', 'Additive');
    await settle(page);
    eq((await get()).blendMode, 'Additive', 'blendMode after picking Additive');
    console.log('modes ok');

    /* 5. switch to the built-in Text algorithm and edit its parameters */
    await setInput(page, '[data-rgb="algorithm"]', 'Text');
    await page.waitFor('!!document.querySelector("[data-rgb=text]")', 5000);
    await settle(page);
    eq((await get()).algorithm.type, 'text', 'algorithm switched to Text');
    await setInput(page, '[data-rgb="text"]', 'HELLO');
    await settle(page, 700);
    eq((await get()).algorithm.text, 'HELLO', 'text parameter');
    await setInput(page, '[data-rgb="font-family"]', 'Arial', true);
    await settle(page, 700);
    eq((await get()).algorithm.font.family, 'Arial', 'font family');
    const ANIM = { staticLetters: 'Letters', horizontal: 'Horizontal', vertical: 'Vertical' };
    const animBefore = (await get()).algorithm.animationStyle; // RGBText's own default, whatever it is
    const animWanted = animBefore === 'vertical' ? 'staticLetters' : 'vertical';
    await pickCombo(page, ANIM[animBefore], ANIM[animWanted]);
    await settle(page, 700);
    eq((await get()).algorithm.animationStyle, animWanted, 'text animation style');
    /* the preview still renders with the text algorithm */
    await page.waitFor('!!document.querySelector("canvas[data-rgb=preview]")', 5000);
    console.log('text algorithm ok');

    /* 6. switch to a script with an apiVersion-3 colour set (Plasma) and back to a colour-less one */
    await setInput(page, '[data-rgb="algorithm"]', 'Plasma');
    await page.waitFor('!!document.querySelector("[data-rgb=script-properties]")', 5000);
    await settle(page);
    const plasma = await get();
    eq(plasma.algorithm.scriptName, 'Plasma', 'algorithm switched to the Plasma script');
    const slotCount = await page.eval('document.querySelectorAll("[data-rgb^=hex]").length');
    eq(slotCount, plasma.algorithm.acceptedColors, 'colour slots shown == acceptedColors');
    console.log('Plasma shows', slotCount, 'colour slots');

    /* 7. fixture group switch */
    const groups = (await api.call('fixtures.group.list')).groups;
    const other = groups.find(g => String(g.id) !== String(original.fixtureGroupId) && g.size && g.size.rows > 1);
    assert(other, 'another fixture group with more than one row exists');
    const curGroup = groups.find(g => String(g.id) === String(original.fixtureGroupId));
    const label = (g) => g.name + '  ' + g.size.columns + ' x ' + g.size.rows;
    await pickCombo(page, label(curGroup), label(other));
    await settle(page, 800);
    eq((await get()).fixtureGroupId, String(other.id), 'fixture group switched');
    await page.waitFor('(function(){ const c = document.querySelector("canvas[data-rgb=preview]"); return c && c.height > 20; })()', 8000);
    console.log('fixture group ok, preview resized');

    /* 8. save and grep the XML */
    const out = path.join(SANDBOX, 'out.qxw');
    try { fs.unlinkSync(out); } catch (e) { }
    await api.call('core.project.saveAs', { target: 'serverPath', path: out });
    await sleep(500);
    const xml = fs.readFileSync(out, 'utf8');
    const block = xml.slice(xml.indexOf('<Function ID="' + fid + '"'), xml.indexOf('</Function>', xml.indexOf('<Function ID="' + fid + '"')));
    assert(block.includes('<Algorithm Type="Script">Plasma</Algorithm>'), 'saved XML has the Plasma algorithm');
    assert(block.includes('<FixtureGroup>' + other.id + '</FixtureGroup>'), 'saved XML has the new fixture group');
    assert(block.includes('<ControlMode>Dimmer</ControlMode>'), 'saved XML has ControlMode Dimmer');
    assert(block.includes('BlendMode="Additive"'), 'saved XML has BlendMode Additive');
    console.log('saved XML ok:', out);

    /* screenshot for the report */
    const shot = path.join(SANDBOX, 'rgbmatrix-editor.png');
    await page.screenshot(shot);
    console.log('screenshot:', shot);

    if (page.consoleErrors.length) { console.log('CONSOLE ERRORS:\n' + page.consoleErrors.join('\n')); failed = new Error('console errors'); }
    else console.log('no console errors');
  } catch (e) {
    failed = e;
    try { const p = b.pages[0]; if (p) { await p.screenshot(path.join(SANDBOX, 'rgbmatrix-failure.png')); console.log('console:', p.consoleErrors.join('\n')); } } catch (e2) { }
  } finally {
    api.close();
    await b.close();
  }
  if (failed) { console.error('FAILED:', failed.message); process.exit(1); }
  console.log('PASS');
})();
