/**
 * keypad-parser.js — browser port of the engine's KeyPadParser (engine/src/keypadparser.cpp),
 * the grammar behind Simple Desk's keypad (qmlui/simpledesk.cpp::sendKeypadCommand). The server
 * has no io.simpleDesk.sendKeypadCommand yet, so the web UI parses locally and applies the result
 * through io.simpleDesk.setChannels.
 *
 * Grammar (tokens separated by spaces, case-insensitive, unknown words are skipped like C++ does):
 *   <ch> [THRU <ch>] [BY <step>] AT <value> [THRU <value>]   set / ramp a value across channels
 *   <ch> [THRU <ch>] [BY <step>] FULL | ZERO                 255 / 0
 *   <ch> [THRU <ch>] [BY <step>] + <n> | - <n>               current value +/- n
 *   <ch> [THRU <ch>] [BY <step>] +% <n> | -% <n>             current value +/- n percent ("+ % 20" too)
 *   AT <value> | FULL | ZERO | + <n> | ...                    no channel: reuse the last channel list
 *   <ch>                                                     re-assert the channel's current value
 * "@" is accepted as an alias of "AT" (KeyPad.qml itself only ever types " AT "). Channels are
 * 1-based in the command, 0-based in the result. One parser instance remembers the last channel
 * list (the C++ m_channels) — keep a single instance per desk, not one per universe.
 *
 * Deviations from the C++ (deliberate, documented): values are clamped to 0-255 instead of wrapping
 * through uchar(); "BY 0" is treated as "BY 1" instead of looping forever; a relative command
 * (+, -, +%, -%) without a channel list is applied relative to each remembered channel's current
 * value (the C++ stores the raw operand there, which is never what the operator meant).
 */
(function (root) {
  'use strict';

  var UNIVERSE_SIZE = 512;
  var CommandNone = 0, CommandAT = 1, CommandTHRU = 2, CommandFULL = 3, CommandZERO = 4, CommandBY = 5,
      CommandPlus = 6, CommandPlusPercent = 7, CommandMinus = 8, CommandMinusPercent = 9;

  function clamp(v) { return Math.max(0, Math.min(255, v)); }
  function lrint(v) { return Math.round(v); }
  function valueAt(uniData, index) {
    if (!uniData || index < 0 || index >= uniData.length) return 0;
    var v = Number(uniData[index]);
    return isNaN(v) ? 0 : v & 0xff;
  }

  function KeyPadParser() {
    /** 0-based channels touched by the last command that named channels (C++ m_channels). */
    this.channels = [];
  }

  /** Upper-case, "@" -> " AT ", collapse whitespace — what the engine gets after toUpper(). */
  KeyPadParser.normalize = function (command) {
    return String(command == null ? '' : command).toUpperCase().replace(/@/g, ' AT ').replace(/\s+/g, ' ').trim();
  };

  /**
   * @param command  the keypad line
   * @param uniData  current values of the shown universe (array-like of 0-255, index = 0-based channel)
   * @returns [{channel, value}] — 0-based channel within the universe, value 0-255. Empty when the
   *          command names no channel and none are remembered, or produces nothing.
   */
  KeyPadParser.prototype.parseCommand = function (command, uniData) {
    var values = [];
    var normalized = KeyPadParser.normalize(command);
    if (!normalized) return values;
    var tokens = normalized.split(' ');

    var lastCommand = CommandNone;
    var fromChannel = 0, toChannel = 0, byChannel = 1;
    var channelSet = false;
    var fromValue = 0, toValue = 0;
    var thruCount = 0;
    var i, number;

    for (var t = 0; t < tokens.length; t++) {
      var token = tokens[t];
      if (!token) continue;

      if (token === 'AT') lastCommand = CommandAT;
      else if (token === 'THRU') lastCommand = CommandTHRU;
      else if (token === 'FULL') { toValue = 255; lastCommand = CommandFULL; }
      else if (token === 'ZERO') { toValue = 0; lastCommand = CommandZERO; }
      else if (token === 'BY') lastCommand = CommandBY;
      else if (token === '+') lastCommand = CommandPlus;
      else if (token === '-') lastCommand = CommandMinus;
      else if (token === '+%') lastCommand = CommandPlusPercent;
      else if (token === '-%') lastCommand = CommandMinusPercent;
      else if (token === '%') {
        if (lastCommand === CommandPlus) lastCommand = CommandPlusPercent;
        else if (lastCommand === CommandMinus) lastCommand = CommandMinusPercent;
      } else {
        /* most likely a number (QString::toUInt: unsigned decimal only) */
        if (!/^\d+$/.test(token)) continue;
        number = parseInt(token, 10);

        switch (lastCommand) {
          case CommandNone:
            if (number <= 0) break;
            fromChannel = number;
            toChannel = fromChannel;
            fromValue = valueAt(uniData, number - 1);
            toValue = fromValue;
            channelSet = true;
            break;
          case CommandAT:
            fromValue = number;
            toValue = fromValue;
            break;
          case CommandTHRU:
            if (thruCount === 0) toChannel = number;
            else toValue = number;
            thruCount++;
            break;
          case CommandFULL:
            fromValue = 255; toValue = 255;
            break;
          case CommandZERO:
            fromValue = 0; toValue = 0;
            break;
          case CommandBY:
            byChannel = number;
            break;
          case CommandPlus:
          case CommandMinus:
            toValue = number;
            break;
          case CommandPlusPercent:
          case CommandMinusPercent:
            toValue = number / 100.0;
            break;
        }
      }
    }

    var apply = function (uniValue) {
      if (lastCommand === CommandPlus) return clamp(uniValue + toValue);
      if (lastCommand === CommandMinus) return clamp(uniValue - toValue);
      if (lastCommand === CommandPlusPercent) return clamp(lrint(uniValue * (1.0 + toValue)));
      if (lastCommand === CommandMinusPercent) return clamp(lrint(uniValue - uniValue * toValue));
      if (lastCommand === CommandZERO) return 0;
      if (lastCommand === CommandFULL) return 255;
      return clamp(Math.trunc(fromValue));
    };

    /* No channel named: re-apply to the channel list of the last command. */
    if (!channelSet) {
      if (!this.channels.length) return values;
      for (i = 0; i < this.channels.length; i++) {
        var ch = this.channels[i];
        var relative = lastCommand >= CommandPlus;
        values.push({ channel: ch, value: relative ? apply(valueAt(uniData, ch)) : clamp(Math.trunc(toValue)) });
      }
      return values;
    }
    this.channels = [];

    if (byChannel < 1) byChannel = 1;
    var valueDelta = 0;
    if (toValue !== fromValue) {
      var steps = (toChannel - fromChannel) / byChannel;
      valueDelta = steps > 0 ? (toValue - fromValue) / steps : 0;
    }

    var last = Math.min(toChannel, UNIVERSE_SIZE) - 1;
    for (i = fromChannel - 1; i <= last; i += byChannel) {
      if (i < 0 || i >= UNIVERSE_SIZE) continue;
      var value = apply(valueAt(uniData, i));
      if (this.channels.indexOf(i) === -1) this.channels.push(i);
      values.push({ channel: i, value: value });
      fromValue += valueDelta;
    }
    return values;
  };

  /** Human-readable one-line grammar hint for the UI. */
  KeyPadParser.HINT = '<ch> [THRU <ch>] [BY <n>] AT <0-255> [THRU <0-255>] · FULL · ZERO · + <n> · - <n> · +% <n> · -% <n>';

  root.QLCKeypadParser = KeyPadParser;
  if (typeof module !== 'undefined' && module.exports) module.exports = { KeyPadParser: KeyPadParser };
})(typeof window !== 'undefined' ? window : this);
