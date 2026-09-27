// Minimal headless-Chrome driver over the DevTools protocol, for end-to-end tests of the web UI.
// No npm dependencies (Node >= 22: global WebSocket). Usage from a test script:
//
//   const { launch } = require('./webui/tools/cdp.js');
//   const b = await launch();                       // headless Chrome, random debugging port
//   const page = await b.open('http://localhost:9121/?ctx=fx');
//   await page.waitFor(() => window.__qlcReady === true || document.querySelector('...'), 15000);
//   const n = await page.eval('document.querySelectorAll("button").length');
//   await page.click('#some-id');                   // real mouse event at the element's centre
//   await page.screenshot('shot.png');
//   console.log(page.consoleErrors);                // console.error + uncaught exceptions
//   await b.close();
//
// Every `eval`/`waitFor` runs in the page; pass a string expression or a function (stringified).
// Tabs that are not in front are throttled by Chrome - `open()` brings its tab to front.

const fs = require('fs'), os = require('os'), path = require('path'), { spawn } = require('child_process'), http = require('http');

const CHROME = [process.env.CHROME_PATH, 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe', 'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe']
  .filter(Boolean).find(p => fs.existsSync(p));

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
function getJson(url) {
  return new Promise((res, rej) => http.get(url, r => { let s = ''; r.on('data', d => s += d); r.on('end', () => { try { res(JSON.parse(s)); } catch (e) { rej(e); } }); }).on('error', rej));
}

class Session {
  constructor(ws) {
    this.ws = ws; this.id = 0; this.pending = new Map(); this.handlers = new Map();
    ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id && this.pending.has(m.id)) { const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); }
      else if (m.method && this.handlers.has(m.method)) this.handlers.get(m.method).forEach(h => h(m.params));
    };
  }
  send(method, params = {}) {
    const id = ++this.id;
    return new Promise((res, rej) => { this.pending.set(id, { res, rej }); this.ws.send(JSON.stringify({ id, method, params })); });
  }
  on(method, h) { if (!this.handlers.has(method)) this.handlers.set(method, []); this.handlers.get(method).push(h); }
}

class Page {
  constructor(session, targetId) { this.s = session; this.targetId = targetId; this.consoleErrors = []; this.consoleAll = []; }
  async init() {
    await this.s.send('Page.enable'); await this.s.send('Runtime.enable'); await this.s.send('Log.enable');
    this.s.on('Runtime.consoleAPICalled', p => {
      const text = p.args.map(a => a.value !== undefined ? String(a.value) : (a.description || a.type)).join(' ');
      this.consoleAll.push(p.type + ': ' + text);
      if (p.type === 'error') this.consoleErrors.push(text);
    });
    this.s.on('Runtime.exceptionThrown', p => this.consoleErrors.push('EXCEPTION: ' + (p.exceptionDetails.exception && p.exceptionDetails.exception.description || p.exceptionDetails.text)));
    this.s.on('Log.entryAdded', p => { if (p.entry.level === 'error') this.consoleErrors.push(p.entry.source + ': ' + p.entry.text + (p.entry.url ? ' @ ' + p.entry.url : '')); });
  }
  async goto(url) {
    await this.s.send('Page.navigate', { url });
    await this.waitFor('document.readyState === "complete"', 30000);
  }
  /** Evaluate an expression or function in the page; returns the JSON value (awaits promises). */
  async eval(exprOrFn) {
    const expression = typeof exprOrFn === 'function' ? '(' + exprOrFn.toString() + ')()' : exprOrFn;
    const r = await this.s.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error('eval failed: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description || r.exceptionDetails.text));
    return r.result.value;
  }
  /** Poll until the expression is truthy; throws after timeout ms. */
  async waitFor(exprOrFn, timeout = 15000, interval = 200) {
    const t0 = Date.now();
    for (;;) {
      let v = false; try { v = await this.eval(exprOrFn); } catch (e) { }
      if (v) return v;
      if (Date.now() - t0 > timeout) throw new Error('waitFor timed out: ' + String(exprOrFn).slice(0, 200));
      await sleep(interval);
    }
  }
  async rectOf(selectorOrFn) {
    const fn = typeof selectorOrFn === 'function' ? '(' + selectorOrFn.toString() + ')()' : 'document.querySelector(' + JSON.stringify(selectorOrFn) + ')';
    const r = await this.eval('(function(){const el=' + fn + '; if(!el) return null; const b=el.getBoundingClientRect(); return {x:b.x,y:b.y,w:b.width,h:b.height};})()');
    if (!r) throw new Error('element not found: ' + String(selectorOrFn).slice(0, 200));
    return r;
  }
  async mouse(type, x, y, extra = {}) { await this.s.send('Input.dispatchMouseEvent', Object.assign({ type, x, y, button: 'left', clickCount: 1 }, extra)); }
  /** Real mouse click at the centre of an element (selector string or element-returning function). */
  async click(selectorOrFn, opts = {}) {
    const r = await this.rectOf(selectorOrFn);
    const x = r.x + r.w / 2, y = r.y + r.h / 2;
    await this.mouse('mouseMoved', x, y);
    await this.mouse('mousePressed', x, y, opts); await this.mouse('mouseReleased', x, y, opts);
  }
  /** Press-move-release drag between two page points. */
  async drag(x1, y1, x2, y2, steps = 8) {
    await this.mouse('mouseMoved', x1, y1); await this.mouse('mousePressed', x1, y1);
    for (let i = 1; i <= steps; i++) await this.mouse('mouseMoved', x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps, { button: 'left', buttons: 1 });
    await this.mouse('mouseReleased', x2, y2);
  }
  /** Type text into the focused element (use click() first). */
  async type(text) {
    for (const ch of text) await this.s.send('Input.dispatchKeyEvent', { type: 'char', text: ch });
  }
  async key(key, opts = {}) {
    const codes = { Enter: { windowsVirtualKeyCode: 13, code: 'Enter', text: '\r' }, Escape: { windowsVirtualKeyCode: 27, code: 'Escape' }, Tab: { windowsVirtualKeyCode: 9, code: 'Tab' }, Backspace: { windowsVirtualKeyCode: 8, code: 'Backspace' }, Delete: { windowsVirtualKeyCode: 46, code: 'Delete' } };
    const c = Object.assign({ key }, codes[key] || {}, opts);
    await this.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyDown' }, c));
    await this.s.send('Input.dispatchKeyEvent', Object.assign({ type: 'keyUp' }, c));
  }
  async screenshot(file) {
    const r = await this.s.send('Page.captureScreenshot', { format: 'png' });
    fs.writeFileSync(file, Buffer.from(r.data, 'base64'));
    return file;
  }
  async bringToFront() { await this.s.send('Page.bringToFront'); }
}

class Browser {
  constructor(proc, port, userDir) { this.proc = proc; this.port = port; this.userDir = userDir; this.pages = []; }
  async open(url, { width = 1600, height = 1000 } = {}) {
    const t = await getJson(`http://127.0.0.1:${this.port}/json/new?about:blank`).catch(async () => {
      // Chrome >= 111 requires PUT for /json/new
      return new Promise((res, rej) => { const req = http.request({ host: '127.0.0.1', port: this.port, path: '/json/new?about:blank', method: 'PUT' }, r => { let s = ''; r.on('data', d => s += d); r.on('end', () => res(JSON.parse(s))); }); req.on('error', rej); req.end(); });
    });
    const ws = new WebSocket(t.webSocketDebuggerUrl);
    await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
    const s = new Session(ws);
    const page = new Page(s, t.id);
    await page.init();
    await s.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    await page.bringToFront();
    if (url) await page.goto(url);
    this.pages.push(page);
    return page;
  }
  async close() {
    try { this.proc.kill(); } catch (e) { }
    await sleep(300);
    try { fs.rmSync(this.userDir, { recursive: true, force: true }); } catch (e) { }
  }
}

/** Launch headless Chrome with a random remote-debugging port (parallel-safe). */
async function launch({ headless = true } = {}) {
  if (!CHROME) throw new Error('Chrome not found; set CHROME_PATH');
  const userDir = fs.mkdtempSync(path.join(os.tmpdir(), 'qlc-cdp-'));
  const args = ['--remote-debugging-port=0', `--user-data-dir=${userDir}`, '--no-first-run', '--no-default-browser-check', '--disable-gpu',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--window-size=1600,1000', 'about:blank'];
  if (headless) args.unshift('--headless=new');
  const proc = spawn(CHROME, args, { stdio: 'ignore' });
  const portFile = path.join(userDir, 'DevToolsActivePort');
  const t0 = Date.now();
  while (!fs.existsSync(portFile)) { if (Date.now() - t0 > 20000) throw new Error('Chrome did not start'); await sleep(100); }
  await sleep(200);
  const port = parseInt(fs.readFileSync(portFile, 'utf8').split('\n')[0], 10);
  return new Browser(proc, port, userDir);
}

module.exports = { launch, sleep };
