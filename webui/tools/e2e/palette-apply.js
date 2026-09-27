/**
 * End-to-end driver for "Palettes: apply to the selected fixtures" (docs/webui-parity.md): the
 * palettes panel applies every palette type through palette.apply, i.e. the engine's
 * QLCPalette::valuesFromFixtures (the desktop's PaletteManager::previewPalette), fanning included.
 *
 *   $env:QLC_TEST_PROJECT = '<test project>'
 *   .\dev-webui-sandbox.ps1 -Name palapply -BuildDir .\build -WebUiRoot .\webui -ApiPort 9350 -WebUiPort 9351
 *   node webui/tools/e2e/palette-apply.js
 *   env: QLC_API (ws://127.0.0.1:9350/), QLC_WEB (http://localhost:9351/),
 *        QLC_SANDBOX (C:\qlcsandbox\palapply: screenshots)
 *
 * Needs the SF3 test project (its "Gobo Spot" fixtures: 16 bit pan / tilt over 540 degrees, a
 * dimmer, a strobe channel, a gobo wheel and a zoom). Four of them are selected in the tree; a
 * Shutter, a Position (pan + tilt in degrees), a Gobo, a Zoom and a Linear-fanned Dimmer palette are
 * applied (double-click and the "Apply to N selected" button) and the DMX is read back with
 * io.dmx.universe.get and compared with the desktop maths (ported below from
 * QLCPalette::valuesFromFixtures / Fixture::positionToValues). A Dimmer palette created in the
 * dialog at 50 % must be stored as DMX (percent * 2.55, like IntensityTool.qml). "Release
 * fixtures" must put the channels back. Exit code 1 when a check fails or the page logged a
 * console error.
 */
const fs = require('fs');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const API = process.env.QLC_API || 'ws://127.0.0.1:9350/';
const WEB = process.env.QLC_WEB || 'http://localhost:9351/';
const SANDBOX = process.env.QLC_SANDBOX || 'C:\\qlcsandbox\\palapply';
const SHOTS = path.join(SANDBOX, 'shots');

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
          if (p) f.ok ? p.res(f.result) : p.rej(Object.assign(new Error(f.error.code + ': ' + f.error.message), f.error));
        } else if (f.type === 'event' && f.data && f.data.docRevision != null) this.rev = f.data.docRevision;
      };
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'palette-apply e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
    });
  }
  call(method, params = {}) {
    const id = 'e-' + (this.next++);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  edit(method, params) { return this.call(method, Object.assign({ baseRevision: this.rev }, params)); }
  close() { try { this.ws.close(); } catch (e) { } }
}

/* ---------------------------------------------------------------- checks */
const failures = [];
function check(cond, msg, extra) {
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
const soon = (fn, what, timeout) => until(fn, what, timeout).catch(() => null);

/* ---------------------------------------------------------------- page helpers */
const CTRL = 2;
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const byText = (tag, text) => `[...document.querySelectorAll(${JSON.stringify(tag)})].reverse().find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
const leafText = (text, root) => `[...(${root ? q(root) : 'document'} || document).querySelectorAll('*')].filter(e => e.children.length === 0 && (e.textContent || '').trim() === ${JSON.stringify(text)}).pop()`;
const byTitle = (prefix) => `[...document.querySelectorAll('[title]')].filter(e => e.title.indexOf(${JSON.stringify(prefix)}) === 0).pop()`;
const dialogBtn = (text) => `[...document.querySelectorAll('button')].filter(b => b.textContent.trim() === ${JSON.stringify(text)}).pop()`;
const rowPart = (label, inner, root) => `(function(){ const r = [...(${root ? q(root) : 'document'} || document).querySelectorAll('div')].filter(d => d.children.length >= 2 && (d.children[0].textContent || '').trim() === ${JSON.stringify(label)}).pop(); return r ? r.querySelectorAll(${JSON.stringify(inner)}) : []; })()`;
async function clickFn(page, fnBody, what, opts) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'center', inline: 'center' }); })()`);
  await sleep(60);
  await page.click(new Function('return ' + fnBody), opts || {});
  await sleep(150);
}
async function dblClickFn(page, fnBody, what) {
  await page.waitFor(`!!(${fnBody})`, 8000).catch(() => { throw new Error('not found: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'center' }); })()`);
  const r = await page.rectOf(new Function('return ' + fnBody));
  const x = r.x + r.w / 2, y = r.y + r.h / 2;
  await page.mouse('mouseMoved', x, y);
  await page.mouse('mousePressed', x, y, { clickCount: 1 }); await page.mouse('mouseReleased', x, y, { clickCount: 1 });
  await page.mouse('mousePressed', x, y, { clickCount: 2 }); await page.mouse('mouseReleased', x, y, { clickCount: 2 });
  await sleep(200);
}
async function setInput(page, selectorFn, value, enter) {
  await page.eval(`(function(){ const el = (${selectorFn}); el.scrollIntoView({ block: 'center' }); el.focus();`
    + ' const proto = el.tagName === "SELECT" ? HTMLSelectElement.prototype : HTMLInputElement.prototype;'
    + ` Object.getOwnPropertyDescriptor(proto, "value").set.call(el, ${JSON.stringify(String(value))});`
    + ' el.dispatchEvent(new Event(el.tagName === "SELECT" ? "change" : "input", { bubbles: true })); })()');
  if (enter) await page.key('Enter');
  await sleep(200);
}
async function search(page, text) {
  const open = await page.eval(`!![...document.querySelectorAll('input')].find(i => i.placeholder === 'Search…')`);
  if (!open) await clickFn(page, `[...document.querySelectorAll('[title="Search fixtures and functions"]')].pop()`, 'search button');
  await page.eval(`(function(){ const el = [...document.querySelectorAll('input')].find(i => i.placeholder === 'Search…');
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, ${JSON.stringify(text)});
    el.dispatchEvent(new Event('input', { bubbles: true })); el.dispatchEvent(new KeyboardEvent('keydown', { key: 'a', bubbles: true })); })()`);
  await sleep(500);
}
const PANEL_PROBE = {
  'Fixture tools': `!!document.querySelector('[data-ff-tools-title]') || document.body.innerText.indexOf('Select one or more fixtures in the tree') !== -1`,
  'Palettes': `!![...document.querySelectorAll('*')].find(e => e.children.length === 0 && /^Palettes( \\(\\d+\\))?$/.test(e.textContent.trim()))`
};
async function panel(page, title, open = true) {
  const sel = JSON.stringify('[title^="' + title + '"]');
  await page.waitFor(`!!document.querySelector(${sel})`, 5000);
  if (!!(await page.eval(PANEL_PROBE[title])) === open) return;
  await page.eval(`[...document.querySelectorAll(${sel})].pop().click()`);
  await page.waitFor(open ? PANEL_PROBE[title] : '!(' + PANEL_PROBE[title] + ')', 5000);
  await sleep(300);
}
async function shot(page, name) { const f = path.join(SHOTS, 'pal-' + name + '.png'); await page.screenshot(f); console.log('  shot ' + f); }

/* ---------------------------------------------------------------- the desktop maths */
/** Fixture::positionToValues: 16 bit = (deg * 65535 / max) truncated; [MSB, LSB] */
const pos16 = (deg, max) => { const v = Math.trunc((deg * 65535) / (max || 360)); return [v >> 8, v & 0xff]; };
/** QLCPalette::valuesFromFixtures, Dimmer with Linear fanning (amount 100 %): per fixture in
    layout (X ascending) order, factor = progress, value = int((fan - value) * factor + value). */
function fannedDimmer(value, fanValue, idsInLayoutOrder) {
  const out = {}; let progress = 0;
  for (const id of idsInLayoutOrder) { out[id] = Math.trunc((fanValue - value) * progress + value); progress += 1 / (idsInLayoutOrder.length - 1); }
  return out;
}

/* ---------------------------------------------------------------- the run */
async function main() {
  if (!/qlcsandbox/i.test(SANDBOX)) { console.log('refusing: QLC_SANDBOX must be a C:\\qlcsandbox\\<name> folder'); process.exit(2); }
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev);

  const fixtures = (await api.call('fixtures.list')).fixtures;
  const gobos = fixtures.filter(f => f.manufacturer === 'Gobo Spot').sort((a, b) => a.universe - b.universe || a.address - b.address).slice(0, 4);
  if (gobos.length < 4) { console.log('the test project needs four "Gobo Spot" fixtures'); process.exit(2); }
  const details = {};
  for (const f of gobos) details[f.id] = await api.call('fixtures.get', { fixtureId: f.id });
  const d0 = details[gobos[0].id];
  const chIdx = (name) => d0.channelList.find(c => c.name === name).index;
  const PAN = chIdx('Pan'), TILT = chIdx('Tilt'), DIM = chIdx('Dimmer'), STROBE = chIdx('Strobe'), GOBO = chIdx('GOBO'), ZOOM = chIdx('Zoom'), RED = chIdx('Red');
  const addr = (id, ch) => details[id].address + ch;
  const universes = [...new Set(gobos.map(f => details[f.id].universe))];
  const readDmx = async () => { const out = {}; for (const u of universes) out[u] = (await api.call('io.dmx.universe.get', { universeId: u })).values; return (id, ch) => out[details[id].universe][addr(id, ch)]; };

  /* setup: monitor positions for the fan (X order differs from the selection order), palettes */
  const xs = [23000, 20000, 21500, 24500];
  await api.edit('fixtures.monitor.setPlacement', { items: gobos.map((f, i) => ({ fixtureId: f.id, position: { x: xs[i], y: 0, z: 20000 }, rotation: { x: 0, y: 0, z: 0 }, locked: false, hidden: false })) });
  const layoutOrder = gobos.map((f, i) => [f.id, xs[i]]).sort((a, b) => a[1] - b[1]).map(p => p[0]);
  for (const old of (await api.call('palette.list')).palettes.filter(x => /^E2E Apply /.test(x.name))) await api.edit('palette.delete', { paletteId: old.id });
  const mk = async (type, name, values, fanning) => {
    const r = await api.edit('palette.create', Object.assign({ type, name, values }, fanning ? { fanning } : {}));
    return { id: r.paletteId, name, type, values };
  };
  const STROBE_SLOW_FAST = 9; // QLCCapability::StrobeSlowToFast
  const pShutter = await mk('Shutter', 'E2E Apply Shutter', [STROBE_SLOW_FAST, 50]);
  const pPos = await mk('PanTilt', 'E2E Apply Position', [270, 135]);
  const pGobo = await mk('Gobo', 'E2E Apply Gobo', [64]);
  const pZoom = await mk('Zoom', 'E2E Apply Zoom', [20]);
  const pColour = await mk('Color', 'E2E Apply Colour', ['#ff8000']);
  const p3d = await mk('Position3D', 'E2E Apply Point', [22, 0, 25]); // metres, in front of the four
  const pFan = await mk('Dimmer', 'E2E Apply Fan', [0], { type: 'Linear', layout: 'XAscending', amount: 100, value: 255 });

  const browser = await launch({ headless: true });
  const page = await browser.open(WEB + '?ctx=fx', { width: 1700, height: 1000 });
  try {
    await page.waitFor(`!!document.querySelector('[data-ff-tree]')`, 30000);
    await page.waitFor(`document.body.innerText.indexOf(${JSON.stringify(gobos[0].name)}) !== -1 || !!document.querySelector('[title="Search fixtures and functions"]')`, 15000);

    /* select the four Gobo Spots in the tree */
    await search(page, gobos[0].name.replace(/\s*\[\d+\]$/, ''));
    for (let i = 0; i < gobos.length; i++) await clickFn(page, leafText(gobos[i].name, '[data-ff-tree]'), 'tree ' + gobos[i].name, i ? { modifiers: CTRL } : undefined);
    await panel(page, 'Palettes');
    await page.waitFor(`!!(${byText('button', 'Apply to 4 selected')}) || true`, 3000);
    const before = await readDmx();

    const listRow = (name) => leafText(name);
    const status = () => page.eval(`(function(){ const e = [...document.querySelectorAll('*')].filter(e => e.children.length === 0 && /^(Applied|"E2E|Could not|This server)/.test((e.textContent || '').trim())).pop(); return e ? e.textContent.trim() : ''; })()`);
    const expectDmx = async (what, expected) => {
      const got = await soon(async () => { const g = await readDmx(); return expected.every(([id, ch, v]) => g(id, ch) === v) ? g : null; }, what);
      const g = got || await readDmx();
      check(!!got, what, got ? undefined : expected.filter(([id, ch, v]) => g(id, ch) !== v).map(([id, ch, v]) => ({ fixture: id, channel: ch, want: v, got: g(id, ch) })));
    };

    // Shutter (double-click): Strobe slow -> fast at 50 % of the 1..127 capability = 1 + 126 * 50 / 100
    console.log('\n[shutter]');
    await dblClickFn(page, listRow(pShutter.name), pShutter.name);
    await expectDmx('Shutter "strobe slow>fast 50%" -> strobe channel 64 on all four', gobos.map(f => [f.id, STROBE, 64]));
    check(/^Applied "E2E Apply Shutter" to 4 fixtures, 4 channels/.test(await status()), 'status line reports 4 fixtures / 4 channels', await status());

    // Position: pan 270 / tilt 135 degrees of 540 (the fixtures' focusPanMax / focusTiltMax)
    console.log('\n[position]');
    const [pm, pl] = pos16(270, d0.physical.focusPanMax), [tm, tl] = pos16(135, d0.physical.focusTiltMax);
    await dblClickFn(page, listRow(pPos.name), pPos.name);
    await expectDmx('Position 270° / 135° -> pan ' + pm + '/' + pl + ', tilt ' + tm + '/' + tl + ' (16 bit)',
      gobos.flatMap(f => [[f.id, PAN, pm], [f.id, PAN + 1, pl], [f.id, TILT, tm], [f.id, TILT + 1, tl]]));

    // Gobo: the engine now maps the gobo wheel (was an expected-fail before)
    console.log('\n[gobo]');
    await dblClickFn(page, listRow(pGobo.name), pGobo.name);
    await expectDmx('Gobo 64 -> gobo wheel 64 on all four', gobos.map(f => [f.id, GOBO, 64]));

    // Zoom through the "Apply to N selected" button. The Gobo Spot definition has a 0..0 degree
    // lens, so Fixture::zoomToValues has no range to map to: assert the channel the server
    // reports is what the output carries (the desktop runs the very same code).
    console.log('\n[zoom]');
    await clickFn(page, listRow(pZoom.name), pZoom.name);
    await clickFn(page, byText('button', 'Apply to 4 selected'), 'Apply to 4 selected');
    check(!!await soon(async () => /^Applied "E2E Apply Zoom" to 4 fixtures/.test(await status()), 'zoom status'), 'the button applied it (status line)', await status());
    const zr =await api.call('palette.apply', { paletteId: pZoom.id, fixtureIds: gobos.map(f => f.id) });
    const zoomCh = zr.channels.filter(c => c.channel === ZOOM);
    check(zoomCh.length === 4, 'Zoom writes the zoom channel of all four', zr.channels);
    await expectDmx('Zoom: the output carries the engine\'s zoom values (' + zoomCh.map(c => c.value).join(',') + ')', zoomCh.map(c => [c.fixtureId, ZOOM, c.value]));

    // Colour: RGB straight from the palette; Position 3D: the engine aims every fixture at the
    // point (metres) from its monitor position - the output must carry exactly what it computed
    console.log('\n[colour / position 3D]');
    await dblClickFn(page, listRow(pColour.name), pColour.name);
    await expectDmx('Colour #ff8000 -> R 255 / G 128 / B 0', gobos.flatMap(f => [[f.id, RED, 255], [f.id, RED + 1, 128], [f.id, RED + 2, 0]]));
    await dblClickFn(page, listRow(p3d.name), p3d.name);
    check(!!await soon(async () => /^Applied "E2E Apply Point"/.test(await status()), 'p3d status'), 'Position 3D applied (status line)', await status());
    const p3r = await api.call('palette.apply', { paletteId: p3d.id, fixtureIds: gobos.map(f => f.id) });
    check(gobos.every(f => p3r.channels.some(c => c.fixtureId === f.id && c.channel === PAN) && p3r.channels.some(c => c.fixtureId === f.id && c.channel === TILT)),
      'Position 3D writes pan and tilt of all four', p3r.channels.length);
    await expectDmx('Position 3D: the output carries the engine\'s pan / tilt', p3r.channels.map(c => [c.fixtureId, c.channel, c.value]));

    // Fanned Dimmer: 0 -> 255 Linear, X ascending over the fixtures' monitor positions
    console.log('\n[fanned dimmer]');
    const fan = fannedDimmer(0, 255, layoutOrder);
    await dblClickFn(page, listRow(pFan.name), pFan.name);
    await expectDmx('fanned Dimmer 0 -> 255 in X order: ' + layoutOrder.map(id => id + '=' + fan[id]).join(' '), gobos.map(f => [f.id, DIM, fan[f.id]]));
    check(new Set(Object.values(fan)).size === 4, 'four different dimmer levels', fan);
    await shot(page, 'applied');

    // Dimmer created in the dialog at 50 % is stored as DMX (IntensityTool.qml: percent * 2.55)
    console.log('\n[dimmer units]');
    await clickFn(page, byTitle('Create a palette'), 'create palette');
    await page.waitFor(`!!document.querySelector('[data-e2e=palette-create]')`, 5000);
    await clickFn(page, `${rowPart('Type', 'button', '[data-e2e=palette-create]')}[0]`, 'type combo');
    await clickFn(page, `[...(${rowPart('Type', 'button', '[data-e2e=palette-create]')})].find(b => b.textContent.trim() === 'Dimmer')`, 'type Dimmer');
    await setInput(page, q('[data-e2e=palette-name]'), 'E2E Apply Half');
    await setInput(page, `${rowPart('Level', 'input', '[data-e2e=palette-create]')}[0]`, 50, true);
    await shot(page, 'create-dimmer');
    await clickFn(page, dialogBtn('Create'), 'Create');
    const half = await soon(async () => (await api.call('palette.list')).palettes.find(x => x.name === 'E2E Apply Half'), 'created');
    const halfD = half ? await api.call('palette.get', { paletteId: half.id }) : null;
    check(halfD && Math.abs(Number(halfD.values[0]) - 127.5) < 0.01, 'Dimmer 50 % is stored as DMX 127.5 (the desktop\'s percent * 2.55)', halfD && halfD.values);
    if (halfD) {
      await dblClickFn(page, listRow('E2E Apply Half'), 'E2E Apply Half');
      const want = (await api.call('palette.apply', { paletteId: half.id, fixtureIds: gobos.map(f => f.id) })).channels.find(c => c.channel === DIM);
      check(want && (want.value === 127 || want.value === 128), 'the engine writes it as DMX ' + (want && want.value));
      await expectDmx('and the output carries it', gobos.map(f => [f.id, DIM, want ? want.value : -1]));
    }

    // Release fixtures (the live tools) clears every override again
    console.log('\n[release]');
    await panel(page, 'Fixture tools');
    await clickFn(page, byText('button', 'Release fixtures'), 'Release fixtures');
    await expectDmx('Release fixtures restores the channels', gobos.flatMap(f => [PAN, PAN + 1, TILT, TILT + 1, DIM, STROBE, GOBO, ZOOM, RED, RED + 1, RED + 2].map(ch => [f.id, ch, before(f.id, ch)])));
    await panel(page, 'Palettes');
    await shot(page, 'final');
  } catch (e) {
    await shot(page, 'failure').catch(() => {});
    failures.push('ERROR ' + e.message);
    console.log('ERROR ' + e.stack);
  } finally {
    const errs = page.consoleErrors.filter(e => !/favicon/i.test(e));
    check(errs.length === 0, 'no console errors', errs.slice(0, 5));
    await browser.close();
    api.close();
  }

  console.log('\n' + (failures.length ? failures.length + ' FAILED:\n  ' + failures.join('\n  ') : 'ALL CHECKS PASSED'));
  process.exit(failures.length ? 1 : 0);
}

main().catch(e => { console.error('ERROR ' + e.stack); process.exit(1); });
