/**
 * virtualconsole domain — qlc.vc.*
 * Generated from docs/api-spec/fragments/virtualconsole.yaml (+ virtualconsole-notes.md) in the
 * QLC+ fork repo (this repository — read-only source, actively being implemented
 * server-side right now, so treat every shape below as "true as of this reading," re-verify if it
 * looks stale). Thin pass-through wrappers only: this.call(method, params) / this.send(method, params).
 * No field remapping — params/result shapes here are exactly the spec's.
 *
 * IMPORTANT — answers a question the prior finding left open: api/qlcplus-api.js's own comments
 * (see getWidgetsList/setWidget there) say "no slider/button value-push method exists server-side"
 * and ship setWidget() as a documented no-op. That was a claim about the running SERVER (checked by
 * probing it), not the spec. Reading the SPEC (this file's source) shows it now fully defines live
 * value-push methods for both: vc.slider.setValue and vc.button.press (both below, §4b live/
 * runtime, no baseRevision, last-write-wins) — plus vc.xyPad.setPosition/setFloorPosition,
 * vc.speedDial.*, vc.animation.setFaderLevel/setPresetKnobValue, vc.cueList.* for other widget
 * types. So: the spec-level gap the prior finding described is closed. Whether THIS particular
 * fork build already implements these on the wire is unverified from here (read-only source, no
 * server probing done for this file) — once it does, setWidget() should call these methods instead
 * of alerting a no-op; until confirmed, treat them as "spec-defined, server support TBD."
 *
 * DISCREPANCY — vc.widget.list result shape: the hand-written getWidgetsList() (qlcplus-api.js)
 * expects each list entry to already carry typeConfig/inputSources/keySequences/externalControls.
 * Per the spec, vc.widget.list (and vc.widget.usage) return VcWidgetSummary entries — id,
 * widgetType, page, parentId?, geometry, zIndex, allowResize, isDisabled, isVisible, style only.
 * Those four extra fields exist solely on VcWidgetDetail, returned by vc.widget.get(widgetId)
 * (one widget at a time) or embedded in the vc.widget.created/updated/configChanged/bulkUpdated
 * event payloads. A client wanting full per-widget detail for every widget must either call
 * vc.widget.get per id, or build a client-side cache from those events — vc.widget.list alone
 * will not populate typeConfig/inputSources/keySequences/externalControls.
 *
 * Tiering per 00-conventions.md (see virtualconsole-notes.md for the judgment calls, e.g. why
 * page/frame "current page" is live despite feeling per-client, and why VCAnimation colors are
 * doc-state despite feeling like a live color-picker drag):
 *  - §4a document-state (needs baseRevision/docRevision): vc.page.* (except select/validatePin),
 *    vc.widget.* structural methods (create/createMatrix/createFromFunctions/update/setConfig/
 *    delete/reparent/reposition/align/distribute/bulkStyle/inputSource.set(-remove)/
 *    keySequence.set(-remove)/preset.add/preset.remove), vc.slider.setLevelChannels,
 *    vc.xyPad.fixture.add(-remove)/setHeadsRange/preset.move/
 *    preset.rename, vc.frame.cloneFirstPage/setPin, vc.clock.schedule.*, vc.speedDial.preset.
 *    update, vc.animation.preset.move, vc.audioTriggers.setBarConfig.
 *  - §4b live/runtime (no baseRevision, last-write-wins): vc.page.select/validatePin, vc.widget.
 *    inputDetect.start/stop, vc.button.press, vc.slider.setValue/flash, vc.xyPad.setPosition/
 *    setFloorPosition, vc.frame.gotoPage/validatePin, vc.clock.playPause/reset, vc.cueList.* (all),
 *    vc.speedDial.* (except preset.update), vc.animation.setFaderLevel/setPresetKnobValue,
 *    vc.audioTriggers.setCaptureEnabled, vc.widget.preset.apply.
 *  - vc.widget.preset.add/remove are §4a even though the preset payload can itself describe a
 *    live-feeling XYPad position/SpeedDial time/Animation color — the STORED preset is document
 *    state; applying it live is the separate vc.widget.preset.apply.
 */
(function () {
  'use strict';

  function ns(self) {
    return {
      animation: {
        /** widgetId: string. level: 0-255 (master intensity of the controlled RGBMatrix). Live
            (§4b), no baseRevision. -> ack; broadcasts vc.animation.faderLevelChanged. */
        setFaderLevel: function (widgetId, level) {
          return self.call('vc.animation.setFaderLevel', { widgetId: widgetId, level: level });
        },
        /** widgetId: string. presetId: integer (a ColorKnob-type preset only). value: 0-255. Live
            (§4b) — a Knob preset's live intensity turn, NOT persisted (the preset's base color is
            what's saved via vc.widget.preset.add). -> ack; broadcasts vc.animation.
            activePresetChanged ({widgetId, activePresetId, knobValue}). */
        setPresetKnobValue: function (widgetId, presetId, value) {
          return self.call('vc.animation.setPresetKnobValue', { widgetId: widgetId, presetId: presetId, value: value });
        },
        preset: {
          /** widgetId: string. presetId: integer. direction: 'up'|'down'. baseRevision: integer.
              -> {docRevision}; broadcasts vc.animation.presetsChanged (widget's full preset list). */
          move: function (widgetId, presetId, direction, baseRevision) {
            return self.call('vc.animation.preset.move', { widgetId: widgetId, presetId: presetId, direction: direction, baseRevision: baseRevision });
          }
        }
      },

      audioTriggers: {
        /** params: {widgetId, index, baseRevision, type?: 'None'|'DMXBar'|'FunctionBar'|
            'VCWidgetBar', minThreshold?, maxThreshold?, functionId?, triggeredWidgetId?
            (VCWidgetBar target), dmxChannels?: [{fixtureId, channel}]}. Partial update of one bar
            (index) by position, not a stable id — re-derive index from the latest barsChanged
            event, don't cache it across a mutation. Document-state. -> {docRevision}; broadcasts
            vc.audioTriggers.barsChanged (widget's full resulting bar list). */
        setBarConfig: function (params) { return self.call('vc.audioTriggers.setBarConfig', params); },
        /** widgetId: string. enabled: boolean — whether the mic/line-in is actively being read.
            Live (§4b), deliberately NOT persisted (a show file loading shouldn't auto-start audio
            capture). -> ack; broadcasts vc.audioTriggers.captureEnabledChanged. */
        setCaptureEnabled: function (widgetId, enabled) {
          return self.call('vc.audioTriggers.setCaptureEnabled', { widgetId: widgetId, enabled: !!enabled });
        }
      },

      button: {
        /** widgetId: string. pressed: boolean. Live (§4b), no baseRevision — the single gesture
            backing Toggle/Blackout/StopAll (full click) AND Flash (momentary). IMPORTANT: for every
            actionType except Flash, send exactly ONE call per click (the engine flips its own state
            unconditionally on every invocation, ignoring $pressed for Blackout/StopAll) — modeling
            a physical click as a press(true)+release(false) pair, the way Flash works, double-fires
            and nets a no-op. Flash is the sole edge-triggered exception: pressed=true on press-down,
            pressed=false on release, exactly like vc.slider.flash's "on" param. -> ack; broadcasts
            vc.button.stateChanged ({widgetId, state: 'Inactive'|'Monitoring'|'Active'}). */
        press: function (widgetId, pressed) {
          return self.call('vc.button.press', { widgetId: widgetId, pressed: !!pressed });
        }
      },

      clock: {
        /** widgetId: string. Stopwatch/Countdown only. Live (§4b). -> ack; broadcasts
            vc.clock.timeChanged off-cadence (immediate), then at 1Hz (Clock) or 10Hz (a running
            Stopwatch/Countdown) while playing. */
        playPause: function (widgetId) { return self.call('vc.clock.playPause', { widgetId: widgetId }); },
        /** widgetId: string. -> ack; broadcasts vc.clock.timeChanged. */
        reset: function (widgetId) { return self.call('vc.clock.reset', { widgetId: widgetId }); },
        schedule: {
          /** widgetId: string. functionIds: string[] (>=1) — one schedule entry per Function id,
              default start/stop/weekFlags. baseRevision: integer. -> {docRevision}; broadcasts
              vc.clock.schedulesChanged (widget's full resulting schedule list). */
          add: function (widgetId, functionIds, baseRevision) {
            return self.call('vc.clock.schedule.add', { widgetId: widgetId, functionIds: functionIds, baseRevision: baseRevision });
          },
          /** widgetId: string. index: integer (positional, not a stable id — re-derive from the
              latest schedulesChanged event). baseRevision: integer. -> {docRevision}; broadcasts
              vc.clock.schedulesChanged. */
          remove: function (widgetId, index, baseRevision) {
            return self.call('vc.clock.schedule.remove', { widgetId: widgetId, index: index, baseRevision: baseRevision });
          },
          /** params: {widgetId, index, baseRevision, startTime?, stopTime?, weekFlags?}. startTime/
              stopTime are seconds-since-midnight; stopTime=-1 means "no stop time". weekFlags packs
              a Mon..Sun bitmask in bits 0-6 (0x7F, all-zero = every day) PLUS a separate repeat flag
              in bit 7 (0x80) — easy to lose by only reading/writing the low 7 bits. -> {docRevision};
              broadcasts vc.clock.schedulesChanged. */
          update: function (params) { return self.call('vc.clock.schedule.update', params); }
        }
      },

      cueList: {
        /** widgetId: string. Steps + live playback state of one cue list (web UI contract, 2026-09):
            -> {steps: [{index, name, functionId, fadeIn, fadeOut, hold, notes}], playbackIndex,
            running, paused}. Rejects NOT_FOUND "Unknown method" on a server predating it. */
        get: function (widgetId) { return self.call('vc.cueList.get', { widgetId: widgetId }); },
        /** widgetId: string. Jump to the next step. -> ack; broadcasts vc.cueList.playbackChanged. */
        next: function (widgetId) { return self.call('vc.cueList.next', { widgetId: widgetId }); },
        /** widgetId: string. Start, or resume from Paused. -> ack; broadcasts
            vc.cueList.playbackChanged (also fires on its own as the attached Chaser runs/finishes
            steps — this is how every client's cue list UI stays in sync with a running chase). */
        play: function (widgetId) { return self.call('vc.cueList.play', { widgetId: widgetId }); },
        /** widgetId: string. -> ack; broadcasts vc.cueList.playbackChanged. */
        previous: function (widgetId) { return self.call('vc.cueList.previous', { widgetId: widgetId }); },
        /** widgetId: string. playbackIndex: integer, -1 stops. Jump directly to a step (e.g.
            double-click a row). -> ack; broadcasts vc.cueList.playbackChanged. */
        setPlaybackIndex: function (widgetId, playbackIndex) {
          return self.call('vc.cueList.setPlaybackIndex', { widgetId: widgetId, playbackIndex: playbackIndex });
        },
        /** widgetId: string. level: 0-255 (confined to 0-100 server-side in Crossfade mode). Live
            (§4b) — meaning depends on the widget's sideFaderMode (Crossfade vs. Steps; INVALID_STATE
            while it is None). -> ack; broadcasts vc.cueList.sideFaderChanged ({widgetId, level,
            nextStepIndex, primaryTop}); seeds are vc.widget.get's sideFaderLevel/nextStepIndex/
            primaryTop. */
        setSideFaderLevel: function (widgetId, level) {
          return self.call('vc.cueList.setSideFaderLevel', { widgetId: widgetId, level: level });
        },
        /** widgetId: string. -> ack; broadcasts vc.cueList.playbackChanged
            ({widgetId, playbackStatus: 'Stopped'|'Playing'|'Paused', playbackIndex, nextStepIndex,
            primaryTop}). */
        stop: function (widgetId) { return self.call('vc.cueList.stop', { widgetId: widgetId }); }
      },

      frame: {
        /** widgetId: string. baseRevision: integer. Duplicates internal page 0's children onto
            every other internal page of a multi-page Frame. Document-state. -> {docRevision}. */
        cloneFirstPage: function (widgetId, baseRevision) {
          return self.call('vc.frame.cloneFirstPage', { widgetId: widgetId, baseRevision: baseRevision });
        },
        /** widgetId: string. -> {pages, currentPage, multipage} — a Frame's internal page cursor
            (web UI contract, 2026-09). Rejects NOT_FOUND "Unknown method" on a server predating it. */
        get: function (widgetId) { return self.call('vc.frame.get', { widgetId: widgetId }); },
        /** widgetId: string. page: integer — a multi-page Frame's OWN internal page cursor
            (distinct from vc.page.select, the top-level VC page). Live (§4b), single shared value
            per frame (matches engine's VCFrame::currentPage, not per-client). -> ack; broadcasts
            vc.frame.pageChanged ({widgetId, page}). The parameter is sent under both the current
            contract name (`page`) and the older spec name (`pageIndex`); listen to both
            vc.frame.pageChanged and the older vc.frame.currentPageChanged. */
        gotoPage: function (widgetId, page) {
          return self.call('vc.frame.gotoPage', { widgetId: widgetId, page: page, pageIndex: page });
        },
        /** widgetId: string (a top-level Frame only). currentPIN/newPIN: string, default ''.
            baseRevision: integer. currentPIN must match the existing PIN; newPIN='' clears
            protection. Document-state (surprising but confirmed against vcframe.cpp::saveXML —
            see notes.md). -> {docRevision}. */
        setPin: function (widgetId, currentPIN, newPIN, baseRevision) {
          return self.call('vc.frame.setPin', { widgetId: widgetId, currentPIN: currentPIN || '', newPIN: newPIN || '', baseRevision: baseRevision });
        },
        /** widgetId: string. pin: string. Live/session-scoped — a correct PIN unlocks the frame
            for this client's session only, not broadcast. -> {valid: boolean}. */
        validatePin: function (widgetId, pin) { return self.call('vc.frame.validatePin', { widgetId: widgetId, pin: pin }); }
      },

      page: {
        /** index: integer (insertion index; new page's caption defaults to "Page N"). baseRevision:
            integer. -> {docRevision}; broadcasts vc.page.created ({page: VcPage, docRevision}). */
        create: function (index, baseRevision) {
          return self.call('vc.page.create', { index: index, baseRevision: baseRevision });
        },
        /** index: integer. baseRevision: integer. Recursively deletes every widget on the page.
            -> {docRevision}; broadcasts vc.page.deleted ({index, deletedWidgetIds, docRevision}). */
        'delete': function (index, baseRevision) {
          return self.call('vc.page.delete', { index: index, baseRevision: baseRevision });
        },
        /** -> {pages: VcPage[] ({index, name, hasPin?}), selectedPage: integer}. selectedPage is
            live/runtime (see select() below), everything else is document-state. */
        list: function () { return self.call('vc.page.list', {}); },
        /** index: integer. name: string (a VCPage's name IS its inherited VCWidget caption).
            baseRevision: integer. -> {docRevision}; broadcasts vc.page.renamed. */
        rename: function (index, name, baseRevision) {
          return self.call('vc.page.rename', { index: index, name: name, baseRevision: baseRevision });
        },
        /** index: integer. Live (§4b), no baseRevision — "which VC page is showing". Modeled as a
            SINGLE shared value (matches VirtualConsole::selectedPage; every operator's screen shows
            the same page, per today's engine model — see notes.md). -> ack; broadcasts
            vc.page.selected ({index}) to every connected client. */
        select: function (index) { return self.call('vc.page.select', { index: index }); },
        /** index: integer. currentPIN/newPIN: string, default ''. baseRevision: integer. Mirrors
            vc.frame.setPin for VirtualConsole's own top-level page PIN. -> {docRevision}. */
        setPin: function (index, currentPIN, newPIN, baseRevision) {
          return self.call('vc.page.setPin', { index: index, currentPIN: currentPIN || '', newPIN: newPIN || '', baseRevision: baseRevision });
        },
        /** index: integer. pin: string. Live/session-scoped, same semantics as vc.frame.
            validatePin. -> {valid: boolean}. */
        validatePin: function (index, pin) { return self.call('vc.page.validatePin', { index: index, pin: pin }); },
        /** index: integer. width/height: integer 1..100000 px (VCPageProperties.qml). baseRevision:
            integer. -> {docRevision}; broadcasts vc.page.updated ({page, docRevision}). */
        setSize: function (index, width, height, baseRevision) {
          return self.call('vc.page.setSize', { index: index, width: width, height: height, baseRevision: baseRevision });
        }
      },

      slider: {
        /** widgetId: string. on: boolean (true on press-down, false on release). Adjust mode's
            momentary flash button (requires adjustFlashEnabled in the widget's config). Live (§4b),
            mirrors vc.button.press's Flash semantics. -> ack. */
        flash: function (widgetId, on) { return self.call('vc.slider.flash', { widgetId: widgetId, 'on': !!on }); },
        /** widgetId: string. channels: [{fixtureId, channel}] — bulk replace of the whole Level-
            mode channel list. baseRevision: integer. Document-state. -> {docRevision}. NOTE: no
            dedicated *Changed event was found for this mutation in the fragment (only vc.widget.
            configChanged/vc.widget.updated cover other config edits) — treat the docRevision
            response as authoritative, or refetch via vc.widget.get, until confirmed against the
            live server. */
        setLevelChannels: function (widgetId, channels, baseRevision) {
          return self.call('vc.slider.setLevelChannels', { widgetId: widgetId, channels: channels, baseRevision: baseRevision });
        },
        /** widgetId: string. value: 0-255. Live (§4b), no baseRevision, last-write-wins (two
            clients can grab the same fader). Meaning depends on the slider's sliderMode: Level
            writes to every configured levelChannel; Adjust maps to the controlled Function's
            attribute fraction; Submaster cascades as an intensity multiplier to sibling widgets;
            GrandMaster forwards verbatim to io.grandMaster.setValue (same engine call — see
            io.grandMaster, and notes.md for the cross-domain duplication). THE live-value-push
            method that supersedes the "no such method exists" finding — see file header. Fire-
            and-forget (this.send): confirmed by vc.slider.valueChanged, and intermediate values
            during a drag are throwaway. */
        setValue: function (widgetId, value) {
          return self.send('vc.slider.setValue', { widgetId: widgetId, value: value });
        }
      },

      speedDial: {
        /** widgetId: string. "Apply" button: pushes currentTime x factor onto every attached
            Function's fadeIn/fadeOut/duration (per-function factor overrides in the widget's own
            config). Live (§4b) trigger; the resulting Function speed changes surface as
            functions.updated events (functions-core), not duplicated here. -> ack. */
        apply: function (widgetId) { return self.call('vc.speedDial.apply', { widgetId: widgetId }); },
        preset: {
          /** widgetId: string. presetId: integer (stable id). baseRevision: integer. name?:
              string. valueMs?: integer — omit either optional field to leave it unchanged.
              -> {docRevision}; broadcasts vc.speedDial.presetsChanged (widget's full preset list).
              Presets are added/removed through widget.preset.add/remove and activated through
              widget.preset.apply (implemented 2026-09-27: for a Speed widget it sets currentTime to
              the preset's valueMs, i.e. what the on-screen preset button does, reported as
              vc.speedDial.valueChanged). The current list also rides along read-only as
              typeConfig.presets in vc.widget.get. */
          update: function (widgetId, presetId, baseRevision, name, valueMs) {
            var params = { widgetId: widgetId, presetId: presetId, baseRevision: baseRevision };
            if (name !== undefined) params.name = name;
            if (valueMs !== undefined) params.valueMs = valueMs;
            return self.call('vc.speedDial.preset.update', params);
          }
        },
        /** widgetId: string. Clears the tap-tempo interval average (right-click on the TAP button).
            -> ack; broadcasts vc.speedDial.tapChanged ({widgetId, tapTimeValue: 0, currentTimeMs})
            when a series was running (nothing if there was none to clear). */
        resetTap: function (widgetId) { return self.call('vc.speedDial.resetTap', { widgetId: widgetId }); },
        /** DEPRECATED: vc.speedDial.setCurrentTime exists in neither the spec nor the server (it was
            renamed to vc.speedDial.setValue before anything implemented it) - kept only so an old
            caller fails at the server with NOT_FOUND instead of a JS TypeError. Use setValue(). */
        setCurrentTime: function (widgetId, valueMs) {
          return self.call('vc.speedDial.setCurrentTime', { widgetId: widgetId, valueMs: valueMs });
        },
        /** widgetId: string. ms: integer >=0. The web UI contract's (2026-09) name for the same
            absolute-time edit: vc.speedDial.setValue {widgetId, ms} -> ack; broadcasts
            vc.speedDial.valueChanged ({widgetId, ms}). Listen to both that and the older
            vc.speedDial.currentTimeChanged ({widgetId, currentTimeMs}). */
        setValue: function (widgetId, ms) {
          return self.call('vc.speedDial.setValue', { widgetId: widgetId, ms: ms });
        },
        /** widgetId: string. factor: one of 'OneSixteenth'|'OneEighth'|'OneFourth'|'Half'|'One'|
            'Two'|'Four'|'Eight'|'Sixteen' (the dial's own factor; 'None'/'Zero' exist only as
            per-function overrides in VcSpeedDialConfig.functions and are rejected here with
            INVALID_PARAMS) — client computes +/- from the widget's last-known factor and sends the
            resulting value. Live (§4b). -> ack; broadcasts vc.speedDial.factorChanged ({widgetId,
            factor}); the seed is vc.widget.get's `factor` field. */
        setFactor: function (widgetId, factor) {
          return self.call('vc.speedDial.setFactor', { widgetId: widgetId, factor: factor });
        },
        /** widgetId: string. Tap-tempo: successive taps within 1.5s compute a new currentTime
            from the interval average. If the widget's controlBPM config is enabled, ALSO sets the
            engine's global BPM (InputOutputMap::setBpmNumber()) — as of this reading, neither
            io.yaml nor core.yaml defines an io.bpm.-prefixed or core.bpm.-prefixed message/event
            to observe that global value; flagged as a spec gap in virtualconsole-notes.md, not fixable from this
            file. -> ack; broadcasts vc.speedDial.tapChanged ({widgetId, tapTimeValue (computed
            BPM), currentTimeMs}). */
        tap: function (widgetId) { return self.call('vc.speedDial.tap', { widgetId: widgetId }); }
      },

      widget: {
        /** widgetId: string. Read: the image file the widget's style.backgroundImage references on
            the QLC+ host, as a data: URL (png/jpeg/gif/bmp/webp/svg, <= 8 MB, local files only).
            -> {widgetId, path|null, mimeType|null, dataUrl|null, reason?}. */
        getBackgroundImage: function (widgetId) { return self.call('vc.widget.getBackgroundImage', { widgetId: widgetId }); },
        /** widgetIds: string[] (>=1). referenceWidgetId: string (the widget others align to).
            alignment: 'left'|'hcenter'|'right'|'top'|'vcenter'|'bottom'. baseRevision: integer.
            -> {docRevision}; broadcasts vc.widget.bulkUpdated (every affected widget's full
            VcWidgetDetail). */
        align: function (widgetIds, referenceWidgetId, alignment, baseRevision) {
          return self.call('vc.widget.align', { widgetIds: widgetIds, referenceWidgetId: referenceWidgetId, alignment: alignment, baseRevision: baseRevision });
        },
        /** params: {widgetIds (>=1), baseRevision, caption?, foregroundColor? (hex or null),
            backgroundColor? (hex or null), backgroundImage? (path/resource or null), font?:
            {family, pointSize, bold, italic, underline}} — at least one style field required.
            Applies the same field(s) to every listed widget in one commit. -> {docRevision};
            broadcasts vc.widget.bulkUpdated. */
        bulkStyle: function (params) { return self.call('vc.widget.bulkStyle', params); },
        /** params: {widgetType (one of 'Button'|'Slider'|'XYPad'|'Frame'|'SoloFrame'|'Label'|
            'AudioTriggers'|'Animation'|'Clock'|'CueList'|'Speed'), page, geometry: {x,y,width,
            height}, baseRevision, parentId? (containing Frame/SoloFrame, absent = page root),
            style?: VcWidgetStyle, typeConfig?: object (shape depends on widgetType — see the
            Vc<Type>Config schemas; omit for engine defaults)}. -> {docRevision, widgetId};
            broadcasts vc.widget.created ({widget: VcWidgetDetail, docRevision}). */
        create: function (params) { return self.call('vc.widget.create', params); },
        /** params: {page, functionIds (>=1), position: {x,y}, widgetHint: 'button'|'adjustSlider'|
            'cueList', baseRevision, parentId?}. The "drop Functions from the Function Manager onto
            the VC" gesture — 'button' makes one VCButton per Function, 'adjustSlider' one VCSlider
            per Function in Adjust mode, 'cueList' a single VCCueList (every functionId must be a
            Chaser). -> {docRevision, widgetIds}; broadcasts vc.widget.bulkUpdated — NOT
            vc.widget.created (a client must listen on bulkUpdated too, or it will miss these). */
        createFromFunctions: function (params) { return self.call('vc.widget.createFromFunctions', params); },
        /** params: {page, matrixType: 'Button'|'Slider' (only these two — anything else is treated
            as Slider server-side), position: {x,y}, matrixSize: {columns,rows}, widgetSize:
            {width,height}, baseRevision, parentId?, soloFrame?: boolean (wrap in a VCSoloFrame)}.
            Bulk-creates a grid of identical widgets. -> {docRevision, widgetIds}; broadcasts
            vc.widget.bulkUpdated (same reused shape/topic as createFromFunctions above). */
        createMatrix: function (params) { return self.call('vc.widget.createMatrix', params); },
        /** widgetIds: string[] (>=1). baseRevision: integer. Deleting a Frame/SoloFrame/Page
            recursively deletes its children too. -> {docRevision}; broadcasts vc.widget.deleted
            ({widgetIds, docRevision}). */
        'delete': function (widgetIds, baseRevision) {
          return self.call('vc.widget.delete', { widgetIds: widgetIds, baseRevision: baseRevision });
        },
        /** widgetIds: string[] (>=3). direction: 'horizontal'|'vertical'. baseRevision: integer.
            -> {docRevision}; broadcasts vc.widget.bulkUpdated. */
        distribute: function (widgetIds, direction, baseRevision) {
          return self.call('vc.widget.distribute', { widgetIds: widgetIds, direction: direction, baseRevision: baseRevision });
        },
        /** widgetId: string. -> VcWidgetDetail (VcWidgetSummary fields + typeConfig, inputSources,
            keySequences, externalControls) — the ONE method that returns full per-widget detail;
            see the file-header DISCREPANCY note re: vc.widget.list not including these fields. */
        get: function (widgetId) { return self.call('vc.widget.get', { widgetId: widgetId }); },
        inputDetect: {
          /** widgetId: string. controlId: integer (one of the widget's externalControls ids).
              "Learn" wizard: server listens for the next external controller signal and binds it,
              then broadcasts vc.widget.inputSourcesChanged. IMPORTANT: there is exactly ONE global
              autodetect slot server-wide (not per-client/session) — a second start() call from any
              client silently steals the slot from whoever called it first, and stop() cancels
              whichever widget currently holds it regardless of caller. Treat "someone is learning"
              as global UI state. -> ack. */
          start: function (widgetId, controlId) {
            return self.call('vc.widget.inputDetect.start', { widgetId: widgetId, controlId: controlId });
          },
          /** No params — cancels learn mode for whichever widget currently holds the single global
              autodetect slot (see start() above); the engine call itself takes no argument. -> ack. */
          stop: function () { return self.call('vc.widget.inputDetect.stop', {}); }
        },
        inputSource: {
          /** widgetId, controlId, universe, channel: integers, baseRevision — all required.
              channel here is VcInputSource's composited encoding (bits 0-15 = bare DMX channel,
              bits 16-31 = a multipage Frame's page number; mask 0x0000FFFF for the bare channel).
              -> {docRevision}; broadcasts vc.widget.inputSourcesChanged
              ({widgetId, inputSources: VcInputSource[], docRevision}). */
          remove: function (params) { return self.call('vc.widget.inputSource.remove', params); },
          /** params: {widgetId, controlId, universe, channel, baseRevision, lowerValue? (0-255),
              upperValue? (0-255), monitorValue? (0-255), lowerChannel?/upperChannel?/
              monitorChannel? (MIDI feedback-routing table index, 1-based on the wire, 0/absent =
              use the input profile's own routing)}. Manual (non-autodetect) binding of an external
              controller input to one of the widget's named controls. -> {docRevision}; broadcasts
              vc.widget.inputSourcesChanged. */
          set: function (params) { return self.call('vc.widget.inputSource.set', params); }
        },
        keySequence: {
          /** widgetId: string. keySequence: string (Qt key sequence text, e.g. "Ctrl+G").
              baseRevision: integer. -> {docRevision}; broadcasts vc.widget.keySequencesChanged
              ({widgetId, keySequences: [{keySequence, controlId}], docRevision}). */
          remove: function (widgetId, keySequence, baseRevision) {
            return self.call('vc.widget.keySequence.remove', { widgetId: widgetId, keySequence: keySequence, baseRevision: baseRevision });
          },
          /** widgetId: string. controlId: integer. keySequence: string, e.g. "Ctrl+G".
              baseRevision: integer. -> {docRevision}; broadcasts vc.widget.keySequencesChanged. */
          set: function (widgetId, controlId, keySequence, baseRevision) {
            return self.call('vc.widget.keySequence.set', { widgetId: widgetId, controlId: controlId, keySequence: keySequence, baseRevision: baseRevision });
          }
        },
        /** params?: {page? (filter: only widgets on this top-level page), parentId? (filter: only
            direct children of this Frame/SoloFrame/Page), typeFilters?: VcWidgetType[]}. -> {widgets:
            VcWidgetSummary[]} — SUMMARY only (id, widgetType, page, parentId?, geometry, zIndex,
            allowResize, isDisabled, isVisible, style), NOT full detail. See the file-header
            DISCREPANCY note: this diverges from the hand-written getWidgetsList()'s expectation of
            typeConfig/inputSources/keySequences/externalControls on every entry — use widget.get()
            per id (or cache from *Detail-carrying events) for those. */
        list: function (params) { return self.call('vc.widget.list', params || {}); },
        preset: {
          /** widgetId: string. preset: one of VcXyPadPresetData/VcSpeedDialPresetData/
              VcAnimationPresetData shape, determined server-side by widgetId's own type (no
              discriminator field here). baseRevision: integer. -> {docRevision, presetId};
              broadcasts the matching vc.xyPad.presetsChanged / vc.speedDial.presetsChanged /
              vc.animation.presetsChanged for that widget's type. */
          add: function (widgetId, preset, baseRevision) {
            return self.call('vc.widget.preset.add', { widgetId: widgetId, baseRevision: baseRevision, preset: preset });
          },
          /** widgetId: string. presetId: integer. Live activation — behaviour depends entirely on
              widgetId's widgetType: XYPad moves the cursor (and starts the Function for an EFX/
              Scene preset); Animation sets/clears a color slot or selects an algorithm (no-ops
              server-side for a Knob-type preset — use animation.setPresetKnobValue instead, the
              actual live control for those); Speed (implemented 2026-09-27) sets currentTime to
              the preset's valueMs like the on-screen preset button, reported as
              vc.speedDial.valueChanged. XYPad/Animation are still host-side stubs (INVALID_STATE).
              -> ack; broadcasts vc.xyPad.activePresetChanged or vc.animation.activePresetChanged
              as applicable. */
          apply: function (widgetId, presetId) {
            return self.call('vc.widget.preset.apply', { widgetId: widgetId, presetId: presetId });
          },
          /** widgetId: string. presetId: integer. baseRevision: integer. -> {docRevision};
              broadcasts the matching *.presetsChanged event for that widget's type. */
          remove: function (widgetId, presetId, baseRevision) {
            return self.call('vc.widget.preset.remove', { widgetId: widgetId, presetId: presetId, baseRevision: baseRevision });
          }
        },
        /** widgetId: string. newParentId?: string (absent = move to page root). position: {x,y}.
            baseRevision: integer. Drag a widget into a different Frame/SoloFrame at a given
            position. -> {docRevision}; broadcasts vc.widget.updated (single-widget — NOT
            bulkUpdated, which is reserved for genuinely multi-widget ops). */
        reparent: function (widgetId, newParentId, position, baseRevision) {
          var params = { widgetId: widgetId, position: position, baseRevision: baseRevision };
          if (newParentId != null) params.newParentId = newParentId;
          return self.call('vc.widget.reparent', params);
        },
        /** widgets: [{widgetId, geometry: {x,y,width,height}}] (>=1, same parent). baseRevision:
            integer. Bulk geometry commit for one drag/resize gesture — one event for the whole
            gesture, not one per widget. -> {docRevision}; broadcasts vc.widget.repositioned. */
        reposition: function (widgets, baseRevision) {
          return self.call('vc.widget.reposition', { widgets: widgets, baseRevision: baseRevision });
        },
        /** widgetId: string. config: object — a PARTIAL patch merged onto the widget's existing
            typeConfig (shape depends on widgetType — see the Vc<Type>Config schemas referenced
            from widget.create). baseRevision: integer. -> {docRevision}; broadcasts
            vc.widget.configChanged ({widget: VcWidgetDetail, docRevision}). */
        setConfig: function (widgetId, config, baseRevision) {
          return self.call('vc.widget.setConfig', { widgetId: widgetId, config: config, baseRevision: baseRevision });
        },
        /** params: {widgetId, baseRevision, geometry?, zIndex?, allowResize?, isDisabled?
            (persisted — e.g. a Frame's "Enable" header button; see notes.md), isVisible?, page?
            (move to a different top-level page), style?} — at least one optional field required.
            For type-specific config use setConfig() instead. -> {docRevision}; broadcasts
            vc.widget.updated (also fires for reparent — see reparent() above). */
        update: function (params) { return self.call('vc.widget.update', params); },
        /** functionId: string. Every widget that references this Function in some way (attached,
            controlled, chaser step, schedule...). -> {widgets: VcWidgetSummary[]} (summary only —
            same shape as widget.list(), see its DISCREPANCY note above). */
        usage: function (functionId) { return self.call('vc.widget.usage', { functionId: functionId }); }
      },

      xyPad: {
        fixture: {
          /** params: {widgetId, baseRevision, fixtureGroupId? | fixtureId? (whole fixture) |
              (fixtureId+headIndex) (single head) | universe? (expands to every fixture in it)} —
              exactly one of that group required. -> {docRevision}; broadcasts
              vc.xyPad.fixturesChanged (widget's full resulting fixture/head list). */
          add: function (params) { return self.call('vc.xyPad.fixture.add', params); },
          /** widgetId: string. heads: [{fixtureId, headIndex}]. baseRevision: integer.
              -> {docRevision}; broadcasts vc.xyPad.fixturesChanged. */
          remove: function (widgetId, heads, baseRevision) {
            return self.call('vc.xyPad.fixture.remove', { widgetId: widgetId, heads: heads, baseRevision: baseRevision });
          }
        },
        preset: {
          /** widgetId: string. presetId: integer (stable id). direction: 'up'|'down'.
              baseRevision: integer. -> {docRevision}; broadcasts vc.xyPad.presetsChanged (widget's
              full resulting preset list). */
          move: function (widgetId, presetId, direction, baseRevision) {
            return self.call('vc.xyPad.preset.move', { widgetId: widgetId, presetId: presetId, direction: direction, baseRevision: baseRevision });
          },
          /** widgetId: string. presetId: integer. name: string. baseRevision: integer.
              -> {docRevision}; broadcasts vc.xyPad.presetsChanged. */
          rename: function (widgetId, presetId, name, baseRevision) {
            return self.call('vc.xyPad.preset.rename', { widgetId: widgetId, presetId: presetId, name: name, baseRevision: baseRevision });
          }
        },
        /** widgetId: string. x/y/z: numbers, metres relative to the environment origin (y = height
            above floor) — only meaningful when the widget's floorControl config is enabled. Live
            (§4b), no baseRevision. -> ack; broadcasts vc.xyPad.floorPositionChanged. Fire-and-
            forget (this.send): explicitly called out in virtualconsole-notes.md as a continuous-
            drag topic, confirmed by the event, intermediate values are throwaway. */
        setFloorPosition: function (widgetId, x, y, z) {
          return self.send('vc.xyPad.setFloorPosition', { widgetId: widgetId, x: x, y: y, z: z });
        },
        /** params: {widgetId, heads: [{fixtureId, headIndex}], xMin, xMax, xReverse, yMin, yMax,
            yReverse, baseRevision} — all required. Per-head Pan/Tilt range restriction, in the
            widget's current displayMode units. -> {docRevision}; broadcasts
            vc.xyPad.fixturesChanged. */
        setHeadsRange: function (params) { return self.call('vc.xyPad.setHeadsRange', params); },
        /** widgetId: string. x/y: numbers. UNIT CAVEAT: the original spec fragment documents
            0..255.99609375 (the engine's DMX-with-fraction domain); the web UI contract (2026-09)
            specifies normalized 0..1 for both this call and vc.xyPad.positionChanged. The web UI
            sends 0..1 and, on receive, treats any coordinate > 1 as DMX-domain and divides by 256,
            so it renders correctly against either server. Live (§4b), no baseRevision. -> ack;
            broadcasts vc.xyPad.positionChanged. Fire-and-forget (this.send): a continuous-drag
            topic, confirmed by the event. */
        setPosition: function (widgetId, x, y) {
          return self.send('vc.xyPad.setPosition', { widgetId: widgetId, x: x, y: y });
        }
      }
    };
  }

  Object.defineProperty(window.QLCPlusAPI.prototype, 'vc', {
    configurable: true,
    get: function () { return ns(this); }
  });

  window.QLCPlusAPI.topics = window.QLCPlusAPI.topics || {};
  window.QLCPlusAPI.topics.vc = [
    // Document-state (§4a) — always delivered to every client, not subscribe-gated. Includes
    // every *.presetsChanged/*.fixturesChanged/schedulesChanged/barsChanged event: each carries
    // docRevision, per virtualconsole-notes.md's §5 summary of this fragment's doc-state events.
    'vc.page.created',
    'vc.page.deleted',
    'vc.page.renamed',
    'vc.widget.created',
    'vc.widget.updated',
    'vc.widget.configChanged',
    'vc.widget.deleted',
    'vc.widget.repositioned',
    'vc.widget.bulkUpdated',              // also the creation event for createMatrix/createFromFunctions
    'vc.widget.inputSourcesChanged',
    'vc.widget.keySequencesChanged',
    'vc.clock.schedulesChanged',
    'vc.audioTriggers.barsChanged',
    'vc.xyPad.fixturesChanged',
    'vc.xyPad.presetsChanged',
    'vc.speedDial.presetsChanged',
    'vc.animation.presetsChanged',
    // Live/runtime (§4b) — recommend subscribe() first; several are high-frequency, see notes.md.
    // None of these carry docRevision (that's the tell distinguishing them from the block above).
    'vc.page.selected',                   // shared single value, not per-client (see notes.md)
    'vc.button.stateChanged',
    'vc.slider.valueChanged',              // subscribe-gated: continuous drag
    'vc.slider.monitorValueChanged',       // subscribe-gated: ~per-DMX-frame while monitoring
    'vc.xyPad.positionChanged',            // subscribe-gated: continuous drag
    'vc.xyPad.floorPositionChanged',       // subscribe-gated: continuous drag
    'vc.xyPad.activePresetChanged',
    'vc.frame.currentPageChanged',
    'vc.frame.pageChanged',                // web UI contract name (2026-09): {widgetId, page}
    'vc.clock.timeChanged',                // subscribe-gated: 1Hz, or 10Hz while a Stopwatch/Countdown runs
    'vc.cueList.playbackChanged',          // {widgetId, playbackIndex, running, paused} (contract) or {playbackStatus, ...} (older spec)
    'vc.cueList.sideFaderChanged',
    'vc.speedDial.currentTimeChanged',
    'vc.speedDial.valueChanged',           // web UI contract name (2026-09): {widgetId, ms}
    'vc.speedDial.factorChanged',
    'vc.speedDial.tapChanged',
    'vc.animation.faderLevelChanged',
    'vc.animation.activePresetChanged',
    'vc.animation.styleChanged',           // live colour / algorithm change {algorithmIndex, colors}
    'vc.audioTriggers.captureEnabledChanged',
    'vc.audioTriggers.levelsChanged'       // subscribe-gated: audio-capture rate, fastest stream here
  ];
})();
