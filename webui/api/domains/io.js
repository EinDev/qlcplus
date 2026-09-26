/**
 * io domain — qlc.io.*
 * Generated from docs/api-spec/fragments/io.yaml (+ io-notes.md) in the QLC+ fork repo
 * (<repo> — read-only source, actively being implemented server-side
 * right now, so treat every shape below as "true as of this reading," re-verify if it looks stale).
 * Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's, using its own field
 * names (0-based `universeId`, flat 0-based `address` = universeId*512 + channelWithinUniverse,
 * `baseRevision`/`docRevision` for structural (§4a) mutations, `profilesRevision` for the
 * input-profile shared-library resource (§4c)). NOTE: api/qlcplus-api.js also wires
 * simpleDesk/grandMaster/blackout directly (setChannel/resetChannel/resetUniverse/watchUniverse/
 * getUniverseValues/setGrandMaster/setBlackout/...) using these same spec field names — this
 * file exists so every OTHER io.* method is reachable too, and so the full raw io.simpleDesk,
 * io.grandMaster and io.blackout surface is available untranslated as well.
 *
 * Tiering per 00-conventions.md:
 *  - §4a document-state (needs baseRevision/docRevision): io.universe.create/delete/update,
 *    io.patch.set/remove/setParameters, io.simpleDesk.dump.
 *  - §4b live/runtime (no baseRevision, last-write-wins): io.universe.setMonitor,
 *    io.patch.output.setState, io.grandMaster.*, io.blackout.*, io.simpleDesk.* (except dump),
 *    io.dmx.universe.get, io.inputProfile.learn.*.
 *  - §4c shared-library resource (domain-local `profilesRevision`, not the global docRevision):
 *    io.inputProfile.save/delete (baseRevision here means "profilesRevision last observed").
 */
(function () {
  'use strict';

  function ns(self) {
    return {
      blackout: {
        /** -> {blackout: boolean} */
        get: function () { return self.call('io.blackout.get', {}); },
        /** blackout: boolean. Live/runtime, no baseRevision. -> ack; broadcasts io.blackout.changed. */
        set: function (blackout) { return self.call('io.blackout.set', { blackout: !!blackout }); },
        /** Flips current state. -> {blackout: boolean} (new state). */
        toggle: function () { return self.call('io.blackout.toggle', {}); }
      },

      dmx: {
        universe: {
          /** universeId: integer (0-based). One-shot full 512-channel *post*-Grand-Master
              snapshot (Universe::postGMValues()) — use to seed a baseline before/after
              subscribing to the delta topic below. -> {universeId, values: number[512]}.
              Ongoing per-universe deltas arrive on the dynamic, subscribe-gated topic
              'io.dmx.universe.<id>.changed' (data: {universeId, changes: [{channel, value}]}) —
              not enumerable as a literal method; use self.subscribe(['io.dmx.universe.'+id+'.changed'])
              then self.on(that topic string, fn). */
          get: function (universeId) { return self.call('io.dmx.universe.get', { universeId: universeId }); }
        }
      },

      grandMaster: {
        /** -> {value: 0-255, channelMode: 'Intensity'|'AllChannels', valueMode: 'Limit'|'Reduce'} */
        get: function () { return self.call('io.grandMaster.get', {}); },
        /** params: {channelMode?, valueMode?} — omit a field to leave it unchanged.
            Live/runtime, no baseRevision. -> ack; broadcasts io.grandMaster.changed. */
        setMode: function (params) { return self.call('io.grandMaster.setMode', params || {}); },
        /** value: integer 0-255. Live/runtime, no baseRevision. -> ack; broadcasts io.grandMaster.changed. */
        setValue: function (value) { return self.call('io.grandMaster.setValue', { value: value }); }
      },

      inputProfile: {
        /** name: string. baseRevision: profilesRevision last observed (§4c domain-local counter,
            NOT docRevision). -> {profilesRevision}; broadcasts io.inputProfile.deleted. */
        'delete': function (name, baseRevision) {
          return self.call('io.inputProfile.delete', { name: name, baseRevision: baseRevision });
        },
        /** name: string. -> {profile: IoInputProfile, profilesRevision}. */
        get: function (name) { return self.call('io.inputProfile.get', { name: name }); },
        /** -> {profiles: [{name, manufacturer, model, type}], profilesRevision}. */
        list: function () { return self.call('io.inputProfile.list', {}); },
        /** profile: full IoInputProfile object (manufacturer, model, type, midiSendNoteOff,
            channels[], colorTable[]?, midiChannelTable[]?). baseRevision: profilesRevision last
            observed. Whole-document upsert (creates, or replaces the profile matching
            (manufacturer, model)). -> {profilesRevision}; broadcasts io.inputProfile.changed. */
        save: function (profile, baseRevision) {
          return self.call('io.inputProfile.save', { profile: profile, baseRevision: baseRevision });
        },
        learn: {
          /** universeId: integer (0-based), an already-patched input universe. Puts the engine
              into MIDI/OSC-learn mode; raw signals arrive as io.inputProfile.learn.signal events
              instead of being consumed normally. Live editor-session state, no baseRevision.
              -> ack. NOTE (io-notes.md): the signal event is a plain broadcast to every client,
              not scoped to the requester — filter client-side if only the caller should react. */
          start: function (universeId) { return self.call('io.inputProfile.learn.start', { universeId: universeId }); },
          /** -> ack. */
          stop: function () { return self.call('io.inputProfile.learn.stop', {}); }
        }
      },

      patch: {
        /** Server contract as implemented (2026-09-26, differs from the spec-shaped set()/remove()
            below): params {universeId, direction: 'input'|'output'|'feedback', plugin, line: int,
            profile?} -> {}. `line` is the plugin's line index from io.plugin.list; `profile` is an
            input profile name (input only). InputOutput.jsx sends baseRevision alongside — harmless
            if ignored, required if the server enforces §4a. -> {}; broadcasts io.universe.updated. */
        assign: function (params) { return self.call('io.patch.set', params); },
        /** Server contract as implemented: {universeId, direction: 'input'|'output'|'feedback'} -> {}.
            No index: the web UI manages one output line per universe. */
        clear: function (params) { return self.call('io.patch.remove', params); },
        /** params: {universeId, patchType: 'input'|'output'|'feedback', index?, baseRevision}.
            `index` required only when patchType='output' (input/feedback are singleton per
            universe). Detaches a patch. -> {docRevision}; broadcasts io.universe.updated
            (full new IoUniverseDetail). */
        remove: function (params) { return self.call('io.patch.remove', params); },
        /** params: {universeId, patchType: 'input'|'output'|'feedback', pluginName, line,
            index?, profileName?, baseRevision}. `line` is the plugin's input/output line index
            (see plugin.getLines). `index`: output patch slot — omit to append, pass existing to
            replace; unused for input/feedback. `profileName`: input profile to attach, input-only.
            Attaches a plugin line to a universe's patch. -> {docRevision}; broadcasts
            io.universe.updated. */
        set: function (params) { return self.call('io.patch.set', params); },
        /** params: {universeId, patchType: 'input'|'output'|'feedback', index? (default 0,
            output-only), parameters: {[key]: string|number|boolean|null}, baseRevision}.
            Generic per-line plugin parameter set (QLCIOPlugin::setParameter/getParameters, e.g.
            ArtNet outputIP/outputUni/transmitMode). A null value reverts that key to the
            plugin's default. -> {docRevision}; broadcasts io.universe.updated. This is the
            remote-friendly replacement for io.plugin.configure (see plugin.configure doc). */
        setParameters: function (params) { return self.call('io.patch.setParameters', params); },
        output: {
          /** params: {universeId, index, paused?, blackout?} — omit a field to leave it
              unchanged. Live/runtime only (OutputPatch::paused/blackout, not persisted), no
              baseRevision. -> ack; broadcasts io.patch.output.stateChanged
              ({universeId, index, paused, blackout}). */
          setState: function (params) { return self.call('io.patch.output.setState', params); }
        }
      },

      plugin: {
        /** pluginName: string. Thin passthrough to InputOutputMap::configurePlugin(). Per
            io-notes.md: on today's engine build this pops a native Qt dialog SERVER-SIDE and is
            NOT usable from a remote Electron client for most plugins — prefer io.patch.setParameters.
            Kept only for parity/completeness (and plugins like dmxusb whose native dialog may do
            things setParameters can't replicate without engine-side changes). -> ack. */
        configure: function (pluginName) { return self.call('io.plugin.configure', { pluginName: pluginName }); },
        /** pluginName: string. -> {inputs: IoPluginLine[], outputs: IoPluginLine[]}
            (each {line, name, uid}). Not registered by the server as of 2026-09-26. */
        getLines: function (pluginName) { return self.call('io.plugin.getLines', { pluginName: pluginName }); },
        /** Server contract as implemented (2026-09-26): -> {plugins: [{name, inputLines: [{index,
            name}], outputLines: [{index, name}], canConfigure}]} - lines inline, no separate
            getLines round trip. (The spec fragment describes {name, capabilities[], description,
            canConfigure, supportsFeedback} instead; InputOutput.jsx codes against the former.) */
        list: function () { return self.call('io.plugin.list', {}); },
        /** pluginName: string. Ask a hotplug-style plugin (DMXUSB, HID, ...) to re-enumerate its
            hardware. Not every plugin supports this. -> ack; broadcasts io.plugin.linesChanged
            ({pluginName, inputs, outputs}) if the line set changed. */
        rescan: function (pluginName) { return self.call('io.plugin.rescan', { pluginName: pluginName }); }
      },

      simpleDesk: {
        /** params: {baseRevision, nonZeroOnly, targetSceneId?, name?, channelGroups?}.
            Unlike every other io.simpleDesk.* method, this IS a §4a structural mutation: bakes
            Simple Desk's currently-held live values into a saved Scene Function
            (SimpleDesk::dumpDmxChannels). targetSceneId: existing Scene id to merge into, omit/
            null to create a new one (named by `name`). channelGroups: QLCChannel::Group names to
            include, omit/empty = all. -> {sceneId, docRevision}. No bespoke event — reported via
            functions.created/functions.updated instead (see io-notes.md). */
        dump: function (params) { return self.call('io.simpleDesk.dump', params); },
        /** universeId: integer (0-based). -> {universeId, channels: [{address, universeId,
            channel, value, group, fixtureId, overridden}], slidersNumber, currentPage}. */
        get: function (universeId) { return self.call('io.simpleDesk.get', { universeId: universeId }); },
        /** address: integer, absolute flat 0-based (universeId*512 + channelWithinUniverse) —
            same encoding as setChannel. Releases a manual override (not the same as setting 0).
            -> ack; broadcasts io.simpleDesk.channelChanged with overridden:false. */
        resetChannel: function (address) { return self.call('io.simpleDesk.resetChannel', { address: address }); },
        /** universeId: integer (0-based). -> ack; broadcasts io.simpleDesk.universeReset
            ({universeId}) — carries no channel data, refetch via get() if needed. */
        resetUniverse: function (universeId) { return self.call('io.simpleDesk.resetUniverse', { universeId: universeId }); },
        /** command: string, console keypad syntax (e.g. "1 THRU 10 @ 50 ENTER"). -> {accepted:
            boolean}. Side effects arrive as ordinary io.simpleDesk.channelChanged events, plus
            io.simpleDesk.commandHistoryChanged ({history: string[]}). */
        sendKeypadCommand: function (command) { return self.call('io.simpleDesk.sendKeypadCommand', { command: command }); },
        /** address: integer, absolute flat 0-based (universeId*512 + channelWithinUniverse).
            value: integer 0-255. Live/runtime, no baseRevision. -> ack; broadcasts
            io.simpleDesk.channelChanged ({address, value, overridden}). */
        setChannel: function (address, value) { return self.call('io.simpleDesk.setChannel', { address: address, value: value }); },
        /** channels: [{address, value}] (same absolute flat 0-based address as setChannel), at
            least 1 entry. Bulk variant for high-frequency multi-channel writers; all-or-nothing
            (one malformed entry rejects the whole request). -> ack; broadcasts one
            io.simpleDesk.channelChanged per entry (no bulk-specific event). */
        setChannels: function (channels) { return self.call('io.simpleDesk.setChannels', { channels: channels }); },
        /** universeId: integer (0-based). Sets which universe Simple Desk's channel view targets
            — global engine state shared by every client. -> ack; broadcasts
            io.simpleDesk.universeFilterChanged ({universeId}). */
        setUniverseFilter: function (universeId) { return self.call('io.simpleDesk.setUniverseFilter', { universeId: universeId }); }
      },

      universe: {
        /** params: {name?, baseRevision}. name omitted -> engine assigns a default ("Universe N").
            -> {universeId, docRevision}; broadcasts io.universe.created (full IoUniverseDetail). */
        create: function (params) { return self.call('io.universe.create', params || {}); },
        /** universeId: integer (0-based). baseRevision: docRevision last observed (server contract
            as implemented 2026-09-26 takes {universeId} only; the extra field is harmless).
            The engine only removes the highest-id universe (InputOutputMap::removeUniverse keeps
            ids contiguous) and refuses to remove the last one.
            -> {}; broadcasts io.universe.deleted ({universeId, docRevision}). */
        'delete': function (universeId, baseRevision) {
          return self.call('io.universe.delete', { universeId: universeId, baseRevision: baseRevision });
        },
        /** universeId: integer (0-based). -> IoUniverseDetail (IoUniverseSummary fields +
            inputPatch, outputPatches[], feedbackPatch). */
        get: function (universeId) { return self.call('io.universe.get', { universeId: universeId }); },
        /** -> {universes: IoUniverseSummary[], docRevision}. */
        list: function () { return self.call('io.universe.list', {}); },
        /** universeId: integer (0-based). monitor: boolean. Live/runtime only
            (Universe::setMonitor, never persisted), no baseRevision. -> ack; broadcasts
            io.universe.monitorChanged ({universeId, monitor}). */
        setMonitor: function (universeId, monitor) {
          return self.call('io.universe.setMonitor', { universeId: universeId, monitor: monitor });
        },
        /** params: {universeId, name?, passthrough?, baseRevision}. Partial update of persisted
            properties — omit a field to leave it unchanged. passthrough: merges the input
            patch's live values onto this universe's output (Universe::setPassthrough).
            -> {docRevision}; broadcasts io.universe.updated (full IoUniverseDetail) — the same
            event also broadcast by every io.patch.* mutation. */
        update: function (params) { return self.call('io.universe.update', params); }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'io', {
    configurable: true,
    get: function () { return ns(this); }
  });

  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.io = [
    'io.universe.created',
    'io.universe.deleted',
    'io.universe.updated',           // also broadcast for every io.patch.* mutation
    'io.universe.monitorChanged',
    'io.patch.output.stateChanged',
    'io.plugin.linesChanged',
    'io.inputProfile.changed',
    'io.inputProfile.deleted',
    'io.inputProfile.learn.signal',  // NOT requester-scoped as of this reading — see io-notes.md
    'io.grandMaster.changed',
    'io.blackout.changed',
    'io.simpleDesk.channelChanged',
    'io.simpleDesk.universeReset',
    'io.simpleDesk.universeFilterChanged',
    'io.simpleDesk.commandHistoryChanged'
    // Dynamic, subscribe-gated pattern (one topic per universe, not enumerable here):
    // 'io.dmx.universe.<id>.changed', e.g. 'io.dmx.universe.1.changed'
  ];
})();
