/**
 * core domain — qlc.core.*
 * Generated from docs/api-spec/fragments/core.yaml (+ core-notes.md) in the QLC+ fork repo
 * (read-only source, a moving target — re-verify against the live spec if something here looks
 * stale later). Covers Doc/project lifecycle, Design/Operate mode, undo/redo (Tardis), and
 * global engine settings. The generic log/error event stream (core.log*) is documented here as
 * topics only — it has no request/response method, just subscribe-gated events.
 * Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's.
 *
 * Naming: core.history.get / core.undo / core.redo stay flat (historyGet/undo/redo) rather than
 * nesting under a `history` object, since undo/redo have no further segments to share with it.
 * core.project.*, core.mode.*, core.settings.* nest under project/mode/settings respectively.
 */
(function () {
  'use strict';
  function ns(self) {
    return {
      /**
       * Lists the shared undo/redo stack (one Tardis instance for the whole engine, not
       * per-client). Reset to empty by project.new/open/close.
       * @param {object} [params] - {limit?: integer — max entries, most recent first; server may cap regardless}
       * @returns {Promise<object>} result - {entries: [{index, actionCode, actionName, tier:'structural'|'live', timestampMs}], canUndo: boolean, canRedo: boolean}
       * @see docs/api-spec/fragments/core.yaml (method: core.history.get)
       */
      historyGet: function (params) { return self.call('core.history.get', params); },

      /**
       * Undoes the last (or last N) recorded action batch(es), global to the engine regardless
       * of which client originally made the change. No baseRevision — undoes whatever is on top
       * of the stack right now. Also fires the affected domain's own structural-change event(s)
       * alongside core.history.changed, since applying a reverted action re-invokes the same
       * engine setters a normal request would.
       * @param {object} [params] - {steps?: integer >=1, default 1 — action batches to undo}
       * @returns {Promise<object>} result - {docRevision: integer, stepsApplied: integer (may be < requested if history ran out), canUndo: boolean, canRedo: boolean}
       * @see docs/api-spec/fragments/core.yaml (method: core.undo)
       */
      undo: function (params) { return self.call('core.undo', params); },

      /**
       * Redoes the last (or last N) undone action batch(es). Same semantics as undo(), reversed.
       * @param {object} [params] - {steps?: integer >=1, default 1}
       * @returns {Promise<object>} result - {docRevision: integer, stepsApplied: integer, canUndo: boolean, canRedo: boolean}
       * @see docs/api-spec/fragments/core.yaml (method: core.redo)
       */
      redo: function (params) { return self.call('core.redo', params); },

      project: {
        /**
         * Discards the current document (unsaved changes are lost) and replaces it with a
         * blank, never-saved project. No baseRevision — full-document replace, nothing to
         * rebase against. Full new state arrives via the core.project.loaded event
         * (reason:'new'), not this response.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.new)
         */
        new: function (params) { return self.call('core.project.new', params); },

        /**
         * Opens a .qxw project, discarding the current document. Two transfer modes: "path"
         * (engine reads a file already on the machine the engine runs on) or "upload" (client
         * sends the whole XML inline, base64). Exactly one of path/contentBase64 is required,
         * matching source. No baseRevision — full-document replace. Full new state arrives via
         * the core.project.loaded event (reason:'opened').
         * @param {object} params - {source: 'path'|'upload', path?: string (required if source='path'), fileName?: string (required if source='upload'), contentBase64?: string (required if source='upload')}
         * @returns {Promise<object>} result - {docRevision: integer, warningsHtml: string|null — non-fatal load problems, HTML-formatted; also emitted individually as core.log warning events}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.open)
         */
        open: function (params) { return self.call('core.project.open', params); },

        /**
         * Closes the current project. The engine always has *a* document open, so this is
         * defined as discard-and-replace-with-blank, identical to project.new, just exposed
         * under its own name for UI-affordance clarity. Full new state arrives via the
         * core.project.loaded event (reason:'closed').
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {docRevision: integer}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.close)
         */
        close: function (params) { return self.call('core.project.close', params); },

        /**
         * Saves to the file the document was last opened/saved-as from. No baseRevision —
         * saving serializes current state to disk, it does not mutate document content.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {docRevision: integer (unchanged by a save, included for confirmation), filePath: string}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.save)
         */
        save: function (params) { return self.call('core.project.save', params); },

        /**
         * Saves the project to a new location. target:"serverPath" writes on the engine's own
         * filesystem at `path` (becomes the file for subsequent project.save calls). target:
         * "download" does not touch the server filesystem — it hands the serialized bytes back
         * in the response for the client to save via its own native dialog; the engine's
         * "current file" is left untouched in this case, and no core.project.saved event fires.
         * CAVEAT: as of the last check, target:"download" is an unimplemented stub server-side
         * (returns an error) — still wrapped normally per spec, since it should work once the
         * server implements it.
         * @param {object} params - {target: 'serverPath'|'download', path?: string (required if target='serverPath'; .qxw appended if missing)}
         * @returns {Promise<object>} result - {docRevision: integer, filePath: string|null (set iff target='serverPath'), fileName: string|null (set iff target='download'), contentBase64: string|null (set iff target='download')}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.saveAs)
         */
        saveAs: function (params) { return self.call('core.project.saveAs', params); },

        /**
         * Current project metadata (path, modified flag, revision, creator info).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {filePath: string|null, fileName: string|null, isModified: boolean, docRevision: integer, docRevisionAtLastSave: integer|null, creator: {name, version, author}|null — read-only, informational}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.get)
         */
        get: function (params) { return self.call('core.project.get', params); },

        /**
         * Most-recently-opened-first list of recent project files, capped at 10.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {files: [{filePath: string, fileName: string}]}
         * @see docs/api-spec/fragments/core.yaml (method: core.project.recentFiles)
         */
        recentFiles: function (params) { return self.call('core.project.recentFiles', params); }
      },

      bpm: {
        /**
         * Global beat generator state (web UI contract, 2026-09; not in the original core.yaml).
         * Rejects NOT_FOUND "Unknown method" on a server predating it — probe once on 'ready'.
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {bpm: integer (0 = generator off), generator: string, beatsPerBar?: integer}
         */
        get: function (params) { return self.call('core.bpm.get', params); },
        /**
         * Sets the global BPM. Live/runtime, no baseRevision. Confirmation is the broadcast
         * core.bpm.changed ({bpm, generator}); a live beat pulse is the payload-less core.beat event.
         * @param {number} bpm - beats per minute
         */
        set: function (bpm) { return self.call('core.bpm.set', { bpm: bpm }); },
        /**
         * Tap tempo: successive taps compute a new BPM from the interval average (mirrors
         * VCSpeedDial's tap with controlBPM). -> ack; broadcasts core.bpm.changed.
         */
        tap: function () { return self.call('core.bpm.tap', {}); }
      },

      mode: {
        /**
         * Current engine Design/Operate mode (Doc::Mode).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {mode: 'design'|'operate'}
         * @see docs/api-spec/fragments/core.yaml (method: core.mode.get)
         */
        get: function (params) { return self.call('core.mode.get', params); },

        /**
         * Sets the engine Design/Operate mode. Live/runtime state (never written to the .qxw
         * file), so no baseRevision. Switching to "operate" auto-starts the project's startup
         * function if one is set; switching away from "operate" has no automatic side effect
         * (running functions keep running). Fire-and-forget: the ack response carries no data,
         * the real confirmation is the broadcast core.mode.changed event.
         * @param {object} params - {mode: 'design'|'operate'}
         * @returns {QLCPlusAPI} self, for chaining — fire-and-forget: this returns `this`, not a
         *   Promise (no `.then()`). Listen for the 'core.mode.changed' event for confirmation;
         *   errors surface via the 'apiError' event instead of a rejection.
         * @see docs/api-spec/fragments/core.yaml (method: core.mode.set)
         */
        set: function (params) { return self.send('core.mode.set', params); }
      },

      settings: {
        /**
         * Global engine settings (not per-project).
         * @param {object} [params] - {} (no fields)
         * @returns {Promise<object>} result - {locale: string|null, defaultWorkingPath: string|null, masterTimerFrequencyHz: integer}
         * @see docs/api-spec/fragments/core.yaml (method: core.settings.get)
         */
        get: function (params) { return self.call('core.settings.get', params); },

        /**
         * Partial update of global engine settings — only include fields being changed
         * (minProperties: 1). Persisted immediately. Note: masterTimerFrequencyHz is read once
         * at engine start (MasterTimer construction) — a change here takes effect on next
         * engine start, not live. locale governs only engine-generated log/error string
         * language, not client UI chrome.
         * @param {object} params - {locale?: string|null, defaultWorkingPath?: string|null, masterTimerFrequencyHz?: integer}
         * @returns {Promise<object>} result - {locale: string|null, defaultWorkingPath: string|null, masterTimerFrequencyHz: integer} — full settings object after the update
         * @see docs/api-spec/fragments/core.yaml (method: core.settings.set)
         */
        set: function (params) { return self.call('core.settings.set', params); }
      },

      fs: {
        /**
         * Read-only listing of one directory on the machine the engine runs on, for server-side
         * file pickers (core.project.open path, functions.audio/video.setSource, ...). Empty /
         * omitted path lists the roots (home + drives) with no entries; every response carries
         * `roots` too. Directories always pass the extension filter.
         * @param {object} [params] - {path?: string — absolute host path, extensions?: string[] — glob patterns like '*.mp3', includeFiles?: boolean — default true}
         * @returns {Promise<object>} result - {path, parent: string|null, entries: [{name, path, isDir, size, mtime}], roots: [{name, path}]}
         * @see docs/api-spec/fragments/core.yaml (method: core.fs.list); errors: INVALID_PARAMS (relative path), NOT_FOUND (missing / not a directory)
         */
        list: function (params) { return self.call('core.fs.list', params || {}); }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'core', {
    configurable: true,
    get: function () { return ns(this); }
  });

  /**
   * Event topics broadcast by this domain. All are delivered to every connected client
   * unconditionally (structural events, §4a of 00-conventions.md) EXCEPT the core.log* family,
   * which is subscribe-gated (call qlc.subscribe(['core.log']) for a firehose, or one of the
   * per-level topics to filter volume — see core.yaml's CoreLogEvent description).
   */
  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.core = [
    'core.project.loaded',           // {reason:'new'|'opened'|'closed', project: CoreProjectMetadata}
    'core.project.saved',            // {docRevision, filePath, fileName} — NOT fired for saveAs target='download'
    'core.project.recentFilesChanged', // {files: [{filePath, fileName}]}
    'core.mode.changed',             // {mode:'design'|'operate'}
    'core.history.changed',          // {direction:'undo'|'redo', stepsApplied, docRevision, canUndo, canRedo, undoText?, redoText?}
    'core.bpm.changed',              // {bpm, generator} — web UI contract (2026-09)
    'core.beat',                     // {} — one pulse per beat of the active generator; web UI contract (2026-09)
    'core.settings.changed',         // CoreSettings — {locale, defaultWorkingPath, masterTimerFrequencyHz}
    'core.log',                      // subscribe-gated firehose — {level, message, timestampMs, file?, line?, function?}
    'core.log.debug',                // subscribe-gated, per-level filter of core.log
    'core.log.info',                 // subscribe-gated, per-level filter of core.log
    'core.log.warning',              // subscribe-gated, per-level filter of core.log
    'core.log.critical'              // subscribe-gated, per-level filter of core.log (fatal messages also land here — no separate core.log.fatal topic)
  ];
})();
