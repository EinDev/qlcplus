// End-to-end check of the Show Wizard (webui/Wizard.jsx) and Import from project
// (webui/ff/ImportProject.jsx) against a sandbox instance (dev-webui-sandbox.ps1).
//
//   .\dev-webui-sandbox.ps1 -Name wizard -BuildDir .\build -WebUiRoot .\webui -ApiPort 9270 -WebUiPort 9271
//   node webui/tools/e2e/wizard-import.js [apiPort] [webUiPort] [sandboxDir]
//
// Starts a NEW project on the sandbox (core.project.new), then drives headless Chrome through
// webui/tools/cdp.js like an operator: opens the wizard from the Fixtures & Functions right rail,
// picks a show type, creates two groups and patches fixtures into them with the wizard's fixture
// browser, picks a stage, toggles effects, walks the controller step, clicks through the VC
// preview and generates. Then imports two functions (with their fixtures) from the sandbox's copy
// of the test project through the Actions menu, loads the same project again as a browser upload,
// and saves into the sandbox directory. Every result is confirmed through a second, plain API
// connection (fixtures.group.list, functions.list/get, vc.widget.list, the imported event) and the
// written .qxw. Exits non-zero on the first failed assertion or on any browser console error.
// Screenshots (wizard-*.png, import.png) land in the sandbox directory.

const path = require('path'), fs = require('fs');
const { launch, sleep } = require('../cdp.js');

const API_PORT = Number(process.argv[2] || 9270);
const WEB_PORT = Number(process.argv[3] || 9271);
const SANDBOX = process.argv[4] || 'C:\\qlcsandbox\\wizard';
const HOST = process.env.E2E_HOST || '127.0.0.1';
const SOURCE = path.join(SANDBOX, 'project.qxw');
const OUT = path.join(SANDBOX, 'out.qxw');

function assert(cond, msg) { if (!cond) throw new Error('ASSERT: ' + msg); }

class Api {
  constructor(port) { this.port = port; this.id = 0; this.pending = new Map(); this.events = []; }
  async open() {
    this.ws = new WebSocket('ws://' + (HOST.indexOf(':') !== -1 ? '[' + HOST + ']' : HOST) + ':' + this.port + '/');
    await new Promise((res, rej) => { this.ws.onopen = res; this.ws.onerror = rej; });
    this.ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.type === 'event') this.events.push(m);
      if (m.type === 'response' && this.pending.has(m.id)) {
        const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id);
        if (m.ok) res(m.result); else { const e = new Error(m.error.code + ': ' + m.error.message); e.code = m.error.code; rej(e); }
      }
    };
    await this.call('hello', { apiVersion: '1', clientName: 'wizard-import e2e' });
  }
  call(method, params = {}) {
    const id = 'e2e-' + (++this.id);
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ type: 'request', id, method, params })); });
  }
  lastEvent(topic) { for (let i = this.events.length - 1; i >= 0; i--) if (this.events[i].topic === topic) return this.events[i].data; return null; }
  close() { try { this.ws.close(); } catch (e) { } }
}

const js = (v) => JSON.stringify(v);
/* element-returning function sources used with page.click / waitFor */
const byAttr = (attr, value) => '(function(){return document.querySelector(' + js('[' + attr + '="' + value.replace(/"/g, '\\"') + '"]') + ');})';
const button = (text, scope) => '(function(){const s=document.querySelector(' + js(scope || 'body') + ');if(!s)return null;return Array.from(s.querySelectorAll("button")).find(b=>b.textContent.trim()===' + js(text) + '&&!b.disabled)||null;})';
const inRow = (rowAttr, rowValue, sel) => '(function(){const r=document.querySelector(' + js('[' + rowAttr + '="' + rowValue.replace(/"/g, '\\"') + '"]') + ');return r?r.querySelector(' + js(sel) + '):null;})';

async function clickEl(page, fnSrc, what) {
  const ok = await page.eval('(function(){const el=(' + fnSrc + ')();if(!el)return false;el.scrollIntoView({block:"center"});return true;})()');
  assert(ok, 'element to click exists: ' + what);
  await page.click(eval(fnSrc));   // cdp.js takes an element-returning function
  await sleep(250);
}
async function typeInto(page, fnSrc, text, what) {
  await clickEl(page, fnSrc, what);
  await page.eval('(function(){const el=(' + fnSrc + ')();el.select&&el.select();})()');
  await page.key('Backspace');
  await page.type(text);
  await sleep(250);
}
async function waitStep(page, n) { await page.waitFor('(function(){const d=document.querySelector("[data-wizard=dialog]");return d&&d.getAttribute("data-wizard-step")===' + js(String(n)) + ';})()', 8000); }

(async () => {
  const api = new Api(API_PORT);
  await api.open();
  const b = await launch();
  let failed = null, page = null;
  try {
    /* a fresh, empty project: the wizard builds everything from scratch */
    await api.call('core.project.new');
    await sleep(800);

    page = await b.open('http://localhost:' + WEB_PORT + '/?ctx=fx');
    await page.waitFor('!!document.querySelector("[data-wizard=open]") && !document.querySelector("[data-wizard=open]").disabled', 20000);
    console.log('F&F screen live, wizard button present');

    /* ---- step 1: show type ---- */
    await clickEl(page, byAttr('data-wizard', 'open'), 'wizard rail button');
    await page.waitFor('!!document.querySelector("[data-wizard-card=show-Concert]")', 10000);
    await clickEl(page, byAttr('data-wizard-card', 'show-Concert'), 'Concert card');
    await clickEl(page, button('Next'), 'Next');
    await waitStep(page, 1);

    /* ---- step 2: two new groups, fixtures patched through the wizard's fixture browser ---- */
    const patchInto = async (box, manufacturer, model, qty) => {
      await clickEl(page, byAttr('data-wizard-box', box), 'box ' + box);
      await typeInto(page, byAttr('data-wizard', 'browser-search'), manufacturer.slice(0, 5), 'browser search');
      await page.waitFor('Array.from(document.querySelectorAll("[data-wizard-def=manufacturer]")).some(e=>e.textContent.trim()===' + js(manufacturer) + ')', 10000);
      await clickEl(page, '(function(){return Array.from(document.querySelectorAll("[data-wizard-def=manufacturer]")).find(e=>e.textContent.trim()===' + js(manufacturer) + ');})', manufacturer);
      await page.waitFor('Array.from(document.querySelectorAll("[data-wizard-def=model]")).some(e=>e.textContent.trim()===' + js(model) + ')', 10000);
      await clickEl(page, '(function(){return Array.from(document.querySelectorAll("[data-wizard-def=model]")).find(e=>e.textContent.trim()===' + js(model) + ');})', model);
      await page.waitFor('(function(){const b=document.querySelector("[data-wizard=browser-add]");return b&&!b.disabled;})()', 10000);
      for (let i = 0; i < qty; i++) {
        const before = await page.eval('(function(){const r=document.querySelector(' + js('[data-wizard-box="' + box + '"]') + ');return r?r.textContent:"";})()');
        await clickEl(page, byAttr('data-wizard', 'browser-add'), 'add to group');
        await page.waitFor('(function(){const r=document.querySelector(' + js('[data-wizard-box="' + box + '"]') + ');return r&&r.textContent!==' + js(before) + '&&!/Patching/.test(document.querySelector("[data-wizard=browser-add]").textContent);})()', 10000);
      }
    };
    await clickEl(page, byAttr('data-wizard', 'add-group'), '+ Add group');
    await page.waitFor('!!document.querySelector("[data-wizard-box=\\"Group 1\\"]")', 5000);
    await typeInto(page, inRow('data-wizard-box', 'Group 1', '[data-wizard-box-name]'), 'Front Movers', 'group name');
    await patchInto('Front Movers', 'Clay Paky', 'Sharpy', 3);
    await clickEl(page, byAttr('data-wizard', 'add-group'), '+ Add group');
    await page.waitFor('!!document.querySelector("[data-wizard-box=\\"Group 2\\"]")', 5000);
    await typeInto(page, inRow('data-wizard-box', 'Group 2', '[data-wizard-box-name]'), 'Wash', 'group name');
    await patchInto('Wash', 'Eurolite', 'LED PAR 56 RGB DMX', 3);
    const patched = (await api.call('fixtures.list')).fixtures;
    assert(patched.length === 6, 'the browser patched 6 fixtures, got ' + patched.length);
    await page.waitFor('!!document.querySelector("[data-wizard-role-row=\\"Front Movers\\"]") && /Pan\\/Tilt/.test(document.querySelector("[data-wizard-role-row=\\"Front Movers\\"]").textContent) && /RGB/.test(document.querySelector("[data-wizard-role-row=\\"Wash\\"]").textContent)', 10000);
    console.log('step 2: groups Front Movers (3 Sharpy) and Wash (3 PAR), capabilities detected');
    await page.screenshot(path.join(SANDBOX, 'wizard-groups.png'));
    await clickEl(page, button('Next'), 'Next');
    await waitStep(page, 2);

    /* ---- step 3: venue ---- */
    await clickEl(page, byAttr('data-wizard-card', 'stage-Rock'), 'Rock stage');
    await page.waitFor('/Front truss/.test(document.querySelector("[data-wizard=dialog]").textContent)', 5000);
    await clickEl(page, button('Next'), 'Next');
    await waitStep(page, 3);

    /* ---- step 4: effects ---- */
    await page.waitFor('!!document.querySelector("[data-wizard-family=Movement]")', 8000);
    const eightOn = () => page.eval('(function(){const c=document.querySelector("[data-wizard-effect=FigureEight]");return c?c.getAttribute("aria-pressed"):null;})()');
    const eightBefore = await eightOn();
    await clickEl(page, byAttr('data-wizard-effect', 'FigureEight'), 'Figure Eight');
    await page.waitFor('(function(){const c=document.querySelector("[data-wizard-effect=FigureEight]");return c&&c.getAttribute("aria-pressed")!==' + js(eightBefore) + ';})()', 5000);
    await clickEl(page, byAttr('data-wizard-family-toggle', 'Matrix'), 'Matrix All/None');
    await sleep(600);
    console.log('step 4: Figure Eight toggled (was ' + eightBefore + '), Matrix family toggled');
    await clickEl(page, button('Next'), 'Next');
    await waitStep(page, 4);

    /* ---- step 5: controller (the sandbox has no plugins, so none is patched) ---- */
    await page.waitFor('/No controller patched/.test(document.querySelector("[data-wizard=dialog]").textContent)', 5000);
    await clickEl(page, byAttr('data-wizard-card', 'ctrl-none'), 'No controller');
    await clickEl(page, button('Next'), 'Next');
    await waitStep(page, 5);

    /* ---- step 6: summary, VC preview, generate ---- */
    await page.waitFor('!!document.querySelector("[data-wizard-vcbutton=\\"Wash\\"]")', 8000);
    await clickEl(page, byAttr('data-wizard-vcbutton', 'Wash'), 'VC preview Wash tab');
    await page.waitFor('/▸ Wash/.test(document.querySelector("[data-wizard=vc-preview]").textContent)', 5000);
    const effectsShown = await page.eval('document.querySelector("[data-wizard=summary-effects]").textContent');
    assert(/Figure Eight/.test(effectsShown) !== /true/.test(String(eightBefore)), 'summary reflects the Figure Eight toggle: ' + effectsShown);
    await page.screenshot(path.join(SANDBOX, 'wizard-summary.png'));
    await page.waitFor('!!' + button('Generate') + '()', 8000);
    await clickEl(page, button('Generate'), 'Generate');
    await page.waitFor('!!document.querySelector("[data-wizard=result]")', 60000);
    const resultText = await page.eval('document.querySelector("[data-wizard=result]").textContent');
    console.log('generated:', resultText);
    await page.screenshot(path.join(SANDBOX, 'wizard-result.png'));

    const groups = (await api.call('fixtures.group.list')).groups.map(g => g.name);
    for (const n of ['Front Movers', 'Wash', 'All Groups']) assert(groups.indexOf(n) !== -1, 'group ' + n + ' exists: ' + groups.join(', '));
    const fnList = (await api.call('functions.list')).functions;
    assert(fnList.length > 20, 'the wizard generated functions: ' + fnList.length);
    const gen = api.lastEvent('core.wizard.generated');
    assert(gen && gen.functionIds.length === fnList.length, 'core.wizard.generated lists every function (' + (gen && gen.functionIds.length) + ' vs ' + fnList.length + ')');
    assert(fnList.some(f => /Figure Eight/.test(f.name)) !== /true/.test(String(eightBefore)), 'Figure Eight generated iff it was switched on');
    assert(fnList.some(f => f.type === 'RGBMatrix'), 'matrix effects were generated');
    const widgets = (await api.call('vc.widget.list', { page: 0 })).widgets || [];
    assert(widgets.length > 0 && gen.vcWidgetIds.length > 10, 'VC widgets were generated: ' + widgets.length + ' top level, ' + gen.vcWidgetIds.length + ' total');
    const monitor = await api.call('fixtures.monitor.get');
    console.log('stage after generate:', JSON.stringify(monitor.stage).slice(0, 160));
    await clickEl(page, button('Close'), 'Close');

    /* ---- import two functions (+ their fixtures) from the sandbox's SF3 copy ---- */
    const list = await api.call('core.project.importList', { source: 'path', path: SOURCE });
    const small = list.functions.filter(f => f.type === 'Scene' && f.dependencies.fixtureIds.length > 0 && f.dependencies.fixtureIds.length <= 4 && !f.dependencies.fixtureGroupIds.length && !f.dependencies.paletteIds.length);
    assert(small.length >= 2, 'the source has two small scenes');
    const picks = [small[0], small.find(f => f.name !== small[0].name && f.dependencies.fixtureIds.join() !== small[0].dependencies.fixtureIds.join()) || small[1]];
    console.log('importing', picks.map(f => f.name + ' (fixtures ' + f.dependencies.fixtureIds.join(',') + ')').join(' + '));

    await clickEl(page, '(function(){return document.querySelector("[aria-label=\\"Actions menu\\"]");})', 'Actions menu');
    await page.waitFor('Array.from(document.querySelectorAll("[role=menu] *")).some(e=>e.textContent.trim()==="Import from project…")', 5000);
    await clickEl(page, '(function(){return Array.from(document.querySelectorAll("[role=menu] > *")).find(e=>e.textContent.trim()==="Import from project…");})', 'Import from project…');
    await page.waitFor('!!document.querySelector("[data-import=dialog]")', 5000);
    await typeInto(page, byAttr('data-import', 'path'), SOURCE, 'import path');
    await clickEl(page, byAttr('data-import', 'load'), 'Load');
    await page.waitFor('!!document.querySelector("[data-import=loaded]")', 20000);
    for (const f of picks) {
      await typeInto(page, byAttr('data-import', 'search-functions'), f.name, 'function search');
      await page.waitFor('!!document.querySelector(' + js('[data-import-row="function-' + f.name.replace(/"/g, '\\"') + '"]') + ')', 5000);
      await clickEl(page, inRow('data-import-row', 'function-' + f.name, 'button'), 'tick ' + f.name);
    }
    /* the dependencies were ticked with them, like the desktop popup */
    await typeInto(page, byAttr('data-import', 'search-functions'), '', 'clear search');
    const depFixture = list.fixtures.find(x => x.id === picks[0].dependencies.fixtureIds[0]);
    await typeInto(page, byAttr('data-import', 'search-fixtures'), depFixture.name, 'fixture search');
    await page.screenshot(path.join(SANDBOX, 'import.png'));
    await clickEl(page, button('Apply'), 'Apply');
    await page.waitFor('!!document.querySelector("[data-import=result]")', 20000);
    console.log('import:', await page.eval('document.querySelector("[data-import=result]").textContent'));
    const imported = api.lastEvent('core.project.imported');
    assert(imported, 'core.project.imported received');
    assert(Object.keys(imported.functionIdMap).length === 2, 'two functions imported: ' + js(imported.functionIdMap));
    for (const f of picks) {
      const newId = imported.functionIdMap[f.id];
      assert(newId, 'function ' + f.name + ' was imported');
      const detail = await api.call('functions.get', { functionId: newId });
      const want = f.dependencies.fixtureIds.map(id => imported.fixtureIdMap[id]).filter(Boolean).sort();
      const got = (detail.typeDetail.fixtures || []).map(String).sort();
      assert(want.length === f.dependencies.fixtureIds.length, 'every fixture of ' + f.name + ' was imported (' + js(imported.fixtureIdMap) + ', skipped ' + js(imported.skippedFixtureIds) + ')');
      assert(js(got) === js(want), f.name + ' references the imported fixtures: ' + js(got) + ' vs ' + js(want));
      for (const fid of got) {
        const fx = await api.call('fixtures.get', { fixtureId: fid });
        assert(fx && fx.name, 'imported fixture ' + fid + ' resolves');
      }
    }
    console.log('imported scenes reference the imported fixture ids');
    await clickEl(page, button('Close'), 'Close');

    /* ---- the same project as a browser upload ---- */
    await clickEl(page, '(function(){return document.querySelector("[aria-label=\\"Actions menu\\"]");})', 'Actions menu');
    await clickEl(page, '(function(){return Array.from(document.querySelectorAll("[role=menu] > *")).find(e=>e.textContent.trim()==="Import from project…");})', 'Import from project…');
    await page.waitFor('!!document.querySelector("[data-import=dialog]")', 5000);
    const doc = await page.s.send('DOM.getDocument', { depth: -1 });
    const node = await page.s.send('DOM.querySelector', { nodeId: doc.root.nodeId, selector: '[data-import=file]' });
    await page.s.send('DOM.setFileInputFiles', { nodeId: node.nodeId, files: [SOURCE] });
    await page.waitFor('!!document.querySelector("[data-import=loaded]") && /project\\.qxw/.test(document.querySelector("[data-import=loaded]").textContent)', 30000);
    const uploadedCount = await page.eval('document.querySelectorAll("[data-import-row^=function-]").length');
    assert(uploadedCount > 10, 'the uploaded project lists its functions: ' + uploadedCount);
    console.log('upload: listed', uploadedCount, 'function rows');
    await clickEl(page, button('Cancel'), 'Cancel');

    /* ---- save into the sandbox and read the file back ---- */
    if (fs.existsSync(OUT)) fs.unlinkSync(OUT);
    await api.call('core.project.saveAs', { target: 'serverPath', path: OUT });
    const xml = fs.readFileSync(OUT, 'utf8');
    for (const n of ['Front Movers', 'Wash', 'All Groups', picks[0].name, picks[1].name]) assert(xml.indexOf(n) !== -1, 'out.qxw contains ' + n);
    console.log('saved', OUT, Math.round(xml.length / 1024) + ' kB');

    /* the generated Virtual Console renders in the VC screen */
    await page.goto('http://localhost:' + WEB_PORT + '/?ctx=vc');
    await page.waitFor('/Blackout/.test(document.body.textContent) && /All Groups/.test(document.body.textContent)', 20000);
    await sleep(800);
    await page.screenshot(path.join(SANDBOX, 'wizard-vc.png'));
    console.log('VC screen shows the generated frame');

    await sleep(500);
    assert(page.consoleErrors.length === 0, 'no console errors: ' + page.consoleErrors.join(' | '));
    console.log('PASS');
  } catch (e) {
    failed = e;
    console.error(e.message);
    if (page) {
      try { await page.screenshot(path.join(SANDBOX, 'wizard-failure.png')); } catch (x) { }
      console.error('console errors:', page.consoleErrors);
    }
  } finally {
    api.close();
    await b.close();
  }
  process.exit(failed ? 1 : 0);
})();
