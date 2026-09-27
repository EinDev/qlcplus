/**
 * functions-advanced domain — qlc.functionsAdvanced.* (audio/rgbmatrix/script/show/video
 * function sub-areas; wire methods are still functions.audio.*, functions.show.*, etc. —
 * only the JS namespace differs, to coexist with functions-core.js's `qlc.functions`).
 * Generated from docs/api-spec/fragments/functions-advanced.yaml in the QLC+ fork repo
 * (read-only source, a moving target — re-verify against the live spec if something here
 * looks stale later). Thin pass-through wrappers only: this.call(method, params) /
 * this.send(method, params). No field remapping — shapes here are exactly the spec's.
 *
 * Every mutation here is a document-state (§4a) change and requires `baseRevision` (the
 * docRevision last observed) in params, and can fail with error.code 'CONFLICT' (current
 * state in error.details) — call() is used throughout, even for mutations whose result is
 * just {docRevision}, so a caller can see that failure rather than have it silently
 * swallowed by send(). The generic function object (id/name/type/start/stop/rename/etc.)
 * lives in functions-core.js; this file only covers type-specific config and sub-resources
 * for the 5 subtypes not owned by functions-core: RGBMatrix, Script, Show (+ Track/Item),
 * Audio, Video.
 */
(function () {
  'use strict';
  function ns(self) {
    return {
      audio: {
        /**
         * Engine build's supported audio source file extensions (decoder capability list).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {extensions: string[] — glob patterns, e.g. '*.mp3', not bare extensions}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.audio.listCapabilities)
         */
        listCapabilities: function (params) { return self.call('functions.audio.listCapabilities', params); },

        /**
         * Sets the Audio function's output device (opaque host-side identifier). Broadcasts
         * functions.audio.deviceChanged.
         * @param {object} params - {functionId: string, audioDevice: string — empty string = use global default output, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.audio.setDevice)
         */
        setDevice: function (params) { return self.call('functions.audio.setDevice', params); },

        /**
         * Overrides the playback duration independent of the source file's own detected length.
         * Broadcasts functions.audio.durationChanged.
         * @param {object} params - {functionId: string, duration: integer — milliseconds, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.audio.setDuration)
         */
        setDuration: function (params) { return self.call('functions.audio.setDuration', params); },

        /**
         * Sets the Audio function's source file. Broadcasts functions.audio.sourceChanged
         * (carries the decoder-detected duration of the new file).
         * @param {object} params - {functionId: string, sourceFileName: string — path resolved on the engine host, not the client, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.audio.setSource)
         */
        setSource: function (params) { return self.call('functions.audio.setSource', params); },

        /**
         * Sets the function's persisted startup volume (0-1). For live fader-drag control while
         * playing, use functions-core's generic attribute-override mechanism instead. Broadcasts
         * functions.audio.volumeChanged.
         * @param {object} params - {functionId: string, volume: number — 0..1, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.audio.setVolume)
         */
        setVolume: function (params) { return self.call('functions.audio.setVolume', params); },
        /** {functionId, muted: bool, baseRevision} -> {docRevision}; broadcasts functions.audio.mutedChanged */
        setMuted: function (params) { return self.call('functions.audio.setMuted', params); },
        /** {functionId} -> {bpm: {state, value, confidence}}; the result follows as functions.audio.bpmChanged */
        detectBpm: function (params) { return self.call('functions.audio.detectBpm', params); }
      },

      media: {
        /**
         * Re-import an Audio/Video's managed copy from the file it was imported from (the
         * editors' Reload button); an external file is re-probed in place. Structural: needs
         * baseRevision, bumps docRevision only when something was actually re-pointed.
         * functions.media.reloaded follows (also for a queued background copy landing later).
         * @param {object} params - {functionId: string, baseRevision: integer}
         * @returns {Promise<object>} result - the refreshed Audio/Video typeDetail + {status: 'reloaded'|'unchanged'|'queued'|'missing'|'notManaged', error?: string}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.media.reload)
         */
        reload: function (params) { return self.call('functions.media.reload', params); },
        /** {} -> {storeDir, staging, external: [{path,size}], unused: [{path,size}], changed: [{functionId,name,type,running}]} */
        status: function (params) { return self.call('functions.media.status', params || {}); },
        /** {baseRevision} -> {copied, queued, failed, error, storeDir, docRevision}; broadcasts functions.media.collected */
        collect: function (params) { return self.call('functions.media.collect', params); },
        /** {files?: string[]} (omitted = all unused) -> {removed, complete, error, unused} */
        removeUnused: function (params) { return self.call('functions.media.removeUnused', params || {}); }
      },

      rgbmatrix: {
        /**
         * Live-rendered pixel frame for one algorithm step, at the bound Fixture Group's size —
         * a one-shot preview render (not a live/subscribable stream).
         * @param {object} params - {functionId: string, step: integer — 0-based, modulo result.stepsCount}
         * @returns {Promise<object>} result - {stepsCount: integer (0 without a fixture group), step: integer — the index actually rendered (params.step normalised), width: integer, height: integer, pixels: integer[][] — height rows x width cols of packed 0xRRGGBB (0 = off)}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.rgbmatrix.getPreview)
         */
        getPreview: function (params) { return self.call('functions.rgbmatrix.getPreview', params); },

        /**
         * UI-editable properties a given RGBScript algorithm exposes (values themselves live
         * per-function in the config's algorithm.scriptProperties).
         * @param {object} params - {scriptName: string — a name returned by listAlgorithms with type=script}
         * @returns {Promise<object>} result - {properties: [{name, displayName, type: 'list'|'range'|'float'|'string', listValues?: string[], rangeMin?: integer, rangeMax?: integer}]}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.rgbmatrix.getScriptProperties)
         */
        getScriptProperties: function (params) { return self.call('functions.rgbmatrix.getScriptProperties', params); },

        /**
         * Global catalog of built-in RGB algorithms and installed RGBScripts (not per-function).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {algorithms: [{name, type: 'plain'|'text'|'script'|'image'|'audio', apiVersion?: integer — type=script only, author?: string — type=script only, acceptedColors?: 0|1|2 — how many of config.colors' 5 slots this algorithm uses}]}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.rgbmatrix.listAlgorithms)
         */
        listAlgorithms: function (params) { return self.call('functions.rgbmatrix.listAlgorithms', params); },

        /**
         * Applies an RGBMatrix config (fixture group, algorithm, up to 5 direct hex color slots,
         * control mode, blend mode). The config may be partial: absent keys (and absent algorithm
         * parameters) are left unchanged. Broadcasts functions.rgbmatrix.configChanged with the
         * full new config read back from the engine.
         * @param {object} params - {functionId: string, config: {fixtureGroupId: string, algorithm: {type, scriptName?, scriptProperties?: [{name,value}], text?, font?: {family,pointSize,bold,italic}, imagePath?, animationStyle?, xOffset?, yOffset?}, colors: (string|null)[] — exactly 5 slots, controlMode: 'rgb'|'white'|'amber'|'uv'|'dimmer'|'shutter', blendMode?, dimmerControl?: boolean — legacy, superseded by controlMode=dimmer}, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.rgbmatrix.setConfig)
         */
        setConfig: function (params) { return self.call('functions.rgbmatrix.setConfig', params); },

        /**
         * Single-property convenience mutation for live-tweaking one script property without
         * resending the whole config. Broadcasts functions.rgbmatrix.scriptPropertyChanged.
         * @param {object} params - {functionId: string, propertyName: string, value: string — always string-encoded, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.rgbmatrix.setScriptProperty)
         */
        setScriptProperty: function (params) { return self.call('functions.rgbmatrix.setScriptProperty', params); },
        /** {functionId, baseRevision} -> {sequenceId, sceneId, stepsCount, docRevision} */
        saveToSequence: function (params) { return self.call('functions.rgbmatrix.saveToSequence', params); }
      },

      script: {
        /**
         * Appends one line verbatim to the end of the Script's source. Broadcasts
         * functions.script.sourceChanged with the full resulting source.
         * @param {object} params - {functionId: string, line: string — one script line, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.script.appendLine)
         */
        appendLine: function (params) { return self.call('functions.script.appendLine', params); },

        /**
         * Static keyword catalog for editor autocomplete (Script::*Cmd set: stoponexit,
         * startfunction, stopfunction, blackout, wait, waitkey, waitfunctionstart,
         * waitfunctionstop, setfixture, systemcommand, label, jump).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {commands: string[]}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.script.listCommands)
         */
        listCommands: function (params) { return self.call('functions.script.listCommands', params); },

        /**
         * Replaces the entire script body. Broadcasts functions.script.sourceChanged with the
         * full resulting source.
         * @param {object} params - {functionId: string, source: string, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.script.setSource)
         */
        setSource: function (params) { return self.call('functions.script.setSource', params); },

        /**
         * Server-side syntax check plus reference extraction. Note two DISTINCT line-numbering
         * spaces in the result: syntaxErrorLines is 1-based over the full raw source (including
         * blank lines — matches setSource's source.split('\n')); functionRefs[].line and
         * fixtureRefs[].line are 0-based over the engine's blank-line-skipping tokenized line
         * list — do not mix the two when highlighting source. functionRefs only recognizes
         * startfunction (a known under-reporting gap vs. stopfunction/waitfunctionstart/
         * waitfunctionstop, which the language supports but this call doesn't report).
         * @param {object} params - {functionId: string}
         * @returns {Promise<object>} result - {syntaxErrorLines: integer[], functionRefs: [{functionId, line}], fixtureRefs: [{fixtureId, line}]}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.script.validate)
         */
        validate: function (params) { return self.call('functions.script.validate', params); }
      },

      show: {
        /**
         * ADR 0001 legacy beat-pseudo-count timelines. get: {showId?, bpm?} -> {creatorVersion,
         * source: 'file'|'upload'|'none', shows: [{id, name, bpm, beatsDivision, itemCount}],
         * preview?: [{name, oldStart, newStart, oldDuration, newDuration}]}.
         * convert: {showId, bpm, baseRevision} -> {docRevision, itemsChanged} (functions.updated).
         * dismiss: {showId} -> {} ("already correct", until the next project load).
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.legacyTiming.*)
         */
        legacyTiming: {
          get: function (params) { return self.call('functions.show.legacyTiming.get', params || {}); },
          convert: function (params) { return self.call('functions.show.legacyTiming.convert', params); },
          dismiss: function (params) { return self.call('functions.show.legacyTiming.dismiss', params); }
        },

        /**
         * Removes $length ms at $cursorTime, pulling/shrinking every item covering or after that
         * point on every track. Broadcasts functions.show.itemsChanged (RFC 6902 patch against
         * tracks[].items).
         * @param {object} params - {showId: string, cursorTime: integer, length: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.rippleCutTime)
         */
        rippleCutTime: function (params) { return self.call('functions.show.rippleCutTime', params); },

        /**
         * Inserts $length ms at $cursorTime, pushing/stretching every item covering or after that
         * point on every track. Broadcasts functions.show.itemsChanged (RFC 6902 patch against
         * tracks[].items).
         * @param {object} params - {showId: string, cursorTime: integer, length: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.rippleInsertTime)
         */
        rippleInsertTime: function (params) { return self.call('functions.show.rippleInsertTime', params); },

        /**
         * Sets the Show's time division (time-based or BPM-quantized). `bpm` is required when
         * timeDivisionType is one of the bpm_4_4/bpm_3_4/bpm_2_4 values. Broadcasts
         * functions.show.timeDivisionChanged.
         * @param {object} params - {functionId: string, timeDivisionType: 'time'|'bpm_4_4'|'bpm_3_4'|'bpm_2_4', bpm?: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.setTimeDivision)
         */
        setTimeDivision: function (params) { return self.call('functions.show.setTimeDivision', params); },

        item: {
          /**
           * Places a generic function object onto a Show track's timeline. `duration` defaults
           * to the placed function's own totalDuration if omitted (and non-zero); if that
           * duration is 0 (e.g. a looping Chaser/Scene), the server falls back to a fixed
           * default (5000ms if timeDivisionType=time, 4000ms if beats). `color` defaults to
           * ShowFunction::defaultColor(type) if omitted. Broadcasts functions.show.item.added.
           * @param {object} params - {showId: string, trackId: string, functionId: string, startTime: integer — ms from Show start, duration?: integer — ms, color?: string — hex, baseRevision: integer}
           * @returns {Promise<object>} result - {itemId: string, docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.item.add)
           */
          add: function (params) { return self.call('functions.show.item.add', params); },

          /**
           * Moves an item to a (possibly different) track and/or start time. Rejected with
           * INVALID_PARAMS (not CONFLICT) if the destination would overlap an existing item on
           * the target track. Broadcasts functions.show.item.moved.
           * @param {object} params - {showId: string, itemId: string, trackId: string — destination track, same as current for a same-track move, startTime: integer, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.item.move)
           */
          move: function (params) { return self.call('functions.show.item.move', params); },

          /**
           * Batch-deletes one or more items. Broadcasts functions.show.item.removed.
           * @param {object} params - {showId: string, itemIds: string[], baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.item.remove)
           */
          remove: function (params) { return self.call('functions.show.item.remove', params); },

          /**
           * Changes an item's own timeline duration/trim only (stretch-mode off; does NOT
           * proportionally rescale an underlying Chaser's per-step durations — a client wanting
           * that must use functions.chaser.setSpeedModes plus per-step writes itself). Rejected
           * with INVALID_PARAMS (not CONFLICT) if it would overlap a following item. Broadcasts
           * functions.show.item.resized.
           * @param {object} params - {showId: string, itemId: string, duration: integer — new duration in ms, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.item.resize)
           */
          resize: function (params) { return self.call('functions.show.item.resize', params); },

          /**
           * Sets an item's timeline display color. Broadcasts functions.show.item.colorChanged.
           * @param {object} params - {showId: string, itemId: string, color: string — hex, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.item.setColor)
           */
          setColor: function (params) { return self.call('functions.show.item.setColor', params); },

          /**
           * Locks/unlocks an item against further edits. Broadcasts
           * functions.show.item.lockedChanged.
           * @param {object} params - {showId: string, itemId: string, locked: boolean, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.item.setLocked)
           */
          setLocked: function (params) { return self.call('functions.show.item.setLocked', params); }
        },

        track: {
          /**
           * Adds a new track to a Show, optionally bound to a Scene function (omit sceneId for
           * an audio/video-only track). Broadcasts functions.show.track.added.
           * @param {object} params - {showId: string, name?: string, sceneId?: string, baseRevision: integer}
           * @returns {Promise<object>} result - {trackId: string, docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.track.add)
           */
          add: function (params) { return self.call('functions.show.track.add', params); },

          /**
           * Moves a track up/down in the Show's track order. Broadcasts
           * functions.show.tracksChanged (RFC 6902 patch against /tracks).
           * @param {object} params - {showId: string, trackId: string, direction: 'up'|'down', baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.track.move)
           */
          move: function (params) { return self.call('functions.show.track.move', params); },

          /**
           * Removes a track from a Show. Broadcasts functions.show.track.removed.
           * @param {object} params - {showId: string, trackId: string, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.track.remove)
           */
          remove: function (params) { return self.call('functions.show.track.remove', params); },

          /**
           * Renames a track. Broadcasts functions.show.track.renamed.
           * @param {object} params - {showId: string, trackId: string, name: string, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.track.rename)
           */
          rename: function (params) { return self.call('functions.show.track.rename', params); },

          /**
           * Mutes/unmutes one track. Broadcasts functions.show.track.muteChanged.
           * @param {object} params - {showId: string, trackId: string, mute: boolean, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.track.setMute)
           */
          setMute: function (params) { return self.call('functions.show.track.setMute', params); },

          /**
           * Convenience mutation mirroring ShowManager::setTrackSolo: solo=true mutes every
           * other track and unmutes this one. solo=false is NOT the inverse — it unmutes EVERY
           * track in the Show (solo isn't a persisted flag, just sugar over multiple mute
           * writes); a caller wanting to restore the prior mute pattern must snapshot it itself
           * before soloing. Broadcasts functions.show.tracksChanged (RFC 6902 patch against
           * /tracks), since this can touch many tracks at once.
           * @param {object} params - {showId: string, trackId: string, solo: boolean, baseRevision: integer}
           * @returns {Promise<object>} result - {docRevision: integer}
           * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.show.track.setSolo)
           */
          setSolo: function (params) { return self.call('functions.show.track.setSolo', params); }
        }
      },

      video: {
        /**
         * Engine build's supported video/picture source extensions and the engine host's
         * available display screens (for setScreenTarget).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {videoExtensions: string[] — glob patterns, e.g. '*.mp4', pictureExtensions: string[] — glob patterns, screens: [{index, name}]}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.video.listCapabilities)
         */
        listCapabilities: function (params) { return self.call('functions.video.listCapabilities', params); },

        /**
         * Sets/clears the Video function's custom render geometry. Omitting `customGeometry`
         * leaves it unchanged; sending it as null clears it (renders at natural/fullscreen
         * size); sending an object sets it — omitted and null are NOT interchangeable.
         * Broadcasts functions.video.geometryChanged (always reports the full resulting value).
         * @param {object} params - {functionId: string, customGeometry?: {x,y,width,height}|null, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.video.setGeometry)
         */
        setGeometry: function (params) { return self.call('functions.video.setGeometry', params); },

        /**
         * Sets the Video function's stacking order (z-index) among other simultaneously-shown
         * Video functions. Broadcasts functions.video.layerChanged.
         * @param {object} params - {functionId: string, zIndex: integer, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.video.setLayer)
         */
        setLayer: function (params) { return self.call('functions.video.setLayer', params); },

        /**
         * Sets the designed/structural rotation (x/y/z degrees). Live per-axis override via a VC
         * control uses functions-core's generic attribute mechanism instead. Broadcasts
         * functions.video.rotationChanged.
         * @param {object} params - {functionId: string, rotation: {x: number, y: number, z: number}, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.video.setRotation)
         */
        setRotation: function (params) { return self.call('functions.video.setRotation', params); },

        /**
         * Sets which engine-host display this Video renders to. Broadcasts
         * functions.video.screenTargetChanged.
         * @param {object} params - {functionId: string, screen: integer — index into listCapabilities' screens array, fullscreen: boolean, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.video.setScreenTarget)
         */
        setScreenTarget: function (params) { return self.call('functions.video.setScreenTarget', params); },

        /**
         * Sets the Video function's source. Broadcasts functions.video.sourceChanged (carries
         * isPicture, detectedResolution, detectedDurationMs, videoCodec, audioCodec detected
         * from the new source).
         * @param {object} params - {functionId: string, sourceUrl: string — path/URL resolved on the engine host, not the client, baseRevision: integer}
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/functions-advanced.yaml (method: functions.video.setSource)
         */
        setSource: function (params) { return self.call('functions.video.setSource', params); },
        /** {functionId, volume: 0..1, baseRevision} -> {docRevision}; broadcasts functions.video.volumeChanged */
        setVolume: function (params) { return self.call('functions.video.setVolume', params); },
        /** {functionId, muted: bool, baseRevision} -> {docRevision}; broadcasts functions.video.mutedChanged */
        setMuted: function (params) { return self.call('functions.video.setMuted', params); },
        /** {functionId, width, height (0 = native), baseRevision} -> {docRevision}; broadcasts functions.video.spoutSizeChanged */
        setSpoutSize: function (params) { return self.call('functions.video.setSpoutSize', params); }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'functionsAdvanced', {
    configurable: true,
    get: function () { return ns(this); }
  });

  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.functionsAdvanced = [
    // rgbmatrix
    'functions.rgbmatrix.configChanged',
    'functions.rgbmatrix.scriptPropertyChanged',
    // script
    'functions.script.sourceChanged',
    // show — structural (broadcast unconditionally, not subscribe-gated)
    'functions.show.timeDivisionChanged',
    'functions.show.track.added',
    'functions.show.track.removed',
    'functions.show.track.renamed',
    'functions.show.track.muteChanged',
    'functions.show.tracksChanged',
    'functions.show.item.added',
    'functions.show.item.removed',
    'functions.show.item.moved',
    'functions.show.item.resized',
    'functions.show.item.colorChanged',
    'functions.show.item.lockedChanged',
    'functions.show.itemsChanged',
    // show — live/subscribe-gated (§4b). Actual topic is per-instance:
    // `functions.show.<functionId>.playhead` — subscribe to that exact string per show.
    'functions.show.playhead',
    // audio
    'functions.audio.sourceChanged',
    'functions.audio.volumeChanged',
    'functions.audio.durationChanged',
    'functions.audio.deviceChanged',
    // audio — live/subscribe-gated (§4b), per-instance: `functions.audio.<functionId>.playback`
    'functions.audio.playback',
    // video
    'functions.video.sourceChanged',
    'functions.video.geometryChanged',
    'functions.video.rotationChanged',
    'functions.video.layerChanged',
    'functions.video.screenTargetChanged',
    // video — live/subscribe-gated (§4b), per-instance: `functions.video.<functionId>.playback`
    'functions.video.playback',
    // audio + video media store: {functionId, source, managed, origin..., status:'reloaded'} after a
    // managed copy was re-imported (functions.media.reload, the QML editors' Reload button, or a
    // queued background copy landing) — originClientId is always null
    'functions.media.reloaded',
    'functions.media.collected',
    'functions.audio.bpmChanged',
    'functions.audio.mutedChanged',
    'functions.video.volumeChanged',
    'functions.video.mutedChanged',
    'functions.video.spoutSizeChanged'
  ];
})();
