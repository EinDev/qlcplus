/* @ds-bundle: {"format":4,"namespace":"PatchDesignSystem_5432c9","components":[{"name":"ContextMenuEntry","sourcePath":"components/buttons/ContextMenuEntry.jsx"},{"name":"DMXPercentageButton","sourcePath":"components/buttons/DMXPercentageButton.jsx"},{"name":"GenericButton","sourcePath":"components/buttons/GenericButton.jsx"},{"name":"IconButton","sourcePath":"components/buttons/IconButton.jsx"},{"name":"MenuBarEntry","sourcePath":"components/buttons/MenuBarEntry.jsx"},{"name":"ChannelStrip","sourcePath":"components/console/ChannelStrip.jsx"},{"name":"KeyPad","sourcePath":"components/console/KeyPad.jsx"},{"name":"TreeNode","sourcePath":"components/console/TreeNode.jsx"},{"name":"CustomPopupDialog","sourcePath":"components/containers/CustomPopupDialog.jsx"},{"name":"SectionBox","sourcePath":"components/containers/SectionBox.jsx"},{"name":"SidePanel","sourcePath":"components/containers/SidePanel.jsx"},{"name":"ViewToolbar","sourcePath":"components/containers/ViewToolbar.jsx"},{"name":"ToolbarSpacer","sourcePath":"components/containers/ViewToolbar.jsx"},{"name":"CustomCheckBox","sourcePath":"components/inputs/CustomCheckBox.jsx"},{"name":"CustomComboBox","sourcePath":"components/inputs/CustomComboBox.jsx"},{"name":"CustomScrollBar","sourcePath":"components/inputs/CustomScrollBar.jsx"},{"name":"CustomSlider","sourcePath":"components/inputs/CustomSlider.jsx"},{"name":"CustomSpinBox","sourcePath":"components/inputs/CustomSpinBox.jsx"},{"name":"CustomTextInput","sourcePath":"components/inputs/CustomTextInput.jsx"},{"name":"QLCPlusFader","sourcePath":"components/inputs/QLCPlusFader.jsx"},{"name":"ShortcutHint","sourcePath":"components/shortcuts/ShortcutHint.jsx"},{"name":"ShortcutContext","sourcePath":"components/shortcuts/ShortcutOverlay.jsx"},{"name":"ShortcutKeys","sourcePath":"components/shortcuts/ShortcutOverlay.jsx"},{"name":"ShortcutOverlay","sourcePath":"components/shortcuts/ShortcutOverlay.jsx"},{"name":"FA","sourcePath":"components/text/FaIcon.jsx"},{"name":"FaIcon","sourcePath":"components/text/FaIcon.jsx"},{"name":"IconTextEntry","sourcePath":"components/text/IconTextEntry.jsx"},{"name":"RobotoText","sourcePath":"components/text/RobotoText.jsx"}],"sourceHashes":{"api/qlcplus-api.js":"18fc0f62ea45","components/buttons/ContextMenuEntry.jsx":"b2a5a04c6d02","components/buttons/DMXPercentageButton.jsx":"2b2c718ef7c7","components/buttons/GenericButton.jsx":"c64598251937","components/buttons/IconButton.jsx":"6b64f17cedf0","components/buttons/MenuBarEntry.jsx":"d59352b2cc53","components/console/ChannelStrip.jsx":"157cc11da38e","components/console/KeyPad.jsx":"8a3347ff451b","components/console/TreeNode.jsx":"7441239cc89a","components/containers/CustomPopupDialog.jsx":"a4570745b18c","components/containers/SectionBox.jsx":"f898cf24b8be","components/containers/SidePanel.jsx":"5da1cfc55a13","components/containers/ViewToolbar.jsx":"d1b0816f4cde","components/inputs/CustomCheckBox.jsx":"11a7569dab12","components/inputs/CustomComboBox.jsx":"fe6fd4cd238f","components/inputs/CustomScrollBar.jsx":"0674e480a423","components/inputs/CustomSlider.jsx":"18380e400ea5","components/inputs/CustomSpinBox.jsx":"fb133eebad52","components/inputs/CustomTextInput.jsx":"0e059087e119","components/inputs/QLCPlusFader.jsx":"e0dfb0cc03eb","components/shortcuts/ShortcutHint.jsx":"ca67f8ece839","components/shortcuts/ShortcutOverlay.jsx":"a0a30c93208b","components/text/FaIcon.jsx":"0863610c1f63","components/text/IconTextEntry.jsx":"5d872b833e08","components/text/RobotoText.jsx":"829fcd1f30d1","ui_kits/qlcplus/App.jsx":"7281db171008","ui_kits/qlcplus/Connection.jsx":"e827d549abd9","ui_kits/qlcplus/FixturesFunctions.jsx":"bd8e9db132a8","ui_kits/qlcplus/InputOutput.jsx":"e49717a9683e","ui_kits/qlcplus/SimpleDesk.jsx":"5a0315e789f2","ui_kits/qlcplus/VirtualConsole.jsx":"347c97ad4758","ui_kits/qlcplus/data.js":"ef9bd265635c"},"inlinedExternals":[],"unexposedExports":[{"name":"useHeldModifier","sourcePath":"components/shortcuts/ShortcutOverlay.jsx"}]} */

(() => {

const __ds_ns = (window.PatchDesignSystem_5432c9 = window.PatchDesignSystem_5432c9 || {});

const __ds_scope = {};

(__ds_ns.__errors = __ds_ns.__errors || []);

// api/qlcplus-api.js
try { (() => {
/**
 * qlcplus-api.js — WebSocket client for the QLC+ Web API.
 *
 * Protocol verified against mcallegari/qlcplus@master:
 *   webaccess/src/webaccess-qml.cpp   (the QLC+ 5 handler — verb dispatch, response formats)
 *   webaccess/res/simpledesk-v5.js    (reference Simple Desk client)
 *   webaccess/res/websocket.js        (reference Virtual Console client)
 *
 * Transport: a plain WebSocket at ws://<host>:<port>/qlcplusWS carrying pipe-delimited text
 * frames. Start QLC+ with `qlcplus --web` (default port 9999).
 *
 * Two things that surprise people:
 *  - **Channel values are NOT pushed.** QLC+ only answers getChannelsValues when asked, so a
 *    live Simple Desk has to poll (the reference client polls every 700ms). Use startPolling().
 *  - **Channel numbers are absolute across universes**: universe 2 address 1 is channel 513.
 *    Use absoluteChannel(universe, address).
 *
 * Usage:
 *   const qlc = new QLCPlusAPI('192.168.1.50');
 *   qlc.on('open', () => qlc.startPolling(1, 1, 64));
 *   qlc.on('channels', (rows) => console.log(rows)); // [{channel, value, type, overriding}]
 *   qlc.connect();
 *   qlc.setChannel(qlc.absoluteChannel(1, 5), 255);
 */
(function (root) {
  'use strict';

  var DEFAULT_PORT = 9999;
  var PATH = '/qlcplusWS';
  function QLCPlusAPI(host, options) {
    options = options || {};
    this.host = host || location.hostname || 'localhost';
    this.port = options.port || DEFAULT_PORT;
    this.autoReconnect = options.autoReconnect !== false;
    this.reconnectDelay = options.reconnectDelay || 1000;
    this.socket = null;
    this._handlers = {};
    this._queue = [];
    this._closedByUser = false;
    this._poll = null;
  }
  QLCPlusAPI.prototype.url = function () {
    return 'ws://' + this.host + ':' + this.port + PATH;
  };

  /**
   * Subscribe. Events:
   *   'open' | 'close' | 'error'      — socket lifecycle
   *   'message'                       — every inbound frame, parsed
   *   'channels'                      — decoded getChannelsValues rows
   *   'widget'                        — {id, kind, value, display} from a VC push
   *   'grandmaster'                   — 0-255
   *   'alert'                         — a string QLC+ wants shown
   *   '<verb>'                        — any raw API verb (getFunctionsList, isProjectLoaded, …)
   */
  QLCPlusAPI.prototype.on = function (event, fn) {
    (this._handlers[event] = this._handlers[event] || []).push(fn);
    return this;
  };
  QLCPlusAPI.prototype._emit = function (event, payload) {
    (this._handlers[event] || []).forEach(function (fn) {
      try {
        fn(payload);
      } catch (e) {/* a listener must not kill the socket */}
    });
  };
  QLCPlusAPI.prototype.connect = function () {
    var self = this;
    this._closedByUser = false;
    var ws;
    try {
      ws = new WebSocket(this.url());
    } catch (e) {
      this._emit('error', e);
      return this;
    }
    this.socket = ws;
    ws.onopen = function () {
      self._queue.splice(0).forEach(function (f) {
        ws.send(f);
      });
      /* The reference clients open with this — it also proves the socket really talks QLC+
         rather than being some other service that happened to accept the upgrade. */
      self.api('isProjectLoaded');
      self._emit('open');
    };
    ws.onerror = function (e) {
      self._emit('error', e);
    };
    ws.onclose = function (e) {
      self.stopPolling();
      self._emit('close', e);
      if (self.autoReconnect && !self._closedByUser) {
        setTimeout(function () {
          self.connect();
        }, self.reconnectDelay);
      }
    };
    ws.onmessage = function (e) {
      self._dispatch(e.data);
    };
    return this;
  };
  QLCPlusAPI.prototype.disconnect = function () {
    this._closedByUser = true;
    this.stopPolling();
    if (this.socket) this.socket.close();
    return this;
  };
  QLCPlusAPI.prototype.connected = function () {
    return !!this.socket && this.socket.readyState === 1;
  };

  /** Send a raw pipe-delimited frame. Dropped (not queued) unless the socket is open. */
  QLCPlusAPI.prototype.send = function (frame) {
    if (this.connected()) this.socket.send(frame);
    return this;
  };

  /** Send an API command: api('getFunctionsList') → "QLC+API|getFunctionsList". */
  QLCPlusAPI.prototype.api = function (verb) {
    var args = Array.prototype.slice.call(arguments, 1);
    var frame = ['QLC+API', verb].concat(args).join('|');
    if (this.connected()) this.socket.send(frame);else this._queue.push(frame);
    return this;
  };

  /** Send a VC/global command: cmd('opMode') → "QLC+CMD|opMode". */
  QLCPlusAPI.prototype.cmd = function () {
    var args = Array.prototype.slice.call(arguments);
    return this.send(['QLC+CMD'].concat(args).join('|'));
  };

  /** Split a frame into { channel, verb, args, raw }. */
  QLCPlusAPI.parse = function (raw) {
    var parts = String(raw).split('|');
    if (parts[0] === 'QLC+API') {
      return {
        channel: 'QLC+API',
        verb: parts[1],
        args: parts.slice(2),
        raw: raw
      };
    }
    return {
      channel: parts[0],
      verb: null,
      args: parts.slice(1),
      raw: raw
    };
  };

  /**
   * Decode a getChannelsValues payload.
   * QLC+ 5 answers "QLC+API|getChannelsValues|<ch>|<val>|<type>|<isOverriding>" repeating,
   * but older builds omit isOverriding — the reference client infers the stride from the
   * payload length, so we do exactly the same.
   */
  QLCPlusAPI.parseChannels = function (args) {
    var stride = args.length % 4 === 0 ? 4 : 3;
    var rows = [];
    for (var i = 0; i + stride - 1 < args.length; i += stride) {
      var ch = parseInt(args[i], 10);
      var v = parseInt(args[i + 1], 10);
      if (isNaN(ch) || isNaN(v)) continue;
      rows.push({
        channel: ch,
        value: v,
        type: args[i + 2] || '',
        overriding: stride === 4 ? args[i + 3] === '1' || args[i + 3] === 'true' : false
      });
    }
    return rows;
  };
  QLCPlusAPI.prototype._dispatch = function (raw) {
    var frame = QLCPlusAPI.parse(raw);
    this._emit('message', frame);
    if (frame.verb) {
      if (frame.verb === 'getChannelsValues') {
        this._emit('channels', QLCPlusAPI.parseChannels(frame.args));
      }
      this._emit(frame.verb, frame);
      return;
    }

    /* Non-API frames. Virtual Console pushes are "<widgetID>|<TYPE>|<value>[|<display>]";
       see webaccess/res/websocket.js. */
    var kind = frame.args[0];
    if (frame.channel === 'GM_VALUE') {
      this._emit('grandmaster', parseInt(frame.args[0], 10));
      return;
    }
    if (frame.channel === 'ALERT') {
      this._emit('alert', frame.args[0]);
      return;
    }
    if (kind === 'SLIDER') {
      this._emit('widget', {
        id: frame.channel,
        kind: 'slider',
        value: parseInt(frame.args[1], 10),
        display: frame.args[2]
      });
      return;
    }
    if (kind === 'BUTTON') {
      this._emit('widget', {
        id: frame.channel,
        kind: 'button',
        value: frame.args[1] === '1' || frame.args[1] === 'true' ? 255 : 0
      });
      return;
    }
    this._emit('widget', {
      id: frame.channel,
      kind: kind,
      value: frame.args[1]
    });
  };

  /* --- Simple Desk ------------------------------------------------------ */

  /** Channel numbers are absolute across universes: universe 2, address 1 → 513. */
  QLCPlusAPI.prototype.absoluteChannel = function (universe, address) {
    return (Math.max(1, universe | 0) - 1) * 512 + (address | 0);
  };

  /** Set one channel. `channel` must be ABSOLUTE — see absoluteChannel(). */
  QLCPlusAPI.prototype.setChannel = function (channel, value) {
    return this.send('CH|' + channel + '|' + Math.max(0, Math.min(255, Math.round(value))));
  };

  /** Ask for a block of channel values. Answered once, as a 'channels' event. */
  QLCPlusAPI.prototype.getChannelsValues = function (universe, address, count) {
    return this.api('getChannelsValues', Math.max(1, universe || 1), address || 1, count || 512);
  };

  /** Release a manual override. This is NOT the same as setting the channel to 0. */
  QLCPlusAPI.prototype.resetChannel = function (absoluteChannel) {
    return this.api('sdResetChannel', absoluteChannel);
  };
  QLCPlusAPI.prototype.resetUniverse = function (universe) {
    return this.api('sdResetUniverse', Math.max(1, universe || 1));
  };

  /**
   * QLC+ never pushes channel values, so a live desk view must poll.
   * The reference client uses 700ms; it also skips polling on a hidden tab.
   */
  QLCPlusAPI.prototype.startPolling = function (universe, address, count, intervalMs) {
    var self = this;
    this.stopPolling();
    var tick = function () {
      if (typeof document !== 'undefined' && document.hidden) return;
      self.getChannelsValues(universe, address, count);
    };
    tick();
    this._poll = setInterval(tick, intervalMs || 700);
    return this;
  };
  QLCPlusAPI.prototype.stopPolling = function () {
    if (this._poll) clearInterval(this._poll);
    this._poll = null;
    return this;
  };

  /* --- Functions -------------------------------------------------------- */

  QLCPlusAPI.prototype.getFunctionsList = function () {
    return this.api('getFunctionsList');
  };
  QLCPlusAPI.prototype.getFunctionsNumber = function () {
    return this.api('getFunctionsNumber');
  };
  QLCPlusAPI.prototype.getFunctionType = function (id) {
    return this.api('getFunctionType', id);
  };
  QLCPlusAPI.prototype.getFunctionStatus = function (id) {
    return this.api('getFunctionStatus', id);
  };
  QLCPlusAPI.prototype.startFunction = function (id) {
    return this.api('setFunctionStatus', id, 1);
  };
  QLCPlusAPI.prototype.stopFunction = function (id) {
    return this.api('setFunctionStatus', id, 0);
  };

  /* --- Virtual Console -------------------------------------------------- */

  QLCPlusAPI.prototype.getWidgetsList = function () {
    return this.api('getWidgetsList');
  };
  QLCPlusAPI.prototype.getWidgetsNumber = function () {
    return this.api('getWidgetsNumber');
  };
  QLCPlusAPI.prototype.getWidgetType = function (id) {
    return this.api('getWidgetType', id);
  };
  QLCPlusAPI.prototype.getWidgetStatus = function (id) {
    return this.api('getWidgetStatus', id);
  };
  /** Push a value to a VC widget. Sliders take 0-255; buttons take 0 or 255. */
  QLCPlusAPI.prototype.setWidget = function (id, value) {
    return this.send(id + '|' + value);
  };
  /** Virtual Console page: "NEXT_PG" | "PREV_PG" | ["PAGE", n]. */
  QLCPlusAPI.prototype.vcPage = function () {
    var args = Array.prototype.slice.call(arguments);
    return this.send(['VC_PAGE'].concat(args).join('|'));
  };

  /* --- Global ----------------------------------------------------------- */

  QLCPlusAPI.prototype.isProjectLoaded = function () {
    return this.api('isProjectLoaded');
  };
  /** Grand master, 0-255. */
  QLCPlusAPI.prototype.setGrandMaster = function (value) {
    return this.send('GM_VALUE|' + Math.max(0, Math.min(255, Math.round(value))));
  };

  /** Log every frame — the fastest way to learn what your build actually speaks. */
  QLCPlusAPI.prototype.probe = function () {
    this.on('message', function (f) {
      console.log('[qlc]', f.raw);
    });
    this.on('open', function () {
      console.log('[qlc] open');
    });
    this.on('error', function () {
      console.warn('[qlc] error');
    });
    this.getFunctionsList();
    this.getWidgetsList();
    this.getChannelsValues(1, 1, 16);
    return this;
  };
  root.QLCPlusAPI = QLCPlusAPI;
  if (typeof module !== 'undefined' && module.exports) module.exports = {
    QLCPlusAPI: QLCPlusAPI
  };
})(typeof window !== 'undefined' ? window : this);
})(); } catch (e) { __ds_ns.__errors.push({ path: "api/qlcplus-api.js", error: String((e && e.message) || e) }); }

// components/buttons/GenericButton.jsx
try { (() => {
/** GenericButton.qml — the standard labelled button. 2px --bg-strong border, blue hover. */
function GenericButton({
  label,
  children,
  iconSource,
  iconPadding = 6,
  fontSize = 'var(--text-size-default)',
  bgColor = 'var(--bg-control)',
  fgColor = 'var(--fg-main)',
  hoverColor = 'var(--highlight)',
  pressedColor = 'var(--highlight-pressed)',
  useFontawesome = false,
  width = 150,
  height = 'var(--icon-size-default)',
  disabled = false,
  onClick,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const [press, setPress] = React.useState(false);
  const text = label != null ? label : children;
  return React.createElement('button', {
    type: 'button',
    disabled,
    onClick,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => {
      setHover(false);
      setPress(false);
    },
    onMouseDown: () => setPress(true),
    onMouseUp: () => setPress(false),
    style: {
      position: 'relative',
      display: 'flex',
      alignItems: 'center',
      justifyContent: iconSource ? 'flex-start' : 'center',
      gap: iconPadding,
      width,
      height,
      padding: iconSource ? '0 ' + iconPadding + 'px' : 0,
      background: hover ? press ? pressedColor : hoverColor : bgColor,
      color: fgColor,
      border: 'var(--border-control)',
      borderRadius: 'var(--radius-none)',
      fontFamily: useFontawesome ? 'var(--font-awesome)' : 'var(--font-roboto)',
      fontSize,
      fontWeight: useFontawesome ? 900 : 400,
      cursor: disabled ? 'default' : 'pointer',
      overflow: 'hidden',
      whiteSpace: 'nowrap',
      textOverflow: 'ellipsis',
      ...style
    },
    ...rest
  }, iconSource ? React.createElement('img', {
    src: iconSource,
    alt: '',
    style: {
      height: 'calc(100% - ' + iconPadding * 2 + 'px)',
      width: 'auto',
      aspectRatio: '1',
      objectFit: 'contain',
      flex: 'none'
    }
  }) : null, React.createElement('span', {
    style: {
      flex: iconSource ? 1 : 'none',
      overflow: 'hidden',
      textOverflow: 'ellipsis'
    }
  }, text), disabled ? React.createElement('span', {
    style: {
      position: 'absolute',
      inset: 0,
      background: 'var(--disabled-veil-strong)'
    }
  }) : null);
}
Object.assign(__ds_scope, { GenericButton });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/buttons/GenericButton.jsx", error: String((e && e.message) || e) }); }

// components/console/KeyPad.jsx
try { (() => {
const DMX_KEYS = [['7', '8', '9', 'AT'], ['4', '5', '6', 'THRU'], ['1', '2', '3', 'FULL'], ['-%', '0', '+%', 'ZERO']];
const PLAIN_KEYS = [['7', '8', '9'], ['4', '5', '6'], ['1', '2', '3'], ['-', '0', '+']];

/** KeyPad.qml — the command keypad: entry line, digits, DMX verbs, ENTER/CLR/BY. */
function KeyPad({
  commandString = '',
  onCommandChange,
  onExecuteCommand,
  showDMXcontrol = true,
  showTapButton = false,
  itemHeight = 'var(--icon-size-default)',
  style,
  ...rest
}) {
  const rows = showDMXcontrol ? DMX_KEYS : PLAIN_KEYS;
  const cols = showDMXcontrol ? 4 : 3;
  const append = t => onCommandChange && onCommandChange(commandString + t);
  const key = label => React.createElement(__ds_scope.GenericButton, {
    key: label,
    label,
    width: '100%',
    height: itemHeight,
    onClick: () => append(/^[0-9]$/.test(label) ? label : ' ' + label + ' ')
  });
  return React.createElement('div', {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(' + cols + ',1fr)',
      gap: 'var(--space-keypad)',
      width: 'calc(var(--big-item-height) * 2.5)',
      background: 'transparent',
      ...style
    },
    ...rest
  }, React.createElement('input', {
    key: 'cmd',
    value: commandString,
    onChange: e => onCommandChange && onCommandChange(e.target.value),
    onKeyDown: e => {
      if (e.key === 'Enter' && onExecuteCommand) onExecuteCommand(commandString);
    },
    style: {
      gridColumn: 'span ' + (showTapButton ? cols - 1 : cols),
      height: itemHeight,
      padding: '0 5px',
      background: 'var(--bg-strong)',
      border: 'var(--border-control)',
      outline: 'none',
      color: 'var(--fg-light)',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)'
    }
  }), showTapButton ? React.createElement(__ds_scope.GenericButton, {
    key: 'tap',
    label: 'Tap',
    width: '100%',
    height: itemHeight
  }) : null, rows.map(r => r.map(key)), React.createElement(__ds_scope.GenericButton, {
    key: 'enter',
    label: 'ENTER',
    width: '100%',
    height: itemHeight,
    style: {
      gridColumn: 'span 2'
    },
    bgColor: 'var(--keypad-enter)',
    hoverColor: 'var(--keypad-enter-hover)',
    pressedColor: 'var(--keypad-enter-pressed)',
    onClick: () => onExecuteCommand && onExecuteCommand(commandString)
  }), React.createElement(__ds_scope.GenericButton, {
    key: 'clr',
    label: 'CLR',
    width: '100%',
    height: itemHeight,
    onClick: () => onCommandChange && onCommandChange('')
  }), showDMXcontrol ? React.createElement(__ds_scope.GenericButton, {
    key: 'by',
    label: 'BY',
    width: '100%',
    height: itemHeight,
    onClick: () => append(' BY ')
  }) : null);
}
Object.assign(__ds_scope, { KeyPad });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/console/KeyPad.jsx", error: String((e && e.message) || e) }); }

// components/containers/CustomPopupDialog.jsx
try { (() => {
/** CustomPopupDialog.qml — modal on a 50%-black dim screen, navy header, 2px light border. */
function CustomPopupDialog({
  open = false,
  title,
  message,
  children,
  standardButtons = ['Cancel', 'Ok'],
  onClicked,
  onClose,
  width = '33%',
  style,
  ...rest
}) {
  if (!open) return null;
  return React.createElement('div', {
    style: {
      position: 'absolute',
      inset: 0,
      zIndex: 99,
      display: 'grid',
      placeItems: 'center',
      background: 'var(--dim-screen)'
    },
    onClick: onClose
  }, React.createElement('div', {
    onClick: e => e.stopPropagation(),
    style: {
      width,
      maxWidth: '92%',
      background: 'var(--bg-medium)',
      border: 'var(--border-dialog)',
      ...style
    },
    ...rest
  }, title ? React.createElement('div', {
    style: {
      margin: 2,
      padding: 'var(--pad-dialog)',
      background: 'var(--section-header)',
      color: 'var(--fg-main)',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)',
      fontWeight: 700,
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      whiteSpace: 'nowrap'
    }
  }, title) : null, React.createElement('div', {
    style: {
      padding: 'var(--pad-dialog)',
      color: 'var(--fg-main)',
      textAlign: message ? 'center' : 'left',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)'
    }
  }, message || children), standardButtons && standardButtons.length ? React.createElement('div', {
    style: {
      display: 'flex',
      justifyContent: 'flex-end',
      gap: 2,
      margin: 2,
      padding: 2,
      background: 'var(--bg-medium)'
    }
  }, standardButtons.map(b => React.createElement(__ds_scope.GenericButton, {
    key: b,
    label: b,
    width: 'calc(var(--big-item-height) * 2)',
    bgColor: 'var(--bg-light)',
    onClick: () => onClicked && onClicked(b)
  }))) : null));
}
Object.assign(__ds_scope, { CustomPopupDialog });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/containers/CustomPopupDialog.jsx", error: String((e && e.message) || e) }); }

// components/containers/SidePanel.jsx
try { (() => {
/** SidePanel.qml — collapsible side rail with a draggable gradient edge. */
function SidePanel({
  children,
  rail,
  isOpen = false,
  alignment = 'right',
  expandedWidth = 'var(--side-panel-width)',
  style,
  ...rest
}) {
  const collapse = 'calc(var(--icon-size-default) * 1.25)';
  const edge = React.createElement('div', {
    key: 'edge',
    style: {
      width: collapse,
      flex: 'none',
      height: '100%',
      background: 'var(--gradient-panel-edge)',
      cursor: 'ew-resize',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      gap: 'var(--space-row)',
      paddingTop: 'var(--space-row)'
    }
  }, rail);
  const body = isOpen ? React.createElement('div', {
    key: 'body',
    style: {
      flex: 1,
      minWidth: 0,
      height: '100%',
      overflow: 'auto',
      background: 'var(--bg-strong)'
    }
  }, children) : null;
  return React.createElement('aside', {
    style: {
      display: 'flex',
      height: '100%',
      flex: 'none',
      width: isOpen ? expandedWidth : collapse,
      background: 'var(--bg-strong)',
      transition: 'width var(--dur-panel) var(--ease-linear)',
      ...style
    },
    ...rest
  }, alignment === 'right' ? [edge, body] : [body, edge]);
}
Object.assign(__ds_scope, { SidePanel });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/containers/SidePanel.jsx", error: String((e && e.message) || e) }); }

// components/containers/ViewToolbar.jsx
try { (() => {
/** The gradient toolbar strip. Repeated inline in MainView/SimpleDesk/FixturesAndFunctions. */
function ViewToolbar({
  children,
  variant = 'sub',
  height,
  style,
  ...rest
}) {
  const h = height || (variant === 'main' ? 'var(--icon-size-default)' : 'var(--icon-size-medium)');
  return React.createElement('div', {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-row)',
      flex: 'none',
      width: '100%',
      minWidth: 0,
      height: h,
      paddingRight: 'var(--space-row)',
      background: variant === 'main' ? 'var(--gradient-toolbar-main)' : 'var(--gradient-toolbar-sub)',
      ...style
    },
    ...rest
  }, children);
}

/** Transparent flexible gap — the QML uses a fillWidth transparent Rectangle for this. */
function ToolbarSpacer() {
  return React.createElement('div', {
    style: {
      flex: '1 1 0',
      minWidth: 0,
      background: 'transparent'
    }
  });
}
Object.assign(__ds_scope, { ViewToolbar, ToolbarSpacer });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/containers/ViewToolbar.jsx", error: String((e && e.message) || e) }); }

// components/inputs/CustomScrollBar.jsx
try { (() => {
/** CustomScrollBar.qml — square, chunky, with a small centre grip. */
function CustomScrollBar({
  position = 0,
  size = 0.3,
  orientation = 'vertical',
  pressed = false,
  style,
  ...rest
}) {
  const vertical = orientation === 'vertical';
  return React.createElement('div', {
    style: {
      position: 'relative',
      background: 'var(--bg-medium)',
      width: vertical ? 'var(--scrollbar-width)' : '100%',
      height: vertical ? '100%' : 'var(--scrollbar-width)',
      flex: 'none',
      ...style
    },
    ...rest
  }, React.createElement('div', {
    style: {
      position: 'absolute',
      background: pressed ? 'var(--highlight)' : 'var(--bg-control)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      left: vertical ? 0 : position * 100 + '%',
      top: vertical ? position * 100 + '%' : 0,
      width: vertical ? '100%' : size * 100 + '%',
      height: vertical ? size * 100 + '%' : '100%'
    }
  }, React.createElement('div', {
    style: {
      background: 'var(--bg-light)',
      width: vertical ? '80%' : 5,
      height: vertical ? 5 : '80%'
    }
  })));
}
Object.assign(__ds_scope, { CustomScrollBar });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/CustomScrollBar.jsx", error: String((e && e.message) || e) }); }

// components/inputs/CustomSlider.jsx
try { (() => {
/** CustomSlider.qml — thin rounded track, --highlight fill, square-ish light handle. */
function CustomSlider({
  value = 0,
  from = 0,
  to = 100,
  onMoved,
  orientation = 'horizontal',
  length = 200,
  disabled = false,
  style,
  ...rest
}) {
  const ref = React.useRef(null);
  const horizontal = orientation === 'horizontal';
  const pos = (value - from) / (to - from || 1);
  const set = (clientX, clientY) => {
    const el = ref.current;
    if (!el || !onMoved) return;
    const r = el.getBoundingClientRect();
    /* The handle's centre travels from handle/2 to length - handle/2 (it stays inside the
       control), so the pointer maps over that same span: the handle follows the cursor
       exactly, and the extremes are reached at the handle's own end positions. The handle
       is square, so its size is the control's thickness. */
    const hs = horizontal ? r.height : r.width;
    const p = horizontal ? (clientX - r.left - hs / 2) / Math.max(1, r.width - hs) : 1 - (clientY - r.top - hs / 2) / Math.max(1, r.height - hs);
    onMoved(Math.round(from + Math.max(0, Math.min(1, p)) * (to - from)));
  };
  const start = e => {
    if (disabled) return;
    e.preventDefault();
    set(e.clientX, e.clientY);
    const move = ev => set(ev.clientX, ev.clientY);
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  };
  const thick = 'calc(var(--list-item-height) * 0.15)';
  const handle = 'calc(var(--list-item-height) * 0.8)';
  return React.createElement('div', {
    ref,
    onMouseDown: start,
    style: {
      position: 'relative',
      flex: 'none',
      width: horizontal ? length : handle,
      height: horizontal ? handle : length,
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      cursor: disabled ? 'default' : 'pointer',
      opacity: disabled ? .4 : 1,
      ...style
    },
    ...rest
  }, React.createElement('div', {
    style: {
      position: 'absolute',
      background: 'var(--bg-light)',
      borderRadius: 999,
      width: horizontal ? '100%' : thick,
      height: horizontal ? thick : '100%'
    }
  }, React.createElement('div', {
    style: {
      position: 'absolute',
      background: 'var(--highlight)',
      borderRadius: 999,
      left: 0,
      bottom: 0,
      /* fill ends under the handle's centre: handle/2 + pos * (track - handle) */
      width: horizontal ? 'calc(' + handle + ' / 2 + ' + pos + ' * (100% - ' + handle + '))' : '100%',
      height: horizontal ? '100%' : 'calc(' + handle + ' / 2 + ' + pos + ' * (100% - ' + handle + '))'
    }
  })), React.createElement('div', {
    style: {
      position: 'absolute',
      width: handle,
      height: handle,
      background: 'var(--fg-main)',
      borderRadius: 'calc(var(--list-item-height) * 0.16)',
      /* The handle stays fully inside the control at both extremes: its leading edge moves
         over (100% - handle). The previous version added pos * 100% AND a pos * (length - 21)px
         translate, so at high values the handle sat past the end of the track (at 255 it was
         almost a full track length too far right and overlapped the value box). */
      left: horizontal ? 'calc(' + pos + ' * (100% - ' + handle + '))' : undefined,
      bottom: horizontal ? undefined : 'calc(' + pos + ' * (100% - ' + handle + '))'
    }
  }));
}
Object.assign(__ds_scope, { CustomSlider });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/CustomSlider.jsx", error: String((e && e.message) || e) }); }

// components/inputs/CustomTextInput.jsx
try { (() => {
/** CustomTextInput.qml — read-only by default; F2 or double-click starts editing, Esc reverts. */
function CustomTextInput({
  text = '',
  onTextConfirmed,
  onClick,
  allowDoubleClick = false,
  editing: editingProp,
  color = 'var(--fg-main)',
  width = 100,
  height = 'var(--list-item-height)',
  align = 'left',
  placeholder,
  style,
  ...rest
}) {
  const [value, setValue] = React.useState(text);
  const [editing, setEditing] = React.useState(false);
  const [original, setOriginal] = React.useState(text);
  React.useEffect(() => {
    setValue(text);
    setOriginal(text);
  }, [text]);
  const active = editingProp != null ? editingProp : editing;
  const start = () => {
    setOriginal(value);
    setEditing(true);
  };
  const finish = () => {
    setEditing(false);
    if (onTextConfirmed) onTextConfirmed(value);
  };
  return React.createElement('input', {
    value,
    placeholder,
    readOnly: !active,
    onChange: e => setValue(e.target.value),
    onClick,
    onDoubleClick: () => {
      if (allowDoubleClick) start();
    },
    onBlur: () => {
      if (active) finish();
    },
    onKeyDown: e => {
      if (e.key === 'F2') {
        e.preventDefault();
        start();
      } else if (e.key === 'Escape') {
        setValue(original);
        setEditing(false);
      } else if (e.key === 'Enter') finish();
    },
    style: {
      width,
      height,
      padding: 0,
      textAlign: align,
      background: 'transparent',
      border: 'none',
      outline: 'none',
      color,
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)',
      cursor: active ? 'text' : 'default',
      ...style
    },
    ...rest
  });
}
Object.assign(__ds_scope, { CustomTextInput });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/CustomTextInput.jsx", error: String((e && e.message) || e) }); }

// components/inputs/QLCPlusFader.jsx
try { (() => {
/** QLCPlusFader.qml — the channel fader: 5px cyan track, grey fill above, metal handle. */
function QLCPlusFader({
  value = 0,
  from = 0,
  to = 255,
  onMoved,
  width = 32,
  height = 100,
  trackColor = 'var(--fader-track)',
  disabled = false,
  style,
  ...rest
}) {
  const ref = React.useRef(null);
  const [press, setPress] = React.useState(false);
  const pos = (value - from) / (to - from || 1);
  const set = clientY => {
    const el = ref.current;
    if (!el || !onMoved) return;
    const r = el.getBoundingClientRect();
    onMoved(Math.round(from + Math.max(0, Math.min(1, 1 - (clientY - r.top) / r.height)) * (to - from)));
  };
  const start = e => {
    if (disabled) return;
    e.preventDefault();
    setPress(true);
    set(e.clientY);
    const move = ev => set(ev.clientY);
    const up = () => {
      setPress(false);
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  };
  return React.createElement('div', {
    ref,
    onMouseDown: start,
    style: {
      position: 'relative',
      width,
      height,
      flex: 'none',
      cursor: disabled ? 'default' : 'ns-resize',
      opacity: disabled ? .5 : 1,
      ...style
    },
    ...rest
  }, React.createElement('div', {
    style: {
      position: 'absolute',
      left: '50%',
      transform: 'translateX(-50%)',
      top: 0,
      bottom: 0,
      width: 5,
      background: trackColor,
      borderRadius: 'var(--radius-fader)'
    }
  }, React.createElement('div', {
    style: {
      position: 'absolute',
      top: 0,
      left: 0,
      right: 0,
      height: (1 - pos) * 100 + '%',
      background: 'var(--fader-fill)',
      borderRadius: 'var(--radius-fader)'
    }
  })), React.createElement('div', {
    style: {
      position: 'absolute',
      left: '50%',
      width: 'min(var(--icon-size-default), 100%)',
      height: 'calc(var(--icon-size-default) * 0.75)',
      top: 'calc(' + (1 - pos) * 100 + '% - (var(--icon-size-default) * 0.375))',
      transform: 'translateX(-50%)',
      background: press ? 'var(--gradient-fader-handle-hover)' : 'var(--gradient-fader-handle)',
      border: '1px solid var(--fader-handle-border)',
      borderRadius: 'var(--radius-handle)'
    }
  }));
}
Object.assign(__ds_scope, { QLCPlusFader });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/QLCPlusFader.jsx", error: String((e && e.message) || e) }); }

// components/shortcuts/ShortcutOverlay.jsx
try { (() => {
const ShortcutContext = React.createContext({
  active: false,
  heldKey: null
});

/**
 * Tracks a held modifier key. Returns the held key name ("Ctrl" | "Shift" | "Alt")
 * or null. Auto-suppressed while a text field has focus, per the MA-style shortcut
 * design's text-field focus guard.
 */
function useHeldModifier(keys = ['Control', 'Shift', 'Alt']) {
  const [held, setHeld] = React.useState(null);
  React.useEffect(() => {
    const typing = () => {
      const el = document.activeElement;
      return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
    };
    const label = {
      Control: 'Ctrl',
      Shift: 'Shift',
      Alt: 'Alt'
    };
    const down = e => {
      if (!typing() && keys.indexOf(e.key) !== -1) setHeld(label[e.key] || e.key);
    };
    const up = e => {
      if (keys.indexOf(e.key) !== -1) setHeld(null);
    };
    const blur = () => setHeld(null);
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    window.addEventListener('blur', blur);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
      window.removeEventListener('blur', blur);
    };
  }, []);
  return held;
}

/** Capital-initial alias so the hook is reachable as <Namespace>.ShortcutKeys.useHeldModifier in plain HTML. */
const ShortcutKeys = {
  useHeldModifier
};

/**
 * Hold-a-modifier shortcut overlay. Dims the view to QLC+'s standard 50% black
 * and lets every <ShortcutHint> inside it surface its own key badge, pinned over
 * the control it belongs to.
 */
function ShortcutOverlay({
  active = false,
  heldKey = 'Ctrl',
  legend,
  children,
  style,
  ...rest
}) {
  return React.createElement(ShortcutContext.Provider, {
    value: {
      active,
      heldKey
    }
  }, React.createElement('div', {
    style: {
      position: 'relative',
      width: '100%',
      height: '100%',
      minHeight: 0,
      ...style
    },
    ...rest
  }, children, active ? React.createElement('div', {
    style: {
      position: 'absolute',
      inset: 0,
      zIndex: 90,
      background: 'var(--dim-screen)',
      pointerEvents: 'none'
    }
  }) : null, active ? React.createElement('div', {
    style: {
      position: 'absolute',
      left: '50%',
      bottom: 12,
      transform: 'translateX(-50%)',
      zIndex: 120,
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-row)',
      padding: '4px var(--pad-dialog)',
      background: 'var(--section-header)',
      border: 'var(--border-dialog)',
      borderRadius: 'var(--radius-control)',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-menubar)',
      color: 'var(--fg-main)',
      whiteSpace: 'nowrap',
      pointerEvents: 'none'
    }
  }, React.createElement('b', null, heldKey), legend || 'held \u2014 release to dismiss') : null));
}
Object.assign(__ds_scope, { ShortcutContext, useHeldModifier, ShortcutKeys, ShortcutOverlay });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/shortcuts/ShortcutOverlay.jsx", error: String((e && e.message) || e) }); }

// components/shortcuts/ShortcutHint.jsx
try { (() => {
/** Pins a key badge over the control it wraps while the shortcut overlay is active. */
function ShortcutHint({
  keys,
  placement = 'center',
  disabled = false,
  children,
  style,
  ...rest
}) {
  const ctx = React.useContext(__ds_scope.ShortcutContext);
  const show = ctx.active && !disabled && !!keys;
  /* Every anchor sits INSIDE the control's box — a badge that overhangs would clip
     against the window edge on toolbars pinned to the top or the sides. */
  const anchor = {
    center: {
      top: '50%',
      left: '50%',
      transform: 'translate(-50%,-50%)'
    },
    top: {
      top: 1,
      left: '50%',
      transform: 'translateX(-50%)'
    },
    bottom: {
      bottom: 1,
      left: '50%',
      transform: 'translateX(-50%)'
    },
    left: {
      top: '50%',
      left: 1,
      transform: 'translateY(-50%)'
    },
    right: {
      top: '50%',
      right: 1,
      transform: 'translateY(-50%)'
    },
    corner: {
      top: 1,
      right: 1
    }
  }[placement] || {};
  return React.createElement('span', {
    style: {
      position: 'relative',
      display: 'inline-flex',
      ...style
    },
    ...rest
  }, children, show ? React.createElement('span', {
    style: {
      position: 'absolute',
      zIndex: 110,
      pointerEvents: 'none',
      display: 'inline-flex',
      alignItems: 'center',
      gap: 3,
      padding: '1px 5px',
      whiteSpace: 'nowrap',
      background: 'var(--selection)',
      color: 'var(--border-color-dark)',
      border: '2px solid var(--border-color-dark)',
      borderRadius: 'var(--radius-bubble)',
      fontFamily: 'var(--font-roboto)',
      fontWeight: 700,
      fontSize: 'var(--text-size-menubar)',
      boxShadow: '0 0 0 1px var(--selection)',
      ...anchor
    }
  }, String(keys).split(/\s+/).map((k, i) => React.createElement('span', {
    key: i,
    style: i === 0 && String(keys).split(/\s+/).length > 1 ? {
      opacity: .7
    } : undefined
  }, k))) : null);
}
Object.assign(__ds_scope, { ShortcutHint });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/shortcuts/ShortcutHint.jsx", error: String((e && e.message) || e) }); }

// components/text/FaIcon.jsx
try { (() => {
/** Font Awesome 7 Solid glyphs, by the names qmlui/qml/FontAwesomeVariables.qml uses. */
const FA = {
  fa_bars: '\uf0c9',
  fa_bug: '\uf188',
  fa_check: '\uf00c',
  fa_chevron_down: '\uf078',
  fa_chevron_up: '\uf077',
  fa_chevron_left: '\uf053',
  fa_chevron_right: '\uf054',
  fa_circle_info: '\uf05a',
  fa_gear: '\uf013',
  fa_hashtag: '#',
  fa_list_ol: '\uf0cb',
  fa_list_ul: '\uf0ca',
  fa_lock: '\uf023',
  fa_octagon: '\uf306',
  fa_pause: '\uf04c',
  fa_play: '\uf04b',
  fa_plus: '+',
  fa_square_minus: '\uf146',
  fa_square_plus: '\uf0fe',
  fa_trash_can: '\uf2ed',
  fa_xmark: '\uf00d'
};

/** Renders one Font Awesome glyph, the way every QML Text { font.family: fontAwesomeFontName } does. */
function FaIcon({
  name,
  size = 16,
  color = 'var(--fg-main)',
  style,
  ...rest
}) {
  const glyph = FA[name] || FA['fa_' + name] || name;
  return React.createElement('span', {
    style: {
      fontFamily: 'var(--font-awesome)',
      fontWeight: 900,
      fontSize: size,
      lineHeight: 1,
      color,
      display: 'inline-block',
      textAlign: 'center',
      flex: 'none',
      ...style
    },
    ...rest
  }, glyph);
}
Object.assign(__ds_scope, { FA, FaIcon });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/text/FaIcon.jsx", error: String((e && e.message) || e) }); }

// components/buttons/IconButton.jsx
try { (() => {
/** IconButton.qml — square icon-only button, 5px radius, 2px #1D1D1D border. */
function IconButton({
  imgSource,
  faSource,
  faColor = 'var(--bg-strong)',
  tooltip,
  size = 'var(--icon-size-default)',
  bgColor = 'var(--bg-light)',
  hoverColor = 'var(--hover)',
  pressColor = 'var(--highlight-pressed)',
  checkedColor = 'var(--highlight)',
  checked = false,
  disabled = false,
  imgMargins = 6,
  borderWidth = 2,
  radius = 'var(--radius-control)',
  onClick,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const [press, setPress] = React.useState(false);
  const bg = checked ? checkedColor : press ? pressColor : hover ? hoverColor : bgColor;
  return React.createElement('button', {
    type: 'button',
    disabled,
    onClick,
    title: tooltip,
    'aria-label': tooltip,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => {
      setHover(false);
      setPress(false);
    },
    onMouseDown: () => setPress(true),
    onMouseUp: () => setPress(false),
    style: {
      position: 'relative',
      width: size,
      height: size,
      flex: 'none',
      padding: 0,
      display: 'inline-flex',
      alignItems: 'center',
      justifyContent: 'center',
      background: bg,
      border: borderWidth ? borderWidth + 'px solid var(--icon-border)' : 'none',
      borderRadius: radius,
      cursor: disabled ? 'default' : 'pointer',
      ...style
    },
    ...rest
  }, imgSource ? React.createElement('img', {
    src: imgSource,
    alt: '',
    style: {
      width: 'calc(100% - ' + imgMargins + 'px)',
      height: 'calc(100% - ' + imgMargins + 'px)',
      objectFit: 'contain'
    }
  }) : null, faSource ? React.createElement(__ds_scope.FaIcon, {
    name: faSource,
    color: faColor,
    size: '70%',
    style: {
      fontSize: '70%'
    }
  }) : null, disabled ? React.createElement('span', {
    style: {
      position: 'absolute',
      inset: 0,
      background: 'var(--disabled-veil-strong)',
      borderRadius: radius
    }
  }) : null);
}
Object.assign(__ds_scope, { IconButton });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/buttons/IconButton.jsx", error: String((e && e.message) || e) }); }

// components/inputs/CustomCheckBox.jsx
try { (() => {
/** CustomCheckBox.qml — a rounded grey square with a lime Font Awesome tick. */
function CustomCheckBox({
  checked = false,
  onToggled,
  tooltip,
  size = 'var(--icon-size-default)',
  bgColor = 'var(--bg-control)',
  hoverColor = 'var(--bg-light)',
  disabled = false,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const [focus, setFocus] = React.useState(false);
  return React.createElement('button', {
    type: 'button',
    disabled,
    title: tooltip,
    'aria-pressed': checked,
    onClick: () => onToggled && onToggled(!checked),
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    onFocus: () => setFocus(true),
    onBlur: () => setFocus(false),
    style: {
      position: 'relative',
      width: size,
      height: size,
      flex: 'none',
      padding: 0,
      display: 'inline-flex',
      alignItems: 'center',
      justifyContent: 'center',
      background: hover ? hoverColor : bgColor,
      border: '2px solid ' + (focus ? 'var(--highlight)' : 'var(--bg-strong)'),
      borderRadius: 'var(--radius-control)',
      cursor: disabled ? 'default' : 'pointer',
      ...style
    },
    ...rest
  }, checked ? React.createElement(__ds_scope.FaIcon, {
    name: 'fa_check',
    color: 'var(--check-lime)',
    size: '80%',
    style: {
      fontSize: '80%'
    }
  }) : null, disabled ? React.createElement('span', {
    style: {
      position: 'absolute',
      inset: 0,
      background: 'var(--disabled-veil-soft)',
      borderRadius: 'var(--radius-control)'
    }
  }) : null);
}
Object.assign(__ds_scope, { CustomCheckBox });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/CustomCheckBox.jsx", error: String((e && e.message) || e) }); }

// components/inputs/CustomComboBox.jsx
try { (() => {
/** CustomComboBox.qml — grey dropdown with an fa_chevron_down indicator. */
function CustomComboBox({
  model = [],
  currValue,
  onValueChanged,
  width = 150,
  height = 'var(--list-item-height)',
  disabled = false,
  style,
  ...rest
}) {
  const [open, setOpen] = React.useState(false);
  const [hover, setHover] = React.useState(false);
  const items = model.map((m, i) => typeof m === 'string' ? {
    mLabel: m,
    mValue: i
  } : m);
  const current = items.find(m => m.mValue === currValue) || items[0] || {
    mLabel: ''
  };
  return React.createElement('div', {
    style: {
      position: 'relative',
      width,
      flex: 'none',
      ...style
    },
    ...rest
  }, React.createElement('button', {
    type: 'button',
    disabled,
    onClick: () => setOpen(!open),
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 5,
      width: '100%',
      height,
      padding: '0 5px 0 3px',
      background: hover ? 'var(--bg-light)' : 'var(--bg-control)',
      border: '1px solid var(--bg-strong)',
      borderRadius: 'var(--radius-spin)',
      cursor: disabled ? 'default' : 'pointer',
      opacity: disabled ? .6 : 1
    }
  }, current.mIcon ? React.createElement('img', {
    src: current.mIcon,
    alt: '',
    style: {
      height: 'calc(100% - 4px)',
      flex: 'none'
    }
  }) : null, current.faIcon ? React.createElement(__ds_scope.FaIcon, {
    name: current.faIcon,
    size: 16
  }) : null, React.createElement('span', {
    style: {
      flex: 1,
      minWidth: 0,
      textAlign: 'left',
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      whiteSpace: 'nowrap',
      color: 'var(--fg-main)',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)'
    }
  }, current.mLabel), React.createElement(__ds_scope.FaIcon, {
    name: 'fa_chevron_down',
    size: 13,
    color: 'var(--fg-light)'
  })), open ? React.createElement('div', {
    onMouseLeave: () => setOpen(false),
    style: {
      position: 'absolute',
      top: '100%',
      left: 0,
      minWidth: '100%',
      zIndex: 40,
      maxHeight: 240,
      overflow: 'auto',
      background: 'var(--bg-light)',
      border: '1px solid var(--bg-lighter)'
    }
  }, items.map((m, i) => React.createElement('button', {
    key: String(m.mValue) + i,
    type: 'button',
    onClick: () => {
      setOpen(false);
      if (onValueChanged) onValueChanged(m.mValue);
    },
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-inline)',
      width: '100%',
      height: 'var(--list-item-height)',
      padding: '0 3px',
      textAlign: 'left',
      background: m.mValue === currValue ? 'var(--highlight)' : 'transparent',
      border: 'none',
      borderBottom: '1px solid var(--fg-main)',
      cursor: 'pointer',
      color: 'var(--fg-main)',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)',
      whiteSpace: 'nowrap'
    },
    onMouseEnter: e => {
      if (m.mValue !== currValue) e.currentTarget.style.background = 'var(--bg-control)';
    },
    onMouseLeave: e => {
      if (m.mValue !== currValue) e.currentTarget.style.background = 'transparent';
    }
  }, m.mIcon ? React.createElement('img', {
    src: m.mIcon,
    alt: '',
    style: {
      height: 'calc(var(--list-item-height) - 4px)'
    }
  }) : null, m.faIcon ? React.createElement(__ds_scope.FaIcon, {
    name: m.faIcon,
    size: 16
  }) : null, m.mLabel))) : null);
}
Object.assign(__ds_scope, { CustomComboBox });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/CustomComboBox.jsx", error: String((e && e.message) || e) }); }

// components/inputs/CustomSpinBox.jsx
try { (() => {
/** CustomSpinBox.qml — right-aligned number field with stacked arrow buttons. */
function CustomSpinBox({
  value = 0,
  onValueModified,
  from = 0,
  to = 255,
  stepSize = 1,
  suffix = '',
  showControls = true,
  width = 'var(--big-item-height)',
  height = 'var(--list-item-height)',
  align = 'right',
  disabled = false,
  style,
  ...rest
}) {
  const set = v => {
    const c = Math.min(to, Math.max(from, v));
    if (onValueModified) onValueModified(c);
  };
  const arrow = dir => React.createElement('button', {
    key: dir,
    type: 'button',
    disabled,
    onClick: () => set(value + dir * stepSize),
    style: {
      flex: 1,
      minHeight: 0,
      display: 'grid',
      placeItems: 'center',
      padding: 0,
      background: 'var(--bg-control)',
      border: '1px solid var(--bg-strong)',
      cursor: 'pointer'
    }
  }, React.createElement(__ds_scope.FaIcon, {
    name: dir === 1 ? 'fa_chevron_up' : 'fa_chevron_down',
    size: 9,
    color: 'var(--fg-main)'
  }));
  return React.createElement('div', {
    style: {
      position: 'relative',
      display: 'flex',
      alignItems: 'stretch',
      width,
      height,
      overflow: 'hidden',
      background: 'var(--bg-control)',
      border: '1px solid var(--spin-border)',
      borderRadius: 'var(--radius-spin)',
      ...style
    }
  }, React.createElement('input', {
    value: value + suffix,
    disabled,
    inputMode: 'numeric',
    onChange: e => {
      const n = parseInt(String(e.target.value).replace(suffix, ''), 10);
      if (!isNaN(n)) set(n);
    },
    style: {
      flex: 1,
      minWidth: 0,
      height: '100%',
      padding: '0 5px 0 0',
      textAlign: align,
      background: 'transparent',
      border: 'none',
      outline: 'none',
      color: 'var(--fg-main)',
      fontFamily: 'var(--font-roboto)',
      fontSize: 'var(--text-size-default)'
    },
    ...rest
  }), showControls ? React.createElement('div', {
    style: {
      display: 'flex',
      flexDirection: 'column',
      width: 'min(var(--icon-size-medium), 33%)',
      flex: 'none'
    }
  }, [arrow(1), arrow(-1)]) : null, disabled ? React.createElement('span', {
    style: {
      position: 'absolute',
      inset: 0,
      background: 'var(--disabled-veil-strong)'
    }
  }) : null);
}
Object.assign(__ds_scope, { CustomSpinBox });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/inputs/CustomSpinBox.jsx", error: String((e && e.message) || e) }); }

// components/text/RobotoText.jsx
try { (() => {
/** RobotoText.qml — the label primitive. Transparent, clipped, vertically centred. */
function RobotoText({
  label,
  children,
  labelColor = 'var(--fg-main)',
  fontSize = 'var(--text-size-default)',
  fontBold = false,
  fontItalic = false,
  wrapText = false,
  textHAlign = 'left',
  leftMargin = 0,
  rightMargin = 0,
  height = 'var(--icon-size-default)',
  disabled = false,
  style,
  ...rest
}) {
  return React.createElement('div', {
    style: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: textHAlign === 'center' ? 'center' : textHAlign === 'right' ? 'flex-end' : 'flex-start',
      height,
      paddingLeft: leftMargin,
      paddingRight: rightMargin,
      background: 'transparent',
      overflow: 'hidden',
      ...style
    },
    ...rest
  }, React.createElement('span', {
    style: {
      fontFamily: 'var(--font-roboto)',
      fontSize,
      fontWeight: fontBold ? 700 : 400,
      fontStyle: fontItalic ? 'italic' : 'normal',
      color: disabled ? 'color-mix(in srgb, ' + labelColor + ' 45%, black)' : labelColor,
      whiteSpace: wrapText ? 'normal' : 'nowrap',
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      lineHeight: wrapText ? 'var(--lh-normal)' : 1
    }
  }, label != null ? label : children));
}
Object.assign(__ds_scope, { RobotoText });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/text/RobotoText.jsx", error: String((e && e.message) || e) }); }

// components/buttons/ContextMenuEntry.jsx
try { (() => {
/** ContextMenuEntry.qml — one row of a popup menu. 1px --bg-light border on every row. */
function ContextMenuEntry({
  imgSource,
  faSource,
  faColor = 'var(--fg-main)',
  entryText,
  bgColor = 'transparent',
  hoverColor = 'var(--highlight)',
  pressColor = 'var(--highlight-pressed)',
  iconHeight = 'var(--icon-size-default)',
  disabled = false,
  onClick,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const [press, setPress] = React.useState(false);
  return React.createElement('div', {
    role: 'menuitem',
    onClick: disabled ? undefined : onClick,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => {
      setHover(false);
      setPress(false);
    },
    onMouseDown: () => setPress(true),
    onMouseUp: () => setPress(false),
    style: {
      position: 'relative',
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-row)',
      padding: '3px var(--pad-menu-entry)',
      minHeight: 'calc(var(--icon-size-default) + 6px)',
      background: press ? pressColor : hover ? hoverColor : bgColor,
      border: 'var(--border-menu)',
      cursor: disabled ? 'default' : 'pointer',
      ...style
    },
    ...rest
  }, imgSource ? React.createElement('img', {
    src: imgSource,
    alt: '',
    style: {
      width: iconHeight,
      height: iconHeight,
      flex: 'none'
    }
  }) : null, faSource ? React.createElement(__ds_scope.FaIcon, {
    name: faSource,
    color: faColor,
    size: 20
  }) : null, React.createElement(__ds_scope.RobotoText, {
    label: entryText,
    fontBold: true,
    height: 'var(--icon-size-default)'
  }), disabled ? React.createElement('span', {
    style: {
      position: 'absolute',
      inset: 0,
      background: 'var(--disabled-veil-strong)'
    }
  }) : null);
}
Object.assign(__ds_scope, { ContextMenuEntry });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/buttons/ContextMenuEntry.jsx", error: String((e && e.message) || e) }); }

// components/buttons/DMXPercentageButton.jsx
try { (() => {
/** DMXPercentageButton.qml — the DMX/% unit toggle. White 2px border on --section-header. */
function DMXPercentageButton({
  dmxMode = true,
  onClick,
  height = 'var(--list-item-height)',
  style,
  ...rest
}) {
  return React.createElement('button', {
    type: 'button',
    onClick,
    style: {
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      width: 'calc(var(--icon-size-default) * 1.1)',
      height,
      padding: 0,
      background: 'var(--section-header)',
      border: '2px solid var(--fg-main)',
      borderRadius: 'var(--radius-control)',
      cursor: 'pointer',
      flex: 'none',
      ...style
    },
    ...rest
  }, React.createElement(__ds_scope.RobotoText, {
    label: dmxMode ? 'DMX' : '%',
    fontBold: true,
    height: '100%'
  }));
}
Object.assign(__ds_scope, { DMXPercentageButton });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/buttons/DMXPercentageButton.jsx", error: String((e && e.message) || e) }); }

// components/buttons/MenuBarEntry.jsx
try { (() => {
/** MenuBarEntry.qml — a top toolbar tab: icon + bold small label + underline when checked. */
function MenuBarEntry({
  imgSource,
  faSource,
  faColor = 'var(--bg-strong)',
  entryText,
  checked = false,
  checkedColor = 'var(--toolbar-selection-main)',
  height = 'var(--icon-size-default)',
  disabled = false,
  onClick,
  onContextMenu,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  const [press, setPress] = React.useState(false);
  const bg = press ? 'var(--gradient-toolbar-pressed)' : checked || hover ? 'var(--gradient-toolbar-hover)' : 'transparent';
  return React.createElement('button', {
    type: 'button',
    disabled,
    onClick,
    onContextMenu,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => {
      setHover(false);
      setPress(false);
    },
    onMouseDown: () => setPress(true),
    onMouseUp: () => setPress(false),
    style: {
      display: 'flex',
      alignItems: 'flex-start',
      gap: 4,
      height,
      padding: '0 2px',
      background: bg,
      border: 'none',
      cursor: disabled ? 'default' : 'pointer',
      opacity: disabled ? .3 : 1,
      flex: 'none',
      ...style
    },
    ...rest
  }, imgSource ? React.createElement('img', {
    src: imgSource,
    alt: '',
    style: {
      width: 'calc(' + (typeof height === 'number' ? height + 'px' : height) + ' - 4px)',
      height: 'calc(' + (typeof height === 'number' ? height + 'px' : height) + ' - 4px)',
      alignSelf: 'center'
    }
  }) : null, faSource ? React.createElement(__ds_scope.FaIcon, {
    name: faSource,
    color: faColor,
    size: 22,
    style: {
      alignSelf: 'center'
    }
  }) : null, entryText ? React.createElement('div', {
    style: {
      position: 'relative',
      height,
      display: 'flex',
      alignItems: 'center'
    }
  }, React.createElement(__ds_scope.RobotoText, {
    label: entryText,
    fontBold: true,
    fontSize: 'var(--text-size-menubar)',
    height: '100%'
  }), React.createElement('span', {
    style: {
      position: 'absolute',
      left: 0,
      right: 0,
      bottom: 2,
      height: 3,
      borderRadius: 1.5,
      background: checked ? checkedColor : 'transparent'
    }
  })) : null);
}
Object.assign(__ds_scope, { MenuBarEntry });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/buttons/MenuBarEntry.jsx", error: String((e && e.message) || e) }); }

// components/console/ChannelStrip.jsx
try { (() => {
/** The Simple Desk channel column: icon, fader, value, address, reset, debug. */
function ChannelStrip({
  address,
  value = 0,
  channelIcon,
  channelName,
  display = 'none',
  isOverride = false,
  dmxValues = true,
  onValueChanged,
  onReset,
  onDebug,
  height = 300,
  style,
  ...rest
}) {
  const bg = isOverride ? 'var(--override-red)' : display === 'odd' ? 'var(--bg-fixture-odd)' : display === 'even' ? 'var(--bg-fixture-even)' : 'transparent';
  const shown = dmxValues ? value : Math.round(value / 255 * 100);
  return React.createElement('div', {
    style: {
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      gap: 2,
      width: 'var(--icon-size-default)',
      height,
      flex: 'none',
      padding: 1,
      background: bg,
      border: 'var(--border-dark)',
      ...style
    },
    ...rest
  }, channelIcon ? React.createElement(__ds_scope.IconButton, {
    imgSource: channelIcon,
    tooltip: channelName,
    borderWidth: 0,
    size: 'var(--icon-size-medium)',
    bgColor: 'var(--bg-light)'
  }) : null, React.createElement(__ds_scope.QLCPlusFader, {
    value,
    onMoved: onValueChanged,
    width: 32,
    height: Math.max(60, height - 130),
    style: {
      flex: 1
    }
  }), React.createElement(__ds_scope.CustomSpinBox, {
    value: shown,
    showControls: false,
    align: 'center',
    to: dmxValues ? 255 : 100,
    suffix: dmxValues ? '' : '%',
    width: 'var(--icon-size-default)',
    height: 'calc(var(--list-item-height) * 0.75)',
    onValueModified: v => onValueChanged && onValueChanged(Math.round(v * (dmxValues ? 1 : 2.55)))
  }), React.createElement(__ds_scope.RobotoText, {
    label: address,
    fontBold: true,
    height: 'calc(var(--list-item-height) * 0.75)',
    textHAlign: 'center'
  }), React.createElement(__ds_scope.IconButton, {
    faSource: 'fa_xmark',
    faColor: 'var(--bg-control)',
    tooltip: 'Reset the channel',
    size: 'var(--icon-size-medium)',
    onClick: onReset
  }), React.createElement(__ds_scope.IconButton, {
    faSource: 'fa_bug',
    faColor: 'var(--bg-control)',
    tooltip: "Debug this channel's value",
    size: 'var(--icon-size-medium)',
    onClick: onDebug
  }));
}
Object.assign(__ds_scope, { ChannelStrip });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/console/ChannelStrip.jsx", error: String((e && e.message) || e) }); }

// components/containers/SectionBox.jsx
try { (() => {
/** SectionBox.qml — collapsible section with a --section-header bar and a +/- box glyph. */
function SectionBox({
  sectionLabel,
  isExpanded = true,
  onToggle,
  children,
  style,
  ...rest
}) {
  const [hover, setHover] = React.useState(false);
  return React.createElement('div', {
    style: {
      width: '100%',
      background: 'transparent',
      overflow: 'hidden',
      ...style
    },
    ...rest
  }, React.createElement('div', {
    onClick: onToggle,
    onMouseEnter: () => setHover(true),
    onMouseLeave: () => setHover(false),
    style: {
      position: 'relative',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      height: 'var(--list-item-height)',
      cursor: 'pointer',
      background: hover ? 'var(--highlight)' : 'var(--section-header)',
      borderBottom: '1px solid var(--section-header-div)'
    }
  }, React.createElement(__ds_scope.RobotoText, {
    label: sectionLabel,
    height: '100%'
  }), React.createElement(__ds_scope.FaIcon, {
    name: isExpanded ? 'fa_square_minus' : 'fa_square_plus',
    size: 'var(--text-size-large)',
    style: {
      position: 'absolute',
      right: 'var(--list-item-height)',
      fontSize: 'var(--text-size-large)'
    }
  })), isExpanded ? React.createElement('div', {
    style: {
      width: '100%'
    }
  }, children) : null);
}
Object.assign(__ds_scope, { SectionBox });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/containers/SectionBox.jsx", error: String((e && e.message) || e) }); }

// components/text/IconTextEntry.jsx
try { (() => {
/** IconTextEntry.qml — icon + label row used for every list item in the app. */
function IconTextEntry({
  iSrc,
  faSource,
  faColor = '#222',
  tLabel,
  tLabelColor = 'var(--fg-main)',
  tFontSize = 'var(--text-size-default)',
  height = 'var(--icon-size-default)',
  style,
  ...rest
}) {
  const iconSize = 'calc(' + (typeof height === 'number' ? height + 'px' : height) + ' - 4px)';
  return React.createElement('div', {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-row)',
      height,
      background: 'transparent',
      minWidth: 0,
      ...style
    },
    ...rest
  }, iSrc ? React.createElement('img', {
    src: iSrc,
    alt: '',
    style: {
      width: iconSize,
      height: iconSize,
      flex: 'none'
    }
  }) : null, faSource ? React.createElement(__ds_scope.FaIcon, {
    name: faSource,
    size: 16,
    color: faColor
  }) : null, React.createElement(__ds_scope.RobotoText, {
    label: tLabel,
    labelColor: tLabelColor,
    fontSize: tFontSize,
    height: '100%',
    style: {
      flex: 1,
      minWidth: 0
    }
  }));
}
Object.assign(__ds_scope, { IconTextEntry });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/text/IconTextEntry.jsx", error: String((e && e.message) || e) }); }

// components/console/TreeNode.jsx
try { (() => {
/** TreeNodeDelegate.qml — a tree row plus its non-virtualised children, indented 20px. */
function TreeNode({
  textLabel,
  itemIcon,
  isExpanded = false,
  isSelected = false,
  isCheckable = false,
  isChecked = false,
  hasChildren = false,
  depth = 0,
  onToggle,
  onSelect,
  onCheck,
  children,
  style,
  ...rest
}) {
  return React.createElement('div', {
    style: {
      width: '100%',
      ...style
    },
    ...rest
  }, React.createElement('div', {
    onClick: onSelect,
    onDoubleClick: hasChildren ? onToggle : undefined,
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 'var(--space-row)',
      height: 'var(--list-item-height)',
      paddingRight: 'var(--space-row)',
      background: isSelected ? 'var(--highlight)' : 'transparent',
      cursor: 'pointer'
    }
  }, hasChildren ? React.createElement('button', {
    type: 'button',
    onClick: e => {
      e.stopPropagation();
      if (onToggle) onToggle();
    },
    style: {
      width: 'var(--list-item-height)',
      height: '100%',
      background: 'transparent',
      border: 'none',
      cursor: 'pointer',
      padding: 0
    }
  }, React.createElement(__ds_scope.FaIcon, {
    name: isExpanded ? 'fa_square_minus' : 'fa_square_plus',
    size: 14
  })) : React.createElement('span', {
    style: {
      width: 'var(--list-item-height)',
      flex: 'none'
    }
  }), isCheckable ? React.createElement(__ds_scope.CustomCheckBox, {
    checked: isChecked,
    size: 'calc(var(--list-item-height) - 6px)',
    onToggled: v => onCheck && onCheck(v)
  }) : null, React.createElement(__ds_scope.IconTextEntry, {
    iSrc: itemIcon,
    tLabel: textLabel,
    height: 'var(--list-item-height)',
    style: {
      flex: 1,
      minWidth: 0
    }
  })), isExpanded && children ? React.createElement('div', {
    style: {
      marginLeft: 'var(--tree-indent)'
    }
  }, children) : null);
}
Object.assign(__ds_scope, { TreeNode });
})(); } catch (e) { __ds_ns.__errors.push({ path: "components/console/TreeNode.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/App.jsx
try { (() => {
const {
  ViewToolbar,
  ToolbarSpacer,
  MenuBarEntry,
  IconButton,
  RobotoText,
  ShortcutOverlay,
  ShortcutHint,
  ShortcutKeys,
  CustomPopupDialog,
  GenericButton,
  CustomTextInput
} = window.PatchDesignSystem_5432c9;
function useHeldFallback() {
  const [held, setHeld] = React.useState(null);
  React.useEffect(() => {
    const typing = () => {
      const el = document.activeElement;
      return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
    };
    const down = e => {
      if (!typing() && e.key === 'Control') setHeld('Ctrl');
    };
    const up = e => {
      if (e.key === 'Control') setHeld(null);
    };
    const blur = () => setHeld(null);
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    window.addEventListener('blur', blur);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
      window.removeEventListener('blur', blur);
    };
  }, []);
  return held;
}
function App() {
  const D = window.QLCData;
  const [ctx, setCtx] = React.useState('fx');
  const [blackout, setBlackout] = React.useState(false);
  const [about, setAbout] = React.useState(false);
  const [beat, setBeat] = React.useState(false);
  const held = (ShortcutKeys && ShortcutKeys.useHeldModifier ? ShortcutKeys.useHeldModifier : useHeldFallback)(['Control']);
  React.useEffect(() => {
    const t = setInterval(() => {
      setBeat(b => !b);
    }, 500);
    return () => clearInterval(t);
  }, []);
  React.useEffect(() => {
    const k = e => {
      if (!e.ctrlKey) return;
      const map = {
        '1': 'fx',
        '2': 'vc',
        '3': 'sd',
        '4': 'io'
      };
      if (map[e.key]) {
        e.preventDefault();
        setCtx(map[e.key]);
      }
      if (e.key.toLowerCase() === 'b') {
        e.preventDefault();
        setBlackout(b => !b);
      }
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, []);
  const Screen = ctx === 'fx' ? FixturesFunctions : ctx === 'vc' ? VirtualConsole : ctx === 'sd' ? SimpleDesk : InputOutput;
  const entry = (id, icon, label, keys) => /*#__PURE__*/React.createElement(ShortcutHint, {
    keys: keys,
    placement: "bottom"
  }, /*#__PURE__*/React.createElement(MenuBarEntry, {
    imgSource: D.icon(icon),
    entryText: label,
    checked: ctx === id,
    onClick: () => setCtx(id)
  }));
  return /*#__PURE__*/React.createElement(ShortcutOverlay, {
    active: !!held,
    heldKey: "Ctrl",
    legend: "held \u2014 release to dismiss"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100vh',
      minHeight: 0,
      background: 'var(--bg-medium)'
    }
  }, /*#__PURE__*/React.createElement(ViewToolbar, {
    variant: "main"
  }, /*#__PURE__*/React.createElement("img", {
    src: D.icon('qlcplus'),
    alt: "QLC+",
    style: {
      width: 30,
      height: 30,
      marginRight: 4,
      cursor: 'pointer'
    },
    onClick: () => setAbout(true)
  }), entry('fx', 'fixture', 'Fixtures & Functions', 'Ctrl 1'), entry('vc', 'virtualconsole', 'Virtual Console', 'Ctrl 2'), entry('sd', 'simpledesk', 'Simple Desk', 'Ctrl 3'), entry('io', 'inputoutput', 'Input / Output', 'Ctrl 4'), /*#__PURE__*/React.createElement(ToolbarSpacer, null), /*#__PURE__*/React.createElement(ConnectionBar, null), /*#__PURE__*/React.createElement("span", {
    style: {
      width: 1,
      alignSelf: 'stretch',
      margin: '6px 2px',
      background: 'var(--border-color-dark)'
    }
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      flex: '0 1 130px',
      minWidth: 0,
      overflow: 'hidden'
    }
  }, /*#__PURE__*/React.createElement(CustomTextInput, {
    text: "Winter Tour.qxw",
    width: "100%",
    align: "right",
    color: "var(--fg-light)"
  })), /*#__PURE__*/React.createElement(ShortcutHint, {
    keys: "Ctrl S",
    placement: "corner"
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('filesave'),
    tooltip: "Save workspace"
  })), /*#__PURE__*/React.createElement(ShortcutHint, {
    keys: "Ctrl B",
    placement: "corner"
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('blackout'),
    checked: blackout,
    onClick: () => setBlackout(!blackout),
    tooltip: "Blackout"
  })), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('stopall'),
    tooltip: "Stop all functions"
  }), /*#__PURE__*/React.createElement("span", {
    title: "Beat indicator \u2014 120 BPM",
    style: {
      width: 18,
      height: 18,
      borderRadius: 9,
      marginLeft: 4,
      background: beat ? 'var(--beat-flash)' : 'var(--bg-strong)',
      border: 'var(--border-dark)'
    }
  }), /*#__PURE__*/React.createElement(IconButton, {
    faSource: "fa_gear",
    tooltip: "Preferences"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'relative',
      flex: 1,
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement(Screen, null), blackout ? /*#__PURE__*/React.createElement("div", {
    style: {
      position: 'absolute',
      inset: 0,
      background: 'var(--dim-screen)',
      display: 'grid',
      placeItems: 'center',
      pointerEvents: 'none',
      zIndex: 90
    }
  }, /*#__PURE__*/React.createElement("span", {
    style: {
      font: '700 40px/1 var(--font-roboto)',
      color: 'var(--override-red)',
      letterSpacing: '.08em'
    }
  }, "BLACKOUT")) : null), /*#__PURE__*/React.createElement(CustomPopupDialog, {
    open: about,
    title: "About QLC+",
    width: 340,
    standardButtons: ['Close'],
    onClicked: () => setAbout(false),
    onClose: () => setAbout(false)
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 14,
      alignItems: 'center'
    }
  }, /*#__PURE__*/React.createElement("img", {
    src: D.icon('qlcplus'),
    alt: "",
    style: {
      width: 64,
      height: 64
    }
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 4
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Q Light Controller+",
    fontBold: true,
    fontSize: 20,
    height: 26
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Version 5 (qmlui) \u2014 design-system recreation",
    fontSize: 14,
    labelColor: "var(--fg-light)",
    height: 22
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Hold Ctrl to see shortcuts",
    fontSize: 14,
    labelColor: "var(--fg-medium)",
    height: 22
  }))))));
}
function AppRoot() {
  return /*#__PURE__*/React.createElement(QLCConnectionProvider, null, /*#__PURE__*/React.createElement(App, null));
}
Object.assign(window, {
  App,
  AppRoot
});
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/App.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/Connection.jsx
try { (() => {
/**
 * Live QLC+ Web API connection for the UI kit.
 *
 * Wraps window.QLCPlusAPI (api/qlcplus-api.js) in a React context so any screen can
 * read connection status and push values to a real desk. Every screen keeps working
 * unconnected — the hook simply reports status "offline" and the send helpers no-op,
 * so the kit stays a click-through mock until someone types a host.
 */
const QLCConnectionContext = React.createContext(null);

/** ws:// from an https page is blocked by the browser and can never succeed — say so up front. */
const WS_BLOCKED = typeof location !== 'undefined' && location.protocol === 'https:';
function diagnose(host) {
  return WS_BLOCKED ? 'Blocked: this page is served over https, and browsers refuse ws:// from a secure page. Open the kit from disk or over plain http.' : 'Could not reach ' + host + '. Check QLC+ is running with --web, that the port is right, and that web access has no password set.';
}
function QLCConnectionProvider({
  children
}) {
  const [host, setHost] = React.useState(() => {
    try {
      return localStorage.getItem('qlc.host') || '';
    } catch (e) {
      return '';
    }
  });
  const [status, setStatus] = React.useState('offline');
  const [log, setLog] = React.useState([]);
  const clientRef = React.useRef(null);
  const didOpen = React.useRef(false);
  const loaded = React.useRef(false);
  const listeners = React.useRef(new Set());
  const push = (level, text) => setLog(l => l.concat([{
    level,
    text
  }]).slice(-40));
  const disconnect = React.useCallback(() => {
    didOpen.current = true;
    if (clientRef.current) clientRef.current.disconnect();
    clientRef.current = null;
    setStatus('offline');
    push('info', 'Disconnected');
  }, []);
  const connect = React.useCallback(h => {
    const target = (h || '').trim();
    if (!target) return;
    if (!window.QLCPlusAPI) {
      push('error', 'qlcplus-api.js not loaded');
      return;
    }
    if (clientRef.current) clientRef.current.disconnect();
    try {
      localStorage.setItem('qlc.host', target);
    } catch (e) {}
    setHost(target);
    setStatus('connecting');
    didOpen.current = false;
    loaded.current = false;
    push('info', 'Connecting to ' + target + ':9999');
    const parts = target.split(':');
    const client = new window.QLCPlusAPI(parts[0], {
      port: parts[1] ? Number(parts[1]) : 9999,
      autoReconnect: false
    });
    client.on('open', () => {
      didOpen.current = true;
      setStatus('online');
      push('ok', 'Socket open — waiting for QLC+');
      client.getFunctionsList();
      client.getWidgetsList();
    });
    /* The handshake. A WebSocket upgrade only proves something answered on that port;
       isProjectLoaded proves it is QLC+ and tells us whether a workspace is loaded. */
    client.on('isProjectLoaded', f => {
      if (loaded.current) return;
      loaded.current = true;
      const yes = f.args[0] === 'true';
      push(yes ? 'ok' : 'error', yes ? 'QLC+ ready on ' + target : 'QLC+ reached, but no workspace is loaded — open a .qxw on the desk');
    });
    client.on('close', () => {
      if (didOpen.current) {
        setStatus('offline');
        push('info', 'Connection closed');
      } else {
        setStatus('error');
        push('error', diagnose(target));
      }
    });
    client.on('error', () => {
      setStatus('error');
      push('error', diagnose(target));
    });
    client.on('message', frame => {
      listeners.current.forEach(fn => {
        try {
          fn(frame);
        } catch (e) {}
      });
      if (frame.verb) push('info', frame.verb + ' · ' + frame.args.length + ' fields');
    });
    client.connect();
    clientRef.current = client;
  }, []);
  React.useEffect(() => () => {
    if (clientRef.current) clientRef.current.disconnect();
  }, []);
  const value = React.useMemo(() => ({
    host,
    status,
    log,
    connect,
    disconnect,
    online: status === 'online',
    client: () => clientRef.current,
    /** Subscribe to every inbound frame. Returns an unsubscribe function. */
    subscribe: fn => {
      listeners.current.add(fn);
      return () => listeners.current.delete(fn);
    },
    /** Subscribe to one decoded event ('channels', 'widget', 'grandmaster', a verb…). */
    subscribeTo: (event, fn) => {
      const c = clientRef.current;
      if (!c) return () => {};
      c.on(event, fn);
      return () => {
        const l = c._handlers[event];
        if (l) l.splice(l.indexOf(fn) >>> 0, 1);
      };
    },
    absoluteChannel: (u, a) => (Math.max(1, u | 0) - 1) * 512 + (a | 0),
    setChannel: (ch, v) => {
      if (clientRef.current) clientRef.current.setChannel(ch, v);
    },
    resetChannel: ch => {
      if (clientRef.current) clientRef.current.resetChannel(ch);
    },
    resetUniverse: u => {
      if (clientRef.current) clientRef.current.resetUniverse(u);
    },
    startPolling: (u, a, n) => {
      if (clientRef.current) clientRef.current.startPolling(u, a, n);
    },
    stopPolling: () => {
      if (clientRef.current) clientRef.current.stopPolling();
    },
    setWidget: (id, v) => {
      if (clientRef.current) clientRef.current.setWidget(id, v);
    },
    startFunction: id => {
      if (clientRef.current) clientRef.current.startFunction(id);
    },
    stopFunction: id => {
      if (clientRef.current) clientRef.current.stopFunction(id);
    }
  }), [host, status, log, connect, disconnect]);
  return React.createElement(QLCConnectionContext.Provider, {
    value
  }, children);
}
function useQLC() {
  return React.useContext(QLCConnectionContext) || {
    host: '',
    status: 'offline',
    online: false,
    log: [],
    connect: () => {},
    disconnect: () => {},
    subscribe: () => () => {},
    client: () => null,
    setChannel: () => {},
    setWidget: () => {},
    startFunction: () => {},
    stopFunction: () => {}
  };
}

/**
 * Toolbar affordance: one 38px icon button whose glyph carries the connection state.
 * Clicking opens a popover with the host field — the connect UI must never cost enough
 * width to push Blackout or Stop-all off a narrow toolbar.
 */
function ConnectionBar() {
  const {
    GenericButton,
    IconButton,
    RobotoText,
    CustomTextInput
  } = window.PatchDesignSystem_5432c9;
  const qlc = useQLC();
  const [open, setOpen] = React.useState(false);
  const [draft, setDraft] = React.useState(qlc.host || '192.168.1.50');
  const anchor = React.useRef(null);
  const [box, setBox] = React.useState({
    top: 0,
    right: 0
  });

  /* The panel is position:fixed, measured off the button, rather than absolutely positioned
     inside the toolbar. A toolbar is a 38px band, so ANY ancestor that clips overflow would
     swallow a dropdown anchored within it — fixed escapes that whole class of bug. */
  const place = () => {
    const el = anchor.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    setBox({
      top: Math.round(r.bottom + 2),
      right: Math.round(window.innerWidth - r.right)
    });
  };
  const toggle = () => {
    if (!open) place();
    setOpen(!open);
  };
  React.useEffect(() => {
    if (!open) return;
    const onKey = e => {
      if (e.key === 'Escape') setOpen(false);
    };
    const onDown = e => {
      if (anchor.current && !anchor.current.contains(e.target)) setOpen(false);
    };
    window.addEventListener('keydown', onKey);
    window.addEventListener('resize', place);
    document.addEventListener('mousedown', onDown, true);
    return () => {
      window.removeEventListener('keydown', onKey);
      window.removeEventListener('resize', place);
      document.removeEventListener('mousedown', onDown, true);
    };
  }, [open]);
  const lamp = {
    offline: 'var(--fg-medium)',
    connecting: 'var(--selection)',
    online: 'var(--check-lime)',
    error: 'var(--override-red)'
  }[qlc.status];
  const title = {
    offline: 'Not connected',
    connecting: 'Connecting…',
    online: 'Live on ' + qlc.host,
    error: 'Connection failed'
  }[qlc.status];
  const tip = 'QLC+ Web API — ' + title;
  /* Show the newest log line, not the bare status: the useful part of a failure is the
     diagnostic ("…is https, ws:// is blocked" / "check --web"), and it has nowhere else to go. */
  const detail = (qlc.log.length ? qlc.log[qlc.log.length - 1] : null) || {
    level: 'info',
    text: title
  };
  return /*#__PURE__*/React.createElement("span", {
    ref: anchor,
    style: {
      position: 'relative',
      display: 'inline-flex',
      flex: 'none'
    }
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: window.QLCData.icon('network'),
    tooltip: tip,
    checked: qlc.status === 'online',
    onClick: toggle
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'absolute',
      right: 3,
      bottom: 3,
      width: 9,
      height: 9,
      borderRadius: 5,
      background: lamp,
      border: '1px solid var(--border-color-dark)',
      pointerEvents: 'none'
    }
  }), open ? /*#__PURE__*/React.createElement("span", {
    style: {
      position: 'fixed',
      top: box.top,
      right: box.right,
      zIndex: 400,
      display: 'flex',
      flexDirection: 'column',
      gap: 6,
      padding: 8,
      width: 216,
      background: 'var(--bg-medium)',
      border: 'var(--border-dialog)'
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "QLC+ Web API",
    fontBold: true,
    fontSize: "var(--text-size-small)",
    height: 20
  }), /*#__PURE__*/React.createElement("span", {
    style: {
      height: 26,
      display: 'flex',
      alignItems: 'center',
      background: 'var(--bg-control)',
      border: '1px solid var(--spin-border)',
      borderRadius: 'var(--radius-spin)',
      padding: '0 5px'
    }
  }, /*#__PURE__*/React.createElement(CustomTextInput, {
    text: draft,
    editing: true,
    onTextConfirmed: setDraft,
    autoFocus: true,
    placeholder: "192.168.1.50",
    onKeyDown: e => {
      if (e.key !== 'Enter') return;
      const v = e.target.value.trim();
      setDraft(v);
      qlc.connect(v);
    },
    width: "100%",
    height: 22,
    color: "var(--fg-main)"
  })), WS_BLOCKED && qlc.status !== 'online' ? /*#__PURE__*/React.createElement(RobotoText, {
    label: diagnose(draft),
    fontSize: "var(--text-size-menubar)",
    wrapText: true,
    labelColor: "var(--selection)",
    height: "auto"
  }) : null, /*#__PURE__*/React.createElement(RobotoText, {
    label: detail.text,
    fontSize: "var(--text-size-menubar)",
    wrapText: true,
    labelColor: detail.level === 'error' ? 'var(--override-red)' : detail.level === 'ok' ? 'var(--check-lime)' : 'var(--fg-light)',
    height: "auto"
  }), qlc.status === 'online' || qlc.status === 'connecting' ? /*#__PURE__*/React.createElement(GenericButton, {
    label: "Disconnect",
    width: "100%",
    height: 26,
    fontSize: "var(--text-size-menubar)",
    onClick: () => {
      qlc.disconnect();
      setOpen(false);
    }
  }) : /*#__PURE__*/React.createElement(GenericButton, {
    label: "Connect",
    width: "100%",
    height: 26,
    fontSize: "var(--text-size-menubar)",
    bgColor: "var(--keypad-enter)",
    hoverColor: "var(--keypad-enter-hover)",
    pressedColor: "var(--keypad-enter-pressed)",
    onClick: () => qlc.connect(draft)
  }), "        ") : null);
}
Object.assign(window, {
  QLCConnectionContext,
  QLCConnectionProvider,
  useQLC,
  ConnectionBar
});
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/Connection.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/FixturesFunctions.jsx
try { (() => {
const {
  ViewToolbar,
  ToolbarSpacer,
  IconButton,
  GenericButton,
  RobotoText,
  TreeNode,
  SidePanel,
  SectionBox,
  CustomSpinBox,
  CustomComboBox,
  CustomCheckBox,
  CustomTextInput,
  QLCPlusFader,
  IconTextEntry,
  ShortcutHint,
  CustomPopupDialog
} = window.PatchDesignSystem_5432c9;
function TreeBranch({
  node,
  selected,
  onSelect,
  expanded,
  onToggle,
  depth = 0
}) {
  const isOpen = expanded.indexOf(node.id) !== -1;
  return /*#__PURE__*/React.createElement(TreeNode, {
    textLabel: node.name,
    itemIcon: node.icon,
    depth: depth,
    hasChildren: !!node.children,
    isExpanded: isOpen,
    onToggle: () => onToggle(node.id),
    isSelected: selected === node.id,
    onSelect: () => onSelect(node.id, node)
  }, isOpen && node.children ? node.children.map(c => /*#__PURE__*/React.createElement(TreeBranch, {
    key: c.id,
    node: c,
    selected: selected,
    onSelect: onSelect,
    expanded: expanded,
    onToggle: onToggle,
    depth: depth + 1
  })) : null);
}
function FixturesFunctions() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [selected, setSelected] = React.useState('f1');
  const [detail, setDetail] = React.useState(D.fixtures[0].children[0]);
  const [expanded, setExpanded] = React.useState(['g-front', 'g-back', 'g-cyc', 'fn-scenes', 'fn-chasers', 'fn-fx']);
  const [panel, setPanel] = React.useState(true);
  const [dlg, setDlg] = React.useState(false);
  const [dimmer, setDimmer] = React.useState(255);
  const [running, setRunning] = React.useState([]);
  const isFunction = !!detail.type;
  const isRunning = running.indexOf(detail.id) !== -1;
  const toggleRun = () => {
    if (isRunning) {
      qlc.stopFunction(detail.id);
      setRunning(p => p.filter(x => x !== detail.id));
    } else {
      qlc.startFunction(detail.id);
      setRunning(p => p.concat([detail.id]));
    }
  };
  const toggle = id => setExpanded(p => p.indexOf(id) === -1 ? p.concat([id]) : p.filter(x => x !== id));
  const pick = (id, node) => {
    setSelected(id);
    if (!node.children) setDetail(node);
  };
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement(ViewToolbar, {
    variant: "sub"
  }, /*#__PURE__*/React.createElement(ShortcutHint, {
    keys: "A",
    placement: "corner"
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('add'),
    size: 26,
    tooltip: "Add fixture",
    onClick: () => setDlg(true)
  })), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('group'),
    size: 26,
    tooltip: "Add fixture group"
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('remap'),
    size: 26,
    tooltip: "Remap addresses"
  }), /*#__PURE__*/React.createElement(IconButton, {
    faSource: "fa_trash_can",
    size: 26,
    tooltip: "Delete selected"
  }), /*#__PURE__*/React.createElement(ToolbarSpacer, null), /*#__PURE__*/React.createElement(RobotoText, {
    label: qlc.online ? 'Live — connected to ' + qlc.host : '8 fixtures · 126 channels · 2 universes',
    fontSize: 14,
    labelColor: qlc.online ? 'var(--check-lime)' : 'var(--fg-light)'
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('search'),
    size: 26,
    tooltip: "Search"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minHeight: 0,
      display: 'flex'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      width: 260,
      minWidth: 260,
      background: 'var(--bg-stronger)',
      borderRight: 'var(--border-dark)',
      overflow: 'auto'
    }
  }, D.fixtures.map(n => /*#__PURE__*/React.createElement(TreeBranch, {
    key: n.id,
    node: n,
    selected: selected,
    onSelect: pick,
    expanded: expanded,
    onToggle: toggle
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      height: 1,
      background: 'var(--border-color-dark)',
      margin: '4px 0'
    }
  }), D.functions.map(n => /*#__PURE__*/React.createElement(TreeBranch, {
    key: n.id,
    node: n,
    selected: selected,
    onSelect: pick,
    expanded: expanded,
    onToggle: toggle
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0,
      display: 'flex',
      flexDirection: 'column',
      background: 'var(--bg-medium)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8,
      height: 38,
      padding: '0 10px',
      background: 'var(--section-header)',
      borderBottom: '2px solid var(--section-header-div)'
    }
  }, /*#__PURE__*/React.createElement("img", {
    src: detail.icon,
    alt: "",
    style: {
      width: 24,
      height: 24
    }
  }), /*#__PURE__*/React.createElement(CustomTextInput, {
    text: detail.name,
    allowDoubleClick: true,
    width: 240
  }), /*#__PURE__*/React.createElement(ToolbarSpacer, null), isFunction ? /*#__PURE__*/React.createElement(IconButton, {
    faSource: isRunning ? 'fa_pause' : 'fa_play',
    size: 26,
    faColor: isRunning ? 'var(--override-red)' : 'var(--check-lime)',
    checked: isRunning,
    onClick: toggleRun,
    tooltip: isRunning ? 'Stop function' : 'Start function'
  }) : null, /*#__PURE__*/React.createElement(RobotoText, {
    label: detail.mode || detail.type || '',
    fontSize: 14,
    labelColor: "var(--fg-light)"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minHeight: 0,
      overflow: 'auto',
      padding: 12,
      display: 'grid',
      gridTemplateColumns: '1fr 1fr',
      gap: 12,
      alignContent: 'start'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Addressing",
    fontBold: true,
    fontSize: 14
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Universe",
    fontSize: 14,
    style: {
      width: 90
    }
  }), /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 170,
    currValue: 1,
    model: D.universes.map(u => ({
      mLabel: u.name,
      mValue: u.id
    }))
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Address",
    fontSize: 14,
    style: {
      width: 90
    }
  }), /*#__PURE__*/React.createElement(CustomSpinBox, {
    value: Number((detail.address || '1.001').split('.')[1]),
    from: 1,
    to: 512,
    width: 110
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Channels",
    fontSize: 14,
    style: {
      width: 90
    }
  }), /*#__PURE__*/React.createElement(CustomSpinBox, {
    value: detail.channels || 8,
    from: 1,
    to: 512,
    width: 110
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(CustomCheckBox, {
    checked: true
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Add to Front Truss group",
    fontSize: 14
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Capabilities",
    fontBold: true,
    fontSize: 14
  }), ['dimmer', 'color', 'position', 'gobo', 'beam'].map(k => /*#__PURE__*/React.createElement(IconTextEntry, {
    key: k,
    iSrc: D.icon(k),
    tLabel: k[0].toUpperCase() + k.slice(1),
    tFontSize: 14,
    height: 26
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      gridColumn: '1 / -1',
      display: 'flex',
      gap: 16,
      alignItems: 'flex-start',
      paddingTop: 4,
      borderTop: 'var(--border-dark)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      gap: 4
    }
  }, /*#__PURE__*/React.createElement(QLCPlusFader, {
    value: dimmer,
    onMoved: setDimmer,
    height: 140
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: 'Dimmer ' + dimmer,
    fontSize: 14,
    labelColor: "var(--fg-light)"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement(GenericButton, {
    label: "Create scene from selection",
    iconSource: D.icon('scene'),
    width: 250
  }), /*#__PURE__*/React.createElement(GenericButton, {
    label: "Create chaser",
    iconSource: D.icon('chaser'),
    width: 250
  }), /*#__PURE__*/React.createElement(GenericButton, {
    label: "Create RGB matrix",
    iconSource: D.icon('rgbmatrix'),
    width: 250
  }))))), /*#__PURE__*/React.createElement(SidePanel, {
    isOpen: panel,
    alignment: "right",
    rail: /*#__PURE__*/React.createElement("div", {
      style: {
        display: 'flex',
        flexDirection: 'column',
        gap: 4,
        padding: 4
      }
    }, /*#__PURE__*/React.createElement(IconButton, {
      imgSource: D.icon('palette'),
      checked: panel,
      onClick: () => setPanel(!panel),
      tooltip: "Palettes"
    }), /*#__PURE__*/React.createElement(IconButton, {
      imgSource: D.icon('fixture-editor'),
      tooltip: "Fixture editor"
    }), /*#__PURE__*/React.createElement(IconButton, {
      imgSource: D.icon('uniview'),
      tooltip: "Universe view"
    }))
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 0
    }
  }, /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Colour",
    isExpanded: true
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(4,1fr)',
      gap: 4,
      padding: 6
    }
  }, ['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'amber', 'white', 'uv', 'lime', 'indigo', 'colorwheel'].map(c => /*#__PURE__*/React.createElement("img", {
    key: c,
    src: D.icon(c),
    alt: c,
    title: c,
    style: {
      width: '100%',
      aspectRatio: 1,
      cursor: 'pointer'
    }
  })))), /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Position"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 6
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "No position palettes",
    fontSize: 14,
    labelColor: "var(--fg-medium)"
  }))), /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Gobo"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 6
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "No gobo palettes",
    fontSize: 14,
    labelColor: "var(--fg-medium)"
  })))))), /*#__PURE__*/React.createElement(CustomPopupDialog, {
    open: dlg,
    title: "Add fixture",
    width: 360,
    standardButtons: ['Cancel', 'OK'],
    onClicked: () => setDlg(false),
    onClose: () => setDlg(false)
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 10
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Manufacturer",
    fontSize: 14,
    style: {
      width: 100
    }
  }), /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 200,
    currValue: 0,
    model: ['Robe', 'Martin', 'Chauvet', 'Generic']
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Model",
    fontSize: 14,
    style: {
      width: 100
    }
  }), /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 200,
    currValue: 0,
    model: ['Pointe', 'Spikie', 'MegaPointe']
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 8
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Quantity",
    fontSize: 14,
    style: {
      width: 100
    }
  }), /*#__PURE__*/React.createElement(CustomSpinBox, {
    value: 4,
    from: 1,
    to: 64,
    width: 90
  })))));
}
Object.assign(window, {
  FixturesFunctions,
  TreeBranch
});
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/FixturesFunctions.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/InputOutput.jsx
try { (() => {
const {
  ViewToolbar,
  ToolbarSpacer,
  IconButton,
  RobotoText,
  CustomComboBox,
  CustomCheckBox,
  GenericButton,
  IconTextEntry,
  SectionBox
} = window.PatchDesignSystem_5432c9;
function InputOutput() {
  const D = window.QLCData;
  const [rows, setRows] = React.useState(D.universes);
  const [sel, setSel] = React.useState(1);
  const set = (id, patch) => setRows(p => p.map(u => u.id === id ? Object.assign({}, u, patch) : u));
  const head = {
    display: 'flex',
    alignItems: 'center',
    height: 30,
    background: 'var(--section-header)',
    borderBottom: '2px solid var(--section-header-div)',
    padding: '0 6px'
  };
  const cell = w => ({
    width: w,
    minWidth: w,
    padding: '0 6px'
  });
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement(ViewToolbar, {
    variant: "sub"
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('add'),
    size: 26,
    tooltip: "Add universe"
  }), /*#__PURE__*/React.createElement(IconButton, {
    faSource: "fa_trash_can",
    size: 26,
    tooltip: "Remove universe"
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('network'),
    size: 26,
    tooltip: "Network settings"
  }), /*#__PURE__*/React.createElement(ToolbarSpacer, null), /*#__PURE__*/React.createElement(RobotoText, {
    label: "4 universes \xB7 3 plugins active",
    fontSize: 14,
    labelColor: "var(--fg-light)"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minHeight: 0,
      display: 'flex'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0,
      overflow: 'auto'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: head
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Universe",
    fontSize: 14,
    fontBold: true,
    style: cell(150)
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Input",
    fontSize: 14,
    fontBold: true,
    style: cell(190)
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Output",
    fontSize: 14,
    fontBold: true,
    style: cell(210)
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Feedback",
    fontSize: 14,
    fontBold: true,
    style: cell(170)
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Passthrough",
    fontSize: 14,
    fontBold: true,
    style: cell(100)
  })), rows.map((u, i) => /*#__PURE__*/React.createElement("div", {
    key: u.id,
    onClick: () => setSel(u.id),
    style: {
      display: 'flex',
      alignItems: 'center',
      height: 38,
      cursor: 'pointer',
      padding: '0 6px',
      background: sel === u.id ? 'var(--highlight)' : i % 2 ? 'var(--bg-strong)' : 'var(--bg-stronger)',
      borderBottom: 'var(--border-dark)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: cell(150)
  }, /*#__PURE__*/React.createElement(IconTextEntry, {
    iSrc: D.icon('uniview'),
    tLabel: u.name,
    tFontSize: 14,
    height: 26
  })), /*#__PURE__*/React.createElement("div", {
    style: cell(190)
  }, /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 175,
    height: 26,
    currValue: u.input,
    onValueChanged: v => set(u.id, {
      input: v
    }),
    model: [{
      mLabel: 'None',
      mValue: 'None'
    }, {
      mLabel: 'MIDI Controller',
      mValue: 'MIDI Controller'
    }, {
      mLabel: 'OSC 9000',
      mValue: 'OSC 9000'
    }]
  })), /*#__PURE__*/React.createElement("div", {
    style: cell(210)
  }, /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 195,
    height: 26,
    currValue: u.output,
    onValueChanged: v => set(u.id, {
      output: v
    }),
    model: [{
      mLabel: 'None',
      mValue: 'None'
    }, {
      mLabel: 'ArtNet 2.0.0.1',
      mValue: 'ArtNet 2.0.0.1'
    }, {
      mLabel: 'E1.31 239.255.0.2',
      mValue: 'E1.31 239.255.0.2'
    }, {
      mLabel: 'DMX USB Pro',
      mValue: 'DMX USB Pro'
    }]
  })), /*#__PURE__*/React.createElement("div", {
    style: cell(170)
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: u.feedback,
    fontSize: 14,
    labelColor: "var(--fg-light)",
    height: 26
  })), /*#__PURE__*/React.createElement("div", {
    style: Object.assign({}, cell(100), {
      display: 'flex',
      justifyContent: 'center'
    })
  }, /*#__PURE__*/React.createElement(CustomCheckBox, {
    checked: u.passthrough,
    onToggled: v => set(u.id, {
      passthrough: v
    })
  }))))), /*#__PURE__*/React.createElement("div", {
    style: {
      width: 240,
      minWidth: 240,
      borderLeft: 'var(--border-dark)',
      background: 'var(--bg-strong)',
      overflow: 'auto'
    }
  }, /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Plugins",
    isExpanded: true
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 6,
      display: 'flex',
      flexDirection: 'column',
      gap: 2
    }
  }, [['artnetplugin', 'ArtNet'], ['e131plugin', 'E1.31'], ['dmxusbplugin', 'DMX USB'], ['midiplugin', 'MIDI'], ['oscplugin', 'OSC'], ['hidplugin', 'HID'], ['olaplugin', 'OLA']].map(([ic, n]) => /*#__PURE__*/React.createElement(IconTextEntry, {
    key: n,
    iSrc: D.icon(ic),
    tLabel: n,
    tFontSize: 14,
    height: 26
  })))), /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Audio"
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      padding: 6
    }
  }, /*#__PURE__*/React.createElement(IconTextEntry, {
    iSrc: D.icon('audiocard'),
    tLabel: "Default output",
    tFontSize: 14,
    height: 26
  }))))));
}
Object.assign(window, {
  InputOutput
});
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/InputOutput.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/SimpleDesk.jsx
try { (() => {
const {
  ViewToolbar,
  ToolbarSpacer,
  IconButton,
  RobotoText,
  ChannelStrip,
  KeyPad,
  CustomComboBox,
  DMXPercentageButton,
  ShortcutHint,
  GenericButton
} = window.PatchDesignSystem_5432c9;
function SimpleDesk() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [values, setValues] = React.useState(D.channels.map(c => c.value));
  const [overrides, setOverrides] = React.useState([5]);
  const [dmx, setDmx] = React.useState(true);
  const [cmd, setCmd] = React.useState('');
  const [universe, setUniverse] = React.useState(1);
  const count = D.channels.length;
  const abs = i => (universe - 1) * 512 + i + 1;
  /* QLC+ never pushes channel values — it only answers getChannelsValues. So a live view
     has to poll; the reference client (webaccess/res/simpledesk-v5.js) uses 700ms. */
  React.useEffect(() => {
    if (!qlc.online) return;
    const off = qlc.subscribeTo('channels', rows => {
      const base = (universe - 1) * 512;
      setValues(prev => {
        const next = prev.slice();
        rows.forEach(r => {
          const i = r.channel - base - 1;
          if (i >= 0 && i < next.length) next[i] = r.value;
        });
        return next;
      });
      setOverrides(rows.filter(r => r.overriding).map(r => r.channel - base - 1).filter(i => i >= 0 && i < count));
    });
    qlc.startPolling(universe, 1, count);
    return () => {
      off();
      qlc.stopPolling();
    };
  }, [qlc.online, universe, count]);
  const setChannel = (i, v) => {
    setValues(p => p.map((x, j) => j === i ? v : x));
    setOverrides(p => p.indexOf(i) === -1 ? p.concat([i]) : p);
    qlc.setChannel(abs(i), v);
  };
  /* Reset releases the manual override back to whatever the playback is doing — which is
     not the same as writing 0, so it needs sdResetChannel rather than CH. */
  const reset = i => {
    setValues(p => p.map((x, j) => j === i ? 0 : x));
    setOverrides(p => p.filter(x => x !== i));
    qlc.resetChannel(abs(i));
  };
  const resetAll = () => {
    setValues(D.channels.map(() => 0));
    setOverrides([]);
    qlc.resetUniverse(universe);
  };
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement(ViewToolbar, {
    variant: "sub"
  }, /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 150,
    height: 26,
    currValue: universe,
    onValueChanged: setUniverse,
    model: D.universes.map(u => ({
      mLabel: u.name,
      mValue: u.id
    }))
  }), /*#__PURE__*/React.createElement(DMXPercentageButton, {
    dmxMode: dmx,
    onClick: () => setDmx(!dmx),
    height: 26
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('dmxdump'),
    size: 26,
    tooltip: "Dump DMX values to a scene"
  }), /*#__PURE__*/React.createElement(ShortcutHint, {
    keys: "Ctrl R",
    placement: "corner"
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('uncheck'),
    size: 26,
    tooltip: "Reset all channels",
    onClick: resetAll
  })), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('network'),
    size: 26,
    tooltip: qlc.online ? 'Refresh values from the desk' : 'Not connected',
    disabled: !qlc.online,
    onClick: () => {
      const c = qlc.client();
      if (c) c.getChannelsValues(universe, 1, count);
    }
  }), /*#__PURE__*/React.createElement(ToolbarSpacer, null), /*#__PURE__*/React.createElement(RobotoText, {
    label: qlc.online ? 'Live — universe ' + universe + ', polling 700ms' : 'Offline — local preview',
    fontSize: 14,
    labelColor: qlc.online ? 'var(--check-lime)' : 'var(--fg-medium)'
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: overrides.length + ' overridden',
    fontSize: 14,
    labelColor: overrides.length ? 'var(--override-red)' : 'var(--fg-light)'
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minHeight: 0,
      display: 'flex'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0,
      overflowX: 'auto',
      display: 'flex',
      alignItems: 'stretch',
      background: 'var(--bg-stronger)'
    }
  }, D.channels.map((c, i) => /*#__PURE__*/React.createElement(ChannelStrip, {
    key: c.address,
    address: c.address,
    value: values[i],
    channelIcon: c.channelIcon,
    channelName: c.channelName,
    display: c.display,
    isOverride: overrides.indexOf(i) !== -1,
    dmxValues: dmx,
    onValueChanged: v => setChannel(i, v),
    onReset: () => reset(i),
    style: {
      height: '100%'
    }
  }))), /*#__PURE__*/React.createElement("div", {
    style: {
      width: 250,
      minWidth: 250,
      borderLeft: 'var(--border-dark)',
      background: 'var(--bg-medium)',
      padding: 6,
      display: 'flex',
      flexDirection: 'column',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement(KeyPad, {
    commandString: cmd,
    onCommandChange: setCmd,
    onExecuteCommand: () => setCmd(''),
    showTapButton: true
  }), /*#__PURE__*/React.createElement(GenericButton, {
    label: "Dump to new scene",
    iconSource: D.icon('scene'),
    width: "100%"
  }))));
}
Object.assign(window, {
  SimpleDesk
});
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/SimpleDesk.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/VirtualConsole.jsx
try { (() => {
const {
  ViewToolbar,
  ToolbarSpacer,
  IconButton,
  RobotoText,
  QLCPlusFader,
  GenericButton,
  ShortcutHint,
  CustomSpinBox,
  SectionBox,
  SidePanel,
  CustomComboBox,
  CustomCheckBox
} = window.PatchDesignSystem_5432c9;
function VCSlider({
  w,
  onChange
}) {
  return /*#__PURE__*/React.createElement("div", {
    style: {
      width: 74,
      background: 'var(--bg-strong)',
      border: 'var(--border-control)',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      gap: 4,
      padding: '6px 0'
    }
  }, /*#__PURE__*/React.createElement(QLCPlusFader, {
    value: w.value,
    onMoved: onChange,
    height: 180
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: String(w.value),
    fontSize: 14,
    labelColor: "var(--fg-light)",
    height: 18,
    textHAlign: "center",
    style: {
      width: '100%'
    }
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: w.label,
    fontSize: 14,
    height: 20,
    textHAlign: "center",
    style: {
      width: '100%'
    }
  }));
}
function VCButton({
  w,
  onToggle
}) {
  return /*#__PURE__*/React.createElement("button", {
    type: "button",
    onClick: onToggle,
    style: {
      width: 108,
      height: 60,
      cursor: 'pointer',
      padding: 4,
      background: w.on ? 'var(--highlight)' : 'var(--bg-control)',
      border: 'var(--border-control)',
      color: 'var(--fg-main)',
      font: '400 var(--text-size-small)/1.2 var(--font-roboto)'
    }
  }, w.label);
}
function VirtualConsole() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [widgets, setWidgets] = React.useState(D.vcWidgets);
  const [edit, setEdit] = React.useState(false);
  const [panel, setPanel] = React.useState(false);

  /* getWidgetsList answers with (id, name, type) triples. Value changes arrive as
     "<id>|SLIDER|<value>|<display>" or "<id>|BUTTON|<state>", which the API client
     decodes into a 'widget' event (see webaccess/res/websocket.js). */
  React.useEffect(() => {
    if (!qlc.online) return;
    const offList = qlc.subscribeTo('getWidgetsList', frame => {
      const a = frame.args,
        next = [];
      for (let i = 0; i + 2 < a.length; i += 3) {
        const type = String(a[i + 2]).toLowerCase();
        next.push({
          id: a[i],
          label: a[i + 1],
          kind: type.indexOf('slider') !== -1 ? 'slider' : 'button',
          value: 0,
          on: false
        });
      }
      if (next.length) setWidgets(next);
    });
    const offVal = qlc.subscribeTo('widget', w => {
      if (w.value == null || isNaN(w.value)) return;
      setWidgets(p => p.map(x => String(x.id) === String(w.id) ? Object.assign({}, x, x.kind === 'slider' ? {
        value: w.value
      } : {
        on: w.value > 0
      }) : x));
    });
    return () => {
      offList();
      offVal();
    };
  }, [qlc.online]);
  const set = (id, patch) => {
    setWidgets(p => p.map(w => w.id === id ? Object.assign({}, w, patch) : w));
    if (patch.value != null) qlc.setWidget(id, patch.value);
    if (patch.on != null) qlc.setWidget(id, patch.on ? 255 : 0);
  };
  return /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      minHeight: 0
    }
  }, /*#__PURE__*/React.createElement(ViewToolbar, {
    variant: "sub"
  }, /*#__PURE__*/React.createElement(ShortcutHint, {
    keys: "Ctrl L",
    placement: "corner"
  }, /*#__PURE__*/React.createElement(IconButton, {
    imgSource: edit ? D.icon('unlock') : D.icon('lock'),
    size: 26,
    checked: edit,
    onClick: () => setEdit(!edit),
    tooltip: edit ? 'Lock editing' : 'Unlock editing'
  })), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('frame'),
    size: 26,
    tooltip: "Add frame",
    disabled: !edit
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('button'),
    size: 26,
    tooltip: "Add button",
    disabled: !edit
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('slider'),
    size: 26,
    tooltip: "Add slider",
    disabled: !edit
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('buttonmatrix'),
    size: 26,
    tooltip: "Add button matrix",
    disabled: !edit
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('xypad'),
    size: 26,
    tooltip: "Add XY pad",
    disabled: !edit
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('network'),
    size: 26,
    disabled: !qlc.online,
    tooltip: qlc.online ? 'Reload widgets from the desk' : 'Not connected',
    onClick: () => {
      const c = qlc.client();
      if (c) c.getWidgetsList();
    }
  }), /*#__PURE__*/React.createElement(ToolbarSpacer, null), /*#__PURE__*/React.createElement(RobotoText, {
    label: qlc.online ? 'Live — ' + widgets.length + ' widgets' : 'Offline — local preview',
    fontSize: 14,
    labelColor: qlc.online ? 'var(--check-lime)' : 'var(--fg-medium)'
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('blackout'),
    size: 26,
    tooltip: "Blackout"
  }), /*#__PURE__*/React.createElement(IconButton, {
    imgSource: D.icon('stopall'),
    size: 26,
    tooltip: "Stop all functions"
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minHeight: 0,
      display: 'flex'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      flex: 1,
      minWidth: 0,
      overflow: 'auto',
      padding: 12,
      background: 'var(--bg-medium)'
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'inline-flex',
      flexDirection: 'column',
      gap: 10,
      padding: 10,
      border: edit ? '2px dashed var(--bg-light)' : 'var(--border-control)',
      background: 'var(--bg-stronger)'
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Main frame",
    fontSize: 14,
    labelColor: "var(--fg-medium)",
    height: 20
  }), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      gap: 8,
      alignItems: 'flex-start'
    }
  }, widgets.filter(w => w.kind === 'slider').map(w => /*#__PURE__*/React.createElement(VCSlider, {
    key: w.id,
    w: w,
    onChange: v => set(w.id, {
      value: v
    })
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'grid',
      gridTemplateColumns: 'repeat(2,auto)',
      gap: 6,
      alignContent: 'start'
    }
  }, widgets.filter(w => w.kind === 'button').map(w => /*#__PURE__*/React.createElement(VCButton, {
    key: w.id,
    w: w,
    onToggle: () => set(w.id, {
      on: !w.on
    })
  })))))), /*#__PURE__*/React.createElement(SidePanel, {
    isOpen: panel,
    alignment: "right",
    rail: /*#__PURE__*/React.createElement("div", {
      style: {
        display: 'flex',
        flexDirection: 'column',
        gap: 4,
        padding: 4
      }
    }, /*#__PURE__*/React.createElement(IconButton, {
      imgSource: D.icon('configure'),
      checked: panel,
      onClick: () => setPanel(!panel),
      tooltip: "Widget properties"
    }), /*#__PURE__*/React.createElement(IconButton, {
      imgSource: D.icon('keybinding'),
      tooltip: "Key bindings"
    }), /*#__PURE__*/React.createElement(IconButton, {
      imgSource: D.icon('inputoutput'),
      tooltip: "External input"
    }))
  }, /*#__PURE__*/React.createElement("div", null, /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Slider",
    isExpanded: true
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      flexDirection: 'column',
      gap: 8,
      padding: 8
    }
  }, /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Mode",
    fontSize: 14,
    style: {
      width: 62
    }
  }), /*#__PURE__*/React.createElement(CustomComboBox, {
    width: 110,
    currValue: 0,
    model: ['Level', 'Playback', 'Submaster']
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "Low",
    fontSize: 14,
    style: {
      width: 62
    }
  }), /*#__PURE__*/React.createElement(CustomSpinBox, {
    value: 0,
    width: 86
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement(RobotoText, {
    label: "High",
    fontSize: 14,
    style: {
      width: 62
    }
  }), /*#__PURE__*/React.createElement(CustomSpinBox, {
    value: 255,
    width: 86
  })), /*#__PURE__*/React.createElement("div", {
    style: {
      display: 'flex',
      alignItems: 'center',
      gap: 6
    }
  }, /*#__PURE__*/React.createElement(CustomCheckBox, {
    checked: true
  }), /*#__PURE__*/React.createElement(RobotoText, {
    label: "Invert",
    fontSize: 14
  })))), /*#__PURE__*/React.createElement(SectionBox, {
    sectionLabel: "Input"
  })))));
}
Object.assign(window, {
  VirtualConsole,
  VCSlider,
  VCButton
});
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/VirtualConsole.jsx", error: String((e && e.message) || e) }); }

// ui_kits/qlcplus/data.js
try { (() => {
window.QLCData = function () {
  const I = '../../assets/icons/';
  /* Icon paths live in JS strings, which a static bundler cannot discover. Every icon is
     therefore declared as an <meta name="ext-resource-dependency"> in index.html; at runtime
     in a bundled page window.__resources[name] holds a blob URL for the inlined bytes. */
  const icon = name => window.__resources && window.__resources[name] || I + name + '.svg';
  const fixtures = [{
    id: 'g-front',
    name: 'Front Truss',
    icon: icon('group'),
    children: [{
      id: 'f1',
      name: 'Robe Pointe 1',
      icon: icon('movinghead'),
      address: '1.001',
      channels: 24,
      mode: 'Mode 1 (24ch)'
    }, {
      id: 'f2',
      name: 'Robe Pointe 2',
      icon: icon('movinghead'),
      address: '1.025',
      channels: 24,
      mode: 'Mode 1 (24ch)'
    }, {
      id: 'f3',
      name: 'Robe Pointe 3',
      icon: icon('movinghead'),
      address: '1.049',
      channels: 24,
      mode: 'Mode 1 (24ch)'
    }]
  }, {
    id: 'g-back',
    name: 'Back Truss',
    icon: icon('group'),
    children: [{
      id: 'f4',
      name: 'Mac Aura 1',
      icon: icon('movinghead'),
      address: '1.073',
      channels: 15,
      mode: 'Basic (15ch)'
    }, {
      id: 'f5',
      name: 'Mac Aura 2',
      icon: icon('movinghead'),
      address: '1.088',
      channels: 15,
      mode: 'Basic (15ch)'
    }]
  }, {
    id: 'g-cyc',
    name: 'Cyc Wash',
    icon: icon('group'),
    children: [{
      id: 'f6',
      name: 'LED Bar 1',
      icon: icon('fixture'),
      address: '2.001',
      channels: 8,
      mode: 'RGBW (8ch)'
    }, {
      id: 'f7',
      name: 'LED Bar 2',
      icon: icon('fixture'),
      address: '2.009',
      channels: 8,
      mode: 'RGBW (8ch)'
    }, {
      id: 'f8',
      name: 'LED Bar 3',
      icon: icon('fixture'),
      address: '2.017',
      channels: 8,
      mode: 'RGBW (8ch)'
    }]
  }];
  const functions = [{
    id: 'fn-scenes',
    name: 'Scenes',
    icon: icon('folder'),
    children: [{
      id: 'sc1',
      name: 'Warm Front',
      icon: icon('scene'),
      type: 'Scene'
    }, {
      id: 'sc2',
      name: 'Cold Back',
      icon: icon('scene'),
      type: 'Scene'
    }, {
      id: 'sc3',
      name: 'Blackout',
      icon: icon('scene'),
      type: 'Scene'
    }]
  }, {
    id: 'fn-chasers',
    name: 'Chasers',
    icon: icon('folder'),
    children: [{
      id: 'ch1',
      name: 'Chase 1',
      icon: icon('chaser'),
      type: 'Chaser'
    }, {
      id: 'ch2',
      name: 'Strobe Hits',
      icon: icon('chaser'),
      type: 'Chaser'
    }]
  }, {
    id: 'fn-fx',
    name: 'Effects',
    icon: icon('folder'),
    children: [{
      id: 'rm1',
      name: 'Rainbow',
      icon: icon('rgbmatrix'),
      type: 'RGB Matrix'
    }, {
      id: 'ef1',
      name: 'Circle EFX',
      icon: icon('efx'),
      type: 'EFX'
    }]
  }];
  const channels = [['Dimmer', 'dimmer', 255], ['Red', 'red', 128], ['Green', 'green', 0], ['Blue', 'blue', 64], ['White', 'white', 0], ['Pan', 'pan', 200], ['Tilt', 'tilt', 90], ['Gobo', 'gobo', 0], ['Colour', 'colorwheel', 32], ['Shutter', 'shutter', 255], ['Prism', 'prism', 0], ['Zoom', 'beam', 110], ['Focus', 'beam', 40], ['Speed', 'speed', 0], ['Strobe', 'strobe', 0], ['Control', 'other', 0]].map((c, i) => ({
    address: i + 1,
    value: c[2],
    channelName: c[0],
    channelIcon: I + c[1] + '.svg',
    display: i < 8 ? 'odd' : 'even'
  }));
  const vcWidgets = [{
    id: 'w1',
    kind: 'slider',
    label: 'Master',
    value: 255
  }, {
    id: 'w2',
    kind: 'slider',
    label: 'Front',
    value: 190
  }, {
    id: 'w3',
    kind: 'slider',
    label: 'Back',
    value: 120
  }, {
    id: 'w4',
    kind: 'slider',
    label: 'Cyc',
    value: 210
  }, {
    id: 'w5',
    kind: 'button',
    label: 'Warm Front',
    on: true
  }, {
    id: 'w6',
    kind: 'button',
    label: 'Cold Back',
    on: false
  }, {
    id: 'w7',
    kind: 'button',
    label: 'Chase 1',
    on: false
  }, {
    id: 'w8',
    kind: 'button',
    label: 'Blackout',
    on: false
  }];
  const universes = [{
    id: 1,
    name: 'Universe 1',
    input: 'None',
    output: 'ArtNet 2.0.0.1',
    feedback: 'None',
    passthrough: false
  }, {
    id: 2,
    name: 'Universe 2',
    input: 'None',
    output: 'E1.31 239.255.0.2',
    feedback: 'None',
    passthrough: false
  }, {
    id: 3,
    name: 'Universe 3',
    input: 'MIDI Controller',
    output: 'DMX USB Pro',
    feedback: 'MIDI Controller',
    passthrough: false
  }, {
    id: 4,
    name: 'Universe 4',
    input: 'None',
    output: 'None',
    feedback: 'None',
    passthrough: true
  }];
  const shortcutGroups = [{
    title: 'Contexts',
    binds: [['Ctrl 1', 'Fixtures & Functions'], ['Ctrl 2', 'Virtual Console'], ['Ctrl 3', 'Simple Desk'], ['Ctrl 4', 'Show Manager'], ['Ctrl 5', 'Input / Output']]
  }, {
    title: 'Workspace',
    binds: [['Ctrl N', 'New workspace'], ['Ctrl O', 'Open'], ['Ctrl S', 'Save'], ['Ctrl Z', 'Undo'], ['Ctrl Y', 'Redo']]
  }, {
    title: 'Output',
    binds: [['Ctrl B', 'Blackout'], ['Ctrl .', 'Stop all functions'], ['Space', 'Tap tempo'], ['Ctrl L', 'Lock editing']]
  }];
  return {
    I,
    icon,
    fixtures,
    functions,
    channels,
    vcWidgets,
    universes,
    shortcutGroups
  };
}();
})(); } catch (e) { __ds_ns.__errors.push({ path: "ui_kits/qlcplus/data.js", error: String((e && e.message) || e) }); }

__ds_ns.ContextMenuEntry = __ds_scope.ContextMenuEntry;

__ds_ns.DMXPercentageButton = __ds_scope.DMXPercentageButton;

__ds_ns.GenericButton = __ds_scope.GenericButton;

__ds_ns.IconButton = __ds_scope.IconButton;

__ds_ns.MenuBarEntry = __ds_scope.MenuBarEntry;

__ds_ns.ChannelStrip = __ds_scope.ChannelStrip;

__ds_ns.KeyPad = __ds_scope.KeyPad;

__ds_ns.TreeNode = __ds_scope.TreeNode;

__ds_ns.CustomPopupDialog = __ds_scope.CustomPopupDialog;

__ds_ns.SectionBox = __ds_scope.SectionBox;

__ds_ns.SidePanel = __ds_scope.SidePanel;

__ds_ns.ViewToolbar = __ds_scope.ViewToolbar;

__ds_ns.ToolbarSpacer = __ds_scope.ToolbarSpacer;

__ds_ns.CustomCheckBox = __ds_scope.CustomCheckBox;

__ds_ns.CustomComboBox = __ds_scope.CustomComboBox;

__ds_ns.CustomScrollBar = __ds_scope.CustomScrollBar;

__ds_ns.CustomSlider = __ds_scope.CustomSlider;

__ds_ns.CustomSpinBox = __ds_scope.CustomSpinBox;

__ds_ns.CustomTextInput = __ds_scope.CustomTextInput;

__ds_ns.QLCPlusFader = __ds_scope.QLCPlusFader;

__ds_ns.ShortcutHint = __ds_scope.ShortcutHint;

__ds_ns.ShortcutContext = __ds_scope.ShortcutContext;

__ds_ns.ShortcutKeys = __ds_scope.ShortcutKeys;

__ds_ns.ShortcutOverlay = __ds_scope.ShortcutOverlay;

__ds_ns.FA = __ds_scope.FA;

__ds_ns.FaIcon = __ds_scope.FaIcon;

__ds_ns.IconTextEntry = __ds_scope.IconTextEntry;

__ds_ns.RobotoText = __ds_scope.RobotoText;

})();
