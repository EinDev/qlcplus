// Socket-level smoke test of the fixturedefs.* domain against a sandbox instance
// (dev-webui-sandbox.ps1 -Name fixdefs -ApiPort 9200 -WebUiPort 9201, SF3 project, with
// QLCPLUS_USER_FIXTURE_DIR pointing at a copy of the user's fixture folder so the SF3 custom
// definitions resolve and nothing can be written into the real profile).
//
//   node webui/tools/e2e/fixturedefs-smoke.js [--api 9200]
//
// No browser: there is no editor screen yet. It lists the library (timing the full force-load),
// opens an SF3 definition in a session, edits it in memory, exports it, checks the events, and
// closes the session again. Nothing is saved. Exit code 0 = every assertion held.

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf('--' + name); return i !== -1 ? args[i + 1] : def; };
const API_PORT = Number(opt('api', 9200));

let failures = 0;
function check(cond, what) { if (cond) console.log('  ok   ' + what); else { failures++; console.log('  FAIL ' + what); } }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function apiClient() {
  const ws = new WebSocket('ws://127.0.0.1:' + API_PORT + '/');
  let id = 0; const pending = new Map(); const events = [];
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'response' && pending.has(m.id)) { const { res, rej } = pending.get(m.id); pending.delete(m.id); m.ok ? res(m.result) : rej(Object.assign(new Error(m.error.message), m.error)); }
    else if (m.type === 'event') events.push(m);
  };
  const call = (method, params) => new Promise((res, rej) => { const rid = 'smoke-' + (++id); pending.set(rid, { res, rej }); ws.send(JSON.stringify({ type: 'request', id: rid, method, params: params || {} })); });
  const ready = new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; }).then(() => call('hello', { apiVersion: '1', clientName: 'fixturedefs smoke' }));
  return { call, ready, events, close: () => ws.close() };
}

(async () => {
  const api = apiClient();
  await api.ready;

  console.log('fixturedefs.list (full library, forces every definition to load)');
  let t = Date.now();
  const all = await api.call('fixturedefs.list');
  const listMs = Date.now() - t;
  check(all.entries.length > 100, 'library has ' + all.entries.length + ' definitions (' + listMs + ' ms)');
  t = Date.now();
  const again = await api.call('fixturedefs.list');
  console.log('  second call: ' + (Date.now() - t) + ' ms (' + again.entries.length + ' entries)');

  const sf3 = await api.call('fixturedefs.list', { manufacturer: 'SF3' });
  check(sf3.entries.length > 0, 'SF3 has ' + sf3.entries.length + ' user definitions: ' + sf3.entries.map(e => e.model).join(', '));
  check(sf3.entries.every(e => e.isUser), 'every SF3 entry is a user definition');

  const target = sf3.entries.find(e => e.model === 'Cob Blinder') || sf3.entries[0];
  const got = await api.call('fixturedefs.get', { manufacturer: 'SF3', model: target.model });
  check(got.definition.channels.length === target.channelCount && got.definition.modes.length === target.modeCount,
        'fixturedefs.get SF3 / ' + target.model + ': ' + got.definition.channels.length + ' channels, ' + got.definition.modes.length + ' modes, defRevision ' + got.defRevision);
  check(typeof got.definition.sourceFile === 'string' && /fixdefs/i.test(got.definition.sourceFile),
        'definition file lives in the sandbox copy: ' + got.definition.sourceFile);

  console.log('fixturedefs.session.open + edit in memory');
  const opened = await api.call('fixturedefs.session.open', { manufacturer: 'SF3', model: target.model });
  check(opened.sessionId && opened.sessionRevision === 0 && opened.isUser === true && opened.baseRevision === got.defRevision,
        'session ' + opened.sessionId + ' opened, revision 0, isUser, baseRevision ' + opened.baseRevision);
  check(JSON.stringify(opened.definition.modes.map(m => m.name)) === JSON.stringify(got.definition.modes.map(m => m.name)),
        'session definition matches the library: modes ' + opened.definition.modes.map(m => m.name).join(', '));
  const firstChannel = opened.definition.channels[0];
  check(firstChannel && firstChannel.channelId === 'ch-1' && Array.isArray(firstChannel.capabilities),
        'first channel "' + (firstChannel && firstChannel.name) + '" (' + (firstChannel && firstChannel.group) + ') has ' + (firstChannel && firstChannel.capabilities.length) + ' capabilities');

  const upd = await api.call('fixturedefs.session.update', { sessionId: opened.sessionId, baseRevision: 0, author: 'smoke test' });
  check(upd.sessionRevision === 1, 'session.update -> sessionRevision 1');
  const added = await api.call('fixturedefs.channel.add', { sessionId: opened.sessionId, baseRevision: 1, name: 'Smoke channel', preset: 'IntensityDimmer' });
  check(/^ch-\d+$/.test(added.channelId) && added.sessionRevision === 2, 'channel.add -> ' + added.channelId + ', revision 2');
  let conflict = null;
  try { await api.call('fixturedefs.session.update', { sessionId: opened.sessionId, baseRevision: 0, author: 'stale' }); } catch (e) { conflict = e; }
  check(conflict && conflict.code === 'CONFLICT' && conflict.details.sessionRevision === 2, 'stale baseRevision -> CONFLICT with current sessionRevision');

  const validation = await api.call('fixturedefs.session.validate', { sessionId: opened.sessionId });
  check(Array.isArray(validation.warnings), 'validate -> ' + validation.warnings.length + ' warning(s)' + (validation.warnings.length ? ': ' + validation.warnings.join(' | ') : ''));

  const exported = await api.call('fixturedefs.export', { sessionId: opened.sessionId });
  const xml = Buffer.from(exported.qxfBase64, 'base64').toString('utf8');
  check(xml.startsWith('<?xml') && xml.includes('<Model>' + target.model + '</Model>') && xml.includes('Smoke channel'),
        'export of the session is a QXF with the unsaved edit (' + exported.fileName + ', ' + xml.length + ' chars)');

  const list = await api.call('fixturedefs.session.list');
  const info = list.sessions.find(s => s.sessionId === opened.sessionId);
  check(info && info.isModified === true && info.sessionRevision === 2, 'session.list shows the session as modified at revision 2');

  // Never saved: the library copy on disk is untouched.
  const libraryAfter = await api.call('fixturedefs.get', { manufacturer: 'SF3', model: target.model });
  check(libraryAfter.definition.channels.length === got.definition.channels.length && libraryAfter.defRevision === got.defRevision,
        'library definition unchanged (nothing saved)');

  const closed = await api.call('fixturedefs.session.close', { sessionId: opened.sessionId });
  check(closed.sessionId === opened.sessionId, 'session.close');
  await sleep(200);
  const topics = api.events.map(e => e.topic);
  check(topics.includes('fixturedefs.session.opened') && topics.includes('fixturedefs.session.updated') && topics.includes('fixturedefs.session.closed'),
        'events seen: ' + Array.from(new Set(topics)).join(', '));
  const updates = api.events.filter(e => e.topic === 'fixturedefs.session.updated');
  check(updates.length === 2 && updates[0].data.changeKind === 'metadata' && updates[1].data.changeKind === 'channel.add' && updates[1].data.definition.channels.length === got.definition.channels.length + 1,
        'session.updated events carry changeKind + full definition snapshot');

  api.close();
  console.log(failures ? failures + ' FAILURE(S)' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch(e => { console.error('error:', e); process.exit(2); });
