/**
 * qlcplus-api.js — WebSocket client for the QLC+ Control API.
 *
 * Protocol verified by reading source directly against a live dev build of a QLC+ fork
 * (controlapi/src/*.cpp, docs/api-spec/) — that repo is under active development elsewhere,
 * so treat every detail below as "true as of that reading," not a stable public spec.
 *
 * Transport: a plain WebSocket at ws://<host>:<port>/ (any path — the server does not filter
 * on it), default port 9010, carrying JSON envelopes:
 *   request:  {"type":"request","id":"<n>","method":"<name>","params":{...}}
 *   response: {"type":"response","id":"<n>","ok":true,"result":{...}}
 *          or {"type":"response","id":"<n>","ok":false,"error":{"code","message","details"?}}
 *   event:    {"type":"event","topic":"<name>","data":{...},"originClientId":<id>|null}
 * A malformed/non-JSON request is silently dropped (no reply). Every method except "hello"
 * is rejected with an UNAUTHORIZED error until "hello" has completed once per connection —
 * this client sends it automatically on open, before anything else. There is no password/auth
 * actually enforced today: hello's params are ignored server-side.
 *
 * Virtual Console live interaction: the spec now defines vc.slider.setValue / vc.button.press
 * (api/domains/virtualconsole.js) — setWidget() below routes to them by widget kind. Whether
 * this fork's running server actually implements them yet (vs. just the earlier-confirmed
 * structural vc.widget.* CRUD) is unverified from a spec reading alone; if they still 404,
 * that's the server catching up, not a client bug.
 *
 * Channel numbering: this client keeps the same "absolute channel" convention the legacy
 * protocol used (1-based, stride 512 per universe: universe 2 address 1 = channel 513) via
 * absoluteChannel(), so callers don't need to know the wire format changed. Internally the
 * server wants a 0-based flat "address" (= absoluteChannel - 1); this client does that
 * conversion at the edges.
 *
 * Usage:
 *   const qlc = new QLCPlusAPI('localhost');
 *   qlc.on('ready', () => qlc.getChannelsValues(1, 1, 64));
 *   qlc.on('channels', (rows) => console.log(rows)); // [{channel, value, type, overriding}]
 *   qlc.connect();
 *   qlc.setChannel(qlc.absoluteChannel(1, 5), 255);
 *
 * This file is the transport + the handful of methods that need real translation (channel
 * numbering, decoded arrays, event routing). Full method coverage of the spec
 * (docs/api-spec/fragments/*.yaml in that repo — core/fixtures/fixturedefs/functions-core/
 * functions-advanced/io/palette/virtualconsole) lives in api/domains/*.js, one file per
 * fragment, each adding one namespace (qlc.core, qlc.fixtures, qlc.fixtureDefs, qlc.functions,
 * qlc.functionsAdvanced, qlc.io, qlc.palette, qlc.vc) of thin pass-through wrappers — load
 * those <script> tags after this one. Any method with no wrapper yet still works via the
 * generic qlc.api.<dot.path>(params) proxy below (=== qlc.call('<dot.path>', params)), and
 * any event topic can always be listened to directly: qlc.on('vc.widget.created', fn).
 */
(function (root) {
  'use strict';

  var DEFAULT_PORT = 9010;

  function QLCPlusAPI(host, options) {
    options = options || {};
    this.host = host || (typeof location !== 'undefined' && location.hostname) || 'localhost';
    this.port = options.port || DEFAULT_PORT;
    this.autoReconnect = options.autoReconnect !== false;
    this.reconnectDelay = options.reconnectDelay || 1000;
    this.socket = null;
    this._handlers = {};
    this._pending = {};
    this._nextId = 1;
    this._lastDeskQuery = null;
    this._closedByUser = false;
    this.clientId = null;
    this.serverVersion = null;
  }

  QLCPlusAPI.prototype.url = function () {
    return 'ws://' + this.host + ':' + this.port + '/';
  };

  /**
   * Subscribe. Events:
   *   'open' | 'close' | 'error'  — socket lifecycle
   *   'ready'                     — hello completed; every other method is safe to call now
   *   'message'                   — every inbound frame, parsed
   *   'event'                     — every inbound {type:"event"} frame, unparsed
   *   'apiError'                  — {method, error} for any {ok:false} response
   *   'channels'                  — decoded Simple Desk rows: [{channel, value, type, overriding}]
   *   'grandmaster' | 'blackout'  — pushed value changes
   *   '<method>'                  — the parsed result of that method's own response (e.g.
   *                                  'getWidgetsList' -> {widgets:[...]}, 'getFunctionsList' -> {functions:[...]})
   */
  QLCPlusAPI.prototype.on = function (event, fn) {
    (this._handlers[event] = this._handlers[event] || []).push(fn);
    return this;
  };

  QLCPlusAPI.prototype._emit = function (event, payload) {
    (this._handlers[event] || []).forEach(function (fn) {
      try { fn(payload); } catch (e) { /* a listener must not kill the socket */ }
    });
  };

  QLCPlusAPI.prototype.connect = function () {
    var self = this;
    this._closedByUser = false;
    var ws;
    try { ws = new WebSocket(this.url()); }
    catch (e) { this._emit('error', e); return this; }
    this.socket = ws;

    ws.onopen = function () {
      self._emit('open');
      /* Every other method 403s until this completes — see file header. */
      self.call('hello', {}).then(function (result) {
        self.clientId = result.clientId;
        self.serverVersion = result.serverVersion;
        self._emit('ready', result);
      }, function (err) {
        if (err && err.code === 'LOCAL_CLOSED') return; // already reported via 'close'
        self._emit('error', err);
      });
    };
    ws.onerror = function (e) { self._emit('error', e); };
    ws.onclose = function (e) {
      var pending = self._pending;
      self._pending = {};
      Object.keys(pending).forEach(function (id) {
        var err = new Error('socket closed');
        err.code = 'LOCAL_CLOSED';
        pending[id].reject(err);
      });
      self._emit('close', e);
      if (self.autoReconnect && !self._closedByUser) {
        setTimeout(function () { self.connect(); }, self.reconnectDelay);
      }
    };
    ws.onmessage = function (e) { self._dispatch(e.data); };
    return this;
  };

  QLCPlusAPI.prototype.disconnect = function () {
    this._closedByUser = true;
    if (this.socket) this.socket.close();
    return this;
  };

  QLCPlusAPI.prototype.connected = function () {
    return !!this.socket && this.socket.readyState === 1;
  };

  /** Send one request; resolves with `result`, rejects with an Error (`.code`, `.details` set
      from the server's error object when the rejection came from an {ok:false} response). */
  QLCPlusAPI.prototype.call = function (method, params) {
    var self = this;
    return new Promise(function (resolve, reject) {
      if (!self.connected()) { var e = new Error('not connected'); e.code = 'NOT_CONNECTED'; reject(e); return; }
      var id = String(self._nextId++);
      self._pending[id] = { resolve: resolve, reject: reject, method: method };
      self.socket.send(JSON.stringify({ type: 'request', id: id, method: method, params: params || {} }));
    });
  };

  /** Fire-and-forget call(): errors still reach 'apiError', but nothing here requires a .catch(). */
  QLCPlusAPI.prototype.send = function (method, params) {
    this.call(method, params).catch(function () {});
    return this;
  };

  QLCPlusAPI.prototype._dispatch = function (raw) {
    var frame;
    try { frame = JSON.parse(raw); } catch (e) { return; }
    this._emit('message', frame);

    if (frame.type === 'response') {
      var pending = this._pending[frame.id];
      if (!pending) return;
      delete this._pending[frame.id];
      if (frame.ok) { pending.resolve(frame.result); return; }
      var err = new Error((frame.error && frame.error.message) || (pending.method + ' failed'));
      err.code = frame.error && frame.error.code;
      err.details = frame.error && frame.error.details;
      this._emit('apiError', { method: pending.method, error: frame.error });
      pending.reject(err);
      return;
    }

    if (frame.type === 'event') {
      this._emit('event', frame);
      this._emit(frame.topic, frame.data); // generic: qlc.on('vc.widget.created', fn) always works
      this._routeEvent(frame.topic, frame.data);
    }
  };

  /** Translate a handful of well-known push topics into the friendlier events the UI kit uses. */
  QLCPlusAPI.prototype._routeEvent = function (topic, data) {
    if (topic === 'io.grandMaster.changed') { this._emit('grandmaster', data.value); return; }
    if (topic === 'io.blackout.changed') { this._emit('blackout', data.blackout); return; }
    if (topic === 'io.simpleDesk.channelChanged') {
      /* One channel per event: {address, value, overridden}. `address` is the same 0-based
         flat encoding setChannel()/resetChannel() send — +1 gets back to absoluteChannel(). */
      this._emit('channels', [{
        channel: data.address + 1, value: data.value, type: '', overriding: !!data.overridden
      }]);
      return;
    }
    if (topic === 'io.simpleDesk.universeReset') {
      /* Unlike a single channel reset, a universe reset doesn't say what the channels landed
         on — refresh whichever universe is currently being watched, if any. */
      var q = this._lastDeskQuery;
      if (q && (q.universe - 1) === data.universeId) this.getChannelsValues(q.universe, q.address, q.count);
      return;
    }
  };

  QLCPlusAPI.prototype.subscribe = function (topics) { return this.call('subscribe', { topics: topics }); };
  QLCPlusAPI.prototype.unsubscribe = function (topics) { return this.call('unsubscribe', { topics: topics }); };

  /* --- Simple Desk --------------------------------------------------------
     Channel numbers passed IN to this client are absolute & 1-based, stride 512 (legacy
     convention) — see absoluteChannel(). Converted to the server's 0-based flat `address`
     (= absoluteChannel - 1) right before sending. */

  QLCPlusAPI.prototype.absoluteChannel = function (universe, address) {
    return ((Math.max(1, universe | 0) - 1) * 512) + (address | 0);
  };

  /** Set one channel. `channel` must be ABSOLUTE — see absoluteChannel(). */
  QLCPlusAPI.prototype.setChannel = function (channel, value) {
    return this.send('io.simpleDesk.setChannel', {
      address: channel - 1, value: Math.max(0, Math.min(255, Math.round(value)))
    });
  };

  /** Ask for every channel value in one universe. Answered once, as a 'channels' event —
      also pushed live afterwards via io.simpleDesk.channelChanged, so no polling needed. */
  QLCPlusAPI.prototype.getChannelsValues = function (universe, address, count) {
    var self = this;
    universe = universe || 1; address = address || 1; count = count || 512;
    this._lastDeskQuery = { universe: universe, address: address, count: count };
    return this.call('io.simpleDesk.get', { universeId: universe - 1 }).then(function (result) {
      var lo = self.absoluteChannel(universe, address) + 1;
      var hi = lo + count - 1;
      var rows = (result.channels || [])
        .map(function (c) { return { channel: c.address + 1, value: c.value, type: '', overriding: !!c.overridden }; })
        .filter(function (r) { return r.channel >= lo && r.channel <= hi; });
      self._emit('channels', rows);
      return rows;
    });
  };

  /** Release a manual override. This is NOT the same as setting the channel to 0. */
  QLCPlusAPI.prototype.resetChannel = function (absoluteChannel) {
    return this.send('io.simpleDesk.resetChannel', { address: absoluteChannel - 1 });
  };

  QLCPlusAPI.prototype.resetUniverse = function (universe) {
    return this.send('io.simpleDesk.resetUniverse', { universeId: Math.max(1, universe || 1) - 1 });
  };

  /** No-ops kept only so callers written for the old poll-based protocol don't need to
      change: io.simpleDesk.channelChanged is now pushed on every change, nothing to poll.
      getChannelsValues() (called once up front) already seeds the initial state. */
  QLCPlusAPI.prototype.startPolling = function (universe, address, count) {
    this.getChannelsValues(universe, address, count);
    return this;
  };
  QLCPlusAPI.prototype.stopPolling = function () { this._lastDeskQuery = null; return this; };

  /* --- Functions -------------------------------------------------------- */

  QLCPlusAPI.prototype.getFunctionsList = function () {
    var self = this;
    return this.call('functions.list', {}).then(function (result) {
      self._emit('getFunctionsList', result);
      return result;
    });
  };
  QLCPlusAPI.prototype.startFunction = function (id) { return this.send('functions.start', { functionId: String(id) }); };
  QLCPlusAPI.prototype.stopFunction = function (id) { return this.send('functions.stop', { functionId: String(id) }); };

  /* --- Virtual Console ----------------------------------------------------
     Structural only for now: this API has no live-interaction methods yet (no
     vc.slider.setValue / vc.button.press equivalent — confirmed absent server-side).
     Listing widgets works; pushing a value does not, so setWidget() is a documented
     no-op until the server grows one. */

  QLCPlusAPI.prototype.getWidgetsList = function () {
    var self = this;
    return this.call('vc.widget.list', {}).then(function (result) {
      self._emit('getWidgetsList', result);
      return result;
    });
  };
  QLCPlusAPI.prototype.setWidget = function () {
    this._emit('alert', 'This server build has no live Virtual Console interaction API yet (structural vc.widget.* only) — the change was not sent.');
    return this;
  };

  /* --- Global ------------------------------------------------------------ */

  QLCPlusAPI.prototype.getProject = function () { return this.call('core.project.get', {}); };
  QLCPlusAPI.prototype.setGrandMaster = function (value) {
    return this.send('io.grandMaster.setValue', { value: Math.max(0, Math.min(255, Math.round(value))) });
  };
  QLCPlusAPI.prototype.getGrandMaster = function () { return this.call('io.grandMaster.get', {}); };
  QLCPlusAPI.prototype.setBlackout = function (on) { return this.send('io.blackout.set', { blackout: !!on }); };
  QLCPlusAPI.prototype.getBlackout = function () { return this.call('io.blackout.get', {}); };

  /** Generic fallback for any method not yet covered by a domains/*.js wrapper: `qlc.api.foo.bar(params)`
      === `qlc.call('foo.bar', params)`. Prefer the generated per-domain namespaces (qlc.core, qlc.io,
      qlc.vc, qlc.fixtures, qlc.fixtureDefs, qlc.functions, qlc.functionsAdvanced, qlc.palette) when a
      method is covered there — this exists so a brand-new spec method works immediately, with zero
      client changes, before its domain wrapper (or the server method itself) exists yet. */
  Object.defineProperty(QLCPlusAPI.prototype, 'api', {
    configurable: true,
    get: function () {
      var self = this;
      function node(prefix) {
        return new Proxy(function () {}, {
          get: function (_, prop) {
            if (typeof prop !== 'string' || prop === 'then') return undefined;
            return node(prefix ? prefix + '.' + prop : prop);
          },
          apply: function (_, thisArg, args) { return self.call(prefix, args[0]); }
        });
      }
      return node('');
    }
  });

  /** Log every frame — the fastest way to learn what your build actually speaks. */
  QLCPlusAPI.prototype.probe = function () {
    this.on('message', function (f) { console.log('[qlc]', f); });
    this.getFunctionsList();
    this.getWidgetsList();
    this.getChannelsValues(1, 1, 16);
    return this;
  };

  root.QLCPlusAPI = QLCPlusAPI;
  if (typeof module !== 'undefined' && module.exports) module.exports = { QLCPlusAPI: QLCPlusAPI };
})(typeof window !== 'undefined' ? window : this);
