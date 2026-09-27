/**
 * End-to-end driver for the "VC / Show Manager leftovers" slice against a sandbox instance
 * (dev-webui-sandbox.ps1) in headless Chrome:
 *  - Virtual Console page Width / Height (vc.page.setSize), widget background image (a PNG written
 *    into the sandbox folder, picked with the server file browser, rendered from
 *    vc.widget.getBackgroundImage) and z-order (raise / front / back)
 *  - Show Manager preview at the cursor (functions.show.preview / endPreview) with the DMX read
 *    back from io.dmx.universe.get, track Spout output size + the mismatch prompt
 *  - palettes: a Pan palette in degrees applied to a moving head (DMX = the fixture's degree
 *    mapping), a Gobo palette that survives saveAs + core.project.open
 *  - the input channel editor's per-type sensitivity range
 *
 *   node webui/tools/e2e/vc-show-leftovers.js --sandbox <sandbox dir> [--api 9310] [--web 9311] [--shots <dir>]
 *
 * <sandbox dir> is the dev-webui-sandbox.ps1 folder of this instance: the test image is written
 * there and core.project.saveAs writes <sandbox dir>\out.qxw (nothing is written anywhere else).
 * Every UI gesture is read back over a second, raw API connection; exit code 1 when a check fails
 * or the page logged a console error.
 */
const fs = require('fs');
const os = require('os');
const path = require('path');
const { launch, sleep } = require('../cdp.js');

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', 9310)), WEB_PORT = Number(opt('web', 9311));
const HOST = process.env.E2E_HOST || '127.0.0.1';
const API = 'ws://' + HOST + ':' + API_PORT + '/';
const WEB = 'http://localhost:' + WEB_PORT + '/';
const SANDBOX = opt('sandbox', process.env.QLC_SANDBOX || '');
const SHOTS = opt('shots', process.env.QLC_SHOTS || path.join(os.tmpdir(), 'qlc-e2e-vcshow'));
if (!SANDBOX || !/qlcsandbox/i.test(SANDBOX)) {
  console.error('--sandbox <dir> is required and must be a dev-webui-sandbox.ps1 folder (…\\qlcsandbox\\<name>)');
  process.exit(2);
}
const OUT = path.join(SANDBOX, 'out.qxw');

/* ---------------------------------------------------------------- raw API client */
class Api {
  constructor(url) { this.url = url; this.rev = 0; this.next = 1; this.pending = new Map(); this.events = []; }
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
      this.ws.onopen = () => this.call('hello', { apiVersion: '1', clientName: 'vc-show-leftovers e2e' }).then(r => { this.rev = r.docRevision; res(r); }, rej);
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
const soft = (p) => p.catch(() => null);

/* ---------------------------------------------------------------- page helpers */
const q = (sel) => `document.querySelector(${JSON.stringify(sel)})`;
const byText = (tag, text, root) => `[...(${root || 'document'}).querySelectorAll(${JSON.stringify(tag)})].find(e => e.textContent.trim() === ${JSON.stringify(text)})`;
async function clickFn(page, fnBody, what, opts) {
  await page.waitFor(`(function(){ const el = (${fnBody}); return !!el && !el.disabled; })()`, 8000).catch(() => { throw new Error('not found or disabled: ' + what); });
  await page.eval(`(function(){ const el = (${fnBody}); if (el && el.scrollIntoView) el.scrollIntoView({ block: 'nearest', inline: 'nearest' }); })()`);
  await page.click(new Function('return ' + fnBody), opts || {});
}
async function selectAll(page) {
  const k = { key: 'a', code: 'KeyA', windowsVirtualKeyCode: 65, modifiers: 2 };
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyDown' }, k));
  await page.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, k));
}
async function typeInto(page, fnBody, text, what) {
  await clickFn(page, fnBody, what);
  await page.eval(`(function(){ const el = document.activeElement; if (el && el.select) el.select(); })()`);
  await selectAll(page);
  await page.s.send('Input.insertText', { text: String(text) });
  await page.key('Enter');
}
/** Click a VC widget near its top-left corner (the overlapping widget covers its centre). */
async function clickWidget(page, id) {
  const r = await page.rectOf(`[data-vc-widget="${id}"]`);
  const x = r.x + 14, y = r.y + 14;
  await page.mouse('mouseMoved', x, y); await page.mouse('mousePressed', x, y); await page.mouse('mouseReleased', x, y);
  await page.waitFor(`!!document.querySelector('[data-vc-widget="${id}"][data-vc-selected]')`, 5000);
}
async function shot(page, name) { const f = path.join(SHOTS, name + '.png'); await page.screenshot(f); console.log('  shot ' + f); return f; }

/* A 64x32 PNG (red left half, blue right half), generated here so the test owns its image. */
function makePng() {
  const zlib = require('zlib');
  const w = 64, h = 32;
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w * 3 + 1)] = 0;
    for (let x = 0; x < w; x++) {
      const o = y * (w * 3 + 1) + 1 + x * 3;
      raw[o] = x < w / 2 ? 220 : 20; raw[o + 1] = 30; raw[o + 2] = x < w / 2 ? 20 : 220;
    }
  }
  const crcTable = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crcTable[n] = c >>> 0; }
  const crc = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([len, td, c]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

/* ================================================================ sections */

async function vcSection(browser, api) {
  console.log('\n[Virtual Console: page size, background image, z-order]');
  if ((await api.call('core.mode.get')).mode !== 'design') await api.call('core.mode.set', { mode: 'design' });
  const pagesBefore = (await api.call('vc.page.list')).pages;
  check(pagesBefore.every(p => p.width > 0 && p.height > 0), 'vc.page.list reports every page\'s width/height', pagesBefore.map(p => [p.width, p.height]));
  await api.structural('vc.page.create', { index: pagesBefore.length });
  const scratch = pagesBefore.length;

  // two overlapping widgets on the scratch page (created over the API; the UI drives the rest)
  const a = await api.structural('vc.widget.create', { widgetType: 'Button', page: scratch, geometry: { x: 40, y: 40, width: 160, height: 100 }, style: { caption: 'Under' } });
  const b = await api.structural('vc.widget.create', { widgetType: 'Button', page: scratch, geometry: { x: 120, y: 80, width: 160, height: 100 }, style: { caption: 'Over' } });
  const A = String(a.widgetId), B = String(b.widgetId);

  const page = await browser.open(WEB + '?ctx=vc', { width: 1600, height: 1000 });
  try {
    await page.waitFor(`document.querySelectorAll('[title^="Page "]').length >= ${scratch + 1}`, 30000);
    await clickFn(page, `[...document.querySelectorAll('[title^="Page "]')].find(e => e.title.startsWith('Page ${scratch + 1}'))`, 'scratch page tab');
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${B}"]')`);
    await clickFn(page, `document.querySelector('button img[src$="/edit.svg"]').closest('button')`, 'edit button');
    await page.waitFor(`document.body.textContent.includes('Pick a widget')`);
    await sleep(500);

    /* ---- page size ---- */
    await clickFn(page, `document.querySelector('button[title="Widget properties"]')`, 'properties rail button');
    await page.waitFor(`!!document.querySelector('[data-vc-page-props]')`);
    await typeInto(page, `document.querySelector('[data-vc-page-width] input')`, '1200', 'page width');
    await until(async () => (await api.call('vc.page.list')).pages[scratch].width === 1200, 'page width 1200');
    await typeInto(page, `document.querySelector('[data-vc-page-height] input')`, '700', 'page height');
    await until(async () => (await api.call('vc.page.list')).pages[scratch].height === 700, 'page height 700');
    check(true, 'page Width/Height -> vc.page.list 1200x700');
    await page.waitFor(`!!document.querySelector('[data-vc-page-area="1200x700"]')`);
    check(true, 'the canvas draws the page area at 1200x700');

    /* ---- background image ---- */
    const img = path.join(SANDBOX, 'e2e-bg.png');
    fs.writeFileSync(img, makePng());
    await clickWidget(page, A);
    await page.waitFor(`!!document.querySelector('[data-vc-bgimage-pick]')`);
    await clickFn(page, q('[data-vc-bgimage-pick]'), 'background image button');
    await page.waitFor(`!!document.querySelector('.qlc-fb-path')`);
    await typeInto(page, q('.qlc-fb-path'), SANDBOX, 'file browser path');
    const row = `[...document.querySelectorAll('.qlc-fb-list > div')].find(e => e.textContent.includes('e2e-bg.png'))`;
    await page.waitFor(`!!(${row})`, 8000);
    await clickFn(page, row, 'e2e-bg.png row');
    await clickFn(page, byText('button', 'Open'), 'Open');
    const got = await until(async () => { const w = await api.call('vc.widget.get', { widgetId: A }); return w.style.backgroundImage ? w : null; }, 'backgroundImage set');
    check(path.resolve(got.style.backgroundImage) === path.resolve(img), 'picked file -> style.backgroundImage is the host path', got.style.backgroundImage);
    const data = await api.call('vc.widget.getBackgroundImage', { widgetId: A });
    check(data.mimeType === 'image/png' && /^data:image\/png;base64,/.test(data.dataUrl || ''), 'vc.widget.getBackgroundImage returns a PNG data URL', { mime: data.mimeType, reason: data.reason });
    await page.waitFor(`!!document.querySelector('[data-vc-widget="${A}"] [data-vc-bg="image"]')`, 8000);
    const bgCss = await page.eval(`(function(){ const b = document.querySelector('[data-vc-widget="${A}"] [data-vc-bg="image"] > div'); return b ? getComputedStyle(b).backgroundImage.slice(0, 40) : ''; })()`);
    check(/url\("data:image\/png/.test(bgCss), 'the widget body renders the image as its background', bgCss);
    // a UNC path is refused by the server
    let unc = null;
    try { await api.structural('vc.widget.update', { widgetId: A, style: { backgroundImage: '\\\\server\\share\\x.png' } }); } catch (e) { unc = e.code; }
    check(unc === 'INVALID_PARAMS', 'a UNC backgroundImage is refused (INVALID_PARAMS)', unc);

    /* ---- z-order: A is under B (both z 0, B created later) ---- */
    await page.waitFor(`!!document.querySelector('[data-vc-z="front"]')`);
    await clickFn(page, q('[data-vc-z="front"]'), 'to front');
    await until(async () => (await api.call('vc.widget.get', { widgetId: A })).zIndex === 1, 'A zIndex 1');
    check(true, 'Front -> zIndex above every sibling (1)');
    await page.waitFor(`(function(){ const a = document.querySelector('[data-vc-widget="${A}"]'), b = document.querySelector('[data-vc-widget="${B}"]'); return a && b && Number(getComputedStyle(a).zIndex) > Number(getComputedStyle(b).zIndex); })()`);
    // leave edit mode: the stacking the operator sees (the selected widget is lifted in edit mode)
    await clickFn(page, `document.querySelector('button img[src$="/edit.svg"]').closest('button')`, 'edit off');
    await sleep(400);
    const topAt = await page.eval(`(function(){ const r = document.querySelector('[data-vc-widget="${B}"]').getBoundingClientRect(); const el = document.elementFromPoint(r.x + 20, r.y + 20); const w = el && el.closest('[data-vc-widget]'); return w ? w.getAttribute('data-vc-widget') : null; })()`);
    check(topAt === A, 'in the overlap the raised widget is on top', topAt);
    await shot(page, 'vcshow-1-vc');
    await clickFn(page, `document.querySelector('button img[src$="/edit.svg"]').closest('button')`, 'edit on');
    await clickWidget(page, A);
    await page.waitFor(`!!document.querySelector('[data-vc-z="back"]:not([disabled])')`);
    await sleep(600); // the side panel slides in
    await clickFn(page, q('[data-vc-z="lower"]'), 'lower');
    await until(async () => (await api.call('vc.widget.get', { widgetId: A })).zIndex === 0, 'A zIndex 0');
    check(true, 'Lower -> zIndex 0');
    /* the next relative step must start from what the panel shows: wait until the widget list
       refresh caught up (a late reload of the previous state would make "raise" go 1 -> 2) */
    await page.waitFor(`!!document.querySelector('[data-vc-zindex="0"]')`, 5000);
    await sleep(500);
    await page.waitFor(`!!document.querySelector('[data-vc-zindex="0"]')`, 5000);
    await clickFn(page, q('[data-vc-z="raise"]'), 'raise');
    await until(async () => (await api.call('vc.widget.get', { widgetId: A })).zIndex === 1, 'A zIndex 1 again');
    check(true, 'Raise -> zIndex 1');
    check(page.consoleErrors.length === 0, 'VC: no console errors', page.consoleErrors);
    return { scratch, A, B, img };
  } catch (e) {
    failures.push('VC EXCEPTION ' + (e && e.stack || e));
    console.log('  EXCEPTION ' + (e && e.stack || e));
    try { await shot(page, 'vcshow-vc-failure'); } catch (x) { }
    if (page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
    return { scratch };
  }
}

async function showSection(browser, api) {
  console.log('\n[Show Manager: preview at the cursor, track Spout size]');
  // a fixture with a plain channel, a Scene setting it, a Show playing the Scene at 1000..3000 ms
  /* an HTP intensity channel: a stopped Scene releases it (an LTP one would keep its last value) */
  const fixtures = (await api.call('fixtures.list')).fixtures;
  let fx = null, ch = null;
  for (const f of fixtures) {
    const d = await api.call('fixtures.get', { fixtureId: String(f.id) });
    const c = d.channelList.find(x => x.group === 'Intensity' && !x.colour);
    if (c) { fx = f; ch = c; break; }
  }
  if (!check(!!ch, 'the project has a fixture with a dimmer channel')) return {};
  const scene = await api.structural('functions.create', { type: 'Scene', name: 'E2E preview scene' });
  const sceneId = String(scene.functionId);
  await api.structural('functions.scene.setValue', { functionId: sceneId, fixture: String(fx.id), channel: ch.index, value: 201 });
  const show = await api.structural('functions.create', { type: 'Show', name: 'E2E Preview Show' });
  const showId = String(show.functionId);
  const tr = await api.structural('functions.show.track.add', { showId });
  const trackId = String(tr.trackId);
  await api.structural('functions.show.item.add', { showId, trackId, functionId: sceneId, startTime: 1000, duration: 2000 });
  /* absoluteAddress = universe * 512 + address (Fixture::channelAddress); values[] is per universe */
  const universe = fx.universe, index = ch.absoluteAddress - 512 * universe;
  const dmxAt = async () => (await api.call('io.dmx.universe.get', { universeId: universe })).values[index];
  const base = await dmxAt();
  check(base !== 201, 'the scene channel is not at 201 before the preview (' + base + ')');

  const page = await browser.open(WEB + '?ctx=show&show=' + showId, { width: 1600, height: 900 });
  try {
    await page.waitFor(`!!document.querySelector('[data-show="timeline"]') && document.querySelectorAll('[data-show="item"]').length === 1`, 30000);
    const tl = await page.rectOf('[data-show="timeline"]');
    const msX = (ms) => tl.x + 170 + ms * 60 / 5000; // TRACK_W + default Time zoom (60 px per 5 s)
    const rulerY = tl.y + 15;

    /* ---- click on the ruler at 2000 ms while stopped: the Show previews there ---- */
    await page.mouse('mouseMoved', msX(2000), rulerY);
    await page.mouse('mousePressed', msX(2000), rulerY);
    await page.mouse('mouseReleased', msX(2000), rulerY);
    await until(async () => (await dmxAt()) === 201, 'DMX 201 under the cursor at 2 s');
    check(true, 'cursor at 2 s while stopped -> the scene\'s value (201) is output (io.dmx.universe.get)');
    const g1 = await api.call('functions.get', { functionId: showId });
    check(g1.typeDetail.previewing === true, 'functions.get typeDetail.previewing is true', g1.typeDetail.previewing);
    await page.waitFor(`!!document.querySelector('[data-show="previewing"]')`);
    check(true, 'the toolbar shows PREVIEW');
    await sleep(1200);
    check((await dmxAt()) === 201 && (await api.call('functions.get', { functionId: showId })).typeDetail.previewing, 'frozen: still 201 and previewing 1.2 s later (the 3 s item end is not reached by playing)');

    /* ---- drag the cursor past the item: output drops ---- */
    await page.drag(msX(2000), rulerY, msX(4500), rulerY, 10);
    await until(async () => (await dmxAt()) === base, 'DMX back to ' + base + ' past the item');
    check(true, 'dragging the cursor to 4.5 s -> the scene is released (' + base + ')');
    await page.drag(msX(4500), rulerY, msX(1500), rulerY, 10);
    await until(async () => (await dmxAt()) === 201, 'DMX 201 back on the item');
    check(true, 'dragging back to 1.5 s -> 201 again');
    await shot(page, 'vcshow-2-preview');

    /* ---- play from the preview, then stop ---- */
    await clickFn(page, q('[data-show="play"]'), 'play');
    await until(async () => { const g = await api.call('functions.get', { functionId: showId }); return g.running && !g.typeDetail.previewing; }, 'playing');
    check(true, 'play leaves the preview and plays on (running, previewing false)');
    await until(async () => (await dmxAt()) === base, 'the item ends while playing', 6000);
    check(true, 'playing on: the item ends at 3 s and releases the channel');
    await clickFn(page, q('[data-show="stop"]'), 'stop');
    await until(async () => !(await api.call('functions.get', { functionId: showId })).running, 'stopped');
    check(true, 'stop stops the Show');

    /* ---- preview then stop keeps the cursor ---- */
    await sleep(300);
    await page.mouse('mouseMoved', msX(2500), rulerY); await page.mouse('mousePressed', msX(2500), rulerY); await page.mouse('mouseReleased', msX(2500), rulerY);
    await until(async () => (await dmxAt()) === 201, 'preview again');
    await clickFn(page, q('[data-show="stop"]'), 'stop preview');
    await until(async () => !(await api.call('functions.get', { functionId: showId })).running, 'preview ended');
    const cursorText = await page.eval(`document.querySelector('[data-show="time"]').textContent`);
    check(/00:00:02\.\d\d/.test(cursorText), 'stop during a preview ends it and keeps the cursor (' + cursorText + ')');

    /* ---- preview toggle off: moving the cursor does not output ---- */
    await clickFn(page, q('[data-show="preview-toggle"]'), 'preview toggle off');
    await page.mouse('mouseMoved', msX(2000), rulerY); await page.mouse('mousePressed', msX(2000), rulerY); await page.mouse('mouseReleased', msX(2000), rulerY);
    await sleep(800);
    check(!(await api.call('functions.get', { functionId: showId })).running, 'with the eye toggle off the cursor does not preview');
    await clickFn(page, q('[data-show="preview-toggle"]'), 'preview toggle on');

    /* ---- track Spout output size ---- */
    let spoutOk = true;
    const video = await api.structural('functions.create', { type: 'Video', name: 'E2E spout clip A' });
    const videoB = await api.structural('functions.create', { type: 'Video', name: 'E2E spout clip B' });
    for (const [v, w, h] of [[video, 1280, 720], [videoB, 640, 360]]) {
      try { await api.structural('functions.video.setScreenTarget', { functionId: String(v.functionId), screen: 0, fullscreen: false, outputMode: 'spout' }); }
      catch (e) { spoutOk = false; console.log('  (Spout output mode not available on this build: ' + e.message + ')'); }
      await api.structural('functions.video.setSpoutSize', { functionId: String(v.functionId), width: w, height: h }).catch(e => console.log('  setSpoutSize: ' + e.message));
    }
    const tr2 = await api.structural('functions.show.track.add', { showId });
    const spoutTrack = String(tr2.trackId);
    await api.structural('functions.show.item.add', { showId, trackId: spoutTrack, functionId: String(video.functionId), startTime: 0, duration: 4000 });
    let g = await api.call('functions.get', { functionId: showId });
    const t2 = g.typeDetail.tracks.find(t => String(t.id) === spoutTrack);
    if (!spoutOk) {
      check(!t2.spout, 'no Spout on this build: the track carries no spout block');
    } else {
      check(t2.spout && t2.spout.clips.length === 1 && t2.spout.clips[0].width === 1280, 'the track reports its Spout clip (1280x720)', t2.spout);
      await page.waitFor(`!!document.querySelector('[data-show="track-spout"][data-track-id="${spoutTrack}"]')`, 8000);
      check(true, 'the track header shows the Spout label');
      await clickFn(page, q(`[data-show="track-spout"][data-track-id="${spoutTrack}"]`), 'Spout label');
      await page.waitFor(`!!document.querySelector('[data-show="track-spout-dialog"]')`);
      await typeInto(page, `document.querySelector('[data-show="track-spout-dialog"] [data-show="spout-w"] input') || document.querySelectorAll('[data-show="track-spout-dialog"] input')[0]`, '1920', 'custom width');
      await typeInto(page, `document.querySelector('[data-show="track-spout-dialog"] [data-show="spout-h"] input') || document.querySelectorAll('[data-show="track-spout-dialog"] input')[1]`, '1080', 'custom height');
      await clickFn(page, q('[data-show="spout-apply"]'), 'Apply');
      g = await until(async () => { const x = await api.call('functions.get', { functionId: showId }); const t = x.typeDetail.tracks.find(t => String(t.id) === spoutTrack); return t.spout && t.spout.fixedSize && t.spout.fixedSize.width === 1920 ? x : null; }, 'fixed 1920x1080');
      check(true, 'Set Spout output size 1920x1080 -> functions.get fixedSize');
      await page.waitFor(`/Spout 1920x1080 •/.test((document.querySelector('[data-show="track-spout"][data-track-id="${spoutTrack}"]') || {}).textContent || '')`, 8000).catch(() => {});
      /* minus the warning glyph (a Font Awesome private-use codepoint) drawn before it on a mismatch */
      const label = await page.eval(`(document.querySelector('[data-show="track-spout"][data-track-id="${spoutTrack}"]') || {textContent: ''}).textContent.replace(/[\\uE000-\\uF8FF]/g, '').trim()`);
      check(label === 'Spout 1920x1080 •', 'the header label shows the fixed size', label);
      check(await page.eval(`document.querySelector('[data-show="track-spout"][data-track-id="${spoutTrack}"]').getAttribute('data-spout-mismatch')`) === 'true', 'the 1280x720 clip is flagged as a mismatch');

      /* placing clip B (640x360) onto the track through the picker asks keep / switch */
      await clickFn(page, `document.querySelector('[data-show="track"][data-track-id="${spoutTrack}"] [data-show="track-name"]')`, 'select the Spout track');
      await page.eval(`(function(){ const i = document.querySelector('[data-show="picker-filter"]'); i.focus(); return true; })()`);
      await page.s.send('Input.insertText', { text: 'E2E spout clip B' });
      await sleep(300);
      await clickFn(page, `document.querySelector('[data-show="picker-row"][data-function-id="${videoB.functionId}"]')`, 'picker row clip B');
      // at the cursor (2.5 s) the track is busy until 4 s: the server moves it to the free spot
      await clickFn(page, q('[data-show="add-picked"]'), 'Add at cursor');
      await page.waitFor(`!!document.querySelector('[data-show="spout-mismatch"]')`, 8000);
      const msg = await page.eval(`document.querySelector('[data-show="spout-mismatch"]').textContent`);
      check(/outputs Spout at 1920x1080/.test(msg) && /640x360/.test(msg), 'the mismatch prompt names both sizes', msg.slice(0, 160));
      await shot(page, 'vcshow-3-spout-mismatch');
      await clickFn(page, q('[data-show="spout-switch"]'), 'Switch track output');
      await until(async () => { const x = await api.call('functions.get', { functionId: showId }); const t = x.typeDetail.tracks.find(t => String(t.id) === spoutTrack); return t.spout && t.spout.fixedSize && t.spout.fixedSize.width === 640; }, 'fixed 640x360');
      check(true, 'Switch -> the track output is 640x360');
    }
    await shot(page, 'vcshow-4-show');
    check(page.consoleErrors.length === 0, 'Show: no console errors', page.consoleErrors);
    return { showId, spoutTrack: spoutOk ? spoutTrack : null };
  } catch (e) {
    failures.push('SHOW EXCEPTION ' + (e && e.stack || e));
    console.log('  EXCEPTION ' + (e && e.stack || e));
    try { await shot(page, 'vcshow-show-failure'); } catch (x) { }
    if (page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
    return { showId };
  } finally {
    await soft(api.call('functions.show.endPreview', { showId, play: false }));
    await soft(api.call('functions.stop', { functionId: showId }));
  }
}

async function paletteSection(browser, api) {
  console.log('\n[Palettes: Pan in degrees, Gobo value]');
  // a moving head with a 16 bit pan: its Scene makes "select fixtures in function" pick it
  const fixtures = (await api.call('fixtures.list')).fixtures;
  let mover = null, moverDetail = null;
  for (const f of fixtures) {
    const d = await api.call('fixtures.get', { fixtureId: String(f.id) });
    const pan = d.channelList.filter(c => c.group === 'Pan');
    if (pan.length >= 1 && d.physical && d.physical.focusPanMax > 0) { mover = f; moverDetail = d; if (pan.length >= 2) break; }
  }
  if (!check(!!mover, 'the project has a fixture with a pan channel')) return {};
  const panMax = moverDetail.physical.focusPanMax;
  const panChs = moverDetail.channelList.filter(c => c.group === 'Pan');
  const scene = await api.structural('functions.create', { type: 'Scene', name: 'E2E pan target' });
  await api.structural('functions.scene.setValue', { functionId: String(scene.functionId), fixture: String(mover.id), channel: panChs[0].index, value: 0 });

  const page = await browser.open(WEB + '?ctx=fx', { width: 1700, height: 1000 });
  const clickTitle = (prefix) => clickFn(page, `[...document.querySelectorAll('[title]')].filter(e => e.title.indexOf(${JSON.stringify(prefix)}) === 0).pop()`, 'title ' + prefix);
  const clickLeaf = (text, root) => clickFn(page, `[...(${root || 'document'}).querySelectorAll('*')].filter(e => e.children.length === 0 && (e.textContent || '').trim() === ${JSON.stringify(text)}).pop()`, 'text ' + text);
  try {
    await page.waitFor(`(document.querySelector('[data-ff-tree]') || {}).textContent && document.querySelector('[data-ff-tree]').textContent.indexOf('Functions') !== -1`, 30000);
    await sleep(800);
    // select the mover through its Scene (tree search, right-click, "Select fixtures in function")
    const searching = await page.eval(`!!Array.from(document.querySelectorAll('input')).find(i => i.placeholder === 'Search…')`);
    if (!searching) await clickTitle('Search fixtures and functions');
    await page.eval(`(function(){ const i = Array.from(document.querySelectorAll('input')).find(i => i.placeholder === 'Search…'); i.focus(); i.select(); return true; })()`);
    await page.s.send('Input.insertText', { text: 'E2E pan target' }); await page.key('Enter');
    await page.waitFor(`Array.from(document.querySelectorAll('[data-ff-tree] *')).some(e => e.children.length === 0 && (e.textContent||'').trim() === 'E2E pan target')`, 8000);
    await clickLeaf('E2E pan target', `document.querySelector('[data-ff-tree]')`);
    await sleep(200);
    await clickFn(page, `[...document.querySelectorAll('[data-ff-tree] *')].filter(e => e.children.length === 0 && (e.textContent || '').trim() === 'E2E pan target').pop()`, 'scene row (right)', { button: 'right' });
    await page.waitFor('!!document.querySelector("[role=menu]")', 5000);
    await clickLeaf('Select fixtures in function', `document.querySelector('[role=menu]')`);
    await page.waitFor(`/Fixture Tools · 1 fixture/.test(document.body.textContent)`, 8000);
    check(true, 'the mover is selected');

    await clickTitle('Palettes');
    await sleep(300);
    await clickTitle('Create a palette');
    await page.waitFor(`!!document.querySelector('[data-e2e=palette-create]')`, 5000);
    // type combo -> Pan
    await page.eval(`(function(){ const row = Array.from(document.querySelectorAll('[data-e2e=palette-create] div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Type').pop(); window.__e2e = row.children[1]; return true; })()`);
    await page.click(() => window.__e2e); await sleep(150);
    await clickLeaf('Pan');
    await page.waitFor(`!!document.querySelector('[data-e2e=palette-deg-pan]')`);
    const shownMax = Number(await page.eval(`document.querySelector('[data-e2e=palette-deg-pan]').getAttribute('data-max')`));
    check(shownMax === panMax, 'the Pan value is edited in degrees up to the fixture\'s range (' + panMax + '°)', shownMax);
    await page.eval(`(function(){ const i = document.querySelector('[data-e2e=palette-name]'); i.focus(); return true; })()`);
    await page.s.send('Input.insertText', { text: 'E2E Pan deg' });
    const deg = Math.round(panMax * 0.75);
    await typeInto(page, `document.querySelector('[data-e2e=palette-deg-pan] input')`, String(deg), 'pan degrees');
    await clickFn(page, byText('button', 'Create'), 'Create');
    const pal = await until(async () => (await api.call('palette.list')).palettes.find(p => p.name === 'E2E Pan deg'), 'palette created');
    const pd = await api.call('palette.get', { paletteId: pal.id });
    check(pal.type === 'Pan' && Number(pd.values[0]) === deg, 'palette.create stored ' + deg + ' degrees', pd.values);
    await shot(page, 'vcshow-5-palette');
    // apply to the selection
    await clickFn(page, byText('button', 'Apply to 1 selected'), 'Apply to 1 selected');
    const v16 = Math.floor(deg * 65535 / panMax);
    const coarse = panChs.find(c => !/fine/i.test(c.name)) || panChs[0];
    const fine = panChs.find(c => /fine/i.test(c.name));
    const dmxOf = async (c) => (await api.call('io.dmx.universe.get', { universeId: mover.universe })).values[c.absoluteAddress - 512 * mover.universe];
    const hit = await until(async () => (await dmxOf(coarse)) === (v16 >> 8), 'pan coarse DMX', 6000).catch(() => false);
    check(!!hit, 'Apply writes pan coarse = ' + (v16 >> 8) + ' (' + deg + '° of ' + panMax + '°)', await dmxOf(coarse));
    if (fine) check((await dmxOf(fine)) === (v16 & 255), 'and pan fine = ' + (v16 & 255), await dmxOf(fine));

    // Gobo palette through the same dialog: DMX value 42
    await clickTitle('Create a palette');
    await page.waitFor(`!!document.querySelector('[data-e2e=palette-create]')`, 5000);
    await page.eval(`(function(){ const row = Array.from(document.querySelectorAll('[data-e2e=palette-create] div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'Type').pop(); window.__e2e = row.children[1]; return true; })()`);
    await page.click(() => window.__e2e); await sleep(150);
    await clickLeaf('Gobo');
    await sleep(200);
    await page.eval(`(function(){ const i = document.querySelector('[data-e2e=palette-name]'); i.focus(); return true; })()`);
    await page.s.send('Input.insertText', { text: 'E2E Gobo 42' });
    await typeInto(page, `(function(){ const row = Array.from(document.querySelectorAll('[data-e2e=palette-create] div')).filter(d => d.children.length >= 2 && (d.children[0].textContent||'').trim() === 'DMX').pop(); return row && row.querySelector('input'); })()`, '42', 'gobo DMX');
    await clickFn(page, byText('button', 'Create'), 'Create gobo');
    const gobo = await until(async () => (await api.call('palette.list')).palettes.find(p => p.name === 'E2E Gobo 42'), 'gobo palette');
    check(Number((await api.call('palette.get', { paletteId: gobo.id })).values[0]) === 42, 'Gobo palette created with value 42');
    check(page.consoleErrors.length === 0, 'palettes: no console errors', page.consoleErrors);
    return { panPalette: pal, deg, gobo };
  } catch (e) {
    failures.push('PALETTE EXCEPTION ' + (e && e.stack || e));
    console.log('  EXCEPTION ' + (e && e.stack || e));
    try { await shot(page, 'vcshow-palette-failure'); } catch (x) { }
    if (page.consoleErrors.length) console.log('  console errors: ' + JSON.stringify(page.consoleErrors));
    return {};
  }
}

async function inputChannelSection(browser) {
  console.log('\n[Input channel editor: sensitivity range per type]');
  const page = await browser.open(WEB + '?ctx=io', { width: 1600, height: 1000 });
  try {
    await page.waitFor(`!!document.querySelector('[data-role="profile-new"]')`, 30000);
    await clickFn(page, q('[data-role="profile-new"]'), 'New profile');
    await page.waitFor(`!!document.querySelector('[data-role="profile-editor"]')`, 8000);
    const leaf = (scope, text) => `[...document.querySelectorAll(${JSON.stringify(scope + ' *')})].filter(e => [...e.children].every(c => c.tagName === 'IMG') && e.textContent.trim() === ${JSON.stringify(text)}).pop()`;
    await clickFn(page, leaf('[data-role="profile-editor"]', 'Add channel'), 'Add channel');
    await page.waitFor(`!!document.querySelector('[data-role="channel-editor"]')`, 5000);
    const sens = `(function(){ const rows = [...document.querySelectorAll('[data-role="channel-editor"] div')].filter(d => d.children.length >= 2 && d.children[0].textContent.trim() === 'Sensitivity'); for (const r of rows) { const i = r.querySelector('input'); if (i) return i; } return null; })()`;
    const setType = async (from, to) => {
      await clickFn(page, leaf('[data-role="channel-editor"]', from), 'type combo ' + from);
      await sleep(200);
      await clickFn(page, leaf('body', to), 'type ' + to);
      await sleep(300);
    };
    await setType('Button', 'Encoder');
    const enc = await page.eval(`(function(){ const i = ${sens}; return i ? i.value : null; })()`);
    await setType('Encoder', 'Slider');
    const sld = await page.eval(`(function(){ const i = ${sens}; return i ? i.value : null; })()`);
    check(String(enc) === '1' && String(sld) === '20', 'type change resets sensitivity like QLCInputChannel::setType (Encoder 1, Slider 20)', { enc, sld });
    // Slider range 10..100: typing 5 is clamped to 10 by the spin box
    await typeInto(page, sens, '5', 'sensitivity');
    await sleep(200);
    const clamped = await page.eval(`(function(){ const i = ${sens}; return i ? i.value : null; })()`);
    check(Number(clamped) >= 10, 'Slider sensitivity cannot go below 10 (InputProfileEditor.qml range)', clamped);
    await shot(page, 'vcshow-6-input-channel');
    await clickFn(page, leaf('body', 'Cancel'), 'Cancel channel');
    await sleep(300);
    check(page.consoleErrors.length === 0, 'input editor: no console errors', page.consoleErrors);
  } catch (e) {
    failures.push('INPUT EXCEPTION ' + (e && e.stack || e));
    console.log('  EXCEPTION ' + (e && e.stack || e));
    try { await shot(page, 'vcshow-input-failure'); } catch (x) { }
  }
}

/* ---------------------------------------------------------------- the run */
async function main() {
  fs.mkdirSync(SHOTS, { recursive: true });
  const api = new Api(API);
  await api.connect();
  console.log('API connected, docRevision ' + api.rev + ', sandbox ' + SANDBOX);
  const browser = await launch({ headless: true });
  try {
    const vc = await vcSection(browser, api);
    const show = await showSection(browser, api);
    const pal = await paletteSection(browser, api);
    await inputChannelSection(browser);

    /* ---- save, grep, reopen ---- */
    console.log('\n[saveAs + core.project.open]');
    await api.call('core.project.saveAs', { target: 'serverPath', path: OUT });
    const xml = fs.readFileSync(OUT, 'utf8');
    const has = (re, what) => check(re.test(xml), 'out.qxw contains ' + what);
    has(/<WindowState [^>]*Width="1200" Height="700"/, 'the 1200x700 page (WindowState)');
    if (vc.img) has(new RegExp('<BackgroundImage>[^<]*e2e-bg\\.png</BackgroundImage>'), 'the widget background image');
    has(/<WindowState [^>]*Z="1"/, 'the raised widget\'s Z="1"');
    if (show.spoutTrack) has(/<Track [^>]*SpoutSize="640,360"/, 'the track SpoutSize 640,360');
    if (pal.panPalette) has(new RegExp('<Palette [^>]*Type="Pan"[^>]*Value="' + pal.deg + '"'), 'the Pan palette in degrees');
    if (pal.gobo) has(/<Palette [^>]*Type="Gobo"[^>]*Value="42"/, 'the Gobo palette value 42');
    await api.call('core.project.open', { source: 'path', path: OUT });
    await sleep(1500);
    await api.call('hello', { apiVersion: '1' }).then(r => { api.rev = r.docRevision; });
    const pages = (await api.call('vc.page.list')).pages;
    check(vc.scratch != null && pages[vc.scratch] && pages[vc.scratch].width === 1200 && pages[vc.scratch].height === 700, 'after reopening: the page is still 1200x700', pages[vc.scratch]);
    const gobo = (await api.call('palette.list')).palettes.find(p => p.name === 'E2E Gobo 42');
    check(!!gobo && Number((await api.call('palette.get', { paletteId: gobo.id })).values[0]) === 42, 'after reopening: the Gobo palette still has its value 42 (was lost before the engine fix)');
    if (vc.A) {
      const w = await api.call('vc.widget.get', { widgetId: vc.A }).catch(() => null);
      check(w && /e2e-bg\.png$/.test(w.style.backgroundImage || '') && w.zIndex === 1, 'after reopening: background image and zIndex kept', w && { bg: w.style.backgroundImage, z: w.zIndex });
    }
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
