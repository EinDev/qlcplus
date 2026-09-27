/**
 * functions-core domain — qlc.functions.*
 * Generated from docs/api-spec/fragments/functions-core.yaml in the QLC+ fork repo (read-only
 * source, a moving target — re-verify against the live spec if something here looks stale).
 * Companion notes: docs/api-spec/fragments/functions-core-notes.md in that repo.
 * Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's.
 *
 * Covers the generic Function base API (list/get/create/delete/rename/move/update/start/stop/
 * setPause/tap/adjustAttribute) plus type-specific CRUD for Scene, Chaser, EFX, Collection,
 * Sequence, and the auxiliary ChannelsGroup resource. Script/RGBMatrix/Show/Audio/Video live in
 * the sibling functions-advanced.yaml fragment (wrapped separately as qlc.functionsAdvanced) —
 * NOT duplicated here even though they share the functions.* wire prefix.
 *
 * Revision model (see 00-conventions.md §4a): every structural mutation here takes a required
 * `baseRevision` (the last-observed docRevision) and its OkResponse result is just
 * `{docRevision: <int>}` (or `{functionId, docRevision}` for create, `{channelsGroupId,
 * docRevision}` for channelsgroup.create) — never the full new resource. Apply state from the
 * broadcast event, not the response, per convention. `start/stop/setPause/tap/adjustAttribute/
 * chaser.setAction` are live (§4b) actions with no baseRevision, a bare ack result, and are
 * `start/stop/setPause` and `chaser.setAction` are wrapped with `send()` (fire-and-forget,
 * returns `this`) since their effect is observable via the `functions.status.changed` event
 * instead. `tap` and `adjustAttribute` are also live (§4b) but stay `call()`-based: the spec
 * documents no confirming event for either (tap has none; adjustAttribute writes a persisted
 * base value with no baseRevision and isn't reflected by `functions.updated` or a running-only
 * `status.changed`), and a caller plausibly wants the server-side clamp/INVALID_PARAMS outcome.
 */
(function () {
  'use strict';

  function ns(self) {
    return {
      /**
       * List every Function (all 10 types), optionally filtered.
       * @param {object} [params] - {typeFilter?: string[] (FunctionsTypeEnum), pathFilter?: string}
       * @returns {Promise<object>} result - {functions: [{id, name, type, path, hidden}]}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.list)
       */
      list: function (params) { return self.call('functions.list', params); },

      /**
       * Fetch one Function's full detail, including its type-specific typeDetail branch.
       * @param {object} params - {functionId: string}
       * @returns {Promise<object>} result - FunctionsDetail: {id, name, type, path, hidden,
       *   runOrder, direction, tempoType, fadeInSpeed, fadeOutSpeed, duration, totalDuration,
       *   blendMode, attributes: [{index, name, value, min, max, overridden?, overrideValue?,
       *   flags?}], typeDetail: <Scene|Chaser|EFX|Collection|Sequence|...-specific object>}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.get)
       */
      get: function (params) { return self.call('functions.get', params); },

      /**
       * Create a new Function. Creating type=Sequence also auto-creates a hidden bound Scene
       * server-side (no sceneId param here — read it back via get(), or repoint later with
       * sequence.setBoundScene).
       * @param {object} params - {type: string (FunctionsTypeEnum), name?: string, path?: string,
       *   fixtures?: string[] (seed fixture IDs), baseRevision: int}
       * @returns {Promise<object>} result - {functionId, docRevision}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.create)
       */
      create: function (params) { return self.call('functions.create', params); },

      /**
       * Delete a Function.
       * @param {object} params - {functionId: string, baseRevision: int}
       * @returns {Promise<object>} result - {docRevision}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.delete)
       */
      delete: function (params) { return self.call('functions.delete', params); },

      /**
       * Rename a Function.
       * @param {object} params - {functionId: string, name: string, baseRevision: int}
       * @returns {Promise<object>} result - {docRevision}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.rename)
       */
      rename: function (params) { return self.call('functions.rename', params); },

      /**
       * Move one or more Functions to a new folder path (multi-select move).
       * @param {object} params - {functionIds: string[], path: string (bare form, no
       *   "<Type>/" prefix — see FunctionsSummary.path), baseRevision: int}
       * @returns {Promise<object>} result - {docRevision}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.move)
       */
      move: function (params) { return self.call('functions.move', params); },

      /**
       * Partially update a Function's generic (non-type-specific) attributes.
       * @param {object} params - {functionId: string, runOrder?: string, direction?: string,
       *   tempoType?: string, fadeInSpeed?: int, fadeOutSpeed?: int, duration?: int,
       *   blendMode?: string (FunctionsBlendModeEnum), baseRevision: int}
       * @returns {Promise<object>} result - {docRevision}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.update)
       */
      update: function (params) { return self.call('functions.update', params); },

      /**
       * Start (run) a Function. Live action (§4b) — result is a bare ack; watch
       * functions.status.changed for the effect.
       * @param {object} params - {functionId: string, overrideFadeIn?: int, overrideFadeOut?: int,
       *   overrideDuration?: int, overrideTempoType?: string} (overrides are one-shot, not persisted)
       * @returns {QLCPlusAPI} this - fire-and-forget; errors surface via the 'apiError' event
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.start)
       */
      start: function (params) { return self.send('functions.start', params); },

      /**
       * Stop a running Function. Live action (§4b) — result is a bare ack.
       * @param {object} params - {functionId: string, preserveAttributes?: boolean (default false)}
       * @returns {QLCPlusAPI} this - fire-and-forget; errors surface via the 'apiError' event
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.stop)
       */
      stop: function (params) { return self.send('functions.stop', params); },

      /**
       * Pause/resume a running Function. Live action (§4b) — result is a bare ack.
       * @param {object} params - {functionId: string, paused: boolean}
       * @returns {QLCPlusAPI} this - fire-and-forget; errors surface via the 'apiError' event
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.setPause)
       */
      setPause: function (params) { return self.send('functions.setPause', params); },

      /**
       * Tap-tempo, meaningful mainly for a running Chaser/Sequence. Live action (§4b), but no
       * confirming event exists in the spec for it — uses call() so failures are observable.
       * @param {object} params - {functionId: string}
       * @returns {Promise<object>} result - {} (ack)
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.tap)
       */
      tap: function (params) { return self.call('functions.tap', params); },

      /**
       * Write a Function attribute's persisted base value (not a runtime override — see
       * FunctionsAttribute.overridden/overrideValue for the read-only override telemetry).
       * Exactly one of attributeIndex/attributeName is required. Live action (§4b, no
       * baseRevision), but not reflected by functions.updated or a running-only status.changed —
       * uses call() so the clamp / INVALID_PARAMS outcome is observable.
       * @param {object} params - {functionId: string, attributeIndex?: int, attributeName?: string,
       *   value: number (clamped server-side to the attribute's own min/max)}
       * @returns {Promise<object>} result - {} (ack)
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.adjustAttribute)
       */
      adjustAttribute: function (params) { return self.call('functions.adjustAttribute', params); },

      /**
       * Copy functions ("<name> (Copy)", same folder; a Sequence gets its own bound Scene copy).
       * @param {object} params - {functionIds: string[], baseRevision: int}
       * @returns {Promise<object>} result - {functionIds: string[] (new ids), docRevision}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.clone)
       */
      clone: function (params) { return self.call('functions.clone', params); },

      /**
       * Who uses a function: referencing functions (+ step / position) and VC widgets.
       * @param {object} params - {functionId: string}
       * @returns {Promise<object>} result - {functions: [{functionId, name, type, position}],
       *   widgets: VcWidgetSnapshot[], vcAvailable, isStartupFunction}
       * @see docs/api-spec/fragments/functions-core.yaml (method: functions.usage)
       */
      usage: function (params) { return self.call('functions.usage', params); },

      scene: {
        /**
         * Full replacement of a Scene's channel value list.
         * @param {object} params - {functionId: string, values: object (map of "<fixtureId>.<channel>"
         *   -> 0-255), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.scene.setValues)
         */
        setValues: function (params) { return self.call('functions.scene.setValues', params); },

        /**
         * Set one channel value in a Scene.
         * @param {object} params - {functionId: string, fixture: string, channel: int (>=0),
         *   value: int (0-255), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.scene.setValue)
         */
        setValue: function (params) { return self.call('functions.scene.setValue', params); },

        /**
         * Remove one channel value from a Scene.
         * @param {object} params - {functionId: string, fixture: string, channel: int (>=0),
         *   baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.scene.unsetValue)
         */
        unsetValue: function (params) { return self.call('functions.scene.unsetValue', params); },

        /**
         * Full replacement of a Scene's non-value membership lists. Omitted properties are
         * left unchanged.
         * @param {object} params - {functionId: string, fixtures?: string[], fixtureGroups?: string[],
         *   channelGroups?: [{id: string, level: int (0-255)}], palettes?: string[] (opaque
         *   Palette ids — palette definitions themselves are out of scope for this fragment),
         *   baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.scene.setMembers)
         */
        setMembers: function (params) { return self.call('functions.scene.setMembers', params); }
      },

      chaser: {
        /**
         * Set a Chaser/Sequence's fadeIn/fadeOut/duration speed modes.
         * @param {object} params - {functionId: string, fadeInMode?: string (FunctionsSpeedModeEnum),
         *   fadeOutMode?: string, durationMode?: string, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.chaser.setSpeedModes)
         */
        setSpeedModes: function (params) { return self.call('functions.chaser.setSpeedModes', params); },

        /**
         * Live playback control for a running Chaser (also drives a running Sequence, which
         * shares Chaser's runner). Live action (§4b, no baseRevision) — bare ack.
         * @param {object} params - {functionId: string, action: 'nextStep'|'previousStep'|
         *   'setStepIndex'|'stopStep'|'pause', stepIndex?: int (required when action is
         *   setStepIndex), masterIntensity?: number (0-1), stepIntensity?: number (0-1),
         *   fadeMode?: string (FunctionsFadeControlModeEnum)}
         * @returns {QLCPlusAPI} this - fire-and-forget; errors surface via the 'apiError' event
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.chaser.setAction)
         */
        setAction: function (params) { return self.send('functions.chaser.setAction', params); }
      },

      efx: {
        /**
         * Partial update of an EFX's pattern/geometry parameters. Omitted properties are left
         * unchanged.
         * @param {object} params - {functionId: string, algorithm?: string (FunctionsEfxAlgorithmEnum),
         *   propagationMode?: string (FunctionsEfxPropagationModeEnum), width?: int (0-127),
         *   height?: int (0-127), rotation?: int (0-359), startOffset?: int (0-359),
         *   isRelative?: boolean, xOffset?: int (0-255), yOffset?: int (0-255),
         *   xFrequency?: int (0-32), yFrequency?: int (0-32), xPhase?: int (0-359),
         *   yPhase?: int (0-359), dimmerControlEnabled?: boolean, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.setParameters)
         */
        setParameters: function (params) { return self.call('functions.efx.setParameters', params); },

        /**
         * Add a fixture (head) as an EFX participant.
         * @param {object} params - {functionId: string, fixture: string, head?: int (default 0),
         *   baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.addFixture)
         */
        addFixture: function (params) { return self.call('functions.efx.addFixture', params); },

        /**
         * Remove a fixture (head) from an EFX.
         * @param {object} params - {functionId: string, fixture: string, head: int, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.removeFixture)
         */
        removeFixture: function (params) { return self.call('functions.efx.removeFixture', params); },

        /**
         * Set one EFX fixture's own direction/offset/mode.
         * @param {object} params - {functionId: string, fixture: string, head: int,
         *   direction?: string (FunctionsDirectionEnum), startOffset?: int,
         *   mode?: string (FunctionsEfxFixtureModeEnum: 'PanTilt'|'Dimmer'|'RGB'), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.setFixtureParameters)
         */
        setFixtureParameters: function (params) { return self.call('functions.efx.setFixtureParameters', params); },

        /**
         * Reorder a fixture's position among EFX participants. Only meaningful with Serial
         * propagation mode.
         * @param {object} params - {functionId: string, fixture: string, head: int,
         *   move: 'raise'|'lower', baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.reorderFixture)
         */
        reorderFixture: function (params) { return self.call('functions.efx.reorderFixture', params); },

        /**
         * Bulk start-offset assignment over every participant (the Qt editor's "Set an offset on
         * all fixtures" popup).
         * @param {object} params - {functionId: string, offset: int (0-360),
         *   mode?: 'Absolute'|'Increasing'|'Random' (default Increasing), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.setFixturesOffset)
         */
        setFixturesOffset: function (params) { return self.call('functions.efx.setFixturesOffset', params); },

        /**
         * Read-only preview data: the 512-point pattern polygon in 0-255 pan/tilt space plus each
         * participant's start index / walking direction along it (what EFXPreview.qml animates).
         * @param {object} params - {functionId: string, includeFixturePaths?: boolean}
         * @returns {Promise<object>} result - {functionId, pattern: [[x, y], ...],
         *   fixtures: [{fixture, head, startIndex, step: 1|-1, path?: [[x, y], ...]}]}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.efx.getPreview)
         */
        getPreview: function (params) { return self.call('functions.efx.getPreview', params); }
      },

      collection: {
        /**
         * Full replacement of a Collection's member function ID list, in run order.
         * @param {object} params - {functionId: string, functions: string[], baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.collection.setMembers)
         */
        setMembers: function (params) { return self.call('functions.collection.setMembers', params); },

        /**
         * Add one member function to a Collection.
         * @param {object} params - {functionId: string, memberFunctionId: string,
         *   index?: int (insertion index; omit/-1 to append), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.collection.addFunction)
         */
        addFunction: function (params) { return self.call('functions.collection.addFunction', params); },

        /**
         * Remove one member function from a Collection.
         * @param {object} params - {functionId: string, memberFunctionId: string, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.collection.removeFunction)
         */
        removeFunction: function (params) { return self.call('functions.collection.removeFunction', params); }
      },

      sequence: {
        /**
         * Repoint a Sequence's bound Scene.
         * @param {object} params - {functionId: string, sceneId: string, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.sequence.setBoundScene)
         */
        setBoundScene: function (params) { return self.call('functions.sequence.setBoundScene', params); },

        /**
         * Capture a set of (live) DMX values into a Sequence step — the typical "record current
         * state as a cue" action. Values are normalized against the bound Scene's channel set.
         * @param {object} params - {functionId: string, values: object (FunctionsSceneValues map,
         *   "<fixtureId>.<channel>" -> 0-255), targetStepIndex?: int (omit to append a new step),
         *   baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.sequence.applyDumpValues)
         */
        applyDumpValues: function (params) { return self.call('functions.sequence.applyDumpValues', params); }
      },

      channelsgroup: {
        /**
         * List every ChannelsGroup (auxiliary resource, not itself a Function).
         * @param {object} [params] - {}
         * @returns {Promise<object>} result - {channelsGroups: [{id, name, channelCount, level}]}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.list)
         */
        list: function (params) { return self.call('functions.channelsgroup.list', params); },

        /**
         * Fetch one ChannelsGroup's full detail.
         * @param {object} params - {channelsGroupId: string}
         * @returns {Promise<object>} result - {id, name, channels: [{fixture, channel}],
         *   level: int (0-255), inputSource?: {universe, channel}|null}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.get)
         */
        get: function (params) { return self.call('functions.channelsgroup.get', params); },

        /**
         * Create a new ChannelsGroup.
         * @param {object} params - {name?: string (defaults to 'New Group'),
         *   channels?: [{fixture: string, channel: int}], baseRevision: int}
         * @returns {Promise<object>} result - {channelsGroupId, docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.create)
         */
        create: function (params) { return self.call('functions.channelsgroup.create', params); },

        /**
         * Rename a ChannelsGroup.
         * @param {object} params - {channelsGroupId: string, name: string, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.rename)
         */
        rename: function (params) { return self.call('functions.channelsgroup.rename', params); },

        /**
         * Delete a ChannelsGroup.
         * @param {object} params - {channelsGroupId: string, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.delete)
         */
        delete: function (params) { return self.call('functions.channelsgroup.delete', params); },

        /**
         * Full replacement of a ChannelsGroup's channel list.
         * @param {object} params - {channelsGroupId: string,
         *   channels: [{fixture: string, channel: int}], baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.setChannels)
         */
        setChannels: function (params) { return self.call('functions.channelsgroup.setChannels', params); },

        /**
         * Set a ChannelsGroup's master level. NOTE: unlike a live fader, this is persisted
         * document state (§4a) — baseRevision is required and every call is revision-gated
         * (see functions-core-notes.md for the fast-drag/conflict-storm tradeoff this implies
         * if bound to a VC slider).
         * @param {object} params - {channelsGroupId: string, level: int (0-255), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.setLevel)
         */
        setLevel: function (params) { return self.call('functions.channelsgroup.setLevel', params); },

        /**
         * Assign (or clear, with null) a ChannelsGroup's external input source.
         * @param {object} params - {channelsGroupId: string,
         *   inputSource?: {universe: int, channel: int}|null, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.channelsgroup.setInputSource)
         */
        setInputSource: function (params) { return self.call('functions.channelsgroup.setInputSource', params); }
      },

      steps: {
        /**
         * Insert a step into a Chaser or Sequence (target type resolved server-side from
         * functionId — step's shape must match: FunctionsChaserStep for a Chaser,
         * FunctionsSequenceStep for a Sequence).
         * @param {object} params - {functionId: string, step: object (ChaserStep:
         *   {targetFunctionId, fadeIn, hold, fadeOut, duration, note?} or SequenceStep:
         *   {fadeIn, hold, fadeOut, duration, note?, values}), index?: int (insertion index;
         *   omit/-1 to append), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.steps.addStep)
         */
        addStep: function (params) { return self.call('functions.steps.addStep', params); },

        /**
         * Remove a step by index from a Chaser or Sequence.
         * @param {object} params - {functionId: string, index: int, baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.steps.removeStep)
         */
        removeStep: function (params) { return self.call('functions.steps.removeStep', params); },

        /**
         * Replace a step at an index in a Chaser or Sequence.
         * @param {object} params - {functionId: string, index: int, step: object (same shape as
         *   steps.addStep's step), baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.steps.replaceStep)
         */
        replaceStep: function (params) { return self.call('functions.steps.replaceStep', params); },

        /**
         * Move one step from one index to another. NOTE: bulk editor operations (multi-move,
         * duplicate, shuffle) have no batch method — they must be replicated as sequential
         * add/move/removeStep calls, each individually baseRevision-gated; a conflict mid-sequence
         * can leave a partially-applied result with no rollback.
         * @param {object} params - {functionId: string, sourceIndex: int, destIndex: int,
         *   baseRevision: int}
         * @returns {Promise<object>} result - {docRevision}
         * @see docs/api-spec/fragments/functions-core.yaml (method: functions.steps.moveStep)
         */
        moveStep: function (params) { return self.call('functions.steps.moveStep', params); }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'functions', {
    configurable: true,
    get: function () { return ns(this); }
  });

  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.functions = [
    'functions.created',
    'functions.deleted',
    'functions.renamed',
    'functions.moved',
    'functions.updated',
    /* Live (§4b) running/elapsed/attribute status. Broadcast unconditionally per the spec as
       read (not listed subscribe-gated in functions-core-notes.md), but the notes flag its
       per-tick emission frequency during playback as unconfirmed — a real subscribe-gate
       candidate the merge pass should verify. */
    'functions.status.changed',
    /* functions.adjustAttribute: {functionId, attributeIndex, attributeName, value} */
    'functions.attributeChanged',
    'functions.scene.valuesChanged',
    'functions.scene.membersChanged',
    /* Emitted by chaser.setSpeedModes only; steps.* on a Chaser emits chaser.stepsChanged instead. */
    'functions.chaser.changed',
    'functions.chaser.stepsChanged',
    'functions.efx.changed',
    'functions.efx.fixturesChanged',
    'functions.collection.membersChanged',
    'functions.sequence.changed',
    /* steps.* on a Sequence, and sequence.applyDumpValues, both emit this. */
    'functions.sequence.stepsChanged',
    'functions.channelsgroup.created',
    'functions.channelsgroup.renamed',
    'functions.channelsgroup.deleted',
    'functions.channelsgroup.changed'
  ];
})();
