/**
 * palette domain — qlc.palette.*
 * Generated from docs/api-spec/fragments/palette.yaml (+ palette-notes.md) in the QLC+ fork
 * repo (read-only source, a moving target — re-verify against the live spec if something here
 * looks stale later). Covers CRUD on `QLCPalette` definitions only (Doc::addPalette/deletePalette/
 * palette/palettes) — color *filters* (qmlui/colorfilters.cpp) are a separate engine concept and
 * explicitly out of scope here (see palette-notes.md). Also out of scope: fanning
 * (palette.setFanning-shaped follow-up), live-preview application of a palette's values to DMX
 * (valuesFromFixtures/valuesFromFixtureGroups/previewPalette — §4b runtime behavior, belongs to a
 * future live-preview/context domain), addPaletteToNewScene (functions.scene.* territory), and
 * isTemporary (an editing-buffer-only flag, irrelevant once a palette is committed).
 * Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's.
 *
 * Revisioning (00-conventions.md §4a): palettes are project/show-file state (Doc::m_palettes),
 * so all three mutations use the shared global `docRevision`/`baseRevision`, not a domain-local
 * counter. Each mutation's response result carries only `docRevision` (plus paletteId for
 * create); the broadcast event carries the full resource. `type` (PaletteTypeEnum) is fixed at
 * creation — palette.update never accepts it; delete and re-create instead.
 *
 * PaletteValues encoding (see PaletteValues in the yaml / palette-notes.md "Value encoding"): a
 * flat positional array, one element per stored value, meaning per `type`:
 *   Dimmer, Zoom:  [level]        — one float, 0.0-100.0 (percent)
 *   Color:         [packed]       — one string "#rrggbb" or "#rrggbbwwaauv" (QLCPalette::colorToString/stringToColor semantics — build/parse with those, not hand-rolled hex)
 *   Pan, Tilt:     [value]        — one integer, raw pan-or-tilt value
 *   PanTilt:       [pan, tilt]    — two integers, in that order
 *   Position3D:    [x, y, z]      — three floats
 *   Shutter:       [value1, value2] — two integers, undocumented meaning upstream; pass-through only
 *   Gobo:          generic pass-through — no defined shape upstream, round-tripped as-is
 * An empty/omitted `values` on palette.create means "use the engine's type-appropriate default"
 * (QLCPalette::resetValues()).
 */
(function () {
  'use strict';
  function ns(self) {
    return {
      /**
       * Lists every palette in the current project as lightweight summaries (id/name/type only —
       * no values). Use palette.get for a single palette's full values.
       * @param {object} [params] - {} (no fields)
       * @returns {Promise<object>} result - {palettes: Array<{id: integer, name: string, type: PaletteTypeEnum}>, docRevision: integer}
       * @see docs/api-spec/fragments/palette.yaml (method: palette.list)
       */
      list: function (params) { return self.call('palette.list', params); },

      /**
       * Fetches one palette's full detail, including its values.
       * @param {object} params - {paletteId: integer}
       * @returns {Promise<object>} result - {id: integer, name: string, type: PaletteTypeEnum, values: Array<number|string>}
       * @see docs/api-spec/fragments/palette.yaml (method: palette.get)
       */
      get: function (params) { return self.call('palette.get', params); },

      /**
       * Creates a new palette. `type` is fixed for the palette's lifetime (never editable via
       * palette.update — delete and re-create to change it). `values` is optional; omit it (or
       * pass an empty array) to get the type's engine default (QLCPalette::resetValues()).
       * @param {object} params - {type: PaletteTypeEnum ('Dimmer'|'Color'|'Pan'|'Tilt'|'PanTilt'|'Position3D'|'Shutter'|'Gobo'|'Zoom'), name: string, values?: Array<number|string> — see PaletteValues encoding above, baseRevision: integer — docRevision this client last observed}
       * @returns {Promise<object>} result - {paletteId: integer, docRevision: integer}
       * @see docs/api-spec/fragments/palette.yaml (method: palette.create)
       */
      create: function (params) { return self.call('palette.create', params); },

      /**
       * Partial update of a palette's name and/or values. `type` is immutable and not accepted
       * here (see module doc). Both `name` and `values` are optional; provide only what changed.
       * @param {object} params - {paletteId: integer, name?: string — renames when present, values?: Array<number|string> — replaces all values when present, see PaletteValues encoding above, baseRevision: integer}
       * @returns {Promise<object>} result - {docRevision: integer}
       * @see docs/api-spec/fragments/palette.yaml (method: palette.update)
       */
      update: function (params) { return self.call('palette.update', params); },

      /**
       * Deletes a palette.
       * @param {object} params - {paletteId: integer, baseRevision: integer}
       * @returns {Promise<object>} result - {docRevision: integer}
       * @see docs/api-spec/fragments/palette.yaml (method: palette.delete)
       */
      "delete": function (params) { return self.call('palette.delete', params); }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'palette', {
    configurable: true,
    get: function () { return ns(this); }
  });

  /**
   * Event topics broadcast by this domain. All three are §4a structural events, broadcast
   * unconditionally to every connected client — palette-notes.md explicitly confirms none of
   * these are subscribe-gated (none exceed the ~2Hz/per-resource threshold in 00-conventions.md
   * §5). `palette` is also a lockable resource type per §6 (advisory soft locks via the generic
   * locks.acquire/locks.release — no palette-specific lock messages).
   */
  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.palette = [
    'palette.created', // {palette: PaletteDetail, docRevision} — after palette.create
    'palette.updated', // {palette: PaletteDetail, docRevision} — after palette.update (rename and/or values change)
    'palette.deleted'  // {paletteId: integer, docRevision} — after palette.delete
  ];
})();
