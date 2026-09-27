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
 * Server coverage (controlapi/src/domains/apifunctionsdomain.cpp): functions.update,
 * functions.scene.setValue/unsetValue/setValues/setMembers, functions.steps.add/remove/replace/
 * moveStep are registered. functions.chaser.setSpeedModes, functions.chaser.setAction,
 * functions.sequence.applyDumpValues are in the spec but not registered - those controls are
 * marked and disabled once the server says so.
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
        <FF.Heading text="Speed" />
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
              <FF.Note text="Drag a fader or click a value to set it; a channel enters the scene with its first value. × removes the channel again. Live preview of the scene is not available from the web UI — use Fixture Tools with target Live output, or start the scene." />
            </> : <RobotoText label={members.length ? 'Pick a fixture on the left.' : 'Add fixtures to start setting values.'} fontSize={14} labelColor="var(--fg-medium)" />}
            <div style={{ borderTop: 'var(--border-dark)', paddingTop: 8 }}>
              <TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} showRun={false} />
            </div>
          </div>
        </div>
        <PickerDialog open={picker === 'fixtures'} title="Add fixtures to the scene" items={fixtureItems} onPick={addFixtures} onClose={() => setPicker(null)} />
        <PickerDialog open={picker === 'palettes'} title="Add palettes to the scene" items={paletteItems} onPick={addPalettes} onClose={() => setPicker(null)} />
      </div>
    );
  }

  /* ---- chaser / sequence editor ------------------------------------------------------------- */
  function ChaserEditor({ qlc, detail, reload, setDetail, functions, fixtures }) {
    const D = window.QLCData;
    const isSeq = detail.type === 'Sequence';
    const td = detail.typeDetail || {};
    const steps = td.steps || [];
    const fid = String(detail.id);
    const [sel, setSel] = React.useState([]);          /* selected step indices */
    const [picker, setPicker] = React.useState(false);
    const [valuesOpen, setValuesOpen] = React.useState(false);
    const [live, setLive] = React.useState(null);
    React.useEffect(() => { setSel(s => s.filter(i => i < steps.length)); }, [steps.length]);
    /* Live playback position when the server reports it (functions.status.changed may carry currentStep). */
    React.useEffect(() => qlc.subscribeTo('functions.status.changed', d => { if (d && String(d.functionId != null ? d.functionId : d.id) === fid && d.currentStep !== undefined) setLive(d.currentStep); }), [fid, qlc.online]);

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

    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 8px', background: 'var(--bg-strong)', flex: 'none' }}>
          <IconButton faSource="fa_plus" size={26} tooltip={isSeq ? 'Add a new step (empty values)' : 'Add a new step'} onClick={() => isSeq ? addStep(Object.assign(defaultStep(), { values: {} })) : setPicker(true)} />
          <IconButton faSource={FF.GLYPH.clone} size={26} tooltip="Duplicate the selected step(s)" disabled={!sel.length} onClick={duplicate} />
          <IconButton faSource={FF.GLYPH.arrowUp} size={26} tooltip="Move the selected step up" disabled={sel.length !== 1 || sel[0] === 0} onClick={() => move(-1)} />
          <IconButton faSource={FF.GLYPH.arrowDown} size={26} tooltip="Move the selected step down" disabled={sel.length !== 1 || sel[0] === steps.length - 1} onClick={() => move(1)} />
          <IconButton faSource={FF.GLYPH.minus} size={26} tooltip="Remove the selected steps" disabled={!sel.length} onClick={removeSelected} />
          <div style={{ flex: 1 }} />
          <RobotoText label={steps.length + ' steps · total ' + FF.ms(detail.totalDuration)} fontSize={13} labelColor="var(--fg-light)" />
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div style={{ flex: 1, minWidth: 0, overflow: 'auto' }}>
            <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--font-roboto)', fontSize: 13, color: 'var(--fg-main)' }}>
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
                    <tr key={i} onClick={(e) => setSel(e.ctrlKey || e.metaKey ? (on ? sel.filter(x => x !== i) : sel.concat([i])) : e.shiftKey && sel.length ? (() => { const a = Math.min(sel[0], i), b = Math.max(sel[0], i); const r = []; for (let k = a; k <= b; k++) r.push(k); return r; })() : [i])}
                      style={{ height: 28, background: on ? 'var(--highlight)' : (i % 2 ? 'var(--bg-medium)' : 'var(--bg-stronger)'), cursor: 'pointer' }}>
                      <td style={{ padding: '0 6px', color: 'var(--fg-light)' }}>{live === i ? <FF.Glyph g="fa_play" size={10} color="var(--check-lime)" /> : null}{' ' + (i + 1)}</td>
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
            {isSeq && curStep ? (
              <div style={{ padding: 10, borderTop: 'var(--border-dark)' }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                  <RobotoText label={'Step ' + (sel[0] + 1) + ' values'} fontBold fontSize={13} />
                  <GenericButton label={valuesOpen ? 'Hide' : 'Edit'} width={60} height={22} onClick={() => setValuesOpen(!valuesOpen)} />
                  {sel[0] > 0 ? <GenericButton label="Copy from previous step" width={175} height={22} onClick={() => replaceStep(sel[0], { values: Object.assign({}, steps[sel[0] - 1].values || {}) })} /> : null}
                </div>
                {valuesOpen ? <SequenceStepValues qlc={qlc} step={curStep} index={sel[0]} fixtures={fixtures} boundSceneId={td.boundSceneId} setStepValue={setStepValue} /> : null}
              </div>
            ) : null}
          </div>
          <div style={{ width: 300, flex: 'none', overflow: 'auto', borderLeft: 'var(--border-dark)', padding: 10, display: 'flex', flexDirection: 'column', gap: 4 }}>
            <TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} />
            <FF.Heading text="Speed modes" style={{ marginTop: 6 }} />
            <FF.Row label="Fade In" width={80}><FF.Choice options={FF.SPEED_MODES} labels={FF.SPEED_MODE_LABELS} value={td.fadeInMode} onChange={v => setSpeedMode('fadeInMode', v)} disabled={speedModesUnsupported} /></FF.Row>
            <FF.Row label="Fade Out" width={80}><FF.Choice options={FF.SPEED_MODES.filter(m => m !== 'Default')} labels={FF.SPEED_MODE_LABELS} value={td.fadeOutMode} onChange={v => setSpeedMode('fadeOutMode', v)} disabled={speedModesUnsupported} /></FF.Row>
            <FF.Row label="Duration" width={80}><FF.Choice options={FF.SPEED_MODES.filter(m => m !== 'Default')} labels={FF.SPEED_MODE_LABELS} value={td.durationMode} onChange={v => setSpeedMode('durationMode', v)} disabled={speedModesUnsupported} /></FF.Row>
            <FF.Note text={speedModesUnsupported ? 'Changing speed modes is not available: this server has no functions.chaser.setSpeedModes. Per-step times are editable only where the mode is already Per Step.' : 'Common = every step uses the Speed values above; Per Step = each step has its own.'} style={{ marginTop: 4 }} />
            {isSeq ? <FF.Note text={'Bound scene: ' + td.boundSceneId + '. Capturing live DMX into a step (functions.sequence.applyDumpValues) is not available on this server; step values are edited per channel below the step list.'} style={{ marginTop: 4 }} /> : null}
            <FF.Note text="Live step control (next / previous / go to step) is not available: functions.chaser.setAction is not registered by the server." style={{ marginTop: 4 }} />
          </div>
        </div>
        <PickerDialog open={picker} title="Add a step: pick the function" items={funcItems} onPick={ids => ids.forEach(id => addStep(Object.assign(defaultStep(), { targetFunctionId: id })))} onClose={() => setPicker(false)} />
      </div>
    );
  }

  /** Sequence step values grouped by fixture; channels of the bound scene's fixtures. */
  function SequenceStepValues({ qlc, step, index, fixtures, setStepValue }) {
    const D = window.QLCData;
    const values = step.values || {};
    const fids = Array.from(new Set(Object.keys(values).map(k => k.split('.')[0])));
    const [extra, setExtra] = React.useState(null);
    const details = FF.useFixtureDetails(qlc, fids.concat(extra ? [extra] : []));
    const list = fids.concat(extra && fids.indexOf(extra) === -1 ? [extra] : []);
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 8, marginTop: 6 }}>
        {list.map(fid => {
          const d = details[fid];
          const f = fixtures.find(x => String(x.id) === fid);
          if (!d) return <RobotoText key={fid} label={(f ? f.name : 'Fixture ' + fid) + ' — loading…'} fontSize={13} labelColor="var(--fg-medium)" />;
          return (
            <div key={fid}>
              <RobotoText label={d.name} fontBold fontSize={13} height={20} />
              <div style={{ display: 'flex', gap: 4, overflowX: 'auto' }}>
                {d.channelList.map(ch => {
                  const key = fid + '.' + ch.index, set = Object.prototype.hasOwnProperty.call(values, key), v = set ? values[key] : 0;
                  return (
                    <div key={ch.index} title={ch.name} style={{ width: 48, flex: 'none', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 3, padding: 3, background: set ? 'var(--bg-strong)' : 'transparent', border: set ? '1px solid var(--bg-control)' : '1px solid transparent', boxSizing: 'border-box' }}>
                      <img src={Icons.channelIcon(ch)} alt="" style={{ width: 18, height: 18, opacity: set ? 1 : 0.45 }} />
                      <div style={{ opacity: set ? 1 : 0.55 }}><QLCPlusFader value={v} height={90} width={26} onMoved={val => setStepValue(index, key, val)} /></div>
                      <FF.InlineNumber value={v} onCommit={val => setStepValue(index, key, Math.max(0, Math.min(255, Math.round(val))))} width={42} />
                      <button type="button" disabled={!set} onClick={() => setStepValue(index, key, null)} title={set ? 'Remove from step' : 'Not in step'}
                        style={{ width: 20, height: 18, border: 'none', background: 'transparent', color: set ? 'var(--fg-light)' : 'transparent', cursor: set ? 'pointer' : 'default', fontFamily: 'var(--font-awesome)', fontWeight: 900, fontSize: 11 }}>{''}</button>
                    </div>
                  );
                })}
              </div>
            </div>
          );
        })}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <RobotoText label="Add fixture to this step" fontSize={13} labelColor="var(--fg-light)" />
          <select value={extra || ''} onChange={e => setExtra(e.target.value || null)} style={Object.assign({ width: 220 }, inputStyle)}>
            <option value="">—</option>
            {fixtures.filter(f => fids.indexOf(String(f.id)) === -1).map(f => <option key={f.id} value={String(f.id)}>{f.name}</option>)}
          </select>
        </div>
      </div>
    );
  }

  Object.assign(FF, { TimingEditor, SceneEditor, ChaserEditor, PickerDialog });
})();
