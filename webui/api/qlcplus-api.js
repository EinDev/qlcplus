/**
 * qlcplus-api.js — WebSocket client for the QLC+ Control API.
 *
 * Protocol per docs/api-spec/00-conventions.md and controlapi/src/ in this repo (the server this
 * client ships with, so the two are versioned together — if a shape below disagrees with the
 * server, fix it here, not in a screen).
 *
 * Transport: a plain WebSocket at ws://<host>:<port>/ (any path — the server does not filter
 * on it), default port 9010, carrying JSON envelopes:
 *   request:  {"type":"request","id":"<n>","method":"<name>","params":{...}}
 *   response: {"type":"response","id":"<n>","ok":true,"result":{...}}
 *          or {"type":"response","id":"<n>","ok":false,"error":{"code","message","details"?}}
 *   event:    {"type":"event","topic":"<name>","data":{...},"originClientId":<id>|null}
 * A malformed/non-JSON request is silently dropped (no reply). Every method except "hello"
 * is rejected with an UNAUTHORIZED error ("Send \"hello\" first") until "hello" has completed
 * once per connection — this client sends it automatically on open, before anything else.
 * There is no password/auth actually enforced today: hello's params are ignored server-side,
 * but we still send the spec's {apiVersion, clientName}.
 *
 * Unknown methods come back as NOT_FOUND with message 'Unknown method "<name>"'. The client
 * remembers those in `unsupported` (and emits 'unsupported') so a screen can grey out a
 * control the running server simply does not have yet, instead of failing every click.
 *
 * Numbering follows the server everywhere: universes are 0-based `universeId`s, a DMX
 * channel inside a universe is 0-based `channel`, and Simple Desk uses the flat 0-based
 * `address` = universeId * 512 + channel. Screens add +1 for display only.
 *
 * Subscriptions: every structural/low-frequency event is delivered to every client. The only
 * subscribe-gated topic on this server is `io.dmx.universe.<id>.changed` (the live DMX output
 * stream, delta-only); watchUniverse() below manages it.
 *
 * Usage:
 *   const qlc = new QLCPlusAPI('localhost', { port: 9010 });
 *   qlc.on('ready', () => qlc.watchUniverse(0));
 *   qlc.on('channels', (rows) => console.log(rows)); // [{address, universeId, channel, value, overridden}]
 *   qlc.connect();
 *   qlc.setChannel(qlc.address(0, 4), 255);
 *
 * This file is the transport + the handful of methods that need real translation (merging the
 * two Simple Desk sources, event routing, unsupported-method tracking). Full method coverage of
 * the spec lives in api/domains/*.js, one file per fragment, each adding one namespace
 * (qlc.core, qlc.fixtures, qlc.fixtureDefs, qlc.functions, qlc.functionsAdvanced, qlc.io,
 * qlc.palette, qlc.vc) of thin pass-through wrappers — load those <script> tags after this one.
 * Any method with no wrapper still works via qlc.call('<dot.path>', params) or the generic
 * qlc.api.<dot.path>(params) proxy, and any event topic can always be listened to directly:
 * qlc.on('vc.widget.created', fn).
 */
(function (root) {
  'use strict';

  var DEFAULT_PORT = 9010;
  var API_VERSION = '1';
  var CLIENT_NAME = 'QLC+ Web UI';

  function QLCPlusAPI(host, options) {
    options = options || {};
    this.host = host || (typeof location !== 'undefined' && location.hostname) || 'localhost';
    this.port = options.port || DEFAULT_PORT;
    this.clientName = options.clientName || CLIENT_NAME;
    /* Reconnect: capped exponential backoff, 1s -> 10s, until disconnect() is called. */
    this.autoReconnect = options.autoReconnect !== false;
    this.reconnectDelay = options.reconnectDelay || 1000;
    this.reconnectMaxDelay = options.reconnectMaxDelay || 10000;
    this.reconnectAttempt = 0;
    this._reconnectTimer = null;
    this.socket = null;
    this._handlers = {};
    this._pending = {};
    this._nextId = 1;
    this._closedByUser = false;
    this._everReady = false;
    this._watchedUniverse = null;
    this.clientId = null;
    this.serverVersion = null;
    this.docRevision = null;
    /** method name -> true for every method this server answered 'Unknown method' to. */
    this.unsupported = {};
  }

  QLCPlusAPI.prototype.url = function () {
    return 'ws://' + this.host + ':' + this.port + '/';
  };

  /**
   * Subscribe. Events:
   *   'open' | 'close' | 'error'  — socket lifecycle
   *   'ready'                     — hello completed; every other method is safe to call now
   *   'reconnecting'              — {attempt, delay} scheduled retry after an unexpected close
   *   'message'                   — every inbound frame, parsed
   *   'event'                     — every inbound {type:"event"} frame, unparsed
   *   'apiError'                  — {method, error} for any {ok:false} response
   *   'unsupported'               — method name the server reported as unknown (once per method)
   *   'channels'                  — Simple Desk / DMX rows: [{address, universeId, channel, value, overridden}]
   *   'grandmaster' | 'blackout'  — pushed value changes
   *   '<topic>'                   — the raw data of any server event topic (qlc.on('functions.created', fn))
   */
  QLCPlusAPI.prototype.on = function (event, fn) {
    (this._handlers[event] = this._handlers[event] || []).push(fn);
    return this;
  };

  QLCPlusAPI.prototype.off = function (event, fn) {
    var list = this._handlers[event];
    if (!list) return this;
    var i = list.indexOf(fn);
    if (i !== -1) list.splice(i, 1);
    return this;
  };

  QLCPlusAPI.prototype._emit = function (event, payload) {
    (this._handlers[event] || []).slice().forEach(function (fn) {
      try { fn(payload); } catch (e) { /* a listener must not kill the socket */ }
    });
  };

  QLCPlusAPI.prototype.connect = function () {
    var self = this;
    this._closedByUser = false;
    if (this._reconnectTimer) { clearTimeout(this._reconnectTimer); this._reconnectTimer = null; }
    var ws;
    try { ws = new WebSocket(this.url()); }
    catch (e) { this._emit('error', e); this._scheduleReconnect(); return this; }
    this.socket = ws;

    ws.onopen = function () {
      self._emit('open');
      /* Every other method is UNAUTHORIZED until this completes — see file header. */
      self.call('hello', { apiVersion: API_VERSION, clientName: self.clientName }).then(function (result) {
        self.clientId = result.clientId;
        self.serverVersion = result.serverVersion;
        self.docRevision = result.docRevision;
        self.reconnectAttempt = 0;
        self._everReady = true;
        self._emit('ready', result);
      }, function (err) {
        if (err && err.code === 'LOCAL_CLOSED') return; // already reported via 'close'
        self._emit('error', err);
      });
    };
    ws.onerror = function (e) { self._emit('error', e); };
    ws.onclose = function (e) {
      if (self.socket === ws) self.socket = null;
      var pending = self._pending;
      self._pending = {};
      Object.keys(pending).forEach(function (id) {
        var err = new Error('socket closed');
        err.code = 'LOCAL_CLOSED';
        pending[id].reject(err);
      });
      self._emit('close', e);
      self._scheduleReconnect();
    };
    ws.onmessage = function (e) { self._dispatch(e.data); };
    return this;
  };

  QLCPlusAPI.prototype._scheduleReconnect = function () {
    var self = this;
    if (!this.autoReconnect || this._closedByUser || this._reconnectTimer) return;
    this.reconnectAttempt += 1;
    var delay = Math.min(this.reconnectMaxDelay, this.reconnectDelay * Math.pow(2, this.reconnectAttempt - 1));
    this._emit('reconnecting', { attempt: this.reconnectAttempt, delay: delay });
    this._reconnectTimer = setTimeout(function () {
      self._reconnectTimer = null;
      if (!self._closedByUser) self.connect();
    }, delay);
  };

  QLCPlusAPI.prototype.disconnect = function () {
    this._closedByUser = true;
    if (this._reconnectTimer) { clearTimeout(this._reconnectTimer); this._reconnectTimer = null; }
    if (this.socket) this.socket.close();
    return this;
  };

  QLCPlusAPI.prototype.connected = function () {
    return !!this.socket && this.socket.readyState === 1;
  };

  /** True for a method this server has already answered 'Unknown method' to. */
  QLCPlusAPI.prototype.isUnsupported = function (method) {
    return this.unsupported[method] === true;
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
      err.method = pending.method;
      if (err.code === 'NOT_FOUND' && /^Unknown method/.test(err.message || '') && !this.unsupported[pending.method]) {
        this.unsupported[pending.method] = true;
        this._emit('unsupported', pending.method);
      }
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

  /** Translate a handful of well-known push topics into the friendlier events the UI uses. */
  QLCPlusAPI.prototype._routeEvent = function (topic, data) {
    var self = this;
    if (topic === 'io.grandMaster.changed') { this._emit('grandmaster', data.value); return; }
    if (topic === 'io.blackout.changed') { this._emit('blackout', data.blackout); return; }
    if (topic === 'core.project.loaded' || topic === 'core.project.saved') {
      if (data && typeof data.docRevision === 'number') this.docRevision = data.docRevision;
      return;
    }
    if (topic === 'io.simpleDesk.channelChanged') {
      /* One overridden channel per event: {address, value, overridden}, flat 0-based address. */
      this._emit('channels', [this._row(data.address, data.value, !!data.overridden)]);
      return;
    }
    if (topic === 'io.simpleDesk.universeReset') {
      /* A universe reset doesn't say what the channels landed on — refresh the watched universe. */
      if (this._watchedUniverse === data.universeId) this.getUniverseValues(data.universeId);
      return;
    }
    var m = /^io\.dmx\.universe\.(\d+)\.changed$/.exec(topic);
    if (m) {
      /* Live DMX output (subscribe-gated): {universeId, changes:[{channel, value}]}, channel
         within the universe. Override flags are unknown here; null means "leave as is". */
      var universeId = Number(m[1]);
      this._emit('channels', (data.changes || []).map(function (c) {
        return self._row(universeId * 512 + c.channel, c.value, null);
      }));
    }
  };

  QLCPlusAPI.prototype._row = function (address, value, overridden) {
    return { address: address, universeId: Math.floor(address / 512), channel: address % 512, value: value, overridden: overridden };
  };

  QLCPlusAPI.prototype.subscribe = function (topics) { return this.call('subscribe', { topics: topics }); };
  QLCPlusAPI.prototype.unsubscribe = function (topics) { return this.call('unsubscribe', { topics: topics }); };

  /* --- Simple Desk / live DMX ------------------------------------------- */

  /** Flat 0-based address of `channel` (0-based) in `universeId` (0-based). */
  QLCPlusAPI.prototype.address = function (universeId, channel) {
    return (Math.max(0, universeId | 0) * 512) + (channel | 0);
  };

  /** Override one channel. `address` is flat 0-based (see address()). */
  QLCPlusAPI.prototype.setChannel = function (address, value) {
    return this.send('io.simpleDesk.setChannel', {
      address: address, value: Math.max(0, Math.min(255, Math.round(value)))
    });
  };

  /** Override several channels at once: [{address, value}]. */
  QLCPlusAPI.prototype.setChannels = function (items) {
    return this.send('io.simpleDesk.setChannels', { channels: items.map(function (i) {
      return { address: i.address, value: Math.max(0, Math.min(255, Math.round(i.value))) };
    }) });
  };

  /**
   * Fetch the full state of one universe, answered as one 'channels' event with 512 rows (and
   * returned). Two server sources are merged: io.dmx.universe.get gives every channel's actual
   * output value, io.simpleDesk.get lists only the channels currently overridden by the desk.
   */
  QLCPlusAPI.prototype.getUniverseValues = function (universeId) {
    var self = this;
    return Promise.all([
      this.call('io.dmx.universe.get', { universeId: universeId }),
      this.call('io.simpleDesk.get', { universeId: universeId })
    ]).then(function (res) {
      var values = res[0].values || [];
      var overridden = {};
      (res[1].channels || []).forEach(function (c) { overridden[c.address] = c.value; });
      var rows = [];
      for (var ch = 0; ch < 512; ch++) {
        var address = universeId * 512 + ch;
        var isOverridden = Object.prototype.hasOwnProperty.call(overridden, address);
        rows.push(self._row(address, isOverridden ? overridden[address] : (values[ch] || 0), isOverridden));
      }
      self._emit('channels', rows);
      return rows;
    });
  };

  /**
   * Follow one universe live: seeds it via getUniverseValues() and subscribes to its DMX output
   * stream (the one subscribe-gated topic). Switching universes unsubscribes the previous one.
   * Server-side subscriptions die with the socket, so call this again after every 'ready'.
   */
  QLCPlusAPI.prototype.watchUniverse = function (universeId) {
    var previous = this._watchedUniverse;
    this._watchedUniverse = universeId;
    if (previous !== null && previous !== universeId && this.connected())
      this.send('unsubscribe', { topics: ['io.dmx.universe.' + previous + '.changed'] });
    this.send('subscribe', { topics: ['io.dmx.universe.' + universeId + '.changed'] });
    return this.getUniverseValues(universeId);
  };

  QLCPlusAPI.prototype.unwatchUniverse = function () {
    if (this._watchedUniverse !== null && this.connected())
      this.send('unsubscribe', { topics: ['io.dmx.universe.' + this._watchedUniverse + '.changed'] });
    this._watchedUniverse = null;
    return this;
  };

  /** Release a manual override. This is NOT the same as setting the channel to 0. */
  QLCPlusAPI.prototype.resetChannel = function (address) {
    return this.send('io.simpleDesk.resetChannel', { address: address });
  };

  QLCPlusAPI.prototype.resetUniverse = function (universeId) {
    return this.send('io.simpleDesk.resetUniverse', { universeId: universeId });
  };

  /* --- Functions -------------------------------------------------------- */

  QLCPlusAPI.prototype.getFunctionsList = function (params) {
    var self = this;
    return this.call('functions.list', params || {}).then(function (result) {
      self._emit('getFunctionsList', result);
      return result;
    });
  };
  QLCPlusAPI.prototype.startFunction = function (id) { return this.call('functions.start', { functionId: String(id) }); };
  QLCPlusAPI.prototype.stopFunction = function (id) { return this.call('functions.stop', { functionId: String(id) }); };

  /* --- Virtual Console -------------------------------------------------- */

  QLCPlusAPI.prototype.getWidgetsList = function (params) {
    var self = this;
    return this.call('vc.widget.list', params || {}).then(function (result) {
      self._emit('getWidgetsList', result);
      return result;
    });
  };
  QLCPlusAPI.prototype.getPagesList = function () { return this.call('vc.page.list', {}); };
  /** Live button press/release (spec vc.button.press). Rejects with NOT_FOUND on a server that
      has not implemented it yet — see isUnsupported('vc.button.press'). */
  QLCPlusAPI.prototype.pressButton = function (widgetId, pressed) {
    return this.call('vc.button.press', { widgetId: String(widgetId), pressed: pressed !== false });
  };
  /** Live slider move (spec vc.slider.setValue), same caveat as pressButton(). */
  QLCPlusAPI.prototype.setSliderValue = function (widgetId, value) {
    return this.call('vc.slider.setValue', { widgetId: String(widgetId), value: Math.max(0, Math.min(255, Math.round(value))) });
  };

  /* --- Global ------------------------------------------------------------ */

  QLCPlusAPI.prototype.getProject = function () { return this.call('core.project.get', {}); };
  QLCPlusAPI.prototype.setGrandMaster = function (value) {
    return this.send('io.grandMaster.setValue', { value: Math.max(0, Math.min(255, Math.round(value))) });
  };
  QLCPlusAPI.prototype.getGrandMaster = function () { return this.call('io.grandMaster.get', {}); };
  QLCPlusAPI.prototype.setBlackout = function (on) { return this.call('io.blackout.set', { blackout: !!on }); };
  QLCPlusAPI.prototype.getBlackout = function () { return this.call('io.blackout.get', {}); };

  /** Generic fallback for any method not covered by a domains/*.js wrapper: `qlc.api.foo.bar(params)`
      === `qlc.call('foo.bar', params)`. */
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

  /** Log every frame — the fastest way to learn what your build actually speaks. Read-only. */
  QLCPlusAPI.prototype.probe = function () {
    this.on('message', function (f) { console.log('[qlc]', f); });
    this.getFunctionsList();
    this.getPagesList();
    this.getWidgetsList();
    this.getUniverseValues(0);
    return this;
  };

  QLCPlusAPI.DEFAULT_PORT = DEFAULT_PORT;
  root.QLCPlusAPI = QLCPlusAPI;
  if (typeof module !== 'undefined' && module.exports) module.exports = { QLCPlusAPI: QLCPlusAPI };
})(typeof window !== 'undefined' ? window : this);
