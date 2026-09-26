/**
 * fixtures domain — qlc.fixtures.*
 * Generated from docs/api-spec/fragments/fixtures.yaml (+ fixtures-notes.md) in the QLC+ fork
 * repo (read-only source, a moving target — re-verify against the live spec if something here
 * looks stale later). Covers: browsing the fixture-definition library (read-only — authoring
 * defs themselves is the separate `fixturedefs` domain/file), patching real fixtures into
 * universes, fixture groups (2D head-layout grid), and one-shot fixture remapping.
 * Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's.
 *
 * All wire IDs (fixtureId, groupId) are strings. universe/address are 0-based on the wire.
 * Every structural mutation here requires `baseRevision` (docRevision last observed) and can
 * fail with error.code 'CONFLICT' (current state in error.details) — call() is used throughout,
 * even for mutations whose result is just {docRevision}, so a caller can see that failure and
 * the fresh docRevision rather than have it silently swallowed by send().
 */
(function () {
  'use strict';
  function ns(self) {
    return {
      /**
       * Patched fixture instances, optionally filtered to one universe.
       * @param {object} [params] - {universe?: integer — 0-based, filter to this universe only}
       * @returns {Promise<object>} result - {fixtures: [{id, name, universe, address, channels, fixtureType, manufacturer?, model?, mode?, isGeneric, crossUniverse?}]}
       * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.list)
       */
      list: function (params) { return self.call('fixtures.list', params); },

      /**
       * One patched fixture's full detail, including its channel list resolved to absolute DMX
       * addresses (universe*512 + address + index) for direct display.
       * @param {object} params - {fixtureId: string}
       * @returns {Promise<object>} result - {...same fields as fixtures.list's entries, plus channelList: [{index, name, group, colour?, absoluteAddress}]}
       * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.get)
       */
      get: function (params) { return self.call('fixtures.get', params); },

      /**
       * Finds a free DMX address block for a fixture footprint in one universe — advisory only
       * (fixtures.patch still authoritatively re-checks and can still return
       * FIXTURES_ADDRESS_OVERLAP if the address was taken meanwhile).
       * @param {object} params - {universe: integer, channels: integer — one instance's footprint, quantity?: integer=1, gap?: integer=0, requestedAddress: integer — 0-based, tried first, excludeFixtureId?: string — exclude this fixture's own current range, e.g. when checking a move}
       * @returns {Promise<object>} result - {available: boolean, address?: integer — requested address if free, else first free found; absent if available=false}
       * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.findAvailableAddress)
       */
      findAvailableAddress: function (params) { return self.call('fixtures.findAvailableAddress', params); },

      /**
       * Patches one or more (bulk via quantity) identical fixture instances into a universe.
       * `definition` is exactly one of {manufacturer,model,mode} (a real library definition) or
       * {generic:{channels}} (a generic dimmer) — INVALID_PARAMS if both or neither are given.
       * Note: the engine appends " [<n>]" to `name` unconditionally (even for quantity=1) using
       * each new fixture's own id+1, NOT a per-batch ordinal starting at 1 — a bulk patch of 3
       * does not necessarily yield "[1]"/"[2]"/"[3]". Broadcasts fixtures.patched; the real new
       * state should be taken from that event, not this response.
       * @param {object} params - {universe: integer, address: integer — 0-based start address for the first fixture, definition: {manufacturer,model,mode}|{generic:{channels}}, name?: string, quantity?: integer=1, gap?: integer=0 — unused channels between instances, baseRevision: integer}
       * @returns {Promise<object>} result - {docRevision: integer, fixtureIds: string[] — length == quantity, in patch order}
       * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.patch)
       */
      patch: function (params) { return self.call('fixtures.patch', params); },

      /**
       * Unpatches (deletes) one or more fixtures in one call. Also silently deletes any fixture
       * group left empty by the removal (engine workaround for issue #2063) — those surface as
       * ordinary fixtures.group.deleted events, not a field on this response. Broadcasts
       * fixtures.unpatched.
       * @param {object} params - {fixtureIds: string[] (>=1), baseRevision: integer}
       * @returns {Promise<object>} result - {docRevision: integer}
       * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.unpatch)
       */
      unpatch: function (params) { return self.call('fixtures.unpatch', params); },

      /**
       * Moves (universe/address) and/or renames an already-patched fixture — at least one of
       * universe/address/name must be present. Unlike fixtures.patch, the underlying engine
       * setters do NOT themselves reject address overlaps (per fixtures-notes.md); the server
       * runs the same overlap check patch does. Broadcasts fixtures.updated with the fixture's
       * full new state.
       * @param {object} params - {fixtureId: string, universe?: integer, address?: integer, name?: string, baseRevision: integer}
       * @returns {Promise<object>} result - {docRevision: integer}
       * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.update)
       */
      update: function (params) { return self.call('fixtures.update', params); },

      defs: {
        /**
         * Sorted list of all manufacturer names known to the fixture-definition library.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {manufacturers: string[]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.defs.listManufacturers)
         */
        listManufacturers: function (params) { return self.call('fixtures.defs.listManufacturers', params); },

        /**
         * Model names available for one manufacturer.
         * @param {object} params - {manufacturer: string}
         * @returns {Promise<object>} result - {models: [{model: string, isUser: boolean — true if from the user's own fixture-definition folder rather than the system one}]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.defs.listModels)
         */
        listModels: function (params) { return self.call('fixtures.defs.listModels', params); },

        /**
         * Browsing-level detail for one fixture definition (manufacturer+model), including a
         * summary (name + channelCount only) of each mode it defines — use fixtures.defs.getMode
         * for full per-mode channel/head detail.
         * @param {object} params - {manufacturer: string, model: string}
         * @returns {Promise<object>} result - {manufacturer, model, fixtureType: string (e.g. "Moving Head","Dimmer","Color Changer","Scanner"), author: string, isUser: boolean, modes: [{name, channelCount}]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.defs.getModel)
         */
        getModel: function (params) { return self.call('fixtures.defs.getModel', params); },

        /**
         * Full detail for one mode of one fixture definition — channel layout, capabilities,
         * physical properties, and head grouping, enough to drive a patch dialog's preview.
         * @param {object} params - {manufacturer: string, model: string, mode: string}
         * @returns {Promise<object>} result - {manufacturer, model, mode, channelCount: integer, masterIntensityChannel: integer|null, physical: {width,height,depth,weight,powerConsumption,dmxConnector,bulbType,bulbLumens,bulbColourTemperature,lensName,lensDegreesMin,lensDegreesMax,focusType,focusPanMax,focusTiltMax,layoutWidth?,layoutHeight?}, useGlobalPhysical: boolean, channels: [{index,name,group,preset?,colour?,controlByte:'MSB'|'LSB',defaultValue,capabilities:[{min,max,name,preset?,presetType?,color1?,color2?}]}], heads: [{index, channels: integer[]}]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.defs.getMode)
         */
        getMode: function (params) { return self.call('fixtures.defs.getMode', params); }
      },

      group: {
        /**
         * All fixture groups, summary form (no head layout — use group.get for that).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {groups: [{id, name, size:{columns,rows}, headCount}]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.list)
         */
        list: function (params) { return self.call('fixtures.group.list', params); },

        /**
         * One fixture group's full state, including its head-to-cell assignments.
         * @param {object} params - {groupId: string}
         * @returns {Promise<object>} result - {id, name, size:{columns,rows}, heads: [{x, y, fixtureId, headIndex}]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.get)
         */
        get: function (params) { return self.call('fixtures.group.get', params); },

        /**
         * Creates a new, empty fixture group. Broadcasts fixtures.group.created.
         * @param {object} params - {name: string, columns?: integer=1, rows?: integer=1, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer, groupId: string}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.create)
         */
        create: function (params) { return self.call('fixtures.group.create', params); },

        /**
         * Renames a fixture group. Broadcasts fixtures.group.renamed.
         * @param {object} params - {groupId: string, name: string, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.rename)
         */
        rename: function (params) { return self.call('fixtures.group.rename', params); },

        /**
         * Deletes a fixture group entirely. Broadcasts fixtures.group.deleted.
         * @param {object} params - {groupId: string, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.delete)
         */
        delete: function (params) { return self.call('fixtures.group.delete', params); },

        /**
         * Resizes a group's grid. Shrinking below the current extent does not itself drop
         * out-of-bounds heads (engine behaviour) — they stay assigned but become unreachable via
         * the grid until explicitly reassigned/unassigned. Broadcasts fixtures.group.updated.
         * @param {object} params - {groupId: string, columns: integer (>=1), rows: integer (>=1), baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.setSize)
         */
        setSize: function (params) { return self.call('fixtures.group.setSize', params); },

        /**
         * Places all of a fixture's heads into consecutive grid cells starting at (x,y),
         * wrapping to the next row at the group's column count. Omit x/y to auto-place in the
         * next free cell (row-major scan). Broadcasts fixtures.group.updated.
         * @param {object} params - {groupId: string, fixtureId: string, x?: integer, y?: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.assignFixture)
         */
        assignFixture: function (params) { return self.call('fixtures.group.assignFixture', params); },

        /**
         * Places one head of a fixture into a grid cell. If that head is already assigned
         * elsewhere in the group it is moved; if (x,y) is already occupied by another head, the
         * two heads swap places. Omit x/y to auto-place in the next free cell. Broadcasts
         * fixtures.group.updated.
         * @param {object} params - {groupId: string, fixtureId: string, headIndex: integer — 0 for a dimmer/non-multi-head fixture, x?: integer, y?: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.assignHead)
         */
        assignHead: function (params) { return self.call('fixtures.group.assignHead', params); },

        /**
         * Clears one grid cell (unassigns whatever head, if any, occupies it). Broadcasts
         * fixtures.group.updated.
         * @param {object} params - {groupId: string, x: integer, y: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.unassignHead)
         */
        unassignHead: function (params) { return self.call('fixtures.group.unassignHead', params); },

        /**
         * Removes every head belonging to one fixture from the group. Broadcasts
         * fixtures.group.updated.
         * @param {object} params - {groupId: string, fixtureId: string, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.unassignFixture)
         */
        unassignFixture: function (params) { return self.call('fixtures.group.unassignFixture', params); },

        /**
         * Exchanges the heads at two grid cells (or just moves one, if the other side is empty).
         * Broadcasts fixtures.group.updated.
         * @param {object} params - {groupId: string, ax: integer, ay: integer, bx: integer, by: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.swapHeads)
         */
        swapHeads: function (params) { return self.call('fixtures.group.swapHeads', params); },

        /**
         * Clears all head assignments in a group, keeping its name/size. Broadcasts
         * fixtures.group.updated.
         * @param {object} params - {groupId: string, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.group.reset)
         */
        reset: function (params) { return self.call('fixtures.group.reset', params); }
      },

      remap: {
        /**
         * Read-only helper: suggests a channel map from an existing (source) fixture to a
         * not-yet-created target definition/mode — 1:1 index mapping when source and target
         * share a definition+mode (or are both generic dimmers), otherwise per-channel semantic
         * matching by group/controlByte/colour. Purely advisory — does not mutate anything.
         * @param {object} params - {sourceFixtureId: string, target: {manufacturer,model,mode}|{generic:{channels}}}
         * @returns {Promise<object>} result - {channelMap: [{sourceChannel: integer, targetChannel: integer}]}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.remap.suggestChannelMap)
         */
        suggestChannelMap: function (params) { return self.call('fixtures.remap.suggestChannelMap', params); },

        /**
         * One-shot atomic batch commit: each mapping entry replaces one existing (source)
         * fixture with a newly-created (target) fixture at the given address, carrying every
         * reference to the old fixture/channel (fixture groups, channel groups, Scene/Sequence/
         * EFX functions) onto the new one via its channelMap. Source fixtures not mentioned in
         * `mappings` and not listed in `unmappedSourceFixtureIds` are left untouched; there is
         * no partial/incremental variant. Broadcasts fixtures.remap.applied — note this event
         * only covers the fixture-level result, per fixtures-notes.md; affected functions/
         * channel-group state may need a separate refetch from those domains.
         * @param {object} params - {mappings: [{sourceFixtureId: string, universe: integer, address: integer, definition: {manufacturer,model,mode}|{generic:{channels}}, name?: string, channelMap: [{sourceChannel,targetChannel}]}] (>=1), unmappedSourceFixtureIds?: string[] — source fixtures to delete outright with no replacement, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/fixtures.yaml (method: fixtures.remap.apply)
         */
        apply: function (params) { return self.call('fixtures.remap.apply', params); }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'fixtures', {
    configurable: true,
    get: function () { return ns(this); }
  });

  /**
   * Event topics broadcast by this domain. All are low-frequency structural (§4a) changes and
   * are delivered to every connected client unconditionally — none are subscribe-gated
   * (fixtures-notes.md: patching/moving/grouping fixtures is an editing action, not a per-tick
   * stream; live DMX values belong to a different domain, not `fixtures`).
   */
  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.fixtures = [
    'fixtures.patched',        // {docRevision, fixtures: [FixturesPatchedFixture]} — full new state, after fixtures.patch
    'fixtures.unpatched',      // {fixtureIds, docRevision} — after fixtures.unpatch (may also trigger fixtures.group.deleted)
    'fixtures.updated',        // {fixture, docRevision} — full new state, after fixtures.update
    'fixtures.group.created',  // {group, docRevision} — after fixtures.group.create
    'fixtures.group.renamed',  // {groupId, name, docRevision} — after fixtures.group.rename
    'fixtures.group.deleted',  // {groupId, docRevision} — after fixtures.group.delete, or a group emptied by fixtures.unpatch
    'fixtures.group.updated',  // {group, docRevision} — shared broadcast for setSize/assignFixture/assignHead/unassignHead/unassignFixture/swapHeads/reset
    'fixtures.remap.applied'   // {fixtures, replacedFixtureIds, deletedFixtureIds, docRevision} — after fixtures.remap.apply
  ];
})();
