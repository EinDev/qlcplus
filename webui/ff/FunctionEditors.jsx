/**
 * FunctionEditors.jsx — Scene and Chaser/Sequence editors plus the shared timing section and the
 * picker dialog, modelled on qmlui/qml/fixturesfunctions/{SceneEditor,ChaserEditor}.qml. The
 * Collection and EFX editors live in ff/CollectionEditor.jsx and ff/EfxEditor.jsx (window.QLCEditors).
 *
 * Every edit is optimistic: the local copy of functions.get's detail is patched immediately and
 * the mutation goes through FF.mutate (serial, revision-gated, key-coalesced). The parent only
 * refetches on *foreign* change events, so a drag is never fought by its own echo; on a rejected
 * mutation the editor calls reload() to resync.
 *
 * Server coverage: functions.update, functions.scene.setValue/unsetValue/setValues/setMembers,
 * functions.steps.add/remove/replace/moveStep (apifunctionsdomain.cpp); functions.chaser.
 * setSpeedModes/setAction, functions.sequence.setBoundScene/applyDumpValues, functions.tap and the
 * functions.chaser.currentStepChanged feed (apifunctionsmiscdomain.cpp).
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, SectionBox, QLCPlusFader, CustomSpinBox, GenericButton, IconButton, IconTextEntry, CustomPopupDialog, CustomTextInput } = window.PatchDesignSystem_5432c9;
  const Icons = window.QLCIcons;

  const rowStyle = { display: 'flex', alignItems: 'center', gap: 6, height: 26 };
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' };

  /* ---- timing / run properties ------------------------------------------------------------- */
  function TimingEditor({ qlc, detail, setDetail, reload, showRun }) {
    const patch = (p) => {
      setDetail(d => Object.assign({}, d, p));
      FF.mutate(qlc, 'functions.update', Object.assign({ functionId: String(detail.id) }, p), { key: 'update:' + detail.id + ':' + Object.keys(p).join(',') }).catch(() => reload && reload());
    };
    const time = (label, field) => (
      <FF.Row label={label} width={80}>
        <FF.InlineNumber value={detail[field] || 0} format={FF.ms} parse={FF.parseMs} onCommit={v => patch({ [field]: v })} width={80} title="Click to edit: 500, 1.5s, 2m, inf" />
        <IconButton faSource={FF.GLYPH.clock} size={22} tooltip="Set infinite" onClick={() => patch({ [field]: FF.INFINITE })} />
        <IconButton faSource="fa_xmark" size={22} tooltip="Set 0" onClick={() => patch({ [field]: 0 })} />
      </FF.Row>
    );
    const isChaser = detail.type === 'Chaser' || detail.type === 'Sequence';
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <FF.Heading text="Speed" style={{ flex: 1 }} />
          {isChaser ? <GenericButton label="Tap" width={50} height={22} tooltip="Tap tempo: re-times the running chaser to your taps (functions.tap)"
            onClick={() => qlc.call('functions.tap', { functionId: String(detail.id) }).catch(e => FF.reportError(e, 'functions.tap'))} /> : null}
        </div>
        {time('Fade in', 'fadeInSpeed')}
        {time('Fade out', 'fadeOutSpeed')}
        {time(isChaser ? 'Step duration' : 'Duration', 'duration')}
        {showRun !== false ? <>
          <FF.Heading text="Run properties" style={{ marginTop: 6 }} />
          <FF.Row label="Run order" width={80}><FF.Choice options={FF.RUN_ORDERS} labels={FF.RUN_ORDER_LABELS} value={detail.runOrder} onChange={v => patch({ runOrder: v })} /></FF.Row>
          <FF.Row label="Direction" width={80}><FF.Choice options={FF.DIRECTIONS} value={detail.direction} onChange={v => patch({ direction: v })} /></FF.Row>
          <FF.Row label="Tempo" width={80}><FF.Choice options={FF.TEMPO_TYPES} value={detail.tempoType} onChange={v => patch({ tempoType: v })} /></FF.Row>
        </> : null}
      </div>
    );
  }

  /* ---- fixture picker dialog (add to scene / collection member picker) --------------------- */
  function PickerDialog({ open, title, items, onPick, onClose, multi = true, confirmLabel = 'Add' }) {
    const [q, setQ] = React.useState('');
    const [sel, setSel] = React.useState([]);
    React.useEffect(() => { if (open) { setQ(''); setSel([]); } }, [open]);
    const shown = items.filter(i => !q || i.name.toLowerCase().indexOf(q.toLowerCase()) !== -1).slice(0, 400);
    return (
      <CustomPopupDialog open={open} title={title} width={420} standardButtons={['Cancel', confirmLabel]}
        onClicked={(b) => { if (b === confirmLabel) onPick(sel); onClose(); }} onClose={onClose}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          <input autoFocus value={q} onChange={e => setQ(e.target.value)} placeholder="Search…" style={Object.assign({ width: '100%' }, inputStyle)} />
          <div style={{ maxHeight: 300, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
            {shown.map(i => {
              const on = sel.indexOf(i.id) !== -1;
              return (
                <div key={i.id} onClick={() => setSel(s => multi ? (on ? s.filter(x => x !== i.id) : s.concat([i.id])) : [i.id])}
                  style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 6px', cursor: 'pointer', background: on ? 'var(--highlight)' : 'transparent' }}>
                  {i.icon ? <img src={i.icon} alt="" style={{ width: 18, height: 18 }} /> : null}
                  <RobotoText label={i.name} fontSize={14} height={26} style={{ flex: 1 }} />
                  {i.hint ? <RobotoText label={i.hint} fontSize={12} height={26} labelColor="var(--fg-light)" /> : null}
                </div>
              );
            })}
            {!shown.length ? <div style={{ padding: 8 }}><RobotoText label="None" fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
          </div>
          <RobotoText label={sel.length + ' selected' + (items.length > 400 && shown.length === 400 ? ' · showing first 400, refine the search' : '')} fontSize={12} labelColor="var(--fg-light)" height={18} />
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- scene editor ------------------------------------------------------------------------- */
  /** One fixture's channel console: vertical faders, unset channels dimmed, × clears a value. */
  function SceneFixtureConsole({ qlc, sceneId, fixture, values, setValue, unsetValue }) {
    const detail = FF.useFixtureDetail(qlc, fixture.id);
    if (!detail) return <RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" />;
    return (
      <div style={{ display: 'flex', gap: 4, overflowX: 'auto', padding: '4px 0' }}>
        {detail.channelList.map(ch => {
          const key = fixture.id + '.' + ch.index;
          const set = Object.prototype.hasOwnProperty.call(values, key);
          const v = set ? values[key] : 0;
          return (
            <div key={ch.index} title={ch.name + (set ? '' : ' (not in scene)')} style={{ width: 48, flex: 'none', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3, padding: 3, background: set ? 'var(--bg-strong)' : 'transparent', border: set ? '1px solid var(--bg-control)' : '1px solid transparent', boxSizing: 'border-box' }}>
              <img src={Icons.channelIcon(ch)} alt="" style={{ width: 18, height: 18, opacity: set ? 1 : 0.45 }} />
              <div style={{ opacity: set ? 1 : 0.55 }}><QLCPlusFader value={v} height={110} width={26} onMoved={val => setValue(fixture.id, ch.index, val)} /></div>
              <FF.InlineNumber value={v} onCommit={val => setValue(fixture.id, ch.index, Math.max(0, Math.min(255, Math.round(val))))} width={42} />
              <RobotoText label={String(ch.index + 1)} fontSize={11} height={14} labelColor="var(--fg-medium)" />
              <button type="button" disabled={!set} onClick={() => unsetValue(fixture.id, ch.index)} title={set ? 'Remove this channel from the scene' : 'Not in the scene'}
                style={{ width: 20, height: 18, border: 'none', background: 'transparent', color: set ? 'var(--fg-light)' : 'transparent', cursor: set ? 'pointer' : 'default', fontFamily: 'var(--font-awesome)', fontWeight: 900, fontSize: 11 }}>{''}</button>
            </div>
          );
        })}
      </div>
    );
  }

  function SceneEditor({ qlc, detail, reload, setDetail, fixtures, palettes, selectedFixtureIds, onSelectFixtures }) {
    const D = window.QLCData;
    const td = detail.typeDetail || {};
    const sceneId = String(detail.id);
    const values = td.values || {};
    const memberIds = td.fixtures || [];
    const members = memberIds.map(id => fixtures.find(f => String(f.id) === String(id)) || { id, name: 'Fixture ' + id + ' (unpatched)', missing: true });
    const [current, setCurrent] = React.useState(null);
    const [checked, setChecked] = React.useState([]);
    const [picker, setPicker] = React.useState(null); /* 'fixtures' | 'palettes' */
    const [showAll, setShowAll] = React.useState(false);
    React.useEffect(() => { if (current == null && members.length) setCurrent(String(members[0].id)); if (current != null && !memberIds.some(id => String(id) === current)) setCurrent(members.length ? String(members[0].id) : null); }, [memberIds.join(',')]);

    const patchTd = (p) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, p) }));
    /* Fixture Tools with target "Scene" writes into this scene from outside this editor. */
    FF.useLocalEvents('scene.value', (d) => {
      if (!d || String(d.sceneId) !== sceneId) return;
      setDetail(cur => {
        const ctd = cur.typeDetail || {};
        const fx = (ctd.fixtures || []).some(id => String(id) === d.fixture) ? ctd.fixtures : (ctd.fixtures || []).concat([d.fixture]);
        return Object.assign({}, cur, { typeDetail: Object.assign({}, ctd, { values: Object.assign({}, ctd.values, { [d.fixture + '.' + d.channel]: d.value }), fixtures: fx }) });
      });
    }, [sceneId]);
    const setValue = (fid, ch, value) => {
      const key = fid + '.' + ch;
      patchTd({ values: Object.assign({}, values, { [key]: value }), fixtures: memberIds.some(id => String(id) === String(fid)) ? memberIds : memberIds.concat([String(fid)]) });
      FF.mutate(qlc, 'functions.scene.setValue', { functionId: sceneId, fixture: String(fid), channel: ch, value }, { key: 'scene:' + sceneId + ':' + key }).catch(() => reload());
    };
    const unsetValue = (fid, ch) => {
      const key = fid + '.' + ch;
      const nv = Object.assign({}, values); delete nv[key];
      patchTd({ values: nv });
      FF.mutate(qlc, 'functions.scene.unsetValue', { functionId: sceneId, fixture: String(fid), channel: ch }).catch(() => reload());
    };
    const setMembers = (p) => { patchTd(p); FF.mutate(qlc, 'functions.scene.setMembers', Object.assign({ functionId: sceneId }, p)).catch(() => reload()); };
    const addFixtures = (ids) => { const next = memberIds.slice(); ids.forEach(id => { if (!next.some(x => String(x) === String(id))) next.push(String(id)); }); setMembers({ fixtures: next }); };
    const removeChecked = () => {
      const nv = Object.assign({}, values);
      Object.keys(nv).forEach(k => { if (checked.indexOf(k.split('.')[0]) !== -1) delete nv[k]; });
      patchTd({ values: nv });
      setMembers({ fixtures: memberIds.filter(id => checked.indexOf(String(id)) === -1) });
      setChecked([]);
    };
    /* SceneEditor.qml "Add a fixture group": the group joins fixtureGroups, its heads' fixtures join fixtures */
    const [groups, setGroups] = React.useState([]);
    const openGroupPicker = () => { qlc.call('fixtures.group.list').then(r => setGroups((r && r.groups) || [])).catch(() => setGroups([])); setPicker('groups'); };
    const addGroups = (gids) => Promise.all(gids.map(g => qlc.call('fixtures.group.get', { groupId: String(g) }).catch(() => null))).then(list => {
      const fx = memberIds.map(String);
      list.forEach(g => ((g && g.heads) || []).forEach(h => { if (fx.indexOf(String(h.fixtureId)) === -1) fx.push(String(h.fixtureId)); }));
      setMembers({ fixtures: fx, fixtureGroups: Array.from(new Set((td.fixtureGroups || []).map(String).concat(gids.map(String)))) });
    });
    const groupItems = groups.filter(g => !(td.fixtureGroups || []).some(x => String(x) === String(g.id))).map(g => ({ id: String(g.id), name: g.name, icon: D.icon('group'), hint: g.headCount + ' heads' }));
    const removePalette = (pid) => setMembers({ palettes: (td.palettes || []).filter(p => String(p) !== String(pid)) });
    const addPalettes = (ids) => setMembers({ palettes: Array.from(new Set((td.palettes || []).map(String).concat(ids.map(String)))) });
    const valuesFor = (fid) => Object.keys(values).filter(k => k.split('.')[0] === String(fid)).length;
    const fixtureItems = fixtures.filter(f => !memberIds.some(id => String(id) === String(f.id))).map(f => ({ id: String(f.id), name: f.name, icon: D.icon(Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture'), hint: 'U' + (f.universe + 1) + '.' + (f.address + 1) }));
    const paletteItems = (palettes || []).filter(p => !(td.palettes || []).some(id => String(id) === String(p.id))).map(p => ({ id: String(p.id), name: p.name, icon: D.icon(FF.PALETTE_ICON[p.type] || 'palette'), hint: p.type }));
    const selectable = selectedFixtureIds.filter(id => !memberIds.some(x => String(x) === String(id)));
    const cur = members.find(m => String(m.id) === current);

    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 8px', background: 'var(--bg-strong)', flex: 'none' }}>
          <IconButton imgSource={D.icon('fixture')} size={26} tooltip="Add fixtures to the scene" onClick={() => setPicker('fixtures')} />
          <IconButton imgSource={D.icon('group')} size={26} tooltip="Add a fixture group (the group and its fixtures join the scene)" onClick={openGroupPicker} />
          {selectable.length ? <GenericButton label={'Add ' + selectable.length + ' selected'} width={120} height={26} onClick={() => addFixtures(selectable)} /> : null}
          <IconButton imgSource={D.icon('palette')} size={26} tooltip="Add a palette" onClick={() => setPicker('palettes')} disabled={!paletteItems.length} />
          <IconButton faSource={FF.GLYPH.minus} size={26} tooltip="Remove the checked fixtures from the scene" disabled={!checked.length} onClick={removeChecked} />
          <div style={{ width: 1, height: 20, background: 'var(--border-color-dark)', margin: '0 3px' }} />
          <IconButton faSource={FF.GLYPH.crosshairs} size={26} tooltip="Select the scene's fixtures in the tree (then use Fixture Tools with target Scene)" disabled={!members.length} onClick={() => onSelectFixtures(memberIds.map(String))} />
          <IconButton imgSource={D.icon('sliders')} size={26} checked={showAll} tooltip="Show every fixture's console at once" onClick={() => setShowAll(!showAll)} />
          <div style={{ flex: 1 }} />
          <RobotoText label={members.length + ' fixtures · ' + Object.keys(values).length + ' values · ' + (td.palettes || []).length + ' palettes'} fontSize={13} labelColor="var(--fg-light)" />
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div style={{ width: 240, flex: 'none', overflow: 'auto', borderRight: 'var(--border-dark)', background: 'var(--bg-stronger)' }}>
            {members.map(m => {
              const id = String(m.id), on = checked.indexOf(id) !== -1, isCur = current === id;
              return (
                <div key={id} onClick={() => setCurrent(id)} style={{ display: 'flex', alignItems: 'center', gap: 4, height: 26, padding: '0 4px', background: isCur ? 'var(--highlight)' : 'transparent', cursor: 'pointer' }}>
                  <input type="checkbox" checked={on} onChange={() => setChecked(c => on ? c.filter(x => x !== id) : c.concat([id]))} onClick={e => e.stopPropagation()} style={{ margin: 0 }} />
                  <img src={D.icon(m.missing ? 'other' : (Icons.FIXTURE_TYPE_ICONS[m.fixtureType] || 'fixture'))} alt="" style={{ width: 18, height: 18 }} />
                  <RobotoText label={m.name} fontSize={13} height={26} style={{ flex: 1 }} labelColor={m.missing ? 'var(--fg-medium)' : 'var(--fg-main)'} />
                  <RobotoText label={String(valuesFor(id))} fontSize={12} height={26} labelColor="var(--fg-light)" />
                </div>
              );
            })}
            {!members.length ? <div style={{ padding: 8 }}><RobotoText label="No fixtures in this scene" fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
            {(td.palettes || []).length ? <div style={{ borderTop: 'var(--border-dark)', marginTop: 4 }}>
              <div style={{ padding: '4px 6px' }}><RobotoText label="Palettes" fontBold fontSize={13} height={20} /></div>
              {(td.palettes || []).map(pid => {
                const p = (palettes || []).find(x => String(x.id) === String(pid));
                return (
                  <div key={pid} style={{ display: 'flex', alignItems: 'center', gap: 4, height: 26, padding: '0 4px' }}>
                    <img src={D.icon(p ? (FF.PALETTE_ICON[p.type] || 'palette') : 'palette')} alt="" style={{ width: 18, height: 18 }} />
                    <RobotoText label={p ? p.name : 'Palette ' + pid} fontSize={13} height={26} style={{ flex: 1 }} />
                    <IconButton faSource="fa_xmark" size={20} tooltip="Remove palette from the scene" onClick={() => removePalette(pid)} />
                  </div>
                );
              })}
            </div> : null}
          </div>
          <div style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 10, display: 'flex', flexDirection: 'column', gap: 10 }}>
            {showAll ? members.filter(m => !m.missing).map(m => (
              <div key={m.id}>
                <RobotoText label={m.name} fontBold fontSize={13} height={20} />
                <SceneFixtureConsole qlc={qlc} sceneId={sceneId} fixture={m} values={values} setValue={setValue} unsetValue={unsetValue} />
              </div>
            )) : cur && !cur.missing ? <>
              <RobotoText label={cur.name + ' — ' + valuesFor(cur.id) + ' channels in scene'} fontBold fontSize={13} height={20} />
              <SceneFixtureConsole qlc={qlc} sceneId={sceneId} fixture={cur} values={values} setValue={setValue} unsetValue={unsetValue} />
              <FF.Note text="Drag a fader or click a value to set it; a channel enters the scene with its first value. × removes the channel again. Preview: start the scene with ▶ in the header (the Qt Function Preview is the same start / stop), or use Fixture Tools with target Live output." />
            </> : <RobotoText label={members.length ? 'Pick a fixture on the left.' : 'Add fixtures to start setting values.'} fontSize={14} labelColor="var(--fg-medium)" />}
            <div style={{ borderTop: 'var(--border-dark)', paddingTop: 8 }}>
              <TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} showRun={false} />
            </div>
          </div>
        </div>
        <PickerDialog open={picker === 'fixtures'} title="Add fixtures to the scene" items={fixtureItems} onPick={addFixtures} onClose={() => setPicker(null)} />
        <PickerDialog open={picker === 'palettes'} title="Add palettes to the scene" items={paletteItems} onPick={addPalettes} onClose={() => setPicker(null)} />
        <PickerDialog open={picker === 'groups'} title="Add fixture groups to the scene" items={groupItems} onPick={addGroups} onClose={() => setPicker(null)} />
      </div>
    );
  }

  /* ---- chaser / sequence editor ------------------------------------------------------------- */
  const GLYPH_PRINT = '', GLYPH_STOPWATCH = '', GLYPH_BACKWARD = '', GLYPH_FORWARD = '', GLYPH_RECORD = '';

  /** running flag of one function: functions.get's summary, then functions.status.changed */
  function useRunning(qlc, fid, initial) {
    const [running, setRunning] = React.useState(!!initial);
    React.useEffect(() => { setRunning(!!initial); }, [fid]);
    React.useEffect(() => qlc.subscribeTo('functions.status.changed', d => { if (d && String(d.functionId != null ? d.functionId : d.id) === fid && d.running !== undefined) setRunning(!!d.running); }), [fid, qlc.online]);
    return running;
  }

  /** Browser print of the step table (ChaserEditor.qml's "Print the Chaser steps"). */
  function printSteps(detail, steps, fnName, isSeq) {
    const esc = (s) => String(s == null ? '' : s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
    const rows = steps.map((s, i) => '<tr><td>' + (i + 1) + '</td><td>' + esc(isSeq ? Object.keys(s.values || {}).length + ' channel values' : fnName(s.targetFunctionId)) + '</td><td>' +
      esc(FF.ms(s.fadeIn)) + '</td><td>' + esc(FF.ms(s.hold)) + '</td><td>' + esc(FF.ms(s.fadeOut)) + '</td><td>' + esc(FF.ms(s.duration)) + '</td><td>' + esc(s.note) + '</td></tr>').join('');
    const html = '<!doctype html><html><head><meta charset="utf-8"><title>' + esc(detail.name) + '</title><style>body{font-family:sans-serif;font-size:12px}' +
      'table{border-collapse:collapse;width:100%}th,td{border:1px solid #888;padding:3px 6px;text-align:left}th{background:#ddd}</style></head><body>' +
      '<h2>' + esc(detail.type) + ': ' + esc(detail.name) + '</h2><table><thead><tr><th>#</th><th>' + (isSeq ? 'Values' : 'Function') +
      '</th><th>Fade In</th><th>Hold</th><th>Fade Out</th><th>Duration</th><th>Note</th></tr></thead><tbody>' + rows + '</tbody></table></body></html>';
    const frame = document.createElement('iframe');
    frame.style.cssText = 'position:fixed;right:0;bottom:0;width:0;height:0;border:0';
    frame.setAttribute('data-e2e', 'print-frame');
    document.body.appendChild(frame);
    frame.contentDocument.open(); frame.contentDocument.write(html); frame.contentDocument.close();
    setTimeout(() => { try { frame.contentWindow.focus(); frame.contentWindow.print(); } catch (e) { /* ignore */ } setTimeout(() => frame.remove(), 1000); }, 50);
  }

  function ChaserEditor({ qlc, detail, reload, setDetail, functions, fixtures }) {
    const D = window.QLCData;
    const isSeq = detail.type === 'Sequence';
    const td = detail.typeDetail || {};
    const steps = td.steps || [];
    const fid = String(detail.id);
    const [sel, setSel] = React.useState([]);          /* selected step indices */
    const [picker, setPicker] = React.useState(false);
    const [valuesOpen, setValuesOpen] = React.useState(true);
    const [live, setLive] = React.useState(null);
    const running = useRunning(qlc, fid, detail.running);
    React.useEffect(() => { setSel(s => s.filter(i => i < steps.length)); }, [steps.length]);
    /* Live playback position: functions.chaser.currentStepChanged (the runner's currentStepChanged). */
    React.useEffect(() => qlc.subscribeTo('functions.chaser.currentStepChanged', d => { if (d && String(d.functionId) === fid) setLive(d.stepIndex); }), [fid, qlc.online]);
    React.useEffect(() => { if (!running) setLive(null); }, [running]);
    /* Other clients' speed-mode changes (own echoes are applied optimistically). */
    FF.useForeignEvents(qlc, ['functions.chaser.changed'], (topic, d) => {
      if (d && String(d.functionId) === fid) setDetail(cur => Object.assign({}, cur, { typeDetail: Object.assign({}, cur.typeDetail, { fadeInMode: d.fadeInMode, fadeOutMode: d.fadeOutMode, durationMode: d.durationMode }) }));
    }, [fid]);

    const patchSteps = (next) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { steps: next }) }));
    const wire = (s) => { const o = { fadeIn: s.fadeIn || 0, hold: s.hold || 0, fadeOut: s.fadeOut || 0 }; if (s.note) o.note = s.note; if (isSeq) o.values = s.values || {}; else o.targetFunctionId = String(s.targetFunctionId); return o; };
    const replaceStep = (index, patch) => {
      const next = steps.slice(); next[index] = Object.assign({}, steps[index], patch, { duration: patch.hold === FF.INFINITE ? FF.INFINITE : ((patch.fadeIn != null ? patch.fadeIn : steps[index].fadeIn) + (patch.hold != null ? patch.hold : steps[index].hold)) });
      patchSteps(next);
      FF.mutate(qlc, 'functions.steps.replaceStep', { functionId: fid, index, step: wire(next[index]) }, { key: 'step:' + fid + ':' + index }).catch(() => reload());
    };
    const addStep = (step, index) => {
      const next = steps.slice(); const at = index == null ? next.length : index; next.splice(at, 0, Object.assign({ duration: (step.fadeIn || 0) + (step.hold || 0) }, step));
      patchSteps(next);
      FF.mutate(qlc, 'functions.steps.addStep', Object.assign({ functionId: fid, step: wire(step) }, index == null ? {} : { index })).catch(() => reload());
    };
    const removeSelected = () => {
      const idx = sel.slice().sort((a, b) => b - a);
      patchSteps(steps.filter((_, i) => sel.indexOf(i) === -1));
      setSel([]);
      FF.mutateSeq(qlc, idx.map(i => ['functions.steps.removeStep', { functionId: fid, index: i }])).catch(() => reload());
    };
    const move = (dir) => {
      if (sel.length !== 1) return;
      const from = sel[0], to = from + dir;
      if (to < 0 || to >= steps.length) return;
      const next = steps.slice(); const [s] = next.splice(from, 1); next.splice(to, 0, s);
      patchSteps(next); setSel([to]);
      FF.mutate(qlc, 'functions.steps.moveStep', { functionId: fid, sourceIndex: from, destIndex: to }).catch(() => reload());
    };
    const duplicate = () => {
      const idx = sel.slice().sort((a, b) => a - b);
      idx.forEach((i, n) => addStep(Object.assign({}, steps[i]), i + n + 1));
      setSel([]);
    };
    /* ChaserEditor::shuffleSteps(): Fisher-Yates over the selected steps (all when fewer than two are
       selected), applied as a series of moveStep calls on a local copy so every call is valid. */
    const shuffle = () => {
      const pool = (sel.length > 1 ? sel.slice() : steps.map((_, i) => i)).sort((a, b) => a - b);
      if (pool.length < 2) return;
      const target = pool.slice();
      for (let i = target.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); const t = target[i]; target[i] = target[j]; target[j] = t; }
      /* the step now at pool[k] must end up being the one originally at target[k] */
      let order = steps.map((_, i) => i);   /* order[pos] = original index at pos */
      const moves = [];
      pool.forEach((pos, k) => {
        const from = order.indexOf(target[k]);
        if (from === pos) return;
        const [x] = order.splice(from, 1); order.splice(pos, 0, x);
        moves.push(['functions.steps.moveStep', { functionId: fid, sourceIndex: from, destIndex: pos }]);
      });
      patchSteps(order.map(i => steps[i]));
      setSel([]);
      FF.mutateSeq(qlc, moves).catch(() => reload());
    };
    /* ChaserEditor::autoSetDurations(): each selected step (all when none) gets its target function's
       total duration (1 s when that is 0), hold = duration - fade in; Duration mode becomes Per Step. */
    const autoDurations = async () => {
      const idx = sel.length ? sel.slice() : steps.map((_, i) => i);
      if (td.durationMode !== 'PerStep') setSpeedMode('durationMode', 'PerStep');
      for (const i of idx) {
        const target = isSeq ? td.boundSceneId : steps[i].targetFunctionId;
        const f = await qlc.call('functions.get', { functionId: String(target) }).catch(() => null);
        const total = (f && f.totalDuration) || 1000;
        const dur = total >= FF.INFINITE ? FF.INFINITE : total;
        replaceStep(i, { hold: dur === FF.INFINITE ? FF.INFINITE : Math.max(0, dur - (steps[i].fadeIn || 0)) });
      }
    };
    /* Playback (ChaserEditor::gotoPreviousStep / gotoNextStep / setPlaybackIndex, previewEnabled). */
    const action = (a, extra) => qlc.call('functions.chaser.setAction', Object.assign({ functionId: fid, action: a }, extra || {})).catch(e => FF.reportError(e, 'functions.chaser.setAction'));
    const togglePreview = () => {
      if (running) { qlc.call('functions.stop', { functionId: fid }).catch(() => {}); return; }
      const start = () => qlc.call('functions.start', { functionId: fid }).catch(e => FF.reportError(e, 'functions.start'));
      if (sel.length === 1) action('setStepIndex', { stepIndex: sel[0] }).then(start); else start();
    };
    const defaultStep = () => ({ fadeIn: detail.fadeInSpeed || 0, hold: detail.duration || 0, fadeOut: detail.fadeOutSpeed || 0 });
    const setSpeedMode = (field, v) => {
      setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { [field]: v }) }));
      FF.mutate(qlc, 'functions.chaser.setSpeedModes', { functionId: fid, [field]: v }).catch(() => reload());
    };
    const speedModesUnsupported = qlc.isUnsupported('functions.chaser.setSpeedModes');
    const fnName = (id) => { const f = functions.find(x => String(x.id) === String(id)); return f ? f.name : 'Function ' + id; };
    const fnIcon = (id) => { const f = functions.find(x => String(x.id) === String(id)); return D.icon(f ? (Icons.FUNCTION_ICONS[f.type] || 'functions') : 'functions'); };
    const funcItems = functions.filter(f => String(f.id) !== fid).map(f => ({ id: String(f.id), name: f.name, icon: D.icon(Icons.FUNCTION_ICONS[f.type] || 'functions'), hint: f.type }));
    const perStep = { fadeIn: td.fadeInMode === 'PerStep', fadeOut: td.fadeOutMode === 'PerStep', duration: td.durationMode === 'PerStep' };
    const timeCell = (i, field, enabled) => (
      <FF.InlineNumber value={steps[i][field] || 0} format={FF.ms} parse={FF.parseMs} onCommit={v => replaceStep(i, { [field]: v })} width={72} disabled={!enabled}
        title={enabled ? 'Click to edit: 500, 1.5s, 2m, inf' : 'Governed by the ' + (field === 'hold' ? 'Duration' : field === 'fadeIn' ? 'Fade In' : 'Fade Out') + ' speed mode (' + (field === 'hold' ? td.durationMode : field === 'fadeIn' ? td.fadeInMode : td.fadeOutMode) + ')'} />
    );
    const curStep = sel.length === 1 ? steps[sel[0]] : null;
    const setStepValue = (i, key, value) => { const vals = Object.assign({}, steps[i].values || {}); if (value == null) delete vals[key]; else vals[key] = value; replaceStep(i, { values: vals }); };
    const sep = <div style={{ width: 1, height: 20, background: 'var(--border-color-dark)', margin: '0 3px' }} />;

    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 8px', background: 'var(--bg-strong)', flex: 'none' }}>
          <IconButton faSource="fa_plus" size={26} tooltip={isSeq ? 'Add a new step (empty values)' : 'Add a new step'} onClick={() => isSeq ? addStep(Object.assign(defaultStep(), { values: {} })) : setPicker(true)} />
          <IconButton faSource={FF.GLYPH.clone} size={26} tooltip="Duplicate the selected step(s)" disabled={!sel.length} onClick={duplicate} />
          <IconButton faSource={FF.GLYPH.arrowUp} size={26} tooltip="Move the selected step up" disabled={sel.length !== 1 || sel[0] === 0} onClick={() => move(-1)} />
          <IconButton faSource={FF.GLYPH.arrowDown} size={26} tooltip="Move the selected step down" disabled={sel.length !== 1 || sel[0] === steps.length - 1} onClick={() => move(1)} />
          <IconButton faSource={FF.GLYPH.shuffle} size={26} tooltip="Randomize the selected step(s) order (all steps when fewer than two are selected)" disabled={steps.length < 2} onClick={shuffle} />
          <IconButton faSource={GLYPH_STOPWATCH} size={26} tooltip="Auto-set step durations from the function total duration (selected steps, or all)" disabled={!steps.length} onClick={autoDurations} />
          <IconButton faSource={FF.GLYPH.minus} size={26} tooltip="Remove the selected steps" disabled={!sel.length} onClick={removeSelected} />
          <IconButton faSource={GLYPH_PRINT} size={26} tooltip="Print the steps" disabled={!steps.length} onClick={() => printSteps(detail, steps, fnName, isSeq)} />
          {sep}
          <IconButton faSource={GLYPH_BACKWARD} size={26} tooltip="Preview the previous step" disabled={!running} onClick={() => action('previousStep')} />
          <IconButton faSource={running ? FF.GLYPH.stop : 'fa_play'} size={26} checked={running} faColor={running ? 'var(--override-red)' : 'var(--check-lime)'}
            tooltip={running ? 'Stop the preview' : (sel.length === 1 ? 'Preview on the output from the selected step' : 'Preview on the output')} onClick={togglePreview} />
          <IconButton faSource={GLYPH_FORWARD} size={26} tooltip="Preview the next step" disabled={!running} onClick={() => action('nextStep')} />
          <IconButton faSource={FF.GLYPH.anglesRight} size={26} tooltip="Jump to the selected step" disabled={!running || sel.length !== 1} onClick={() => action('setStepIndex', { stepIndex: sel[0] })} />
          <div style={{ flex: 1 }} />
          <RobotoText label={(running && live != null ? 'step ' + (live + 1) + ' · ' : '') + steps.length + ' steps · total ' + FF.ms(detail.totalDuration)} fontSize={13} labelColor="var(--fg-light)" />
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div style={{ flex: 1, minWidth: 0, overflow: 'auto' }}>
            <table data-e2e="chaser-steps" style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--font-roboto)', fontSize: 13, color: 'var(--fg-main)' }}>
              <thead>
                <tr style={{ background: 'var(--bg-strong)', height: 24, textAlign: 'left' }}>
                  <th style={{ width: 34, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>#</th>
                  <th style={{ padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>{isSeq ? 'Values' : 'Function'}</th>
                  <th style={{ width: 84, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Fade In</th>
                  <th style={{ width: 84, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Hold</th>
                  <th style={{ width: 84, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Fade Out</th>
                  <th style={{ width: 84, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Duration</th>
                  <th style={{ padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Note</th>
                </tr>
              </thead>
              <tbody>
                {steps.map((s, i) => {
                  const on = sel.indexOf(i) !== -1;
                  return (
                    <tr key={i} data-step={i} onClick={(e) => setSel(e.ctrlKey || e.metaKey ? (on ? sel.filter(x => x !== i) : sel.concat([i])) : e.shiftKey && sel.length ? (() => { const a = Math.min(sel[0], i), b = Math.max(sel[0], i); const r = []; for (let k = a; k <= b; k++) r.push(k); return r; })() : [i])}
                      style={{ height: 28, background: on ? 'var(--highlight)' : (i % 2 ? 'var(--bg-medium)' : 'var(--bg-stronger)'), cursor: 'pointer', outline: running && live === i ? '1px solid var(--check-lime)' : 'none', outlineOffset: -1 }}>
                      <td style={{ padding: '0 6px', color: 'var(--fg-light)' }}>{running && live === i ? <FF.Glyph g="fa_play" size={10} color="var(--check-lime)" /> : null}{' ' + (i + 1)}</td>
                      <td style={{ padding: '0 6px' }}>
                        {isSeq ? <span>{Object.keys(s.values || {}).length + ' channel values'}</span>
                          : <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}><img src={fnIcon(s.targetFunctionId)} alt="" style={{ width: 16, height: 16 }} />{fnName(s.targetFunctionId)}</span>}
                      </td>
                      <td style={{ padding: '0 6px' }}>{timeCell(i, 'fadeIn', perStep.fadeIn)}</td>
                      <td style={{ padding: '0 6px' }}>{timeCell(i, 'hold', perStep.duration)}</td>
                      <td style={{ padding: '0 6px' }}>{timeCell(i, 'fadeOut', perStep.fadeOut)}</td>
                      <td style={{ padding: '0 6px', color: 'var(--fg-light)' }}>{FF.ms(s.duration)}</td>
                      <td style={{ padding: '0 6px' }}>
                        <input value={s.note || ''} placeholder="—" onClick={e => e.stopPropagation()} onChange={e => { const next = steps.slice(); next[i] = Object.assign({}, s, { note: e.target.value }); patchSteps(next); }}
                          onBlur={e => { if ((e.target.value || '') !== (td.steps[i] && td.steps[i].note || '')) replaceStep(i, { note: e.target.value }); }}
                          style={{ width: '100%', minWidth: 60, height: 22, boxSizing: 'border-box', background: 'transparent', color: 'var(--fg-main)', border: 'none', borderBottom: '1px solid var(--bg-control)', fontFamily: 'var(--font-roboto)', fontSize: 13 }} />
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
            {!steps.length ? <div style={{ padding: 12 }}><RobotoText label="No steps. Use + to add one." fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
            {isSeq ? <SequencePanel qlc={qlc} detail={detail} steps={steps} sel={sel} fixtures={fixtures} functions={functions} running={running}
              valuesOpen={valuesOpen} setValuesOpen={setValuesOpen} setStepValue={setStepValue} replaceStep={replaceStep} reload={reload} setDetail={setDetail} setSel={setSel} /> : null}
          </div>
          <div style={{ width: 300, flex: 'none', overflow: 'auto', borderLeft: 'var(--border-dark)', padding: 10, display: 'flex', flexDirection: 'column', gap: 4 }}>
            <TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} />
            <FF.Heading text="Speed modes" style={{ marginTop: 6 }} />
            <FF.Row label="Fade In" width={80}><FF.Choice options={FF.SPEED_MODES} labels={FF.SPEED_MODE_LABELS} value={td.fadeInMode} onChange={v => setSpeedMode('fadeInMode', v)} disabled={speedModesUnsupported} /></FF.Row>
            <FF.Row label="Fade Out" width={80}><FF.Choice options={FF.SPEED_MODES.filter(m => m !== 'Default')} labels={FF.SPEED_MODE_LABELS} value={td.fadeOutMode} onChange={v => setSpeedMode('fadeOutMode', v)} disabled={speedModesUnsupported} /></FF.Row>
            <FF.Row label="Duration" width={80}><FF.Choice options={FF.SPEED_MODES.filter(m => m !== 'Default')} labels={FF.SPEED_MODE_LABELS} value={td.durationMode} onChange={v => setSpeedMode('durationMode', v)} disabled={speedModesUnsupported} /></FF.Row>
            <FF.Note text={speedModesUnsupported ? 'Changing speed modes is not available: this server has no functions.chaser.setSpeedModes. Per-step times are editable only where the mode is already Per Step.' : 'Default = each step uses its function\'s own fade; Common = every step uses the Speed values above; Per Step = each step has its own.'} style={{ marginTop: 4 }} />
            <FF.Note text="Preview: ▶ runs the function on the output (from the selected step); ◀◀ / ▶▶ step through it while it runs, » jumps to the selected step. Tap tempo (above) re-times a running chaser." style={{ marginTop: 4 }} />
          </div>
        </div>
        <PickerDialog open={picker} title="Add a step: pick the function" items={funcItems} onPick={ids => ids.forEach(id => addStep(Object.assign(defaultStep(), { targetFunctionId: id })))} onClose={() => setPicker(false)} />
      </div>
    );
  }

  /**
   * Sequence side (SequenceEditor.qml): bound Scene (pick another one), the Scene's fixtures (add /
   * remove, functions.scene.setMembers on the bound Scene), capture of the live output into the
   * selected step (functions.sequence.applyDumpValues captureLive), preview of the selected step on
   * the output (the bound Scene gets the step's values and runs, as ChaserEditor::setPlaybackIndex
   * does in the Qt editor) and the selected step's per-channel console (the Scene editor's console,
   * fed with the step values; writes go through functions.steps.replaceStep).
   */
  function SequencePanel({ qlc, detail, steps, sel, fixtures, functions, running, valuesOpen, setValuesOpen, setStepValue, replaceStep, reload, setSel }) {
    const D = window.QLCData;
    const td = detail.typeDetail || {};
    const fid = String(detail.id);
    const sceneId = td.boundSceneId != null ? String(td.boundSceneId) : null;
    const [scene, setScene] = React.useState(null);
    const [picker, setPicker] = React.useState(false);
    const [preview, setPreview] = React.useState(false);
    const [busy, setBusy] = React.useState('');
    const loadScene = React.useCallback(() => { if (sceneId) qlc.call('functions.get', { functionId: sceneId }).then(setScene).catch(() => setScene(null)); }, [sceneId, qlc.online]);
    React.useEffect(() => { setScene(null); loadScene(); }, [sceneId, qlc.online]);
    React.useEffect(() => qlc.subscribeTo('functions.scene.membersChanged', d => { if (d && String(d.functionId) === sceneId) loadScene(); }), [sceneId, qlc.online]);
    const sceneRunning = useRunning(qlc, sceneId || '', scene && scene.running);
    const members = ((scene && scene.typeDetail && scene.typeDetail.fixtures) || []).map(String);
    const curIndex = sel.length === 1 ? sel[0] : -1;
    const curStep = curIndex >= 0 ? steps[curIndex] : null;

    /* preview: the step's values into the bound Scene, and the Scene running */
    const pushPreview = (step) => step ? FF.mutate(qlc, 'functions.scene.setValues', { functionId: sceneId, values: step.values || {} }, { key: 'seqpreview:' + sceneId }).catch(() => {}) : Promise.resolve();
    React.useEffect(() => { if (preview && curStep) pushPreview(curStep); }, [preview, curIndex, curStep && JSON.stringify(curStep.values)]);
    React.useEffect(() => { if (preview && !sceneRunning && !running) setPreview(false); }, [sceneRunning]);
    const togglePreview = () => {
      if (!sceneId) return;
      if (preview) { setPreview(false); qlc.call('functions.stop', { functionId: sceneId }).catch(() => {}); return; }
      setPreview(true);
      pushPreview(curStep).then(() => qlc.call('functions.start', { functionId: sceneId })).catch(e => FF.reportError(e, 'functions.start'));
    };
    React.useEffect(() => () => { /* leaving the editor ends the preview */ }, []);

    const capture = () => {
      setBusy('Capturing…');
      FF.mutate(qlc, 'functions.sequence.applyDumpValues', Object.assign({ functionId: fid, captureLive: true }, curIndex >= 0 ? { targetStepIndex: curIndex } : {}))
        .then(r => { setBusy('Captured ' + ((r && r.capturedChannels) || 0) + ' channels into step ' + ((r && r.stepIndex != null ? r.stepIndex : 0) + 1)); if (r && r.stepIndex != null) setSel([r.stepIndex]); reload(); })
        .catch(e => setBusy((e && e.message) || 'capture failed'));
    };
    const setMembers = (ids) => FF.mutate(qlc, 'functions.scene.setMembers', { functionId: sceneId, fixtures: ids.map(String) }).then(loadScene).catch(() => loadScene());
    const removeFixture = (id) => {
      setMembers(members.filter(m => m !== String(id)));
      /* drop that fixture's channels from every step, like ChaserEditor::removeFixtures() */
      steps.forEach((s, i) => { const vals = Object.assign({}, s.values || {}); let hit = false; Object.keys(vals).forEach(k => { if (k.split('.')[0] === String(id)) { delete vals[k]; hit = true; } }); if (hit) replaceStep(i, { values: vals }); });
    };
    /* a channel new to the Sequence joins the bound Scene too (as in the Qt editor, where the step
       console is the bound Scene's editor) - otherwise the next applyDumpValues re-normalisation,
       which aligns every step with the Scene's channel set, would drop it again */
    const sceneValues = (scene && scene.typeDetail && scene.typeDetail.values) || {};
    const setChannel = (fx, ch, v) => {
      const key = fx + '.' + ch;
      if (sceneId && !Object.prototype.hasOwnProperty.call(sceneValues, key)) {
        setScene(s => s ? Object.assign({}, s, { typeDetail: Object.assign({}, s.typeDetail, { values: Object.assign({}, sceneValues, { [key]: 0 }) }) }) : s);
        FF.mutate(qlc, 'functions.scene.setValue', { functionId: sceneId, fixture: String(fx), channel: ch, value: 0 }).catch(() => loadScene());
      }
      setStepValue(curIndex, key, v);
    };
    const bindScene = (id) => FF.mutate(qlc, 'functions.sequence.setBoundScene', { functionId: fid, sceneId: String(id) }).then(reload).catch(() => reload());
    const sceneItems = (functions || []).filter(f => f.type === 'Scene').map(f => ({ mLabel: f.name + (String(f.id) === sceneId ? '' : ''), mValue: String(f.id) }));
    const hiddenBound = sceneId && !sceneItems.some(m => m.mValue === sceneId);
    const fixtureItems = (fixtures || []).filter(f => members.indexOf(String(f.id)) === -1).map(f => ({ id: String(f.id), name: f.name, icon: D.icon(Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture'), hint: 'U' + (f.universe + 1) + '.' + (f.address + 1) }));
    const valueFixtureIds = curStep ? Object.keys(curStep.values || {}).map(k => k.split('.')[0]) : [];
    const shownIds = Array.from(new Set(members.concat(valueFixtureIds)));

    return (
      <div data-e2e="sequence-panel" style={{ padding: 10, borderTop: 'var(--border-dark)', display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
          <RobotoText label="Bound scene" fontSize={13} labelColor="var(--fg-light)" />
          <select value={sceneId || ''} onChange={e => { if (e.target.value && e.target.value !== sceneId) bindScene(e.target.value); }} style={Object.assign({ width: 200 }, inputStyle)} data-e2e="seq-bound-scene">
            {hiddenBound ? <option value={sceneId}>{(scene && scene.name) || 'Scene ' + sceneId} (hidden)</option> : null}
            {sceneItems.map(m => <option key={m.mValue} value={m.mValue}>{m.mLabel}</option>)}
          </select>
          <IconButton imgSource={D.icon('fixture')} size={24} tooltip="Add fixtures to the bound scene" disabled={!sceneId} onClick={() => setPicker(true)} />
          <div style={{ width: 1, height: 18, background: 'var(--border-color-dark)' }} />
          <GenericButton label="Capture live" width={100} height={24} onClick={capture} disabled={!sceneId}
            tooltip={curIndex >= 0 ? 'Store the live output of the scene channels into step ' + (curIndex + 1) : 'Append a step with the live output of the scene channels'} />
          <IconButton faSource={preview ? FF.GLYPH.eye : FF.GLYPH.eyeSlash} size={24} checked={preview} disabled={!sceneId}
            tooltip={preview ? 'Stop previewing the selected step on the output' : 'Preview the selected step on the output (the bound scene runs with the step values)'} onClick={togglePreview} />
          {busy ? <RobotoText label={busy} fontSize={12} labelColor="var(--fg-light)" /> : null}
        </div>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
          {members.map(id => {
            const f = (fixtures || []).find(x => String(x.id) === id);
            return (
              <span key={id} style={{ display: 'inline-flex', alignItems: 'center', gap: 4, padding: '0 4px 0 6px', height: 24, background: 'var(--bg-strong)', borderRadius: 3 }}>
                <RobotoText label={f ? f.name : 'Fixture ' + id} fontSize={12} height={22} />
                <IconButton faSource="fa_xmark" size={18} tooltip="Remove this fixture from the bound scene and every step" onClick={() => removeFixture(id)} />
              </span>
            );
          })}
          {!members.length ? <RobotoText label="The bound scene has no fixtures: add some, then set step values or capture them." fontSize={12} labelColor="var(--fg-medium)" /> : null}
        </div>
        {curStep ? (
          <div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <RobotoText label={'Step ' + (curIndex + 1) + ' values'} fontBold fontSize={13} />
              <GenericButton label={valuesOpen ? 'Hide' : 'Edit'} width={60} height={22} onClick={() => setValuesOpen(!valuesOpen)} />
              {curIndex > 0 ? <GenericButton label="Copy from previous step" width={175} height={22} onClick={() => replaceStep(curIndex, { values: Object.assign({}, steps[curIndex - 1].values || {}) })} /> : null}
            </div>
            {valuesOpen ? shownIds.map(id => {
              const f = (fixtures || []).find(x => String(x.id) === id);
              if (!f) return <RobotoText key={id} label={'Fixture ' + id + ' (unpatched)'} fontSize={12} labelColor="var(--fg-medium)" />;
              return (
                <div key={id} data-e2e={'seq-console-' + id}>
                  <RobotoText label={f.name} fontBold fontSize={13} height={20} />
                  <SceneFixtureConsole qlc={qlc} sceneId={sceneId} fixture={f} values={curStep.values || {}}
                    setValue={(fx, ch, v) => setChannel(fx, ch, v)} unsetValue={(fx, ch) => setStepValue(curIndex, fx + '.' + ch, null)} />
                </div>
              );
            }) : null}
          </div>
        ) : <RobotoText label="Select one step to edit its channel values." fontSize={13} labelColor="var(--fg-medium)" />}
        <PickerDialog open={picker} title="Add fixtures to the bound scene" items={fixtureItems} onPick={ids => setMembers(members.concat(ids))} onClose={() => setPicker(false)} />
      </div>
    );
  }

  Object.assign(FF, { TimingEditor, SceneEditor, ChaserEditor, PickerDialog });
})();
