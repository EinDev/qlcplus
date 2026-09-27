// End-to-end check of the Show Manager screen against a sandbox instance (dev-webui-sandbox.ps1).
//
//   .\dev-webui-sandbox.ps1 -Name show -BuildDir .\build -WebUiRoot .\webui -ApiPort 9160 -WebUiPort 9161
//   node webui/tools/e2e/show.js [apiPort] [webUiPort] [sandboxDir]
//
// Drives headless Chrome through webui/tools/cdp.js like an operator would (clicks, drags, typed
// text) on the SF3 Show "Midnight City" and confirms every change by reading the server back over
// a second, plain WebSocket API connection (functions.get) and finally through the .qxw written by
// core.project.saveAs into the sandbox directory. Exits non-zero on the first failed assertion or
// on any browser console error. Writes show-manager.png next to the sandbox log for a look.

const path = require('path'), fs = require('fs');
const { launch, sleep } = require('../cdp.js');

const API_PORT = Number(process.argv[2] || 9160);
const WEB_PORT = Number(process.argv[3] || 9161);
const SANDBOX = process.argv[4] || 'C:\\qlcsandbox\\show';
const SHOW_NAME = 'Midnight City'; // SF3: Show 111, BPM 4/4 @105, one track with one Audio item

function assert(cond, msg) { if (!cond) throw new Error('ASSERT: ' + msg); }
function eq(a, b, msg) { if (a !== b) throw new Error('ASSERT: ' + msg + ' — got ' + JSON.stringify(a) + ', expected ' + JSON.stringify(b)); }

/* ---- a second API client, so nothing the UI does is trusted without a read-back ---- */
class Api {
  constructor(port) { this.port = port; this.id = 0; this.pending = new Map(); this.events = []; }
  async open() {
    this.ws = new WebSocket('ws://127.0.0.1:' + this.port + '/');
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.type === 'event') this.events.push(m);
      if (m.type === 'response' && this.pending.has(m.id)) {
        const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id);
        if (m.ok) res(m.result); else { const e = new Error(m.error.code + ': ' + m.error.message); e.code = m.error.code; e.details = m.error.details; rej(e); }
      }
    };
    await this.call('hello', { apiVersion: '1', clientName: 'show e2e' });
  }
  call(method, params = {}) {
    const id = 'e2e-' + (++this.id);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  close() { try { this.ws.close(); } catch (e) { } }
}

const q = (sel) => 'document.querySelector(' + JSON.stringify(sel) + ')';
async function rect(page, sel) { return page.rectOf(sel); }
async function settle(ms = 600) { await sleep(ms); }

(async () => {
  const api = new Api(API_PORT);
  await api.open();
  const b = await launch();
  let failed = null;
  try {
    /* start from the sandbox's pristine copy of the project, so the run is repeatable */
    await api.call('core.project.open', { source: 'path', path: path.join(SANDBOX, 'project.qxw') });
    await sleep(1500);
    const list = await api.call('functions.list');
    const show = (list.functions || []).find(f => f.name === SHOW_NAME && f.type === 'Show');
    assert(show, 'SF3 has the Show "' + SHOW_NAME + '"');
    const sid = String(show.id);
    const get = async () => (await api.call('functions.get', { functionId: sid }));
    const td = async () => (await get()).typeDetail;
    const original = await td();
    console.log('show', sid, 'tracks', original.tracks.length, 'division', original.timeDivisionType, original.timeDivisionBPM);
    eq(original.tracks.length, 1, 'Midnight City starts with one track');

    const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=show&show=' + sid);
    await page.waitFor('!!document.querySelector("[data-show=screen]")', 20000);
    await page.waitFor('document.querySelectorAll("[data-show=item]").length >= 1', 20000);
    console.log('screen open, timeline rendered from the server');
    /* every mutation the page sends is echoed to the console (printed on failure) */
    await page.eval('(function(){ const o = window.FF.mutate; window.FF.mutate = function(q, m, p, x) { console.log("mutate " + m + " " + JSON.stringify(p)); return o(q, m, p, x); }; })()');
    const nameText = await page.eval(q('[data-show="name"]') + '.value');
    assert(String(nameText).indexOf(SHOW_NAME) !== -1, 'show name shown: ' + nameText);

    /* 1. add a track */
    await page.click('[data-show="add-track"]');
    await page.waitFor('document.querySelectorAll("[data-show=track]").length === 2', 8000);
    let d = await td();
    eq(d.tracks.length, 2, 'functions.get has two tracks after Add track');
    const track2 = d.tracks[1];
    console.log('track added:', track2.id, track2.name);

    /* 2. two Scenes from the picker, added back to back at the cursor (0) on the new track */
    const scenes = (list.functions || []).filter(f => f.type === 'Scene' && !f.hidden).slice(0, 2);
    eq(scenes.length, 2, 'two Scenes available');
    await page.click(new Function('return document.querySelector(\'[data-show=track][data-track-id="' + track2.id + '"]\')'));
    for (const s of scenes) {
      await page.eval('(function(){ const el = document.querySelector(\'[data-show=picker-row][data-function-id="' + s.id + '"]\'); el.scrollIntoView({block:"nearest"}); })()');
      await page.click(new Function('return document.querySelector(\'[data-show=picker-row][data-function-id="' + s.id + '"]\')'));
    }
    await page.click('[data-show="add-picked"]');
    await page.waitFor('document.querySelectorAll("[data-show=item]").length === 3', 10000);
    await settle();
    d = await td();
    let t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.length, 2, 'two items on the new track');
    eq(t2.items[0].startTime, 0, 'first picked item starts at the cursor (0)');
    eq(t2.items[0].duration, 4000, 'a Scene (totalDuration 0) gets the Beats default of 4000 ms');
    eq(t2.items[1].startTime, 4000, 'second picked item follows back to back');
    eq(t2.items[0].functionId, String(scenes[0].id), 'first item is the first picked Scene');
    const itemA = t2.items[0], itemB = t2.items[1];
    console.log('items added:', itemA.id, itemB.id);

    /* pixel scale of the timeline, from the two known items */
    const ra = await rect(page, '[data-show=item][data-item-id="' + itemA.id + '"]');
    const rb = await rect(page, '[data-show=item][data-item-id="' + itemB.id + '"]');
    const pxPerMs = (rb.x - ra.x) / (itemB.startTime - itemA.startTime);
    assert(pxPerMs > 0.001, 'timeline scale is sane: ' + pxPerMs);
    console.log('px/ms', pxPerMs.toFixed(4));

    /* 3. drag item B 20 s to the right (a same-track move). "Add at cursor" left both new items
          selected and a drag moves the whole selection (like the Qt editor), so B is clicked
          first to make it the only selected item */
    const dragMs = 20000;
    await page.click('[data-show=item][data-item-id="' + itemB.id + '"]');
    await sleep(150);
    await page.drag(rb.x + rb.w / 2, rb.y + rb.h / 2, rb.x + rb.w / 2 + dragMs * pxPerMs, rb.y + rb.h / 2, 12);
    await settle(900);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    let movedB = t2.items.find(i => i.id === itemB.id);
    assert(Math.abs(movedB.startTime - (4000 + dragMs)) < 1500, 'item B moved to ~' + (4000 + dragMs) + ', got ' + movedB.startTime);
    console.log('drag move ok:', movedB.startTime);

    /* 4. overlap: dropping B onto A is not accepted as such - like the Qt editor it lands on the
          nearest free spot (right after A), and the server itself refuses a raw overlapping move */
    const rb2 = await rect(page, '[data-show=item][data-item-id="' + itemB.id + '"]');
    const ra2 = await rect(page, '[data-show=item][data-item-id="' + itemA.id + '"]');
    await page.drag(rb2.x + rb2.w / 2, rb2.y + rb2.h / 2, ra2.x + ra2.w * 0.4, ra2.y + ra2.h / 2, 12);
    await settle(900);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    movedB = t2.items.find(i => i.id === itemB.id);
    const a = t2.items.find(i => i.id === itemA.id);
    assert(movedB.startTime >= a.startTime + a.duration || movedB.startTime + movedB.duration <= a.startTime, 'B does not overlap A after the drop: A ' + a.startTime + '+' + a.duration + ', B ' + movedB.startTime);
    eq(movedB.startTime, a.startTime + a.duration, 'B was placed right after A (nearest free spot)');
    let rejected = null;
    try { await api.call('functions.show.item.move', { showId: sid, itemId: itemB.id, trackId: track2.id, startTime: a.startTime + 1000, baseRevision: d.docRevision }); }
    catch (e) { rejected = e; }
    assert(rejected && rejected.code === 'INVALID_PARAMS', 'server rejects an overlapping item.move with INVALID_PARAMS');
    eq(rejected.details.blockingItemId, itemA.id, 'rejection names the blocking item');
    eq(rejected.details.suggestedStartTime, a.startTime + a.duration, 'rejection suggests the nearest free start');
    console.log('overlap rejection ok');

    /* 5. cross-track move: drag A straight up onto track 1. Its spot there is under the 4-minute
          audio clip, so - like the Qt editor's drop - it slides to the nearest free spot, right
          after that clip */
    const audio = d.tracks[0].items[0];
    const ra4 = await rect(page, '[data-show=item][data-item-id="' + itemA.id + '"]');
    const audioRect = await rect(page, '[data-show=item][data-item-id="' + audio.id + '"]');
    await page.click('[data-show=item][data-item-id="' + itemA.id + '"]');
    await sleep(150);
    await page.drag(ra4.x + 10, ra4.y + ra4.h / 2, ra4.x + 14, audioRect.y + audioRect.h / 2, 14);
    await settle(900);
    d = await td();
    const t1 = d.tracks[0]; t2 = d.tracks.find(t => t.id === track2.id);
    assert(t1.items.some(i => i.id === itemA.id), 'A is on track 1 now');
    eq(t2.items.length, 1, 'track 2 keeps only B');
    const aOn1 = t1.items.find(i => i.id === itemA.id);
    eq(aOn1.startTime, audio.startTime + audio.duration, 'A slid to the free spot right after the audio clip');
    const noticeText = await page.eval('(function(){ const el = document.querySelector("[data-show=notice]"); return el ? el.textContent : ""; })()');
    assert(/nearest free spot/.test(noticeText), 'the shift is announced: ' + noticeText);
    console.log('cross-track move ok:', aOn1.startTime);

    /* 6. resize B by its right edge (+8 s) */
    const rb3 = await rect(page, '[data-show=item][data-item-id="' + itemB.id + '"]');
    const handleX = rb3.x + rb3.w - 3;
    await page.drag(handleX, rb3.y + rb3.h / 2, handleX + 8000 * pxPerMs, rb3.y + rb3.h / 2, 10);
    await settle(900);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    movedB = t2.items.find(i => i.id === itemB.id);
    assert(Math.abs(movedB.duration - 12000) < 1500, 'B resized to ~12000, got ' + movedB.duration);
    console.log('resize ok:', movedB.duration);

    /* 7. lock B (toolbar button on the selection), then try to drag it: nothing moves */
    await page.click('[data-show=item][data-item-id="' + itemB.id + '"]');
    await page.click('[data-show="lock"]');
    await settle(700);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.find(i => i.id === itemB.id).locked, true, 'B is locked');
    const rbL = await rect(page, '[data-show=item][data-item-id="' + itemB.id + '"]');
    await page.drag(rbL.x + 10, rbL.y + rbL.h / 2, rbL.x + 10 + 10000 * pxPerMs, rbL.y + rbL.h / 2, 8);
    await settle(700);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.find(i => i.id === itemB.id).startTime, movedB.startTime, 'a locked item does not move');
    await page.click('[data-show="lock"]');
    await settle(700);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.find(i => i.id === itemB.id).locked, false, 'B unlocked again');
    console.log('lock ok');

    /* 8. recolour B through the colour input (React-controlled: native setter + change event) */
    await page.eval('(function(){ const el = ' + q('[data-show="color"]') + '; Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, "#ff8800"); el.dispatchEvent(new Event("change", { bubbles: true })); })()');
    await settle(700);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.find(i => i.id === itemB.id).color, '#ff8800', 'B recoloured to #ff8800');
    console.log('colour ok');

    /* 9. ripple insert 1 s at a cursor inside B: B grows by 1000, A (later, on track 1) shifts by 1000 */
    const rbC = await rect(page, '[data-show=item][data-item-id="' + itemB.id + '"]');
    const ruler = await rect(page, '[data-show="timeline"]');
    await page.mouse('mouseMoved', rbC.x + rbC.w / 2, ruler.y + 15);
    await page.mouse('mousePressed', rbC.x + rbC.w / 2, ruler.y + 15);
    await page.mouse('mouseReleased', rbC.x + rbC.w / 2, ruler.y + 15);
    await settle(300);
    const cursorText = await page.eval(q('[data-show="time"]') + '.textContent');
    assert(/^\d\d:\d\d:\d\d\.\d\d$/.test(cursorText), 'time readout formatted: ' + cursorText);
    const before = await td();
    const bBefore = before.tracks.find(t => t.id === track2.id).items.find(i => i.id === itemB.id);
    const aBefore = before.tracks[0].items.find(i => i.id === itemA.id);
    await page.click('[data-show="insert-time"]');
    await settle(900);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.find(i => i.id === itemB.id).duration, bBefore.duration + 1000, 'ripple insert grew B by 1000 ms');
    eq(d.tracks[0].items.find(i => i.id === itemA.id).startTime, aBefore.startTime + 1000, 'ripple insert shifted A (after the cursor) by 1000 ms');
    console.log('ripple insert ok; cursor', cursorText);

    /* 10. time division: Markers combo BPM 4/4 -> Time and back */
    const pickCombo = async (from, to) => {
      const leaf = (text, last) => new Function('const all = Array.from(document.querySelectorAll("[data-show=screen] *")).filter(el => el.children.length === 0 && el.textContent.trim() === ' + JSON.stringify(text) + '); return ' + (last ? 'all[all.length - 1]' : 'all[0]') + ';');
      await page.click(leaf(from)); await sleep(200);
      await page.click(leaf(to, true)); await sleep(200);
    };
    await pickCombo('BPM 4/4', 'Time');
    await settle(700);
    eq((await td()).timeDivisionType, 'time', 'time division switched to Time');
    await pickCombo('Time', 'BPM 4/4');
    await settle(700);
    d = await td();
    eq(d.timeDivisionType, 'bpm_4_4', 'time division back to BPM 4/4');
    eq(d.timeDivisionBPM, 105, 'BPM kept');
    console.log('time division ok');

    /* 11. mute / solo / rename the new track */
    await page.click(new Function('return document.querySelector(\'[data-show=track][data-track-id="' + track2.id + '"] [data-show=mute]\')'));
    await settle(600);
    eq((await td()).tracks.find(t => t.id === track2.id).mute, true, 'track 2 muted');
    await page.click(new Function('return document.querySelector(\'[data-show=track][data-track-id="' + track2.id + '"] [data-show=solo]\')'));
    await settle(600);
    d = await td();
    eq(d.tracks.find(t => t.id === track2.id).mute, false, 'solo unmutes track 2');
    eq(d.tracks[0].mute, true, 'solo mutes track 1');
    await page.click(new Function('return document.querySelector(\'[data-show=track][data-track-id="' + track2.id + '"] [data-show=solo]\')'));
    await settle(600);
    d = await td();
    assert(d.tracks.every(t => !t.mute), 'solo off unmutes every track');
    const hdr = await rect(page, '[data-show=track][data-track-id="' + track2.id + '"] [data-show=track-name]');
    await page.mouse('mouseMoved', hdr.x + 10, hdr.y + 5);
    await page.mouse('mousePressed', hdr.x + 10, hdr.y + 5, { clickCount: 1 }); await page.mouse('mouseReleased', hdr.x + 10, hdr.y + 5, { clickCount: 1 });
    await page.mouse('mousePressed', hdr.x + 10, hdr.y + 5, { clickCount: 2 }); await page.mouse('mouseReleased', hdr.x + 10, hdr.y + 5, { clickCount: 2 });
    await page.waitFor('!!document.querySelector("[data-show=track-name-input]")', 3000);
    await page.eval('(function(){ const el = ' + q('[data-show="track-name-input"]') + '; el.select(); })()');
    await page.type('Vocals');
    await page.key('Enter');
    await settle(700);
    eq((await td()).tracks.find(t => t.id === track2.id).name, 'Vocals', 'track renamed through the header');
    console.log('track mute/solo/rename ok');

    /* 12. play from the cursor: the running state and the playhead stream drive the cursor */
    const cursorBefore = await page.eval(q('[data-show="cursor"]') + '.style.left');
    await page.click('[data-show="play"]');
    await page.waitFor(() => { const el = document.querySelector('[data-show=stop]'); return !!el && /red/.test(el.getAttribute('style') || ''); }, 5000).catch(() => {});
    await settle(200);
    eq((await get()).running, true, 'show is running after Play');
    const readings = [];
    for (let i = 0; i < 6; i++) { readings.push(parseFloat(await page.eval(q('[data-show="cursor"]') + '.style.left'))); await sleep(200); }
    console.log('cursor px while playing:', readings.map(v => v.toFixed(1)).join(' '));
    assert(readings[readings.length - 1] > readings[0] + 2, 'cursor advances while playing (playhead events)');
    assert(parseFloat(cursorBefore) <= readings[0] + 1, 'cursor started at the click position: ' + cursorBefore + ' -> ' + readings[0]);
    /* pause / resume through Space, then stop */
    await page.eval('document.activeElement && document.activeElement.blur()');
    await page.s.send('Input.dispatchKeyEvent', { type: 'keyDown', key: ' ', code: 'Space', text: ' ', windowsVirtualKeyCode: 32 });
    await page.s.send('Input.dispatchKeyEvent', { type: 'keyUp', key: ' ', code: 'Space', windowsVirtualKeyCode: 32 });
    await settle(600);
    let g = await get();
    eq(g.paused, true, 'Space paused the show');
    const pausedPx = parseFloat(await page.eval(q('[data-show="cursor"]') + '.style.left'));
    await sleep(400);
    eq(parseFloat(await page.eval(q('[data-show="cursor"]') + '.style.left')), pausedPx, 'cursor holds while paused');
    await page.click('[data-show="stop"]');
    await page.waitFor(() => { const el = document.querySelector('[data-show=stop]'); return !!el && !/red/.test(el.getAttribute('style') || ''); }, 5000).catch(() => {});
    await settle(400);
    g = await get();
    eq(g.running, false, 'show stopped');
    console.log('transport ok');

    /* 13. copy/paste B at a free cursor spot (Ctrl C / Ctrl V) */
    await page.click('[data-show=item][data-item-id="' + itemB.id + '"]');
    await page.click('[data-show="copy"]');
    const rbP = await rect(page, '[data-show=item][data-item-id="' + itemB.id + '"]');
    const pasteX = rbP.x + rbP.w + 15000 * pxPerMs;
    await page.mouse('mouseMoved', pasteX, ruler.y + 15); await page.mouse('mousePressed', pasteX, ruler.y + 15); await page.mouse('mouseReleased', pasteX, ruler.y + 15);
    await settle(300);
    const countBefore = (await td()).tracks.find(t => t.id === track2.id).items.length;
    await page.click('[data-show="paste"]');
    await settle(1000);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.length, countBefore + 1, 'paste added one item on B\'s track');
    const pasted = t2.items.filter(i => i.id !== itemB.id).sort((x, y) => y.startTime - x.startTime)[0];
    eq(pasted.functionId, movedB.functionId, 'pasted item plays the same function');
    eq(pasted.color, '#ff8800', 'pasted item keeps the colour');
    console.log('copy/paste ok');

    /* 14. delete the pasted item through the toolbar + confirm dialog */
    await page.click('[data-show=item][data-item-id="' + pasted.id + '"]');
    await page.click('[data-show="delete"]');
    await sleep(300);
    await page.click(() => Array.from(document.querySelectorAll('button, [role=button]')).find(el => el.textContent.trim() === 'OK'));
    await settle(800);
    d = await td(); t2 = d.tracks.find(t => t.id === track2.id);
    eq(t2.items.length, countBefore, 'pasted item deleted');
    console.log('delete ok');

    /* 15. save and grep the XML */
    const out = path.join(SANDBOX, 'out.qxw');
    try { fs.unlinkSync(out); } catch (e) { }
    await api.call('core.project.saveAs', { target: 'serverPath', path: out });
    await sleep(600);
    const xml = fs.readFileSync(out, 'utf8');
    const start = xml.indexOf('<Function ID="' + sid + '"');
    const block = xml.slice(start, xml.indexOf('</Function>', start));
    assert(block.includes('<Track ID="' + track2.id + '" Name="Vocals"'), 'saved XML has the renamed track');
    assert(block.includes('<ShowFunction ID="' + movedB.functionId + '" StartTime="' + t2.items.find(i => i.id === itemB.id).startTime + '" Duration="' + t2.items.find(i => i.id === itemB.id).duration + '" Color="#ff8800"'), 'saved XML has item B with its time, duration and colour');
    assert(block.includes('<TimeDivision Type="BPM_4_4" BPM="105"/>'), 'saved XML keeps the time division');
    console.log('saved XML ok:', out);

    const shot = path.join(SANDBOX, 'show-manager.png');
    await page.screenshot(shot);
    console.log('screenshot:', shot);

    if (page.consoleErrors.length) { console.log('CONSOLE ERRORS:\n' + page.consoleErrors.join('\n')); failed = new Error('console errors'); }
    else console.log('no console errors');
  } catch (e) {
    failed = e;
    try { const p = b.pages[0]; if (p) { await p.screenshot(path.join(SANDBOX, 'show-failure.png')); console.log('console errors:', p.consoleErrors.join('\n')); console.log('last console lines:\n' + p.consoleAll.slice(-25).join('\n')); } } catch (e2) { }
  } finally {
    api.close();
    await b.close();
  }
  if (failed) { console.error('FAILED:', failed.message); process.exit(1); }
  console.log('PASS');
})();
