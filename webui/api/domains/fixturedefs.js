/**
 * fixturedefs domain — qlc.fixtureDefs.*
 * Generated from docs/api-spec/fragments/fixturedefs.yaml (+ fixturedefs-notes.md) in the QLC+
 * fork repo (read-only source, a moving target — re-verify against the live spec if something
 * here looks stale later). Covers fixture *definition* authoring only — the .qxf editor:
 * manufacturer/model/type metadata, channels, capabilities, aliases, modes, heads, physical
 * properties, and the library/session/save/import/export lifecycle around all of that. This is
 * explicitly NOT patching a definition onto a real fixture instance in a universe/address, and
 * NOT read-only definition browsing for the patch UI — both of those live under the separate
 * `fixtures.*` domain (see fixtures.js), not here.
 * Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's.
 *
 * Revisioning (see fixturedefs-notes.md, 00-conventions.md §4c): this domain does NOT use the
 * show's global docRevision. Two independent, domain-local counters instead:
 *   - defRevision — per-definition (manufacturer+model) revision in the shared on-disk library.
 *     Used as `baseRevision` by fixturedefs.delete and fixturedefs.save.
 *   - sessionRevision — an in-memory editing session's own revision, starting at 0 when the
 *     session is created/opened/imported. Used as `baseRevision` by every channel/mode/
 *     capability/alias/session-metadata mutation, which all operate against a `sessionId`.
 * A session is a private draft clone of a definition; nothing touches the shared library/cache
 * or disk until fixturedefs.save. Every successful session mutation broadcasts the single shared
 * 'fixturedefs.session.updated' event carrying the session's full current definition snapshot
 * (not a JSON Patch — definitions are small; see notes.md) plus a `changeKind` tag.
 *
 * Server status (2026-09-27): every method below is implemented in
 * controlapi/src/domains/apifixturedefsdomain.cpp and covered by controlapi/test/apifixturedefsdomain.
 * What an editor screen built on this needs to know (details in fixturedefs-notes.md,
 * "Implemented 2026-09-27"):
 *   - Lifecycle: session.create | session.open({manufacturer, model}) | session.import({fileName,
 *     qxfBase64}) -> {sessionId, definition, sessionRevision: 0, isUser, baseRevision}. Keep
 *     sessionRevision from every ack / session.updated event and send it as baseRevision on the
 *     next mutation; a CONFLICT carries {sessionRevision, definition} in error.details to rebase
 *     from. Sessions survive a page reload — session.list shows them, session.close frees them.
 *   - Saving: fixturedefs.save({sessionId, baseRevision}) where baseRevision is the defRevision
 *     from session.open / the previous save result (null for a never-saved definition). A session
 *     opened from a bundled definition (isUser:false) must session.forkToUser first or save is
 *     FIXTUREDEFS_SYSTEM_READONLY. Save answers {defRevision, warnings} and broadcasts
 *     'fixturedefs.saved'; the session stays open. Files land in the user fixture directory
 *     (overridable server-side through QLCPLUS_USER_FIXTURE_DIR for sandboxes).
 *   - Ids: channelId ("ch-N") / modeId ("mode-N") are stable for the session's life and appear in
 *     every definition snapshot; capabilities, aliases and heads are addressed by index within
 *     their parent. Ids from fixturedefs.get are index-based and only valid in that response.
 *   - Names must be unique per definition (channels and modes): add/rename with a taken name is
 *     INVALID_PARAMS. Aliases are name-addressed; renaming a channel or mode does not retarget
 *     aliases pointing at it (see fixturedefs.mode.rename in the spec).
 *   - Errors specific to this domain: FIXTUREDEFS_SYSTEM_READONLY, FIXTUREDEFS_IN_USE (delete of a
 *     definition patched in the open project; details.fixtureIds), FIXTUREDEFS_RANGE_OVERLAP
 *     (capability add/update/wizard), FIXTUREDEFS_ACTS_ON_SELF (mode.setChannels).
 *   - The two wizard methods (channel.wizard, channel.capability.wizard) are this fork's addition
 *     for the desktop "Fixture Editor Wizard" popup.
 */
(function () {
  'use strict';
  function ns(self) {
    return {
      // ================================================================
      // Library browsing (read-only) + delete/export/save (flat, per spec)
      // ================================================================

      /**
       * Lists shared-library fixture definition summary rows (manufacturer/model/type/author/
       * isUser/channelCount/modeCount/defRevision). Cost warning from the spec: populating
       * channelCount/modeCount/defRevision for every row may force the server to lazy-load
       * (parse XML for) every not-yet-warmed definition in the cache — potentially expensive on
       * a fully-populated install. Generic placeholder defs (Fixture::genericDimmerDef() etc.)
       * never appear here.
       * @param {object} [params] - {manufacturer?: string — filter to this manufacturer only}
       * @returns {Promise<object>} result - {entries: [{manufacturer, model, type, author, isUser, channelCount, modeCount, defRevision}]}
       * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.list)
       */
      list: function (params) { return self.call('fixturedefs.list', params); },

      /**
       * Fetches one full shared-library definition (channels, modes, physical, etc.) by
       * manufacturer+model. Generic-only pairs (manufacturer/model "Generic"/"RGBPanel") are not
       * real library entries and return NOT_FOUND, same as any nonexistent pair.
       * @param {object} params - {manufacturer: string, model: string}
       * @returns {Promise<object>} result - {definition: FixtureDefsDefinition, defRevision: integer}
       * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.get)
       */
      get: function (params) { return self.call('fixturedefs.get', params); },

      /**
       * Deletes a definition from the shared library (removes its .qxf from disk + the in-memory
       * cache). Only ever possible for a user (isUser:true) definition — a bundled/system
       * definition always rejects with error.code FIXTUREDEFS_SYSTEM_READONLY (no engine concept
       * of hiding/disabling a system definition). A definition still in use by a patched fixture
       * in the current project should also be rejected (FIXTUREDEFS_IN_USE-ish, conservative
       * server behavior per the spec).
       * @param {object} params - {manufacturer: string, model: string, baseRevision: integer — defRevision last observed for this manufacturer/model}
       * @returns {Promise<object>} result - {manufacturer: string, model: string}
       * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.delete)
       */
      "delete": function (params) { return self.call('fixturedefs.delete', params); },

      /**
       * Exports a definition as a raw QXF (XML) file, base64-encoded inline in the result (no
       * separate binary transport — see fixturedefs-notes.md). Provide either `sessionId` (export
       * the in-progress working copy, possibly unsaved) or `manufacturer`+`model` (export the
       * saved library entry); omitting both, or an unknown sessionId, is NOT_FOUND.
       * @param {object} params - {sessionId?: string, manufacturer?: string, model?: string}
       * @returns {Promise<object>} result - {fileName: string, qxfBase64: string — base64 raw QXF/XML bytes}
       * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.export)
       */
      "export": function (params) { return self.call('fixturedefs.export', params); },

      /**
       * Writes a session's working copy to disk and into the shared library/cache (mirrors
       * EditorView::save). The session stays open afterwards — edit and save again freely. If the
       * session's isUser is currently false (opened from a bundled/system definition), this
       * rejects with FIXTUREDEFS_SYSTEM_READONLY; call session.forkToUser first. Non-blocking
       * validation warnings never block the save.
       * @param {object} params - {sessionId: string, baseRevision: integer|null — defRevision this session was last opened/saved against; null only if never saved}
       * @returns {Promise<object>} result - {sessionId, manufacturer, model, defRevision: integer — new library revision, warnings: string[]}
       * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.save)
       */
      save: function (params) { return self.call('fixturedefs.save', params); },

      // ================================================================
      // Channel CRUD (definition-level channel pool) + nested capability/alias
      // ================================================================

      channel: {
        /**
         * Adds a new channel to the session's channel pool (not yet assigned to any mode — see
         * mode.setChannels for that). If `preset` is given, applies the same auto group/colour +
         * single auto-generated capability shortcut as the desktop editor's addPresetChannel;
         * otherwise creates a blank channel with one full-range (0-255), empty-name capability.
         * @param {object} params - {sessionId: string, baseRevision: integer, name?: string (default 'New channel N'), group?: string — QLCChannel::Group name e.g. Intensity/Colour/Gobo/Speed/Pan/Tilt/Shutter/Prism/Beam/Effect/Maintenance/Nothing/"Position X"/"Position Y"/"Position Z"/"Rotation X"/"Rotation Y"/"Rotation Z"/"Scale X"/"Scale Y"/"Scale Z", colour?: string — QLCChannel::PrimaryColour name (Generic/Red/Green/Blue/Cyan/Magenta/Yellow/Amber/White/UV/Lime/Indigo), only meaningful when group is Intensity, preset?: string — shortcut preset name e.g. IntensityRed/PositionPan/ColorMacro}
         * @returns {Promise<object>} result - {sessionId, sessionRevision, channelId: string — server-assigned, stable for the session}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.add)
         */
        add: function (params) { return self.call('fixturedefs.channel.add', params); },

        /**
         * Partial update of a channel's own fields. Setting `preset` to anything other than
         * Custom replaces the channel's entire capability list with the preset's auto-generated
         * capability (visible in the following session.updated event). Renaming a channel does
         * NOT retarget any alias whose targetChannel references the old name (aliases are
         * name-addressed) — re-point those explicitly via channel.capability.alias.update.
         * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, name?: string, group?: string, colour?: string, preset?: string, defaultValue?: integer (0-255), controlByte?: 'MSB'|'LSB'}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.update)
         */
        update: function (params) { return self.call('fixturedefs.channel.update', params); },

        /**
         * Batch-removes channels from the pool. Cascades: also removed from every mode's channel
         * list/heads, and any alias capabilities targeting a removed channel are dropped.
         * @param {object} params - {sessionId: string, baseRevision: integer, channelIds: string[] (>=1)}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.remove)
         */
        remove: function (params) { return self.call('fixturedefs.channel.remove', params); },

        /**
         * Bulk-creates preset channels (the channel face of the desktop "Fixture Editor Wizard",
         * PopupChannelWizard.qml). `label`'s `#` becomes the 1-based index. `type` is a primary
         * colour name (Red/Green/Blue/White/Amber/UV/Lime/Indigo/...: Intensity channel of that
         * colour), a compound set (RGB/RGBW/RGBA/RGBL/RGBAW: one channel per component named
         * "<Colour> N", label ignored) or a channel group (Intensity or "Dimmer", Pan, Tilt, Colour
         * or "Color Macro", Shutter, Beam, Effect, ...). Every generated name must be free, or the
         * whole call is refused with INVALID_PARAMS and nothing is created. One session mutation:
         * one baseRevision, one session.updated event (changeKind 'channel.wizard').
         * @param {object} params - {sessionId: string, baseRevision: integer, type: string, amount?: integer (1-1000, default 1), label?: string (default 'Channel #')}
         * @returns {Promise<object>} result - {sessionId, sessionRevision, channelIds: string[] — created channels in order}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.wizard)
         */
        wizard: function (params) { return self.call('fixturedefs.channel.wizard', params); },

        capability: {
          /**
           * Adds a capability (DMX sub-range) to a channel. If min/max are omitted, the server
           * picks the next free range the same way the desktop editor does (min = previous
           * capability's max + 1, max = 255).
           * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, min?: integer (0-255), max?: integer (0-255), name?: string}
           * @returns {Promise<object>} result - {sessionId, sessionRevision, capabilityIndex: integer — position within the channel's capability list; capabilities have no engine-level id}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.add)
           */
          add: function (params) { return self.call('fixturedefs.channel.capability.add', params); },

          /**
           * Partial update of a capability's range/name/preset/resources. `resources`'
           * interpretation depends on the resulting preset's presetType: SingleColor/DoubleColor
           * expect 1-2 "#RRGGBB" hex strings, SingleValue/DoubleValue expect 1-2 numbers (Hz, or
           * prism face count), Picture expects a single resource-name string; other presets
           * ignore it. `aliases` is read-only here — mutate via channel.capability.alias.*.
           * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, capabilityIndex: integer, min?: integer (0-255), max?: integer (0-255), name?: string, preset?: string, resources?: Array<string|number>}
           * @returns {Promise<object>} result - {sessionId, sessionRevision}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.update)
           */
          update: function (params) { return self.call('fixturedefs.channel.capability.update', params); },

          /**
           * Removes one capability from a channel by its positional index.
           * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, capabilityIndex: integer}
           * @returns {Promise<object>} result - {sessionId, sessionRevision}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.remove)
           */
          remove: function (params) { return self.call('fixturedefs.channel.capability.remove', params); },

          /**
           * Bulk-authoring convenience for a Colour-group channel: title-cases all-lowercase
           * capability names and auto-detects colour names against the bundled namedrgb filter,
           * setting ColorMacro/ColorDoubleMacro preset + resources accordingly. No-op (still
           * ok:true) if the channel isn't group=Colour or nothing matched.
           * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string}
           * @returns {Promise<object>} result - {sessionId, sessionRevision}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.autoPatchColors)
           */
          autoPatchColors: function (params) { return self.call('fixturedefs.channel.capability.autoPatchColors', params); },

          /**
           * Bulk-creates `amount` consecutive capabilities of `width` DMX values from `start`
           * (capability i covers [start + width*i, start + width*i + width - 1]), named from
           * `label` with `#` replaced by the 1-based index — the capability face of the desktop
           * wizard. The whole range must fit 0..255 (INVALID_PARAMS) and must not touch an
           * existing capability (FIXTUREDEFS_RANGE_OVERLAP, the wizard's "Overlapping range
           * detected"); nothing is created otherwise. changeKind 'capability.wizard'.
           * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, start?: integer (0-254, default 0), width?: integer (1-255, default 1), amount?: integer (>=1, default 1), label?: string (default 'Capability #')}
           * @returns {Promise<object>} result - {sessionId, sessionRevision, capabilityIndexes: integer[] — positions of the created capabilities}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.wizard)
           */
          wizard: function (params) { return self.call('fixturedefs.channel.capability.wizard', params); },

          alias: {
            /**
             * Adds a name-addressed alias substitution to a capability (the capability at
             * capabilityIndex must have preset Alias). targetMode must be a mode that contains
             * channelId. No-op if an identical alias already exists.
             * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, capabilityIndex: integer, targetMode: string — mode name this substitution applies in, targetChannel: string — channel name that replaces this channel while the DMX value is in [min,max]}
             * @returns {Promise<object>} result - {sessionId, sessionRevision, aliasIndex: integer — position within this capability's alias list}
             * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.alias.add)
             */
            add: function (params) { return self.call('fixturedefs.channel.capability.alias.add', params); },

            /**
             * Repoints an existing alias's targetMode/targetChannel.
             * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, capabilityIndex: integer, aliasIndex: integer, targetMode?: string, targetChannel?: string}
             * @returns {Promise<object>} result - {sessionId, sessionRevision}
             * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.alias.update)
             */
            update: function (params) { return self.call('fixturedefs.channel.capability.alias.update', params); },

            /**
             * Removes one alias from a capability by its positional index.
             * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, capabilityIndex: integer, aliasIndex: integer}
             * @returns {Promise<object>} result - {sessionId, sessionRevision}
             * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.alias.remove)
             */
            remove: function (params) { return self.call('fixturedefs.channel.capability.alias.remove', params); },

            /**
             * Creates one alias per mode containing channelId that doesn't already have one for
             * this capability, defaulting each to `targetChannel` (or the first available channel
             * if omitted). Modes that already have an alias here are skipped.
             * @param {object} params - {sessionId: string, baseRevision: integer, channelId: string, capabilityIndex: integer, targetChannel?: string}
             * @returns {Promise<object>} result - {sessionId, sessionRevision, addedCount: integer — aliases actually added}
             * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.channel.capability.alias.applyToAllModes)
             */
            applyToAllModes: function (params) { return self.call('fixturedefs.channel.capability.alias.applyToAllModes', params); }
          }
        }
      },

      // ================================================================
      // Mode CRUD + nested heads
      // ================================================================

      mode: {
        /**
         * Adds a new, empty mode. Assign channels afterwards with mode.setChannels.
         * @param {object} params - {sessionId: string, baseRevision: integer, name?: string (default 'New mode')}
         * @returns {Promise<object>} result - {sessionId, sessionRevision, modeId: string — server-assigned, stable for the session}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.add)
         */
        add: function (params) { return self.call('fixturedefs.mode.add', params); },

        /**
         * Renames a mode. Only ever renames — never touches channels/physical/heads, unlike a
         * true partial update. Renaming does NOT retarget any alias whose targetMode equals the
         * old name (aliases are name-addressed) — such aliases silently stop resolving until a
         * client re-points them via channel.capability.alias.update.
         * @param {object} params - {sessionId: string, baseRevision: integer, modeId: string, name: string}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.rename)
         */
        rename: function (params) { return self.call('fixturedefs.mode.rename', params); },

        /**
         * Removes a mode.
         * @param {object} params - {sessionId: string, baseRevision: integer, modeId: string}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.remove)
         */
        remove: function (params) { return self.call('fixturedefs.mode.remove', params); },

        /**
         * Replaces a mode's entire ordered channel slot list in one call (array index = DMX
         * offset within the mode). Deliberately collapses the desktop editor's fine-grained
         * add/move/delete/setActsOnChannel primitives into a single "set the whole list"
         * operation (see fixturedefs-notes.md) — matches how a drag-and-drop reorder is really
         * one user action. Every channelId must already exist in the definition's channel pool
         * (channel.add it first if needed). A slot's actsOnChannelId cannot equal its own
         * channelId — rejected immediately with FIXTUREDEFS_ACTS_ON_SELF (INVALID_PARAMS-class).
         * @param {object} params - {sessionId: string, baseRevision: integer, modeId: string, channels: Array<{channelId: string, actsOnChannelId?: string|null — the coarse channel this fine/secondary slot refines}>}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.setChannels)
         */
        setChannels: function (params) { return self.call('fixturedefs.mode.setChannels', params); },

        /**
         * Sets or clears a mode's physical-property override. useGlobalPhysical:true (physical
         * omitted) resets the mode to inherit the definition's global physical properties;
         * useGlobalPhysical:false with a `physical` object overrides them for this mode only.
         * @param {object} params - {sessionId: string, baseRevision: integer, modeId: string, useGlobalPhysical: boolean, physical?: FixtureDefsPhysical — required iff useGlobalPhysical is false}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.setPhysical)
         */
        setPhysical: function (params) { return self.call('fixturedefs.mode.setPhysical', params); },

        head: {
          /**
           * Adds a head (multi-beam channel grouping) to a mode. Every channelId must already be
           * one of the mode's assigned channels (see mode.setChannels). Heads are addressed by
           * channelId membership rather than a raw position index, so membership survives
           * channel-slot reordering.
           * @param {object} params - {sessionId: string, baseRevision: integer, modeId: string, channelIds: string[] (>=1)}
           * @returns {Promise<object>} result - {sessionId, sessionRevision, headIndex: integer — position within the mode's head list}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.head.add)
           */
          add: function (params) { return self.call('fixturedefs.mode.head.add', params); },

          /**
           * Batch-removes heads from a mode by positional index.
           * @param {object} params - {sessionId: string, baseRevision: integer, modeId: string, headIndexes: integer[] (>=1)}
           * @returns {Promise<object>} result - {sessionId, sessionRevision}
           * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.mode.head.remove)
           */
          remove: function (params) { return self.call('fixturedefs.mode.head.remove', params); }
        }
      },

      // ================================================================
      // Editing session lifecycle
      // ================================================================

      session: {
        /**
         * Creates a session around a brand-new, blank fixture definition.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {sessionId, definition: FixtureDefsDefinition, sessionRevision: integer (starts at 0), isUser: boolean, baseRevision: integer|null — null for a never-saved session}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.create)
         */
        create: function (params) { return self.call('fixturedefs.session.create', params); },

        /**
         * Opens an existing library definition for editing by cloning it into a new session.
         * @param {object} params - {manufacturer: string, model: string}
         * @returns {Promise<object>} result - {sessionId, definition: FixtureDefsDefinition, sessionRevision: integer, isUser: boolean, baseRevision: integer|null — the library's defRevision when opened}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.open)
         */
        open: function (params) { return self.call('fixturedefs.session.open', params); },

        /**
         * Loads a raw QXF file's bytes into a brand-new session (Avolites D4 .d4 import is out of
         * scope for this API).
         * @param {object} params - {fileName: string, qxfBase64: string — base64-encoded raw bytes of a .qxf XML file}
         * @returns {Promise<object>} result - {sessionId, definition: FixtureDefsDefinition, sessionRevision: integer, isUser: boolean, baseRevision: integer|null}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.import)
         */
        import: function (params) { return self.call('fixturedefs.session.import', params); },

        /**
         * Discards a session's working copy without saving. Any unsaved edits are lost.
         * @param {object} params - {sessionId: string}
         * @returns {Promise<object>} result - {sessionId}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.close)
         */
        close: function (params) { return self.call('fixturedefs.session.close', params); },

        /**
         * Lists every currently open editing session from every connected client.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {sessions: Array<{sessionId, manufacturer, model, isUser, isModified: boolean — diverged from disk since opened/last saved, sessionRevision, baseRevision: integer|null}>}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.list)
         */
        list: function (params) { return self.call('fixturedefs.session.list', params); },

        /**
         * Turns a session opened from a bundled/system (read-only) definition into a user-owned
         * copy that can be saved — required before fixturedefs.save whenever the session's isUser
         * is currently false (see FIXTUREDEFS_SYSTEM_READONLY in fixturedefs-notes.md).
         * @param {object} params - {sessionId: string, baseRevision: integer — sessionRevision last observed for this session}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.forkToUser)
         */
        forkToUser: function (params) { return self.call('fixturedefs.session.forkToUser', params); },

        /**
         * Partial update of a session's top-level metadata — any subset of manufacturer/model/
         * type/author. `type` is one of QLCFixtureDef::FixtureType's engine wire strings (e.g.
         * "Moving Head", "Color Changer", "LED Bar (Pixels)", "Other", ...); any unrecognized
         * string silently maps to "Other".
         * @param {object} params - {sessionId: string, baseRevision: integer, manufacturer?: string, model?: string, type?: string, author?: string}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.update)
         */
        update: function (params) { return self.call('fixturedefs.session.update', params); },

        /**
         * Sets the definition's global physical properties. Per-mode overrides use
         * mode.setPhysical instead.
         * @param {object} params - {sessionId: string, baseRevision: integer, physical: FixtureDefsPhysical}
         * @returns {Promise<object>} result - {sessionId, sessionRevision}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.setPhysical)
         */
        setPhysical: function (params) { return self.call('fixturedefs.session.setPhysical', params); },

        /**
         * On-demand validation of a session without saving (the same checks fixturedefs.save runs
         * automatically). Useful for a live "ready to save" UI badge. Warnings are never blocking.
         * @param {object} params - {sessionId: string}
         * @returns {Promise<object>} result - {warnings: string[] — e.g. 'No channels provided', 'Empty capability description in channel X', 'Mode Y has no channels defined', 'No modes provided'}
         * @see docs/api-spec/fragments/fixturedefs.yaml (method: fixturedefs.session.validate)
         */
        validate: function (params) { return self.call('fixturedefs.session.validate', params); }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'fixtureDefs', {
    configurable: true,
    get: function () { return ns(this); }
  });

  /**
   * Event topics broadcast by this domain. All are delivered to every connected client
   * unconditionally (structural/library events, not high-frequency) — fixturedefs-notes.md
   * explicitly flags this domain as not needing any subscribe-gated (§5) topics.
   */
  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.fixtureDefs = [
    'fixturedefs.deleted',         // {manufacturer, model} — after fixturedefs.delete
    'fixturedefs.session.opened',  // {sessionId, source:'created'|'opened'|'imported', definition, sessionRevision, isUser, baseRevision} — after session.create/open/import
    'fixturedefs.session.closed',  // {sessionId} — after session.close
    'fixturedefs.session.updated', // {sessionId, sessionRevision, changeKind, definition} — after EVERY session mutation (metadata/physical/channel/capability/alias/mode/head/forkToUser); full definition snapshot, not a JSON Patch
    'fixturedefs.saved'            // {sessionId, manufacturer, model, defRevision, definition, warnings} — after fixturedefs.save
  ];
})();
