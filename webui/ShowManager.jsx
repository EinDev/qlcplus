/**
 * ShowManager.jsx — the Show Manager screen, modelled on qmlui/qml/showmanager/ShowManager.qml
 * (+ TrackDelegate.qml, ShowItem.qml, HeaderAndCursor.qml, TimingUtils.qml). Registers itself as
 * window.QLCScreens.show (Ctrl+5).
 *
 * Server side: controlapi/src/domains/apishowdomain.cpp — functions.get's typeDetail for a Show
 * (time division, tracks with their items, totalDuration), functions.show.setTimeDivision,
 * functions.show.track.* / item.* / rippleInsertTime / rippleCutTime, and the subscribe-gated
 * functions.show.<id>.playhead stream while the Show runs. Playback is the generic
 * functions.start (with the startTime offset = the cursor) / setPause / stop.
 *
 * The timeline is the document: every edit is queued through FF.mutate (revision-gated) and
 * the detail is re-read from the server afterwards (and on every foreign functions.show.* event),
 * so what is drawn is always what the engine has. Drag/resize show an optimistic preview only
 * while the pointer is down. Overlap rules are the server's (= the Qt editor's): a drop on an
 * occupied spot is shifted to the nearest free one the server suggests, exactly like the QML
 * drag; a group drop that still does not fit is refused as a whole.
 *
 * Not mirrored (desktop-only or no API yet): the preview-at-cursor scrub mode, the stretch
 * toggle (rescales a Chaser's steps on resize), track Spout output size, the legacy timing
 * conversion dialog, audio waveforms / beat markers inside items.
 */
(function () {
  'use strict';
  const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomTextInput, SectionBox, CustomPopupDialog, ShortcutHint, FaIcon } = window.PatchDesignSystem_5432c9;
  const FF = window.FF;

  const TRACK_H = 60;        /* UISettings.mediumItemHeight */
  const TRACK_W = 170;       /* UISettings.bigItemHeight * 1.6 */
  const HEADER_H = 30;       /* UISettings.iconSizeMedium */
  const BASE_TICK = 60;      /* px per ruler tick (18 * pixelDensity in the QML) */
  const TAIL_MS = 300000;    /* the ruler runs 5 minutes past the last item, like HeaderAndCursor */
  const SNAP_PX = 8;         /* edge-snap distance while dragging */
  const DIVISIONS = [['time', 'Time'], ['bpm_4_4', 'BPM 4/4'], ['bpm_3_4', 'BPM 3/4'], ['bpm_2_4', 'BPM 2/4']];
  const BEATS = { bpm_4_4: 4, bpm_3_4: 3, bpm_2_4: 2 };
  const NO_ID = '4294967295';

  /* ---- time maths ------------------------------------------------------------------------------ */
  const pad = (n, w) => String(n).padStart(w, '0');
  /** TimeUtils.msToStringWithPrecision: hh:mm:ss.cc (1 decimal while playing) */
  function fmtTime(ms, decimals = 2) {
    ms = Math.max(0, Math.round(ms || 0));
    const h = Math.floor(ms / 3600000), m = Math.floor(ms / 60000) % 60, s = Math.floor(ms / 1000) % 60;
    const frac = decimals === 1 ? Math.floor((ms % 1000) / 100) : Math.floor((ms % 1000) / 10);
    return pad(h, 2) + ':' + pad(m, 2) + ':' + pad(s, 2) + '.' + pad(frac, decimals);
  }
  /** TimeUtils.timeToQlcString for the timing panel: mm:ss.mmm (or h:mm:ss.mmm) */
  function fmtFull(ms) {
    ms = Math.max(0, Math.round(ms || 0));
    const h = Math.floor(ms / 3600000), m = Math.floor(ms / 60000) % 60, s = Math.floor(ms / 1000) % 60, f = ms % 1000;
    return (h ? h + ':' : '') + pad(m, 2) + ':' + pad(s, 2) + '.' + pad(f, 3);
  }
  /** "1:02.500", "62.5", "62500" (plain ms) -> ms; null when unparsable */
  function parseTime(text) {
    const t = String(text || '').trim();
    if (!t) return null;
    if (/^\d+$/.test(t)) return Number(t);
    const m = t.match(/^(?:(\d+):)?(?:(\d+):)?(\d+)(?:\.(\d{1,3}))?$/);
    if (!m) return null;
    const parts = [m[1], m[2]].filter(x => x != null).map(Number);
    let ms = Number(m[3]) * 1000 + (m[4] ? Number((m[4] + '00').slice(0, 3)) : 0);
    if (parts.length === 1) ms += parts[0] * 60000;
    if (parts.length === 2) ms += parts[0] * 3600000 + parts[1] * 60000;
    return ms;
  }
  function minDuration(division) { return division === 'time' ? 1 : 125; }
  /** ms per ruler tick: timeScale seconds in Time mode, one bar in Beats mode */
  function tickMs(division, timeScale, bpm) {
    if (division === 'time') return timeScale * 1000;
    return bpm > 0 ? (60000 / bpm) * (BEATS[division] || 4) : 0;
  }
  function tickPx(division, timeScale) { return division === 'time' ? BASE_TICK : BASE_TICK * timeScale; }

  /* ---- overlap rules (ShowMoveHelper, half-open intervals) --------------------------------------- */
  const overlaps = (aS, aD, bS, bD) => aS < bS + bD && bS < aS + aD;
  function firstBlocker(items, start, duration, movingIds) {
    for (const it of items) { if (movingIds.has(it.id)) continue; if (overlaps(start, duration, it.startTime, it.duration)) return it; }
    return null;
  }
  /** nearest free start for [start, +duration) on a track, ignoring movingIds (ShowMoveHelper::resolveCollision) */
  function resolveCollision(items, start, duration, movingIds) {
    start = Math.max(0, start);
    if (!firstBlocker(items, start, duration, movingIds)) return start;
    const blocks = items.filter(it => !movingIds.has(it.id)).sort((a, b) => a.startTime - b.startTime).reduce((acc, it) => {
      const end = it.startTime + it.duration;
      if (acc.length && it.startTime <= acc[acc.length - 1][1]) acc[acc.length - 1][1] = Math.max(acc[acc.length - 1][1], end);
      else acc.push([it.startTime, end]);
      return acc;
    }, []);
    let best = -1, bestShift = 0;
    const consider = (gs, ge) => {
      if (ge >= 0 && ge - gs < duration) return;
      let pos = Math.max(start, gs);
      if (ge >= 0) pos = Math.min(pos, ge - duration);
      const shift = Math.abs(pos - start);
      if (best < 0 || shift < bestShift || (shift === bestShift && pos < best)) { best = pos; bestShift = shift; }
    };
    let gs = 0;
    blocks.forEach(b => { consider(gs, b[0]); gs = b[1]; });
    consider(gs, -1);
    return best;
  }

  /* ---- data ------------------------------------------------------------------------------------- */
  /** every Show of the project (functions.list), kept fresh on the structural events */
  function useShows(qlc) {
    const [shows, setShows] = React.useState(null);
    React.useEffect(() => {
      if (!qlc.online) { setShows(null); return undefined; }
      let alive = true, timer = null;
      const load = () => qlc.call('functions.list').then(r => { if (alive) setShows((r.functions || []).filter(f => f.type === 'Show' && !f.hidden)); }).catch(() => {});
      const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
      load();
      const offs = ['functions.created', 'functions.deleted', 'functions.renamed', 'functions.moved', 'core.project.loaded']
        .map(t => qlc.subscribeTo(t, debounced)).concat([qlc.subscribeTo('core.history.changed', d => { if (!FF.isOwnHistory(d)) debounced(); })]);
      return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
    }, [qlc.online]);
    return shows;
  }

  /** the full function list for the picker (name/type/id), same refresh rules */
  function useFunctionList(qlc) {
    const [list, setList] = React.useState([]);
    React.useEffect(() => {
      if (!qlc.online) { setList([]); return undefined; }
      let alive = true, timer = null;
      const load = () => qlc.call('functions.list').then(r => { if (alive) setList((r.functions || []).filter(f => !f.hidden)); }).catch(() => {});
      const debounced = () => { clearTimeout(timer); timer = setTimeout(load, 150); };
      load();
      const offs = ['functions.created', 'functions.deleted', 'functions.renamed', 'core.project.loaded'].map(t => qlc.subscribeTo(t, debounced));
      return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
    }, [qlc.online]);
    return list;
  }

  const SHOW_TOPICS = ['functions.show.timeDivisionChanged', 'functions.show.tracksChanged', 'functions.show.itemsChanged',
    'functions.show.track.added', 'functions.show.track.removed', 'functions.show.track.renamed', 'functions.show.track.muteChanged',
    'functions.show.item.added', 'functions.show.item.removed', 'functions.show.item.moved', 'functions.show.item.resized',
    'functions.show.item.colorChanged', 'functions.show.item.lockedChanged', 'functions.renamed', 'functions.updated'];

  /** functions.get of the open Show, re-read on every foreign change of it */
  function useShowDetail(qlc, showId) {
    const [detail, setDetail] = React.useState(null);
    const load = React.useCallback(() => {
      if (!qlc.online || !showId) return Promise.resolve();
      return qlc.call('functions.get', { functionId: String(showId) }).then(d => setDetail(cur => (cur && String(cur.id) !== String(showId)) ? cur : d)).catch(() => {});
    }, [qlc.online, showId]);
    React.useEffect(() => { setDetail(null); load(); }, [showId, qlc.online]);
    React.useEffect(() => qlc.subscribeTo('core.history.changed', d => { if (!FF.isOwnHistory(d)) load(); }), [load]);
    React.useEffect(() => qlc.subscribeTo('core.project.loaded', () => load()), [load]);
    FF.useForeignEvents(qlc, SHOW_TOPICS, (topic, d) => {
      const id = d && (d.showId != null ? d.showId : d.functionId);
      if (id != null && String(id) === String(showId)) load();
    }, [showId]);
    return [detail, load, setDetail];
  }

  /** running/paused of the Show: seeded from functions.get, kept by functions.status.changed */
  function useRunState(qlc, showId, detail) {
    const [state, setState] = React.useState({ running: false, paused: false });
    React.useEffect(() => { if (detail) setState({ running: !!detail.running, paused: !!detail.paused }); }, [detail && detail.running, detail && detail.paused, showId]);
    React.useEffect(() => qlc.subscribeTo('functions.status.changed', d => {
      const id = d ? (d.functionId != null ? d.functionId : d.id) : null;
      if (id != null && String(id) === String(showId)) setState({ running: !!d.running, paused: !!d.paused });
    }), [showId, qlc.online]);
    return state;
  }

  /** the gated playhead stream of the open Show */
  function usePlayhead(qlc, showId, onTime) {
    const ref = React.useRef(onTime);
    ref.current = onTime;
    React.useEffect(() => {
      if (!qlc.online || !showId) return undefined;
      const topic = 'functions.show.' + showId + '.playhead';
      qlc.call('subscribe', { topics: [topic] }).catch(() => {});
      const off = qlc.subscribeTo(topic, d => { if (d && d.time != null) ref.current(Number(d.time)); });
      return () => { off(); qlc.call('unsubscribe', { topics: [topic] }).catch(() => {}); };
    }, [qlc.online, showId]);
  }

  const glyphButton = (glyph, tooltip, disabled, onClick, extra) => (
    <IconButton faSource={glyph} size={26} tooltip={tooltip} disabled={!!disabled} onClick={onClick} {...(extra || {})} />
  );
  /** Font Awesome has no free "stop" in the bundle: a square, as an image the IconButton can show */
  const STOP_ICON = 'data:image/svg+xml;utf8,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><rect x="5" y="5" width="14" height="14" rx="2" fill="#f0f0f0"/></svg>');

  /* ---- the screen ------------------------------------------------------------------------------ */
  function ShowManager() {
    const D = window.QLCData;
    const qlc = useQLC();
    const live = qlc.online;
    const shows = useShows(qlc);
    const allFunctions = useFunctionList(qlc);
    const [showId, setShowId] = React.useState(() => { try { return new URLSearchParams(location.search).get('show') || ''; } catch (e) { return ''; } });
    const [detail, reload] = useShowDetail(qlc, showId);
    const run = useRunState(qlc, showId, detail);
    const [cursor, setCursorState] = React.useState(0);
    const movedWhilePaused = React.useRef(false);
    const [selection, setSelection] = React.useState([]);        /* item ids */
    const [selectedTrackId, setSelectedTrackId] = React.useState(null);
    const [timeScale, setTimeScale] = React.useState(5);
    const [grid, setGrid] = React.useState(false);
    const [clipboard, setClipboard] = React.useState([]);
    const [dialog, setDialog] = React.useState(null);
    const [notice, setNotice] = React.useState('');
    const [drag, setDrag] = React.useState(null);                 /* {kind:'move'|'resize', ids, dx, dTrack, ...} */
    const [renamingTrack, setRenamingTrack] = React.useState(null);
    const [picked, setPicked] = React.useState([]);              /* function ids ticked in the picker */
    const [filter, setFilter] = React.useState('');
    const [collapsed, setCollapsed] = React.useState({});
    const areaRef = React.useRef(null);
    const lastError = FF.useLastError();
    const unsupported = qlc.isUnsupported('functions.show.item.add');

    const td = (detail && detail.typeDetail) || {};
    const division = td.timeDivisionType || 'time';
    const bpm = td.timeDivisionBPM || 120;
    const tracks = td.tracks || [];
    const total = Number(td.totalDuration) || 0;
    const tick = tickPx(division, timeScale), tMs = tickMs(division, timeScale, bpm);
    const msToPx = React.useCallback(ms => tMs > 0 ? ms * tick / tMs : 0, [tick, tMs]);
    const pxToMs = React.useCallback(px => tick > 0 ? px * tMs / tick : 0, [tick, tMs]);
    const contentW = Math.max(800, Math.ceil(msToPx(total + TAIL_MS)));
    const contentH = Math.max(3, tracks.length + 2) * TRACK_H;
    const itemsById = React.useMemo(() => { const m = new Map(); tracks.forEach((t, ti) => t.items.forEach(it => m.set(String(it.id), Object.assign({ trackIndex: ti, trackId: t.id }, it)))); return m; }, [tracks]);
    const selectedItems = selection.map(id => itemsById.get(String(id))).filter(Boolean);
    const anyLocked = selectedItems.some(it => it.locked);

    /* the first Show is opened when none is picked (or the picked one is gone) */
    React.useEffect(() => {
      if (!shows) return;
      if (showId && shows.some(s => String(s.id) === String(showId))) return;
      setShowId(shows.length ? String(shows[0].id) : '');
    }, [shows]);
    React.useEffect(() => { setSelection([]); setSelectedTrackId(null); setCursorState(0); movedWhilePaused.current = false; setTimeScale(5); }, [showId]);
    React.useEffect(() => { setTimeScale(division === 'time' ? 5 : 1); }, [division]);
    React.useEffect(() => { if (!notice) return undefined; const t = setTimeout(() => setNotice(''), 6000); return () => clearTimeout(t); }, [notice]);
    /* items that disappeared (deleted elsewhere) leave the selection */
    React.useEffect(() => { setSelection(s => s.filter(id => itemsById.has(String(id)))); }, [itemsById]);

    usePlayhead(qlc, showId, t => setCursorState(t));
    const setCursor = (ms) => { ms = Math.max(0, Math.round(ms)); if (run.running && run.paused) movedWhilePaused.current = true; setCursorState(ms); };

    /* ---- mutations ---- */
    const showMutate = (method, params, opts) => FF.mutate(qlc, method, Object.assign({ showId: String(showId) }, params), opts).then(r => { reload(); return r; }).catch(e => { reload(); throw e; });
    const snapMs = (ms) => { if (!grid || tMs <= 0) return ms; return Math.round(ms / tMs) * tMs; };

    const play = async () => {
      if (!live || !showId) return;
      try {
        if (!run.running) { movedWhilePaused.current = false; await qlc.call('functions.start', { functionId: String(showId), startTime: Math.round(cursor) }); return; }
        if (run.paused) {
          if (movedWhilePaused.current) {
            /* ShowManager::playShow: a cursor moved while paused restarts the Show from there */
            await qlc.call('functions.stop', { functionId: String(showId) });
            for (let i = 0; i < 40; i++) { const g = await qlc.call('functions.get', { functionId: String(showId) }); if (!g.running) break; await new Promise(r => setTimeout(r, 50)); }
            movedWhilePaused.current = false;
            await qlc.call('functions.start', { functionId: String(showId), startTime: Math.round(cursor) });
          } else await qlc.call('functions.setPause', { functionId: String(showId), paused: false });
          return;
        }
        await qlc.call('functions.setPause', { functionId: String(showId), paused: true });
      } catch (e) { FF.reportError(e, 'playback'); }
    };
    const stop = () => {
      if (!live || !showId) return;
      if (run.running) { movedWhilePaused.current = false; qlc.call('functions.stop', { functionId: String(showId) }).catch(e => FF.reportError(e, 'functions.stop')); return; }
      if (cursor !== 0) setCursorState(0);
    };

    const createShow = () => FF.mutate(qlc, 'functions.create', { type: 'Show' }).then(r => { if (r && r.functionId != null) setShowId(String(r.functionId)); }).catch(() => {});
    const renameShow = (name) => { if (name && detail && name !== detail.name) FF.mutate(qlc, 'functions.rename', { functionId: String(showId), name }).then(reload).catch(() => {}); };
    const setDivision = (type) => showMutate('functions.show.setTimeDivision', { functionId: String(showId), timeDivisionType: type, bpm }).catch(() => {});
    const setBpm = (v) => showMutate('functions.show.setTimeDivision', { functionId: String(showId), timeDivisionType: division, bpm: v }, { key: 'show:bpm:' + showId }).catch(() => {});

    const addTrack = () => showMutate('functions.show.track.add', {}).then(r => { if (r && r.trackId != null) setSelectedTrackId(String(r.trackId)); }).catch(() => {});
    const renameTrack = (trackId, name) => { setRenamingTrack(null); if (name) showMutate('functions.show.track.rename', { trackId: String(trackId), name }).catch(() => {}); };
    const muteTrack = (t) => showMutate('functions.show.track.setMute', { trackId: String(t.id), mute: !t.mute }).catch(() => {});
    const soloTrack = (t) => {
      /* ShowManager::setTrackSolo: "solo" is every other track muted; a second press unmutes them all */
      const isSolo = !t.mute && tracks.length > 1 && tracks.every(o => o.id === t.id || o.mute);
      showMutate('functions.show.track.setSolo', { trackId: String(t.id), solo: !isSolo }).catch(() => {});
    };
    const moveTrack = (t, direction) => showMutate('functions.show.track.move', { trackId: String(t.id), direction }).then(r => { if (r && r.trackId != null) setSelectedTrackId(String(r.trackId)); }).catch(() => {});
    const requestTrackDeletion = (t) => { if (!t) return; if (t.items.length) setDialog({ kind: 'deleteTrack', track: t }); else deleteTrack(t); };
    const deleteTrack = (t) => { setDialog(null); showMutate('functions.show.track.remove', { trackId: String(t.id) }).then(() => { if (String(selectedTrackId) === String(t.id)) setSelectedTrackId(null); }).catch(() => {}); };

    const deleteItems = (ids) => { setDialog(null); if (!ids.length) return; showMutate('functions.show.item.remove', { itemIds: ids.map(String) }).then(() => setSelection([])).catch(() => {}); };
    const setLocked = (locked) => FF.mutateSeq(qlc, selectedItems.map(it => ['functions.show.item.setLocked', { showId: String(showId), itemId: String(it.id), locked }])).then(reload).catch(reload);
    const setColor = (color) => FF.mutateSeq(qlc, selectedItems.map(it => ['functions.show.item.setColor', { showId: String(showId), itemId: String(it.id), color }])).then(reload).catch(reload);
    const resizeItem = (it, duration) => {
      duration = Math.max(minDuration(division), Math.round(duration));
      /* clamp to the next clip on the track (the Qt editor refuses the overlap; here it stops at the edge) */
      const track = tracks[it.trackIndex];
      let max = Infinity;
      if (track) track.items.forEach(o => { if (String(o.id) !== String(it.id) && o.startTime >= it.startTime) max = Math.min(max, o.startTime - it.startTime); });
      if (duration > max) { duration = max; setNotice('Resize stopped at the next item on the track'); }
      if (duration === it.duration) return Promise.resolve();
      return showMutate('functions.show.item.resize', { itemId: String(it.id), duration }).catch(e => { if (e && e.details && e.details.maxDuration != null) setNotice('Cannot resize: ' + e.message); });
    };
    /** move every unlocked item of `ids` by the same delta, the grabbed one landing on the
        nearest free spot (like ShowManager::checkAndMoveItems); dstTrackIndex past the last
        track creates one first */
    const moveItems = async (ids, grabbedId, wantedStart, dstTrackIndex, timeDeltaOnly) => {
      const items = ids.map(id => itemsById.get(String(id))).filter(it => it && !it.locked);
      const grabbed = itemsById.get(String(grabbedId));
      if (!grabbed || grabbed.locked || !items.length) return;
      const moving = new Set(items.map(it => String(it.id)));
      let trackList = tracks;
      const minTrack = Math.min.apply(null, items.map(it => it.trackIndex));
      let trackDelta = Math.max(dstTrackIndex - grabbed.trackIndex, -minTrack);
      if (timeDeltaOnly) trackDelta = 0;
      const dstIdx = grabbed.trackIndex + trackDelta;
      const dstItems = dstIdx < trackList.length ? trackList[dstIdx].items : [];
      /* grid snap, then collision-resolve the grabbed item's spot */
      let requested = Math.max(0, snapMs(wantedStart));
      const resolved = resolveCollision(dstItems, requested, grabbed.duration, moving);
      if (resolved !== requested) setNotice('Moved to the nearest free spot');
      const minStart = Math.min.apply(null, items.map(it => it.startTime));
      const timeDelta = Math.max(resolved - grabbed.startTime, -minStart);
      /* every item of the group must land free of clips outside the group */
      for (const it of items) {
        const ti = it.trackIndex + trackDelta;
        if (ti >= trackList.length) continue;
        const b = firstBlocker(trackList[ti].items, it.startTime + timeDelta, it.duration, moving);
        if (b) { setNotice('Cannot move here: "' + (b.functionName || 'another item') + '" is in the way'); return; }
      }
      if (timeDelta === 0 && trackDelta === 0) return;
      try {
        /* tracks the group needs below the last one */
        const maxDst = Math.max.apply(null, items.map(it => it.trackIndex + trackDelta));
        const newTrackIds = [];
        while (trackList.length + newTrackIds.length <= maxDst) {
          const r = await FF.mutate(qlc, 'functions.show.track.add', { showId: String(showId) });
          newTrackIds.push(String(r.trackId));
        }
        const trackIdAt = (i) => i < trackList.length ? String(trackList[i].id) : newTrackIds[i - trackList.length];
        /* order so that group mates never block each other on the way */
        const ordered = items.slice().sort((a, b) => (trackDelta > 0 ? b.trackIndex - a.trackIndex : a.trackIndex - b.trackIndex) || (timeDelta > 0 ? b.startTime - a.startTime : a.startTime - b.startTime));
        for (const it of ordered)
          await FF.mutate(qlc, 'functions.show.item.move', { showId: String(showId), itemId: String(it.id), trackId: trackIdAt(it.trackIndex + trackDelta), startTime: Math.round(it.startTime + timeDelta) });
      } catch (e) {
        if (e && e.details && e.details.suggestedStartTime != null) setNotice('Cannot move here: ' + e.message);
      }
      reload();
    };

    const copy = () => { if (!selectedItems.length) return; setClipboard(selectedItems.map(it => ({ functionId: it.functionId, startTime: it.startTime, duration: it.duration, color: it.color, locked: it.locked, trackIndex: it.trackIndex }))); };
    const paste = async () => {
      /* ShowManager::pasteFromClipboard: the earliest copy goes to the cursor, the others keep
         their relative offsets and stay on their sources' tracks; the anchor spot is
         collision-resolved, then the whole group must be free (nothing is exempt, not even the sources) */
      if (!clipboard.length || !live) return;
      const earliest = clipboard.reduce((a, b) => b.startTime < a.startTime ? b : a, clipboard[0]);
      const anchorItems = tracks[earliest.trackIndex] ? tracks[earliest.trackIndex].items : [];
      const resolved = resolveCollision(anchorItems, Math.max(0, cursor), earliest.duration, new Set());
      const delta = resolved - earliest.startTime;
      for (const c of clipboard) {
        const tr = tracks[c.trackIndex];
        if (!tr) { setNotice('Cannot paste: the source track is gone'); return; }
        const b = firstBlocker(tr.items, c.startTime + delta, c.duration, new Set());
        if (b) { setNotice('The copied items do not fit at the cursor as a group: "' + (b.functionName || 'another item') + '" is in the way'); return; }
      }
      const added = [];
      try {
        for (const c of clipboard) {
          const r = await FF.mutate(qlc, 'functions.show.item.add', { showId: String(showId), trackId: String(tracks[c.trackIndex].id), functionId: String(c.functionId), startTime: Math.round(c.startTime + delta), duration: c.duration, color: c.color });
          added.push(String(r.itemId));
          if (c.locked) await FF.mutate(qlc, 'functions.show.item.setLocked', { showId: String(showId), itemId: String(r.itemId), locked: true });
        }
      } catch (e) { /* reported by FF.mutate */ }
      await reload();
      setSelection(added);
    };

    /** the picker's "Add at cursor": one item per picked function, back to back from the cursor,
        on the selected track (or the first one; a new one when the Show has none) */
    const addPicked = async () => {
      if (!picked.length || !live) return;
      let trackId = selectedTrackId && tracks.some(t => String(t.id) === String(selectedTrackId)) ? String(selectedTrackId) : (tracks[0] ? String(tracks[0].id) : null);
      try {
        if (!trackId) { const r = await FF.mutate(qlc, 'functions.show.track.add', { showId: String(showId) }); trackId = String(r.trackId); }
        let start = Math.max(0, Math.round(cursor));
        const added = [];
        for (const fid of picked) {
          const g = await qlc.call('functions.get', { functionId: String(fid) });
          const duration = Number(g.totalDuration) || (division === 'time' ? 5000 : 4000);
          let r;
          try { r = await FF.mutate(qlc, 'functions.show.item.add', { showId: String(showId), trackId, functionId: String(fid), startTime: start }); }
          catch (e) {
            if (!(e && e.details && e.details.suggestedStartTime != null)) throw e;
            start = Number(e.details.suggestedStartTime);
            setNotice('Added at the nearest free spot');
            r = await FF.mutate(qlc, 'functions.show.item.add', { showId: String(showId), trackId, functionId: String(fid), startTime: start });
          }
          added.push(String(r.itemId));
          start += duration;
        }
        setPicked([]);
        await reload();
        setSelection(added);
        setSelectedTrackId(trackId);
      } catch (e) { reload(); }
    };

    const insertTime = (length) => showMutate('functions.show.rippleInsertTime', { cursorTime: Math.round(cursor), length: Math.max(minDuration(division), length) }).then(r => { if (r && r.changed === false) setNotice('Nothing to insert at the cursor'); }).catch(() => {});
    const cutTime = (length) => showMutate('functions.show.rippleCutTime', { cursorTime: Math.round(cursor), length: Math.max(minDuration(division), length) }).then(r => { if (r && r.changed === false) setNotice('Nothing to cut at the cursor'); }).catch(() => {});
    const alignStart = () => FF.mutateSeq(qlc, selectedItems.filter(it => !it.locked).map(it => ['functions.show.item.move', { showId: String(showId), itemId: String(it.id), trackId: String(it.trackId), startTime: Math.round(cursor) }])).then(reload).catch(reload);
    const alignEnd = () => FF.mutateSeq(qlc, selectedItems.filter(it => !it.locked).map(it => ['functions.show.item.resize', { showId: String(showId), itemId: String(it.id), duration: Math.max(minDuration(division), Math.round(cursor - it.startTime)) }])).then(reload).catch(reload);
    const setStart = (it, v) => showMutate('functions.show.item.move', { itemId: String(it.id), trackId: String(it.trackId), startTime: Math.max(0, Math.round(v)) }).catch(() => {});

    /* ---- keyboard: Space play/pause, Delete, Ctrl+C/V, Escape, Home ---- */
    React.useEffect(() => {
      const typing = () => { const el = document.activeElement; return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable); };
      const onButton = () => { const el = document.activeElement; return !!el && (el.tagName === 'BUTTON' || el.getAttribute('role') === 'button'); };
      const k = (e) => {
        if (dialog || typing()) return;
        if (e.key === ' ' && !e.ctrlKey && !onButton()) { e.preventDefault(); e.stopPropagation(); play(); return; }   /* before App.jsx's BPM tap (capture phase) */
        if (e.key === 'Delete' && selection.length) { e.preventDefault(); e.stopPropagation(); setDialog({ kind: 'deleteItems', ids: selection.slice() }); return; }
        if (e.key === 'Escape') { setSelection([]); return; }
        if (e.key === 'Home' && !e.ctrlKey) { e.preventDefault(); setCursor(0); return; }
        if (e.ctrlKey && e.key.toLowerCase() === 'c') { e.preventDefault(); e.stopPropagation(); copy(); }
        else if (e.ctrlKey && e.key.toLowerCase() === 'v') { e.preventDefault(); e.stopPropagation(); paste(); }
        else if (e.ctrlKey && e.key.toLowerCase() === 'a') { e.preventDefault(); setSelection(Array.from(itemsById.keys())); }
      };
      window.addEventListener('keydown', k, true);
      return () => window.removeEventListener('keydown', k, true);
    });

    /* ---- pointer interaction on the timeline ---- */
    const areaPos = (e) => { const r = areaRef.current.getBoundingClientRect(); return { x: e.clientX - r.left + areaRef.current.scrollLeft, y: e.clientY - r.top + areaRef.current.scrollTop }; };
    const onAreaPointerDown = (e) => {
      if (e.button !== 0 || !areaRef.current) return;
      const p = areaPos(e);
      if (p.x < TRACK_W) return;
      if (p.y < HEADER_H) {
        /* the ruler: the cursor follows the press and any drag (HeaderAndCursor.qml) */
        setCursor(pxToMs(p.x - TRACK_W));
        setSelection([]);
        const move = (ev) => { const q = areaPos(ev); setCursor(pxToMs(Math.max(0, q.x - TRACK_W))); };
        const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
        window.addEventListener('pointermove', move); window.addEventListener('pointerup', up);
        return;
      }
      /* empty area: cursor + clear selection (rubber band: a drag selects every item it touches) */
      const startX = p.x, startY = p.y;
      let band = null;
      const move = (ev) => {
        const q = areaPos(ev);
        if (!band && Math.abs(q.x - startX) < 8 && Math.abs(q.y - startY) < 8) return;
        band = { x: Math.min(startX, q.x), y: Math.min(startY, q.y), w: Math.abs(q.x - startX), h: Math.abs(q.y - startY) };
        setDrag({ kind: 'band', band });
      };
      const up = () => {
        window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up);
        setDrag(null);
        if (band) {
          const ids = [];
          tracks.forEach((t, ti) => t.items.forEach(it => {
            const x = TRACK_W + msToPx(it.startTime), w = Math.max(2, msToPx(it.duration)), y = HEADER_H + ti * TRACK_H;
            if (x < band.x + band.w && band.x < x + w && y < band.y + band.h && band.y < y + TRACK_H) ids.push(String(it.id));
          }));
          setSelection(ids);
        } else { setCursor(pxToMs(startX - TRACK_W)); setSelection([]); }
      };
      window.addEventListener('pointermove', move); window.addEventListener('pointerup', up);
    };
    const onItemPointerDown = (e, it) => {
      e.stopPropagation();
      if (e.button !== 0) return;
      const wasSelected = selection.indexOf(String(it.id)) !== -1;
      let ids = e.ctrlKey ? (wasSelected ? selection.filter(x => x !== String(it.id)) : selection.concat([String(it.id)])) : (wasSelected ? selection : [String(it.id)]);
      setSelection(ids);
      if (it.locked || e.ctrlKey || !live) return;
      const group = ids.map(id => itemsById.get(id)).filter(x => x && !x.locked).map(x => String(x.id));
      const p0 = areaPos(e);
      let moved = false, last = { dx: 0, dTrack: 0 };
      const edges = [];
      tracks.forEach(t => t.items.forEach(o => { if (group.indexOf(String(o.id)) === -1) { edges.push(msToPx(o.startTime)); edges.push(msToPx(o.startTime + o.duration)); } }));
      const move = (ev) => {
        const p = areaPos(ev);
        let dx = p.x - p0.x;
        if (!moved && Math.abs(dx) < 3 && Math.abs(p.y - p0.y) < 3) return;
        moved = true;
        /* snap the grabbed item's start or end edge to another item's edge */
        const sx = msToPx(it.startTime) + dx, ex = sx + msToPx(it.duration);
        let snapped = null;
        edges.forEach(edge => { if (Math.abs(sx - edge) < SNAP_PX) { dx += edge - sx; snapped = edge; } else if (Math.abs(ex - edge) < SNAP_PX) { dx += edge - ex; snapped = edge; } });
        const dTrack = Math.round((p.y - p0.y) / TRACK_H);
        last = { dx, dTrack, snapped };
        setDrag({ kind: 'move', ids: group, dx, dTrack, snapped });
      };
      const up = () => {
        window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up);
        setDrag(null);
        /* ShowManager::selectItemByClick: a plain click on one item of a multi-selection makes it
           the only selected item (a drag that started on it moved the whole group instead) */
        if (!moved) { if (wasSelected && ids.length > 1) setSelection([String(it.id)]); return; }
        const wanted = it.startTime + pxToMs(last.dx);
        moveItems(group, String(it.id), wanted, Math.max(0, Math.min(tracks.length, it.trackIndex + last.dTrack)), false);
      };
      window.addEventListener('pointermove', move); window.addEventListener('pointerup', up);
    };
    const onHandlePointerDown = (e, it) => {
      e.stopPropagation();
      if (e.button !== 0 || it.locked || !live) return;
      setSelection([String(it.id)]);
      const p0 = areaPos(e);
      let dw = 0;
      const move = (ev) => { dw = areaPos(ev).x - p0.x; setDrag({ kind: 'resize', id: String(it.id), dw }); };
      const up = () => {
        window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up);
        setDrag(null);
        if (dw === 0) return;
        resizeItem(it, snapMs(it.duration + pxToMs(dw)) || minDuration(division));
      };
      window.addEventListener('pointermove', move); window.addEventListener('pointerup', up);
    };

    /* ---- render ---- */
    const showModel = (shows || []).map(s => ({ mLabel: s.name, mValue: String(s.id) }));
    const timeLabel = fmtTime(cursor, run.running && !run.paused ? 1 : 2);
    const ticks = [];
    if (tMs > 0) for (let x = 0, i = 0; x <= contentW && i < 2000; x += tick, i++) ticks.push({ x, ms: i * tMs, bar: i });
    const sub = division === 'time' ? Math.min(5, Math.round(timeScale)) : (BEATS[division] || 4);
    const selectedTrack = tracks.find(t => String(t.id) === String(selectedTrackId));
    const selectedTrackIndex = tracks.indexOf(selectedTrack);
    const single = selectedItems.length === 1 ? selectedItems[0] : null;
    const pickerList = allFunctions.filter(f => String(f.id) !== String(showId) && (!filter || f.name.toLowerCase().indexOf(filter.toLowerCase()) !== -1));
    const sectionOpen = (k) => !collapsed[k];
    const toggleSection = (k) => setCollapsed(c => Object.assign({}, c, { [k]: !c[k] }));
    const FUNCTION_ICONS = { Scene: 'scene', Chaser: 'chaser', Sequence: 'sequence', EFX: 'efx', Collection: 'collection', RGBMatrix: 'rgbmatrix', Show: 'showmanager', Script: 'script', Audio: 'audio', Video: 'video' };

    return (
      <div data-show="screen" style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0, background: 'var(--bg-medium)' }}>
        <ViewToolbar variant="sub">
          <RobotoText label="Show" fontSize={14} height={30} style={{ paddingLeft: 4 }} />
          <CustomComboBox width={200} height={26} currValue={showId} onValueChanged={v => setShowId(String(v))} model={showModel.length ? showModel : [{ mLabel: live ? 'No Show yet' : 'Connect first', mValue: '' }]} />
          {glyphButton('fa_plus', 'Create a new Show', !live, createShow, { 'data-show': 'new' })}
          <RobotoText label="Name" fontSize={14} height={30} style={{ paddingLeft: 6 }} />
          <span style={{ width: 180 }}>
            <CustomTextInput text={detail ? detail.name : ''} editing width="100%" height={22} disabled={!detail} onTextConfirmed={renameShow} data-show="name" />
          </span>
          <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
          <label title={selectedItems.length ? 'Show items color' : 'Show items color — select items first'} style={{ display: 'inline-flex', alignItems: 'center', opacity: selectedItems.length ? 1 : .45 }}>
            <img src={D.icon('color')} alt="" style={{ width: 22, height: 22 }} />
            <input type="color" data-show="color" disabled={!selectedItems.length} value={single ? single.color : '#646464'} onChange={e => setColor(e.target.value)} style={{ width: 22, height: 22, padding: 0, border: 'none', background: 'transparent', cursor: 'pointer' }} />
          </label>
          <IconButton imgSource={D.icon(anyLocked ? 'unlock' : 'lock')} size={26} disabled={!selectedItems.length} tooltip={anyLocked ? 'Unlock the selected items' : 'Lock the selected items'} onClick={() => setLocked(!anyLocked)} data-show="lock" />
          <IconButton imgSource={D.icon('grid')} size={26} checked={grid} tooltip="Snap to grid" onClick={() => setGrid(!grid)} />
          <IconButton faSource="fa_square_minus" faColor="crimson" size={26} disabled={!selectedItems.length && !selectedTrack} tooltip="Remove the selected items, or the selected track when no item is selected" data-show="delete"
            onClick={() => { if (selectedItems.length) setDialog({ kind: 'deleteItems', ids: selection.slice() }); else requestTrackDeletion(selectedTrack); }} />
          {glyphButton('fa_square_plus', 'Copy the selected items in the clipboard (Ctrl C)', !selectedItems.length, copy, { 'data-show': 'copy' })}
          <IconButton imgSource={D.icon('import')} size={26} disabled={!clipboard.length} tooltip={'Paste items in the clipboard at cursor position (Ctrl V)' + (clipboard.length ? ' — ' + clipboard.length + ' item(s)' : '')} onClick={paste} data-show="paste" />
          <ToolbarSpacer />
          <span data-show="time" title="Cursor position" style={{ display: 'inline-flex', alignItems: 'center', height: 24, padding: '0 10px', border: '1px solid var(--fg-medium)', borderRadius: 5, color: 'var(--fg-main)', font: '400 14px var(--font-mono)' }}>{timeLabel}</span>
          <RobotoText label={'/ ' + fmtTime(total, 2)} fontSize={13} labelColor="var(--fg-light)" height={30} style={{ fontFamily: 'var(--font-mono)' }} />
          <ShortcutHint keys="Space" placement="corner">
            <IconButton faSource={run.running && !run.paused ? 'fa_pause' : 'fa_play'} size={26} disabled={!detail} data-show="play"
              bgColor={run.paused ? 'green' : run.running ? 'darkorange' : undefined}
              tooltip={run.running && !run.paused ? 'Pause' : run.paused ? 'Resume' : 'Play from the cursor'} onClick={play} />
          </ShortcutHint>
          <IconButton imgSource={STOP_ICON} size={26} disabled={!detail} tooltip={run.running ? 'Stop' : 'Rewind'} onClick={stop} bgColor={run.running ? 'red' : undefined} data-show="stop" />
          <ToolbarSpacer />
          <RobotoText label="Markers" fontSize={14} height={30} />
          <CustomComboBox width={110} height={26} currValue={division} onValueChanged={v => { if (v !== division) setDivision(v); }} model={DIVISIONS.map(([v, l]) => ({ mLabel: l, mValue: v }))} />
          <RobotoText label="BPM" fontSize={14} height={30} />
          <CustomSpinBox value={bpm} from={20} to={1000} width={80} height={26} disabled={!detail || division === 'time'} onValueModified={setBpm} />
          <IconButton faSource="fa_square_minus" size={26} tooltip="Zoom out" onClick={() => setTimeScale(s => s >= 1 ? s + 1 : Math.round((s + 0.1) * 10) / 10)} />
          <IconButton faSource="fa_square_plus" size={26} tooltip="Zoom in" onClick={() => setTimeScale(s => s > 1 ? s - 1 : Math.max(0.1, Math.round((s - 0.1) * 10) / 10))} />
        </ViewToolbar>

        {notice || lastError ? (
          <div style={{ padding: '3px 10px', background: 'var(--bg-strong)', borderBottom: 'var(--border-dark)', display: 'flex', alignItems: 'center', gap: 8 }} data-show="notice">
            <RobotoText label={notice || (lastError.method + ': ' + lastError.message)} fontSize={13} labelColor={notice ? 'var(--fg-light)' : 'var(--override-red)'} height={18} />
          </div>
        ) : null}

        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          {/* ---- timeline ---- */}
          <div ref={areaRef} onPointerDown={onAreaPointerDown} data-show="timeline"
            style={{ flex: 1, minWidth: 0, overflow: 'auto', position: 'relative', background: 'var(--bg-strong)', userSelect: 'none', cursor: 'default' }}>
            {!live ? <div style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center' }}><RobotoText label="Connect to a QLC+ instance to edit its Shows" fontSize={16} labelColor="var(--fg-medium)" /></div>
              : !showId ? <div style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center' }}><RobotoText label="Create a Show with the + button to start a timeline" fontSize={16} labelColor="var(--fg-medium)" /></div>
              : !detail ? <div style={{ padding: 20 }}><RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /></div> : (
              <div style={{ position: 'relative', width: TRACK_W + contentW, height: HEADER_H + contentH }}>
                {/* ruler */}
                <div style={{ position: 'sticky', top: 0, zIndex: 3, height: HEADER_H, width: TRACK_W + contentW, pointerEvents: 'none' }}>
                  <div style={{ position: 'absolute', left: TRACK_W, top: 0, width: contentW, height: HEADER_H, background: '#000', borderBottom: '1px solid var(--bg-light)', overflow: 'hidden' }}>
                    {ticks.map(t => (
                      <React.Fragment key={t.x}>
                        <div style={{ position: 'absolute', left: t.x, top: 0, width: 1, height: HEADER_H, background: '#fff' }} />
                        <span style={{ position: 'absolute', left: t.x + 3, top: 5, color: '#fff', font: '400 12px var(--font-roboto)', whiteSpace: 'nowrap' }}>{division === 'time' ? fmtTime(t.ms, 2).slice(3, 8) : t.bar}</span>
                        {sub > 1 ? Array.from({ length: sub - 1 }, (_, k) => <div key={k} style={{ position: 'absolute', left: t.x + (k + 1) * tick / sub, top: HEADER_H * 0.75, width: 1, height: HEADER_H * 0.25, background: 'var(--bg-light)' }} />) : null}
                      </React.Fragment>
                    ))}
                    <div data-show="cursor-head" style={{ position: 'absolute', left: msToPx(cursor) - 5, top: HEADER_H - 10, width: 10, height: 10, background: 'var(--selection)' }} />
                  </div>
                  {/* corner: track move buttons */}
                  <div style={{ position: 'absolute', left: 0, top: 0, width: TRACK_W, height: HEADER_H, background: 'var(--bg-strong)', borderRight: '3px solid var(--bg-light)', borderBottom: '1px solid var(--bg-light)', display: 'flex', alignItems: 'center', gap: 2, pointerEvents: 'auto', boxSizing: 'border-box' }}>
                    {selectedTrack && selectedTrackIndex > 0 ? <IconButton faSource="fa_chevron_up" size={24} tooltip="Move the selected track up" onClick={() => moveTrack(selectedTrack, 'up')} data-show="track-up" /> : null}
                    {selectedTrack && selectedTrackIndex < tracks.length - 1 ? <IconButton faSource="fa_chevron_down" size={24} tooltip="Move the selected track down" onClick={() => moveTrack(selectedTrack, 'down')} data-show="track-down" /> : null}
                  </div>
                </div>
                {/* track headers (sticky left) */}
                <div style={{ position: 'sticky', left: 0, zIndex: 2, width: TRACK_W, height: contentH, marginTop: 0, float: 'left' }} onPointerDown={e => e.stopPropagation()}>
                  {tracks.map((t, ti) => {
                    const sel = String(t.id) === String(selectedTrackId);
                    const solo = !t.mute && tracks.length > 1 && tracks.every(o => o.id === t.id || o.mute);
                    return (
                      <div key={t.id} data-show="track" data-track-id={t.id} onClick={() => setSelectedTrackId(String(t.id))} onDoubleClick={() => setRenamingTrack({ id: t.id, name: t.name })}
                        style={{ position: 'relative', height: TRACK_H, boxSizing: 'border-box', background: sel ? 'var(--highlight)' : '#313F4A', borderBottom: '2px solid #263039', borderRight: '3px solid var(--bg-light)', cursor: 'pointer', overflow: 'hidden' }}>
                        {renamingTrack && String(renamingTrack.id) === String(t.id) ? (
                          <input autoFocus defaultValue={t.name} data-show="track-name-input" onClick={e => e.stopPropagation()} onBlur={e => renameTrack(t.id, e.target.value.trim())}
                            onKeyDown={e => { if (e.key === 'Enter') renameTrack(t.id, e.target.value.trim()); else if (e.key === 'Escape') setRenamingTrack(null); }}
                            style={{ position: 'absolute', left: 4, top: 6, width: TRACK_W - 70, height: 22, background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', font: '400 14px var(--font-roboto)', padding: '0 4px' }} />
                        ) : <span data-show="track-name" title="Double-click to rename" style={{ position: 'absolute', left: 6, top: 6, right: 66, color: 'var(--fg-main)', font: '400 14px var(--font-roboto)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{t.name}</span>}
                        <button type="button" data-show="solo" title="Solo this track" onClick={e => { e.stopPropagation(); soloTrack(t); }}
                          style={{ position: 'absolute', right: 32, top: 3, width: 26, height: 18, border: 'none', borderRadius: 3, background: solo ? 'yellow' : '#8191A0', color: '#3C4A55', font: '700 12px var(--font-roboto)', cursor: 'pointer' }}>S</button>
                        <button type="button" data-show="mute" title="Mute this track" onClick={e => { e.stopPropagation(); muteTrack(t); }}
                          style={{ position: 'absolute', right: 3, top: 3, width: 26, height: 18, border: 'none', borderRadius: 3, background: t.mute ? 'red' : '#8191A0', color: '#3C4A55', font: '700 12px var(--font-roboto)', cursor: 'pointer' }}>M</button>
                        <button type="button" data-show="track-delete" title="Delete track" onClick={e => { e.stopPropagation(); setSelectedTrackId(String(t.id)); requestTrackDeletion(t); }}
                          style={{ position: 'absolute', right: 3, top: 24, width: 26, height: 18, border: 'none', borderRadius: 3, background: '#8191A0', cursor: 'pointer', display: 'grid', placeItems: 'center' }}><FaIcon name="fa_trash_can" size={11} color="crimson" /></button>
                        <span style={{ position: 'absolute', left: 6, bottom: 3, color: 'var(--fg-light)', font: '400 11px var(--font-roboto)' }}>{t.items.length} item{t.items.length === 1 ? '' : 's'}{t.sceneId ? ' · scene' : ''}</span>
                      </div>
                    );
                  })}
                  <div style={{ height: TRACK_H, display: 'flex', alignItems: 'center', justifyContent: 'center', borderRight: '3px solid var(--bg-light)', boxSizing: 'border-box' }}>
                    <GenericButton label="+ Add track" width={TRACK_W - 20} height={26} fontSize={13} disabled={!live} onClick={addTrack} data-show="add-track" />
                  </div>
                </div>
                {/* items area */}
                <div style={{ position: 'absolute', left: TRACK_W, top: HEADER_H, width: contentW, height: contentH }}>
                  {grid && tMs > 0 ? ticks.map(t => <div key={'g' + t.x} style={{ position: 'absolute', left: t.x, top: 0, width: 1, height: contentH, background: 'var(--bg-light)', opacity: .5 }} />) : null}
                  {tracks.map((t, ti) => <div key={'d' + t.id} style={{ position: 'absolute', left: 0, top: (ti + 1) * TRACK_H - 1, width: contentW, height: 1, background: 'var(--bg-light)' }} />)}
                  {tracks.length ? null : <div style={{ position: 'absolute', left: 20, top: 16, color: 'var(--fg-medium)', font: '400 14px var(--font-roboto)' }}>Add a track, then pick functions on the right and add them at the cursor</div>}
                  <div style={{ position: 'absolute', left: 0, top: tracks.length * TRACK_H, width: contentW, height: TRACK_H, display: 'grid', placeItems: 'center', color: 'var(--fg-medium)', font: '400 13px var(--font-roboto)', opacity: drag && drag.kind === 'move' ? 1 : 0 }}>Drop here to create a new track</div>
                  {tracks.map((t, ti) => t.items.map(it => {
                    const id = String(it.id);
                    const selected = selection.indexOf(id) !== -1;
                    let dx = 0, dy = 0, w = Math.max(2, msToPx(it.duration));
                    if (drag && drag.kind === 'move' && drag.ids.indexOf(id) !== -1) { dx = drag.dx; dy = drag.dTrack * TRACK_H; }
                    if (drag && drag.kind === 'resize' && drag.id === id) w = Math.max(2, w + drag.dw);
                    return (
                      <div key={id} data-show="item" data-item-id={id} data-track-id={t.id} data-locked={it.locked ? '1' : '0'} title={(it.functionName || it.functionId) + '  ' + fmtFull(it.startTime) + ' – ' + fmtFull(it.startTime + it.duration)}
                        onPointerDown={e => onItemPointerDown(e, Object.assign({ trackIndex: ti, trackId: t.id }, it))}
                        style={{ position: 'absolute', left: msToPx(it.startTime), top: ti * TRACK_H + 2, width: w, height: TRACK_H - 5, boxSizing: 'border-box', transform: dx || dy ? 'translate(' + dx + 'px,' + dy + 'px)' : undefined,
                          background: it.color, border: selected ? '2px solid var(--selection)' : '1px solid rgba(255,255,255,.35)', borderRadius: 4, overflow: 'hidden', cursor: it.locked ? 'not-allowed' : 'grab', opacity: drag && drag.kind === 'move' && dx ? .8 : 1, zIndex: selected ? 2 : 1 }}>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '2px 4px', color: '#fff', font: '400 12px var(--font-roboto)', whiteSpace: 'nowrap', textShadow: '0 0 2px #000' }}>
                          {it.functionType && FUNCTION_ICONS[it.functionType] ? <img src={D.icon(FUNCTION_ICONS[it.functionType])} alt="" style={{ width: 14, height: 14, flex: 'none' }} /> : null}
                          <span style={{ overflow: 'hidden', textOverflow: 'ellipsis' }}>{it.functionName || ('Function ' + it.functionId)}</span>
                        </div>
                        <div style={{ position: 'absolute', left: 4, bottom: 2, color: 'rgba(255,255,255,.8)', font: '400 10px var(--font-mono)' }}>{fmtFull(it.duration)}</div>
                        {it.locked ? <img src={D.icon('lock')} alt="locked" style={{ position: 'absolute', right: 4, bottom: 3, width: 12, height: 12 }} /> : null}
                        {!it.locked ? <div data-show="resize-handle" onPointerDown={e => onHandlePointerDown(e, Object.assign({ trackIndex: ti, trackId: t.id }, it))} style={{ position: 'absolute', right: 0, top: 0, width: 8, height: '100%', cursor: 'ew-resize', background: selected ? 'rgba(255,255,255,.25)' : 'transparent' }} /> : null}
                      </div>
                    );
                  }))}
                  {drag && drag.kind === 'move' && drag.snapped != null ? <div style={{ position: 'absolute', left: drag.snapped, top: 0, width: 1, height: contentH, background: '#00FF00', zIndex: 4 }} /> : null}
                  {drag && drag.kind === 'band' ? <div style={{ position: 'absolute', left: drag.band.x - TRACK_W, top: drag.band.y - HEADER_H, width: drag.band.w, height: drag.band.h, background: 'rgba(9,120,255,.25)', border: '1px solid var(--selection)', zIndex: 5 }} /> : null}
                  <div data-show="cursor" style={{ position: 'absolute', left: msToPx(cursor), top: 0, width: 1, height: contentH, background: 'var(--selection)', zIndex: 3, pointerEvents: 'none' }} />
                </div>
              </div>
            )}
          </div>

          {/* ---- right panel ---- */}
          <div style={{ width: 270, flex: 'none', borderLeft: 'var(--border-dark)', overflow: 'auto', display: 'flex', flexDirection: 'column' }} data-show="panel">
            <SectionBox sectionLabel="Functions" isExpanded={sectionOpen('fn')} onToggle={() => toggleSection('fn')}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
                <input value={filter} data-show="picker-filter" placeholder="Filter functions…" onChange={e => setFilter(e.target.value)} style={{ height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', font: '400 13px var(--font-roboto)', padding: '0 6px' }} />
                <div data-show="picker" style={{ maxHeight: 220, overflow: 'auto', background: 'var(--bg-strong)', border: 'var(--border-dark)' }}>
                  {pickerList.map(f => {
                    const on = picked.indexOf(String(f.id)) !== -1;
                    return (
                      <div key={f.id} data-show="picker-row" data-function-id={f.id} onClick={() => setPicked(p => on ? p.filter(x => x !== String(f.id)) : p.concat([String(f.id)]))}
                        style={{ display: 'flex', alignItems: 'center', gap: 6, height: 24, padding: '0 4px', cursor: 'pointer', background: on ? 'var(--highlight)' : 'transparent' }}>
                        <CustomCheckBox checked={on} size={16} onToggled={() => setPicked(p => on ? p.filter(x => x !== String(f.id)) : p.concat([String(f.id)]))} />
                        <img src={D.icon(FUNCTION_ICONS[f.type] || 'functions')} alt="" style={{ width: 16, height: 16 }} />
                        <span style={{ color: 'var(--fg-main)', font: '400 13px var(--font-roboto)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{f.name}</span>
                      </div>
                    );
                  })}
                  {!pickerList.length ? <RobotoText label={live ? 'No functions' : 'Connect first'} fontSize={12} labelColor="var(--fg-medium)" height={24} leftMargin={6} /> : null}
                </div>
                <GenericButton label={'Add at cursor' + (picked.length ? ' (' + picked.length + ')' : '')} width="100%" height={26} fontSize={13} disabled={!picked.length || !live || !showId} onClick={addPicked} data-show="add-picked" />
                <FF.Note text={selectedTrack ? 'onto track "' + selectedTrack.name + '"' : tracks.length ? 'onto the first track (click a track header to pick another)' : 'a first track is created for them'} />
              </div>
            </SectionBox>
            <SectionBox sectionLabel="Alignment" isExpanded={sectionOpen('al')} onToggle={() => toggleSection('al')}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
                <GenericButton label="Align start to cursor" width="100%" height={26} fontSize={13} disabled={!selectedItems.length} onClick={alignStart} data-show="align-start" />
                <GenericButton label="Align end to cursor" width="100%" height={26} fontSize={13} disabled={!selectedItems.length} onClick={alignEnd} data-show="align-end" />
              </div>
            </SectionBox>
            <SectionBox sectionLabel="Timings" isExpanded={sectionOpen('ti')} onToggle={() => toggleSection('ti')}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
                <FF.Row label="Start time" width={80}>{single ? <FF.InlineNumber value={single.startTime} format={fmtFull} parse={parseTime} width={110} disabled={single.locked} title="mm:ss.mmm or ms" onCommit={v => setStart(single, v)} /> : selectedItems.length ? 'Multiple' : '--'}</FF.Row>
                <FF.Row label="End time" width={80}>{single ? <FF.InlineNumber value={single.startTime + single.duration} format={fmtFull} parse={parseTime} width={110} disabled={single.locked} title="mm:ss.mmm or ms" onCommit={v => resizeItem(single, v - single.startTime)} /> : selectedItems.length ? 'Multiple' : '--'}</FF.Row>
                <FF.Row label="Duration" width={80}>{single ? <FF.InlineNumber value={single.duration} format={fmtFull} parse={parseTime} width={110} disabled={single.locked} title="mm:ss.mmm or ms" onCommit={v => resizeItem(single, v)} /> : selectedItems.length ? 'Multiple' : '--'}</FF.Row>
              </div>
            </SectionBox>
            <SectionBox sectionLabel="Cut/Insert" isExpanded={sectionOpen('ci')} onToggle={() => toggleSection('ci')}>
              <CutInsert division={division} disabled={!detail || !live} onInsert={insertTime} onCut={cutTime} />
            </SectionBox>
            {unsupported && live ? <FF.Note text="This server has no functions.show.* methods; the timeline is read-only." style={{ padding: 6 }} /> : null}
          </div>
        </div>

        <CustomPopupDialog open={!!dialog && dialog.kind === 'deleteItems'} title="Delete show items" width={420}
          message={dialog && dialog.kind === 'deleteItems' ? 'Are you sure you want to remove the following items?\n(Note that the original functions will not be deleted)\n' + dialog.ids.map(id => (itemsById.get(String(id)) || {}).functionName || id).join(', ') : ''}
          standardButtons={['Cancel', 'OK']} onClose={() => setDialog(null)} onClicked={(b) => { if (b === 'OK') deleteItems(dialog.ids); else setDialog(null); }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'deleteTrack'} title="Delete track" width={420}
          message={dialog && dialog.kind === 'deleteTrack' ? 'Delete track "' + dialog.track.name + '" and its ' + dialog.track.items.length + ' item(s)?\n(Note that the original functions will not be deleted)' : ''}
          standardButtons={['Cancel', 'OK']} onClose={() => setDialog(null)} onClicked={(b) => { if (b === 'OK') deleteTrack(dialog.track); else setDialog(null); }} />
      </div>
    );
  }

  /** TimingUtils.qml's Cut/Insert box: a length (default 1 s) and the two ripple buttons */
  function CutInsert({ division, disabled, onInsert, onCut }) {
    const [length, setLength] = React.useState(1000);
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
        <FF.Row label="Length" width={80}><FF.InlineNumber value={length} format={fmtFull} parse={parseTime} width={110} title="mm:ss.mmm or ms" onCommit={v => setLength(Math.max(minDuration(division), v))} /></FF.Row>
        <GenericButton label="Insert time" width="100%" height={26} fontSize={13} disabled={disabled} onClick={() => onInsert(length)} data-show="insert-time" />
        <GenericButton label="Cut time" width="100%" height={26} fontSize={13} disabled={disabled} onClick={() => onCut(length)} data-show="cut-time" />
        <FF.Note text="Applied at the cursor to every unlocked item covering it; later items ripple along." />
      </div>
    );
  }

  window.QLCScreens = Object.assign(window.QLCScreens || {}, {
    show: { id: 'show', icon: 'showmanager', label: 'Show Manager', keys: 'Ctrl 5', hotkey: '5', component: ShowManager, order: 10 }
  });
  Object.assign(window, { ShowManager });
})();
