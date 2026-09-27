// End-to-end check of the EFX and Collection editors against a sandbox instance
// (dev-webui-sandbox.ps1 -Name efx -ApiPort 9120 -WebUiPort 9121). Drives headless Chrome through
// webui/tools/cdp.js the way an operator would (clicks, typing), reads the server back over its own
// WebSocket after every step, saves the project with core.project.saveAs and greps the XML.
//
//   node webui/tools/e2e/efx-collection.js            # defaults: API 9120, web UI 9121
//   E2E_API_PORT=.. E2E_WEB_PORT=.. E2E_OUT=<dir> node webui/tools/e2e/efx-collection.js
//
// Exit code 0 only if every assertion held and the page logged no console error.

const fs = require('fs'), path = require('path'), os = require('os');
const { launch, sleep } = require('../cdp.js');

const API_PORT = Number(process.env.E2E_API_PORT || 9120);
const WEB_PORT = Number(process.env.E2E_WEB_PORT || 9121);
const OUT = process.env.E2E_OUT || path.join(os.tmpdir(), 'qlc-e2e-efx');
const SAVE_PATH = process.env.E2E_SAVE_PATH || 'C:\\qlcsandbox\\efx\\out.qxw';
fs.mkdirSync(OUT, { recursive: true });
const TAG = String(Date.now()).slice(-6);
const EFX_NAME = 'E2E EFX ' + TAG, COLL_NAME = 'E2E Collection ' + TAG;

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function eq(a, b, what) { check(JSON.stringify(a) === JSON.stringify(b), what + ' (got ' + JSON.stringify(a) + ', want ' + JSON.stringify(b) + ')'); }

/* ---- a second, independent API client for reading the server back ------------------------ */
function api(port) {
  const ws = new WebSocket('ws://127.0.0.1:' + port + '/');
  const pending = new Map(); let seq = 0, docRevision = 0;
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'response' && pending.has(m.id)) {
      const { res, rej } = pending.get(m.id); pending.delete(m.id);
      if (m.ok) { if (m.result && typeof m.result.docRevision === 'number') docRevision = Math.max(docRevision, m.result.docRevision); res(m.result); }
      else rej(Object.assign(new Error(m.error.code + ': ' + m.error.message), m.error));
    } else if (m.type === 'event' && m.data && typeof m.data.docRevision === 'number') docRevision = Math.max(docRevision, m.data.docRevision);
  };
  const call = (method, params) => new Promise((res, rej) => { const id = 'e2e-' + (++seq); pending.set(id, { res, rej }); ws.send(JSON.stringify({ type: 'request', id, method, params: params || {} })); });
  const mutate = (method, params) => call(method, Object.assign({ baseRevision: docRevision }, params));
  return {
    async connect() { await ready; const w = await call('hello', { apiVersion: '1', clientName: 'e2e' }); docRevision = w.docRevision; },
    call, mutate, close: () => ws.close(), get docRevision() { return docRevision; }
  };
}

/* ---- page helpers ------------------------------------------------------------------------- */
// Element-returning function source for cdp's click()/rectOf(): the deepest visible element whose
// trimmed text equals `text`, optionally inside `within` (a CSS selector). Overlays come last in
// the DOM, so the last match wins.
function byText(text, within) {
  return new Function('const root = ' + (within ? 'document.querySelector(' + JSON.stringify(within) + ')' : 'document') + '; if (!root) return null;' +
    'const all = Array.from(root.querySelectorAll("*")).filter(el => el.childElementCount === 0 && el.textContent.trim() === ' + JSON.stringify(text) + ' && el.getBoundingClientRect().width > 0);' +
    'const el = all.length ? all[all.length - 1] : null; if (el) el.scrollIntoView({ block: "nearest" }); return el;');
}
async function clickText(page, text, within) {
  /* waitFor needs a serialisable value, so test for the element rather than returning it */
  await page.waitFor('!!(' + byText(text, within).toString() + ')()', 8000);
  await page.click(byText(text, within));
}
async function setSpin(page, selector, value) {
  await page.waitFor('!!document.querySelector(' + JSON.stringify(selector) + ')', 8000);
  await page.click(selector);
  await page.eval('(function(){const el=document.querySelector(' + JSON.stringify(selector) + '); el.focus(); el.select(); return true;})()');
  await page.type(String(value));
  await page.key('Enter');
  await sleep(120);
}
async function poll(fn, timeout = 6000) { const t0 = Date.now(); for (;;) { const v = await fn(); if (v) return v; if (Date.now() - t0 > timeout) return null; await sleep(150); } }
const canvasHash = '(function(){const c=document.querySelector(\'[data-e2e="efx-preview"] canvas\'); if(!c) return null; const d=c.getContext("2d").getImageData(0,0,c.width,c.height).data; let h=0; for(let i=0;i<d.length;i+=97) h=(h*31+d[i])>>>0; return h;})()';
const headsHash = '(function(){const c=document.querySelectorAll(\'[data-e2e="efx-preview"] canvas\')[1]; if(!c) return null; const d=c.getContext("2d").getImageData(0,0,c.width,c.height).data; let h=0; for(let i=0;i<d.length;i+=13) h=(h*31+d[i])>>>0; return h;})()';

(async () => {
  const srv = api(API_PORT);
  await srv.connect();
  const listIds = async (type) => (await srv.call('functions.list', { typeFilter: [type] })).functions.map(f => String(f.id));
  const detailOf = async (id) => (await srv.call('functions.get', { functionId: String(id) }));
  const b = await launch();
  const page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=fx');
  try {
    console.log('connect');
    await page.waitFor('(function(){const b=document.querySelector(\'button[title^="Add a new function"]\'); return !!b && !b.disabled;})()', 30000);

    /* ================= EFX ================= */
    console.log('EFX: create through the + menu');
    const efxBefore = await listIds('EFX');
    await page.click('button[title^="Add a new function"]');
    await clickText(page, 'New EFX');
    const efxId = await poll(async () => (await listIds('EFX')).find(id => efxBefore.indexOf(id) === -1));
    check(!!efxId, 'a new EFX exists on the server (' + efxId + ')');
    await srv.mutate('functions.rename', { functionId: efxId, name: EFX_NAME });
    /* the tree shows the renamed node; open it (creation alone does not always select it) */
    await clickText(page, EFX_NAME);
    await page.waitFor('!!document.querySelector(\'[data-e2e="efx-editor"]\')', 15000);

    console.log('EFX: add fixtures through the picker');
    await page.click('[data-e2e="efx-add"]');
    await page.waitFor('!!document.querySelector(\'input[placeholder="Search…"]\')', 8000);
    await page.click('input[placeholder="Search…"]');
    await page.type('Truss 1');
    await sleep(200);
    const rowsFn = 'document.querySelector(\'input[placeholder="Search…"]\').nextElementSibling.children';
    await page.waitFor('(' + rowsFn + ').length >= 2', 8000);
    await page.click(new Function('return (' + rowsFn + ')[0];'));
    await page.click(new Function('return (' + rowsFn + ')[1];'));
    await clickText(page, 'Add');
    await page.waitFor('document.querySelectorAll(\'[data-e2e="efx-heads"] tr\').length >= 2', 10000);
    let d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.fixtures.length >= 2 ? x : null; });
    check(!!d, 'server lists the added heads');
    const rowCount = await page.eval('document.querySelectorAll(\'[data-e2e="efx-heads"] tr\').length');
    eq(rowCount, d.typeDetail.fixtures.length, 'head rows match functions.get');
    const fixtureIds = Array.from(new Set(d.typeDetail.fixtures.map(f => f.fixture)));
    eq(fixtureIds.length, 2, 'two fixtures were added (every head of each)');

    console.log('EFX: preview');
    await page.waitFor('document.querySelector(\'[data-e2e="efx-preview"]\').getAttribute("data-points") === "512"', 8000);
    eq(await page.eval('document.querySelector(\'[data-e2e="efx-preview"]\').getAttribute("data-heads")'), String(d.typeDetail.fixtures.length), 'preview knows every head');
    const h1 = await page.eval(headsHash); await sleep(400); const h2 = await page.eval(headsHash);
    check(h1 !== h2, 'heads animate along the pattern (canvas changes over time)');
    const patternCircle = await page.eval(canvasHash);

    console.log('EFX: algorithm');
    await page.click('[data-e2e="efx-algorithm"] button');
    await clickText(page, 'Lissajous', '[data-e2e="efx-algorithm"]');
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.algorithm === 'Lissajous' ? x : null; });
    check(!!d, 'algorithm Lissajous stored');
    await sleep(500);
    const patternLissajous = await page.eval(canvasHash);
    check(patternCircle !== patternLissajous, 'pattern canvas redrawn for the new algorithm');

    console.log('EFX: relative / dimmer control / propagation');
    await page.click('[data-e2e="efx-relative"]');
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.isRelative ? x : null; });
    check(!!d && d.typeDetail.xOffset === 127 && d.typeDetail.yOffset === 127, 'relative on re-centres the offsets to 127/127');
    await page.click('[data-e2e="efx-relative"]');
    d = await poll(async () => { const x = await detailOf(efxId); return !x.typeDetail.isRelative ? x : null; });
    check(!!d, 'relative off again');
    await page.click('[data-e2e="efx-dimmer"]');
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.dimmerControlEnabled ? x : null; });
    check(!!d, 'dimmer control stored');
    await clickText(page, 'Serial');
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.propagationMode === 'Serial' ? x : null; });
    check(!!d, 'propagation Serial stored');

    console.log('EFX: geometry spinners');
    const want = { width: 60, height: 40, xOffset: 100, yOffset: 150, rotation: 45, startOffset: 30, xFrequency: 4, yFrequency: 5, xPhase: 45, yPhase: 90 };
    for (const k of Object.keys(want)) await setSpin(page, '[data-e2e="efx-' + k + '"]', want[k]);
    d = await poll(async () => { const x = await detailOf(efxId); return Object.keys(want).every(k => x.typeDetail[k] === want[k]) ? x : null; }, 10000);
    check(!!d, 'every pattern parameter stored: ' + JSON.stringify(d ? Object.fromEntries(Object.keys(want).map(k => [k, d.typeDetail[k]])) : null));
    await sleep(400);
    const patternScaled = await page.eval(canvasHash);
    check(patternLissajous !== patternScaled, 'pattern canvas redrawn for the new geometry');

    console.log('EFX: per-head mode / reverse / start offset');
    const first = d.typeDetail.fixtures[0];
    const other = (first.availableModes || []).find(m => m !== first.mode);
    if (other) {
      await page.click('[data-e2e="efx-heads"] tr:first-child [data-e2e="efx-head-mode"] button');
      const label = { PanTilt: 'Position', Dimmer: 'Dimmer', RGB: 'RGB' }[other];
      await clickText(page, label, '[data-e2e="efx-heads"] tr:first-child [data-e2e="efx-head-mode"]');
      d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.fixtures[0].mode === other ? x : null; });
      check(!!d, 'head mode ' + other + ' stored');
    } else console.log('  skip head mode: fixture offers only ' + first.mode);
    await page.click('[data-e2e="efx-heads"] tr:first-child [data-e2e="efx-head-reverse"]');
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.fixtures[0].direction === 'Backward' ? x : null; });
    check(!!d, 'head reverse (Backward) stored');
    await setSpin(page, '[data-e2e="efx-heads"] tr:first-child [data-e2e="efx-head-offset"]', 45);
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.fixtures[0].startOffset === 45 ? x : null; });
    check(!!d, 'head start offset 45 stored');

    console.log('EFX: reorder');
    const before = d.typeDetail.fixtures.map(f => f.fixture + ':' + f.head);
    await page.click('[data-e2e="efx-heads"] tr:first-child td');
    await page.click('[data-e2e="efx-down"]');
    d = await poll(async () => { const x = await detailOf(efxId); const now = x.typeDetail.fixtures.map(f => f.fixture + ':' + f.head); return now[1] === before[0] && now[0] === before[1] ? x : null; });
    check(!!d, 'first head lowered to position 2');

    console.log('EFX: set an offset on all fixtures');
    await page.click('[data-e2e="efx-offsets"]');
    await setSpin(page, '[data-e2e="efx-offset-value"]', 90);
    await clickText(page, 'Increasing', '[data-e2e="efx-offset-dialog"]');
    await clickText(page, 'Apply');
    d = await poll(async () => { const x = await detailOf(efxId); const offs = x.typeDetail.fixtures.map(f => f.startOffset); return offs.every((o, i) => o === (i * 90) % 360) ? x : null; });
    check(!!d, 'increasing offsets 0, 90, 180, ... stored');

    console.log('EFX: remove a head');
    const countBefore = d.typeDetail.fixtures.length;
    /* remove the first row: the reversed head sits at position 2 after the reorder and must survive for the XML check */
    await page.click('[data-e2e="efx-heads"] tr:first-child td');
    await page.click('[data-e2e="efx-remove"]');
    d = await poll(async () => { const x = await detailOf(efxId); return x.typeDetail.fixtures.length === countBefore - 1 ? x : null; });
    check(!!d, 'first head removed');
    check(!!d && d.typeDetail.fixtures[0].direction === 'Backward', 'the remaining head is the reversed one');
    await page.waitFor('document.querySelector(\'[data-e2e="efx-preview"]\').getAttribute("data-heads") === "' + (countBefore - 1) + '"', 8000);

    console.log('EFX: duration through the timing block');
    await page.click(byText('20 s'));
    await page.waitFor('document.activeElement && document.activeElement.tagName === "INPUT"', 5000);
    await page.eval('(function(){document.activeElement.select(); return true;})()');
    await page.type('10s');
    await page.key('Enter');
    d = await poll(async () => { const x = await detailOf(efxId); return x.duration === 10000 ? x : null; });
    check(!!d, 'duration 10 s stored');

    await sleep(300);
    await page.screenshot(path.join(OUT, 'efx-editor.png'));

    /* ================= Collection ================= */
    console.log('Collection: create through the + menu');
    const collBefore = await listIds('Collection');
    await page.click('button[title^="Add a new function"]');
    await clickText(page, 'New Collection');
    const collId = await poll(async () => (await listIds('Collection')).find(id => collBefore.indexOf(id) === -1));
    check(!!collId, 'a new Collection exists on the server (' + collId + ')');
    await srv.mutate('functions.rename', { functionId: collId, name: COLL_NAME });
    await clickText(page, COLL_NAME);
    await page.waitFor('!!document.querySelector(\'[data-e2e="collection-editor"]\')', 15000);

    console.log('Collection: add members through the picker');
    await page.click('[data-e2e="collection-add"] button');
    await page.waitFor('!!document.querySelector(\'input[placeholder="Search…"]\')', 8000);
    await page.click('input[placeholder="Search…"]');
    await page.type(EFX_NAME);
    await sleep(200);
    await page.waitFor('(' + rowsFn + ').length >= 1', 8000);
    await page.click(new Function('return (' + rowsFn + ')[0];'));
    await page.eval('(function(){const el=document.querySelector(\'input[placeholder="Search…"]\'); el.focus(); el.select(); return true;})()');
    await page.type('Tilt bars on');
    await sleep(200);
    await page.waitFor('(' + rowsFn + ').length >= 1', 8000);
    await page.click(new Function('return (' + rowsFn + ')[0];'));
    await clickText(page, 'Add');
    let c = await poll(async () => { const x = await detailOf(collId); return x.typeDetail.functions.length === 2 ? x : null; });
    check(!!c, 'two members stored');
    eq(c && c.typeDetail.functions[0], efxId, 'first member is the E2E EFX');
    const sceneId = c && c.typeDetail.functions[1];
    await page.waitFor('document.querySelectorAll(\'[data-e2e="collection-members"] tr\').length === 2', 8000);
    check(await page.eval('!!(' + byText(EFX_NAME, '[data-e2e="collection-members"]').toString() + ')()'), 'member row shows the EFX name');

    console.log('Collection: reorder');
    await page.click('[data-e2e="collection-members"] tr:last-child td');
    await page.click('[data-e2e="collection-up"] button');
    c = await poll(async () => { const x = await detailOf(collId); return x.typeDetail.functions[0] === sceneId && x.typeDetail.functions[1] === efxId ? x : null; });
    check(!!c, 'second member moved up (setMembers order)');

    console.log('Collection: a loop is refused by the server');
    let loopErr = null;
    try { await srv.mutate('functions.collection.addFunction', { functionId: collId, memberFunctionId: collId }); } catch (e) { loopErr = e.code; }
    eq(loopErr, 'INVALID_PARAMS', 'self-membership rejected');

    await sleep(300);
    await page.screenshot(path.join(OUT, 'collection-editor.png'));

    console.log('Collection: remove a member');
    await page.click('[data-e2e="collection-members"] tr:first-child td');
    await page.click('[data-e2e="collection-remove"] button');
    c = await poll(async () => { const x = await detailOf(collId); return x.typeDetail.functions.length === 1 && x.typeDetail.functions[0] === efxId ? x : null; });
    check(!!c, 'member removed, EFX remains');

    /* ================= save + grep ================= */
    console.log('save and grep the .qxw');
    const saved = await srv.call('core.project.saveAs', { target: 'serverPath', path: SAVE_PATH });
    check(saved && saved.filePath, 'saved to ' + (saved && saved.filePath));
    const xml = fs.readFileSync(SAVE_PATH, 'utf8');
    const efxBlock = (xml.match(new RegExp('<Function ID="' + efxId + '" Type="EFX" Name="' + EFX_NAME + '">[\\s\\S]*?</Function>')) || [])[0] || '';
    check(!!efxBlock, 'EFX function block present in the XML');
    check(/<Algorithm>Lissajous<\/Algorithm>/.test(efxBlock), 'XML Algorithm Lissajous');
    check(/<PropagationMode>Serial<\/PropagationMode>/.test(efxBlock), 'XML PropagationMode Serial');
    check(/<Width>60<\/Width>/.test(efxBlock) && /<Height>40<\/Height>/.test(efxBlock) && /<Rotation>45<\/Rotation>/.test(efxBlock), 'XML width/height/rotation');
    check(/<DimmerControl>1<\/DimmerControl>/.test(efxBlock), 'XML DimmerControl');
    check((efxBlock.match(/<Fixture>/g) || []).length === countBefore - 1, 'XML lists ' + (countBefore - 1) + ' fixture heads');
    check(/<Direction>Backward<\/Direction>/.test(efxBlock), 'XML has the reversed head');
    check(/<Speed FadeIn="\d+" FadeOut="\d+" Duration="10000"/.test(efxBlock), 'XML duration 10000');
    const collBlock = (xml.match(new RegExp('<Function ID="' + collId + '" Type="Collection" Name="' + COLL_NAME + '">[\\s\\S]*?</Function>')) || [])[0] || '';
    check(!!collBlock, 'Collection function block present in the XML');
    check(new RegExp('<Step Number="0">' + efxId + '</Step>').test(collBlock), 'XML collection step 0 is the EFX');
    check((collBlock.match(/<Step /g) || []).length === 1, 'XML collection has exactly one step');
  } catch (e) {
    failures++;
    console.log('  EXCEPTION ' + (e && e.stack || e));
    try { await page.screenshot(path.join(OUT, 'failure.png')); } catch (e2) { }
  }
  console.log('console errors: ' + JSON.stringify(page.consoleErrors));
  if (page.consoleErrors.length) failures++;
  await b.close();
  srv.close();
  console.log(failures ? 'FAILED (' + failures + ')' : 'ALL OK') ;
  console.log('screenshots in ' + OUT);
  process.exit(failures ? 1 : 0);
})();
