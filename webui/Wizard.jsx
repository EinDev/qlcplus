/**
 * Wizard.jsx — the Show Wizard (qmlui/qml/stagewizard/ShowWizard.qml and its six steps) for the web
 * UI: show type, fixture groups & roles (with a fixture browser that patches new fixtures straight
 * into a group), venue & stage, effects, external controller, summary + generate.
 *
 * Server: core.wizard.getOptions (catalogues + the project's groups / patched controllers),
 * core.wizard.preview (the resolved choice set: auto roles, show-type effect defaults, suggested
 * stage size, availability, the summary rows) and core.wizard.generate (controlapi/src/domains/
 * apiwizarddomain.cpp over a headless StageWizard). The client only keeps the choice set; every
 * derived value comes from preview, so what the dialog shows is what generate will build.
 *
 * Opened from the Fixtures & Functions right rail (hat button) and the Actions menu, through a
 * 'qlc-wizard-open' window event caught by an invisible toolbar item (like MediaActions.jsx).
 * Unlike the desktop dialog, generating is NOT undoable (API edits bypass the undo history).
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, GenericButton, IconButton, CustomComboBox, CustomSpinBox, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };
  const STEPS = ['Show Type', 'Fixture Groups & Roles', 'Venue & Stage', 'Effects', 'Controller', 'Summary'];
  const FAMILIES = ['Color', 'Intensity', 'Movement', 'Matrix', 'Show Cues'];
  const SWATCHES = ['#FF0000', '#00FF00', '#0000FF', '#00FFFF', '#FF00FF', '#FFFF00', '#FFFFFF'];
  const HAT = '';
  const open = () => window.dispatchEvent(new CustomEvent('qlc-wizard-open'));

  let boxSeq = 0;
  const newBox = (name) => ({ key: 'n' + (++boxSeq), groupId: null, name, fixtureIds: [], selected: true, role: null, edited: true });

  /* The wire choice set (CoreWizardChoices) from the dialog state */
  function wireChoices(st) {
    const groups = st.boxes.filter(b => b.selected).map(b => {
      const g = {};
      if (b.groupId != null) { g.groupId = b.groupId; if (b.edited) g.fixtureIds = b.fixtureIds; }
      else { g.name = b.name; g.fixtureIds = b.fixtureIds; }
      if (b.role) g.role = b.role;
      return g;
    });
    const c = { showType: st.showType, groups };
    if (st.stageType) c.stageType = st.stageType;
    if (st.envSize) c.envSize = st.envSize;
    if (st.effects) c.effects = st.effects;
    if (st.controller) c.controller = st.controller;
    return c;
  }
  const hasFixtures = (st) => st.boxes.some(b => b.selected && b.fixtureIds.length > 0);

  /* ---- small pieces ------------------------------------------------------------------------------ */
  function Tag({ text, color }) {
    return <span style={{ display: 'inline-block', padding: '1px 6px', margin: '0 4px 2px 0', borderRadius: 3, background: color || 'var(--bg-light)', color: 'var(--fg-main)', fontFamily: 'var(--font-roboto)', fontSize: 11 }}>{text}</span>;
  }
  function capTags(g) {
    const t = [];
    if (g.hasMovement) t.push(<Tag key="m" text="Pan/Tilt" color="#3A3A6A" />);
    if (g.hasRGB) t.push(<Tag key="c" text="RGB" color="#3A2A6A" />);
    if (g.hasColorWheel) t.push(<Tag key="w" text="Colour wheel" color="#2A3A6A" />);
    if (g.hasGobo) t.push(<Tag key="g" text="Gobo" color="#2A4A3A" />);
    if (g.hasShutter) t.push(<Tag key="s" text="Shutter" color="#4A3A2A" />);
    if (g.hasDimmer) t.push(<Tag key="d" text="Dimmer" color="#3A3A3A" />);
    return t;
  }
  function Card({ selected, onClick, children, dataAttr }) {
    return (
      <div onClick={onClick} data-wizard-card={dataAttr} style={{ cursor: 'pointer', padding: 10, borderRadius: 6, background: selected ? 'var(--highlight)' : 'var(--bg-stronger)', border: selected ? '2px solid var(--fg-light)' : 'var(--border-dark)', display: 'flex', flexDirection: 'column', gap: 4 }}>
        {children}
      </div>
    );
  }
  function StepIndicator({ step, skipVenue, onPick }) {
    return (
      <div style={{ display: 'flex', gap: 4, marginBottom: 8 }} data-wizard="steps">
        {STEPS.map((s, i) => {
          const skipped = i === 2 && skipVenue;
          return (
            <div key={s} onClick={() => i < step && !skipped && onPick(i)} style={{ flex: 1, padding: '4px 6px', borderRadius: 4, cursor: i < step && !skipped ? 'pointer' : 'default', background: i === step ? 'var(--highlight)' : 'var(--bg-stronger)', opacity: skipped ? 0.4 : 1 }}>
              <RobotoText label={(i + 1) + '. ' + s} fontSize={12} height={18} labelColor={i <= step ? 'var(--fg-main)' : 'var(--fg-medium)'} />
            </div>
          );
        })}
      </div>
    );
  }

  /* ---- step 2: the fixture browser (WizardFixtureBrowser.qml) ------------------------------------ */
  function FixtureBrowser({ qlc, target, onPatched }) {
    const [manufacturers, setManufacturers] = React.useState(null);
    const [manufacturer, setManufacturer] = React.useState('');
    const [models, setModels] = React.useState([]);
    const [model, setModel] = React.useState('');
    const [modes, setModes] = React.useState([]);
    const [mode, setMode] = React.useState('');
    const [search, setSearch] = React.useState('');
    const [quantity, setQuantity] = React.useState(1);
    const [universe, setUniverse] = React.useState(1);
    const [busy, setBusy] = React.useState(false);
    const [error, setError] = React.useState('');
    React.useEffect(() => {
      let alive = true;
      qlc.call('fixtures.defs.listManufacturers').then(r => { if (alive) setManufacturers((r && r.manufacturers) || []); }).catch(() => { if (alive) setManufacturers([]); });
      return () => { alive = false; };
    }, []);
    React.useEffect(() => {
      setModels([]); setModel('');
      if (!manufacturer) return undefined;
      let alive = true;
      qlc.call('fixtures.defs.listModels', { manufacturer }).then(r => { if (alive) setModels(((r && r.models) || []).map(m => typeof m === 'string' ? m : m.model)); }).catch(() => {});
      return () => { alive = false; };
    }, [manufacturer]);
    React.useEffect(() => {
      setModes([]); setMode('');
      if (!manufacturer || !model) return undefined;
      let alive = true;
      qlc.call('fixtures.defs.getModel', { manufacturer, model }).then(r => { if (!alive) return; const m = (r && r.modes) || []; setModes(m); if (m.length) setMode(m[0].name); }).catch(() => {});
      return () => { alive = false; };
    }, [manufacturer, model]);

    const modeInfo = modes.find(m => m.name === mode);
    const canAdd = !!(target && modeInfo && !busy);
    const add = async () => {
      if (!canAdd) return;
      setBusy(true); setError('');
      try {
        const free = await qlc.call('fixtures.findAvailableAddress', { universe: universe - 1, channels: modeInfo.channelCount, quantity, gap: 0, requestedAddress: 0 });
        if (!free || free.available !== true) throw new Error('No free range for ' + quantity + ' × ' + modeInfo.channelCount + ' channels in universe ' + universe);
        const r = await FF.mutate(qlc, 'fixtures.patch', { universe: universe - 1, address: free.address, definition: { manufacturer, model, mode }, name: model, quantity });
        onPatched((r && r.fixtureIds) || []);
      } catch (e) { setError((e && e.message) || 'Patch failed'); }
      setBusy(false);
    };
    const s = search.toLowerCase();
    const manuList = (manufacturers || []).filter(m => !s || m.toLowerCase().indexOf(s) !== -1 || m === manufacturer);
    const modelList = models.filter(m => !s || s.length < 2 || m.toLowerCase().indexOf(s) !== -1 || manufacturer.toLowerCase().indexOf(s) !== -1);
    const row = (label, sel, onClick, attr) => (
      <div key={label} onClick={onClick} data-wizard-def={attr} style={{ height: 22, padding: '0 6px', cursor: 'pointer', background: sel ? 'var(--highlight)' : 'transparent', overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }}>
        <RobotoText label={label} fontSize={12} height={22} />
      </div>
    );
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0, height: '100%' }} data-wizard="browser">
        <RobotoText label="Fixture Browser" fontBold fontSize={13} height={20} />
        <FF.Note text="Pick a definition and add it: it is patched at the next free address and put into the highlighted group." />
        <input value={search} onChange={e => setSearch(e.target.value)} placeholder="Search…" style={Object.assign({ width: '100%' }, inputStyle)} data-wizard="browser-search" />
        <div style={{ flex: 1, minHeight: 90, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
          {manufacturers === null ? <RobotoText label="Loading…" fontSize={12} labelColor="var(--fg-medium)" leftMargin={6} /> : null}
          {manuList.map(m => row(m, m === manufacturer, () => setManufacturer(m), 'manufacturer'))}
        </div>
        <div style={{ flex: 1, minHeight: 90, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
          {!manufacturer ? <RobotoText label="Pick a manufacturer" fontSize={12} labelColor="var(--fg-medium)" leftMargin={6} /> : null}
          {modelList.map(m => row(m, m === model, () => setModel(m), 'model'))}
        </div>
        <CustomComboBox width="100%" currValue={mode} model={modes.map(m => ({ mLabel: m.name + ' (' + m.channelCount + ' ch)', mValue: m.name }))} onValueChanged={setMode} disabled={!modes.length} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <RobotoText label="Qty" fontSize={12} height={24} />
          <CustomSpinBox value={quantity} from={1} to={64} width={70} onValueModified={setQuantity} />
          <RobotoText label="Uni" fontSize={12} height={24} />
          <CustomSpinBox value={universe} from={1} to={64} width={64} onValueModified={setUniverse} />
        </div>
        <GenericButton label={busy ? 'Patching…' : (target ? '+ Add to ' + target.name : 'Select a group first')} width="100%" height={26} fontSize={12} disabled={!canAdd} onClick={add} data-wizard="browser-add" />
        {error ? <RobotoText label={error} fontSize={12} labelColor="var(--override-red)" wrapText height="auto" /> : null}
      </div>
    );
  }

  /* ---- step 6: the Virtual Console preview (WizardStep6Summary.qml's schematic) -------------------- */
  function VcPreview({ groups }) {
    const [page, setPage] = React.useState(0);
    const anyMove = groups.some(g => g.hasMovement);
    const data = page === 0 ? { name: 'All Groups', hasRGB: true, hasDimmer: true, hasMovement: anyMove, all: true } : (groups[page - 1] || {});
    const btn = (label, hi, onClick, key) => (
      <div key={key || label} onClick={onClick} data-wizard-vcbutton={label} style={{ padding: '2px 8px', borderRadius: 3, cursor: onClick ? 'pointer' : 'default', background: hi ? '#3A3A6A' : '#1A1A2A', border: '1px solid #333355' }}>
        <RobotoText label={label} fontSize={11} height={18} labelColor={hi ? '#FFFFFF' : '#AAAACC'} />
      </div>
    );
    return (
      <div style={{ background: '#080810', border: '1px solid #2A2A44', borderRadius: 8, padding: 10, display: 'flex', flexDirection: 'column', gap: 8 }} data-wizard="vc-preview">
        <RobotoText label="Show Wizard · multipage frame" fontSize={11} fontBold height={16} labelColor="#7777AA" />
        <div style={{ display: 'flex', gap: 6, flexWrap: 'wrap', alignItems: 'center' }}>
          {btn('All Groups', page === 0, () => setPage(0))}
          {groups.map((g, i) => btn(g.name, page === i + 1, () => setPage(i + 1), 'g' + i))}
          <div style={{ flex: 1 }} />
          {btn('Blackout', false)}
        </div>
        <div style={{ height: 1, background: '#1A1A30' }} />
        <RobotoText label={(data.all ? '◉ ' : '▸ ') + (data.name || '')} fontSize={12} fontBold height={18} labelColor={data.all ? '#E9C046' : '#AAAACC'} />
        {(data.hasDimmer || data.hasRGB) ? (
          <div style={{ display: 'flex', gap: 8, alignItems: 'flex-end' }}>
            <div style={{ width: 16, height: 64, borderRadius: 6, background: '#0A0A1A', border: '1px solid #222244', display: 'flex', alignItems: 'flex-end', justifyContent: 'center' }} title="Intensity slider">
              <div style={{ width: 10, height: '70%', borderRadius: 4, background: '#E94560' }} />
            </div>
            {data.hasMovement ? (
              <div style={{ width: 64, height: 64, borderRadius: 4, background: '#0A0A1A', border: '1px solid #222244', position: 'relative' }} title="XY pad">
                <div style={{ position: 'absolute', left: 27, top: 27, width: 10, height: 10, borderRadius: 5, background: '#E94560' }} />
              </div>
            ) : null}
            {data.hasRGB ? (
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 14px)', gap: 4 }} title="Colour buttons">
                {SWATCHES.map(c => <div key={c} style={{ width: 14, height: 14, borderRadius: 3, background: c, border: '1px solid #222244' }} />)}
              </div>
            ) : null}
          </div>
        ) : null}
        <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap' }} title="Effect buttons">
          {Array.from({ length: data.hasMovement ? 5 : 2 }).map((_, i) => <div key={i} style={{ width: 30, height: 14, borderRadius: 3, background: '#1A1A2A', border: '1px solid #333355' }} />)}
        </div>
        <div style={{ height: 1, background: '#1A1A30' }} />
        <div style={{ display: 'flex', gap: 6 }}>{btn('Ambient', false)}{btn('Blinder', false)}</div>
      </div>
    );
  }

  /* ---- the dialog ---------------------------------------------------------------------------------- */
  const EMPTY = { step: 0, showType: 'ClubNight', boxes: null, active: null, stageType: null, envSize: null, effects: null, controller: null };

  function WizardHost({ qlc }) {
    const [isOpen, setOpen] = React.useState(false);
    const [st, setSt] = React.useState(EMPTY);
    const [options, setOptions] = React.useState(null);
    const [fixtures, setFixtures] = React.useState([]);
    const [preview, setPreview] = React.useState(null);
    const [error, setError] = React.useState('');
    const [busy, setBusy] = React.useState(false);
    const [result, setResult] = React.useState(null);
    const [pickFixture, setPickFixture] = React.useState('');
    const up = (patch) => setSt(s => Object.assign({}, s, typeof patch === 'function' ? patch(s) : patch));

    React.useEffect(() => {
      const on = () => { setOpen(true); setResult(null); setError(''); };
      window.addEventListener('qlc-wizard-open', on);
      return () => window.removeEventListener('qlc-wizard-open', on);
    }, []);

    const loadFixtures = () => qlc.call('fixtures.list').then(r => setFixtures((r && r.fixtures) || [])).catch(() => {});
    React.useEffect(() => {
      if (!isOpen || !qlc.online) return;
      setError('');
      qlc.call('core.wizard.getOptions').then(o => {
        setOptions(o);
        setSt(s => s.boxes ? s : Object.assign({}, s, { boxes: (o.groups || []).map(g => ({ key: 'g' + g.groupId, groupId: g.groupId, name: g.name, fixtureIds: g.fixtureIds.slice(), selected: false, role: null, edited: false, caps: g, suggestedRole: g.suggestedRole })) }));
      }).catch(e => setError((e && e.message) || 'core.wizard.getOptions failed'));
      loadFixtures();
    }, [isOpen, qlc.online]);

    /* the resolved choice set, refreshed whenever the choices change */
    const choices = st.boxes ? wireChoices(st) : null;
    const choicesKey = JSON.stringify(choices);
    React.useEffect(() => {
      if (!isOpen || !choices || !hasFixtures(st)) { setPreview(null); return undefined; }
      let alive = true;
      const t = setTimeout(() => {
        qlc.call('core.wizard.preview', { choices }).then(p => { if (alive) { setPreview(p); setError(''); } })
          .catch(e => { if (alive) { setPreview(null); setError((e && e.message) || 'core.wizard.preview failed'); } });
      }, 150);
      return () => { alive = false; clearTimeout(t); };
    }, [isOpen, choicesKey]);

    const close = () => setOpen(false);
    const cancel = () => { setOpen(false); setSt(EMPTY); setPreview(null); setOptions(null); setResult(null); };
    const skipVenue = !!preview && preview.placesFixtures === false;
    const canNext = st.step === 1 ? hasFixtures(st) && !!preview : true;
    const go = (to) => {
      if (to === 2 && skipVenue) to = st.step < 2 ? 3 : 1;
      up({ step: to });
    };

    const generate = async () => {
      setBusy(true); setError('');
      try {
        const r = await FF.mutate(qlc, 'core.wizard.generate', { choices });
        setResult(r);
      } catch (e) { setError((e && e.message) || 'Generate failed'); }
      setBusy(false);
    };

    const fixtureName = (id) => { const f = fixtures.find(x => String(x.id) === String(id)); return f ? f.name : '#' + id; };
    const assign = (key, ids) => up(s => ({
      boxes: s.boxes.map(b => {
        const others = b.fixtureIds.filter(id => ids.indexOf(id) === -1);   /* one box per fixture */
        if (b.key === key) return Object.assign({}, b, { fixtureIds: others.concat(ids), edited: true, selected: true });
        return others.length !== b.fixtureIds.length ? Object.assign({}, b, { fixtureIds: others, edited: true }) : b;
      })
    }));
    const unassign = (key, id) => up(s => ({ boxes: s.boxes.map(b => b.key === key ? Object.assign({}, b, { fixtureIds: b.fixtureIds.filter(x => x !== id), edited: true }) : b) }));
    const setBox = (key, patch) => up(s => ({ boxes: s.boxes.map(b => b.key === key ? Object.assign({}, b, patch) : b) }));
    const previewGroup = (b) => preview ? (preview.groups || []).find(g => b.groupId != null ? g.groupId === b.groupId : (g.groupId == null && g.name === b.name)) : null;

    /* ---------- step bodies ---------- */
    const body = () => {
      if (!options || !st.boxes) return <RobotoText label={error || 'Loading…'} fontSize={14} labelColor={error ? 'var(--override-red)' : 'var(--fg-medium)'} />;
      const selectedBoxes = st.boxes.filter(b => b.selected);

      if (st.step === 0) return (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <RobotoText label="What kind of show are you creating?" fontBold fontSize={15} height={22} />
          <FF.Note text="Your choice sets sensible defaults for stage layout and effects — you can customise everything in the next steps." />
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(190px, 1fr))', gap: 8 }}>
            {options.showTypes.map(t => (
              <Card key={t.id} selected={st.showType === t.id} dataAttr={'show-' + t.id} onClick={() => up({ showType: t.id, effects: null, stageType: null, envSize: null })}>
                <RobotoText label={t.name} fontBold fontSize={14} height={20} />
                <RobotoText label={t.description} fontSize={12} labelColor="var(--fg-light)" wrapText height="auto" />
                <RobotoText label={'Stage: ' + t.stage} fontSize={11} labelColor="var(--fg-medium)" height={16} />
                <div>{(t.tags || []).map(x => <Tag key={x} text={x} />)}</div>
              </Card>
            ))}
          </div>
        </div>
      );

      if (st.step === 1) {
        const activeBox = st.boxes.find(b => b.key === st.active) || null;
        const assigned = new Set([].concat(...st.boxes.map(b => b.fixtureIds)));
        const unassigned = fixtures.filter(f => !assigned.has(String(f.id)));
        return (
          <div style={{ display: 'grid', gridTemplateColumns: '220px 1fr 1fr', gap: 10, height: 430 }}>
            <FixtureBrowser qlc={qlc} target={activeBox} onPatched={(ids) => { loadFixtures(); if (activeBox) assign(activeBox.key, ids.map(String)); }} />
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
              <div style={{ display: 'flex', alignItems: 'center' }}>
                <RobotoText label="Fixture Groups" fontBold fontSize={13} height={24} style={{ flex: 1 }} />
                <GenericButton label="+ Add group" width={100} height={24} fontSize={12} data-wizard="add-group"
                  onClick={() => { const b = newBox('Group ' + (st.boxes.filter(x => x.groupId == null).length + 1)); up(s => ({ boxes: s.boxes.concat([b]), active: b.key })); }} />
              </div>
              <div style={{ flex: 1, overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 6 }}>
                {!st.boxes.length ? <FF.Note text="Add a group, then add fixtures to it from the browser." /> : null}
                {st.boxes.map(b => (
                  <div key={b.key} data-wizard-box={b.name} onClick={() => up({ active: b.key })}
                    style={{ border: st.active === b.key ? '2px solid var(--fg-light)' : 'var(--border-dark)', background: 'var(--bg-stronger)', borderRadius: 4, padding: 4 }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                      <CustomCheckBox checked={b.selected} onToggled={(v) => setBox(b.key, { selected: v })} size={20} data-wizard-box-check={b.name} />
                      {b.groupId == null
                        ? <input value={b.name} onChange={e => setBox(b.key, { name: e.target.value })} style={Object.assign({ flex: 1, minWidth: 0 }, inputStyle)} data-wizard-box-name="1" />
                        : <RobotoText label={b.name} fontSize={13} height={24} style={{ flex: 1 }} />}
                      <RobotoText label={'(' + b.fixtureIds.length + ')'} fontSize={12} height={24} labelColor="var(--fg-light)" />
                      {b.groupId == null ? <IconButton faSource="fa_xmark" size={22} tooltip="Remove this group box" onClick={(e) => { e && e.stopPropagation && e.stopPropagation(); up(s => ({ boxes: s.boxes.filter(x => x.key !== b.key) })); }} /> : null}
                    </div>
                    {b.fixtureIds.slice(0, 40).map(id => (
                      <div key={id} style={{ display: 'flex', alignItems: 'center', paddingLeft: 26 }}>
                        <RobotoText label={fixtureName(id)} fontSize={12} height={18} style={{ flex: 1 }} labelColor="var(--fg-light)" />
                        <span onClick={(e) => { e.stopPropagation(); unassign(b.key, id); }} style={{ cursor: 'pointer', color: 'var(--fg-medium)', fontSize: 12, padding: '0 4px' }} title="Remove from the group">×</span>
                      </div>
                    ))}
                    {b.fixtureIds.length > 40 ? <RobotoText label={'… ' + (b.fixtureIds.length - 40) + ' more'} fontSize={11} leftMargin={26} labelColor="var(--fg-medium)" /> : null}
                  </div>
                ))}
              </div>
              {activeBox && unassigned.length ? (
                <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
                  <CustomComboBox width="100%" currValue={pickFixture} model={[{ mLabel: 'Add a patched fixture…', mValue: '' }].concat(unassigned.map(f => ({ mLabel: f.name, mValue: String(f.id) })))} onValueChanged={setPickFixture} data-wizard="pick-fixture" />
                  <GenericButton label="Add" width={50} height={24} fontSize={12} disabled={!pickFixture} onClick={() => { assign(activeBox.key, [pickFixture]); setPickFixture(''); }} data-wizard="pick-fixture-add" />
                </div>
              ) : null}
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
              <RobotoText label="Detected capabilities & roles" fontBold fontSize={13} height={24} />
              <div style={{ flex: 1, overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 6 }}>
                {!selectedBoxes.length ? <FF.Note text="Tick a group box to detect its capabilities here." /> : null}
                {selectedBoxes.map(b => {
                  const g = previewGroup(b);
                  return (
                    <div key={b.key} style={{ background: 'var(--bg-stronger)', border: 'var(--border-dark)', borderRadius: 4, padding: 6 }} data-wizard-role-row={b.name}>
                      <RobotoText label={b.name + ' (' + b.fixtureIds.length + ')'} fontSize={13} fontBold height={20} />
                      <div>{g ? capTags(g) : <Tag text={b.fixtureIds.length ? '…' : 'empty'} />}</div>
                      <CustomComboBox width="100%" currValue={b.role || (g && g.role) || ''} model={options.roles.map(r => ({ mLabel: r.name, mValue: r.id }))} onValueChanged={(v) => setBox(b.key, { role: v })} data-wizard-role={b.name} />
                    </div>
                  );
                })}
              </div>
            </div>
          </div>
        );
      }

      if (st.step === 2) {
        const env = (preview && preview.envSize) || st.envSize || { width: 10, height: 5, depth: 8 };
        const stage = (preview && preview.stageType) || st.stageType;
        const setEnv = (k, v) => up({ envSize: Object.assign({ width: env.width, height: env.height, depth: env.depth }, { [k]: v }) });
        return (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
            <RobotoText label="Venue type" fontBold fontSize={15} height={22} />
            <FF.Note text="Fixtures are automatically positioned following industry rigging conventions." />
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 8 }}>
              {options.stageTypes.map(t => (
                <Card key={t.id} selected={stage === t.id} dataAttr={'stage-' + t.id} onClick={() => up({ stageType: t.id })}>
                  <RobotoText label={t.name} fontBold fontSize={14} height={20} />
                  <RobotoText label={t.description} fontSize={12} labelColor="var(--fg-light)" wrapText height="auto" />
                  <div>{(t.bestFor || []).map(x => <Tag key={x} text={x} />)}</div>
                </Card>
              ))}
            </div>
            <RobotoText label="Stage size (metres)" fontBold fontSize={14} height={22} />
            <div style={{ display: 'flex', gap: 12, alignItems: 'center' }} data-wizard="env">
              {[['width', 'W', 100], ['height', 'H', 30], ['depth', 'D', 100]].map(([k, l, max]) => (
                <span key={k} style={{ display: 'inline-flex', gap: 4, alignItems: 'center' }}>
                  <RobotoText label={l} fontSize={13} height={24} />
                  <CustomSpinBox value={Math.round(env[k])} from={2} to={max} width={80} onValueModified={(v) => setEnv(k, v)} data-wizard-env={k} />
                </span>
              ))}
            </div>
            <FF.Note text="Suggested from your fixture count. Adjust if your real stage differs." />
            <RobotoText label="Automatic fixture placement" fontBold fontSize={14} height={22} />
            {((preview && preview.groups) || []).map(g => {
              const r = options.roles.find(x => x.id === g.role);
              return <FF.Row key={g.name} label={g.name} width={160}>{(r ? r.name + ': ' : '') + (r ? r.placement : '')}</FF.Row>;
            })}
          </div>
        );
      }

      if (st.step === 3) {
        const effects = (preview && preview.effects) || [];
        const enabledIds = effects.filter(e => e.enabled).map(e => e.id);
        const toggle = (id, on) => up({ effects: (on ? enabledIds.concat([id]) : enabledIds.filter(x => x !== id)) });
        const toggleFamily = (fam) => {
          const avail = effects.filter(e => e.family === fam && e.available);
          const anyOff = avail.some(e => !e.enabled);
          const ids = enabledIds.filter(id => !avail.find(e => e.id === id));
          up({ effects: anyOff ? ids.concat(avail.map(e => e.id)) : ids });
        };
        return (
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(250px, 1fr))', gap: 10 }}>
            {!preview ? <RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /> : null}
            {FAMILIES.map(fam => {
              const list = effects.filter(e => e.family === fam);
              if (!list.length) return null;
              return (
                <div key={fam} style={{ background: 'var(--bg-stronger)', border: 'var(--border-dark)', borderRadius: 4, padding: 6 }} data-wizard-family={fam}>
                  <div style={{ display: 'flex', alignItems: 'center' }}>
                    <RobotoText label={fam} fontBold fontSize={13} height={24} style={{ flex: 1 }} />
                    <GenericButton label="All / None" width={80} height={22} fontSize={11} onClick={() => toggleFamily(fam)} disabled={!list.some(e => e.available)} data-wizard-family-toggle={fam} />
                  </div>
                  {list.map(e => (
                    <div key={e.id} style={{ display: 'flex', alignItems: 'center', gap: 6, opacity: e.available ? 1 : 0.45 }} title={e.available ? '' : 'No fixtures in the selected groups can do this'}>
                      <CustomCheckBox checked={e.enabled} disabled={!e.available} onToggled={(v) => toggle(e.id, v)} size={18} data-wizard-effect={e.id} />
                      <RobotoText label={e.name} fontSize={13} height={22} style={{ flex: 1 }} />
                      <RobotoText label={e.preview} fontSize={11} height={22} labelColor="var(--fg-medium)" />
                    </div>
                  ))}
                </div>
              );
            })}
          </div>
        );
      }

      if (st.step === 4) {
        const ctrl = (preview && preview.controller) || { universe: -1, map: true, feedback: true, colors: true };
        const setCtrl = (patch) => up({ controller: Object.assign({ universe: ctrl.universe, map: ctrl.map, feedback: ctrl.feedback, colors: ctrl.colors }, patch) });
        const opt = (label, key, text) => (
          <div style={{ display: 'flex', gap: 8, alignItems: 'flex-start', opacity: ctrl.universe >= 0 ? 1 : 0.5 }}>
            <CustomCheckBox checked={!!ctrl[key]} disabled={ctrl.universe < 0} onToggled={(v) => setCtrl({ [key]: v })} size={18} data-wizard-ctrl-opt={key} />
            <div><RobotoText label={label} fontSize={13} fontBold height={20} /><FF.Note text={text} /></div>
          </div>
        );
        return (
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
              <RobotoText label="External controller mapping" fontBold fontSize={15} height={22} />
              <FF.Note text="Pick a patched MIDI, OSC or DMX controller and the wizard will bind it to the Virtual Console it generates. This step is optional — you can skip it and assign controls later." />
              <RobotoText label="Connected controllers" fontBold fontSize={13} height={22} />
              <Card selected={ctrl.universe < 0} dataAttr="ctrl-none" onClick={() => setCtrl({ universe: null })}>
                <RobotoText label="No controller" fontSize={13} height={20} />
              </Card>
              {options.controllers.map(c => (
                <Card key={c.universe} selected={ctrl.universe === c.universe} dataAttr={'ctrl-' + c.universe} onClick={() => setCtrl({ universe: c.universe })}>
                  <RobotoText label={(c.lineName || c.plugin || 'Controller') + ' · Universe ' + (c.universe + 1)} fontSize={13} fontBold height={20} />
                  <div>
                    {c.hasProfile ? <Tag text={c.profile} /> : <Tag text="No input profile" color="#4A3A1A" />}
                    {c.buttons ? <Tag text={c.buttons + ' buttons'} /> : null}
                    {c.faders ? <Tag text={c.faders + ' faders'} /> : null}
                    {c.hasColorTable ? <Tag text="colour LEDs" color="#3A1A3A" /> : null}
                    {c.feedback ? <Tag text="feedback on" color="#2A4A2A" /> : null}
                  </div>
                </Card>
              ))}
              {!options.controllers.length ? <FF.Note text="No controller patched: patch your controller's input line to a universe on the Inputs/Outputs screen, then reopen the wizard." /> : null}
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              <RobotoText label="Mapping options" fontBold fontSize={15} height={22} />
              {opt('Auto-map Virtual Console controls', 'map', 'Bind the generated buttons, faders and XY pads to the controller\'s channels.')}
              {opt('Send feedback to the controller', 'feedback', 'Patch the controller\'s output line so QLC+ lights its LEDs and moves its motorised faders.')}
              {opt('Match LED colours to button colours', 'colors', 'For controllers whose input profile has a colour table.')}
              {ctrl.mappingPreview ? <FF.Row label="Estimated usage" width={120}>{ctrl.mappingPreview}</FF.Row> : null}
            </div>
          </div>
        );
      }

      /* step 5: summary */
      const effects = ((preview && preview.effects) || []).filter(e => e.enabled && e.available);
      return (
        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <RobotoText label="Ready to generate" fontBold fontSize={15} height={22} />
            <FF.Note text="Review what will be created and press Generate. Unlike the desktop wizard this cannot be undone from the web UI." />
            {((preview && preview.summary) || []).map(r => <FF.Row key={r.section} label={r.section} width={120}>{r.detail}</FF.Row>)}
            <RobotoText label="Selected effects" fontBold fontSize={13} height={22} />
            <div data-wizard="summary-effects">{effects.map(e => <Tag key={e.id} text={e.name} />)}</div>
            {result ? (
              <div data-wizard="result" style={{ padding: 6, background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
                <RobotoText label={'Generated ' + result.functionIds.length + ' functions, ' + result.fixtureGroupIds.length + ' fixture groups, ' + result.paletteIds.length + ' palettes and ' + result.vcWidgetIds.length + ' Virtual Console widgets.'} fontSize={13} labelColor="var(--check-lime)" wrapText height="auto" />
              </div>
            ) : null}
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <RobotoText label="Virtual Console layout preview" fontBold fontSize={13} height={22} />
            <VcPreview groups={(preview && preview.groups) || []} />
          </div>
        </div>
      );
    };

    const last = st.step === STEPS.length - 1;
    const buttons = result ? ['Close'] : last ? ['Cancel', 'Back', busy ? 'Generating…' : 'Generate'] : st.step === 0 ? ['Cancel', 'Next'] : ['Cancel', 'Back', 'Next'];
    const disabled = [];
    if (!canNext) disabled.push('Next');
    if (busy || !preview) disabled.push('Generate', 'Generating…');
    return (
      <CustomPopupDialog open={isOpen} title="Show Wizard" width={980} standardButtons={buttons} disabledButtons={disabled}
        onClose={close}
        onClicked={(b) => {
          if (b === 'Cancel') cancel();
          else if (b === 'Close') cancel();
          else if (b === 'Back') go(st.step - 1);
          else if (b === 'Next') go(st.step + 1);
          else if (b === 'Generate') generate();
        }}>
        <div data-wizard="dialog" data-wizard-step={st.step} style={{ display: 'flex', flexDirection: 'column' }}>
          <StepIndicator step={st.step} skipVenue={skipVenue} onPick={go} />
          <div style={{ height: 470, overflow: 'auto' }}>{body()}</div>
          {error ? <RobotoText label={error} fontSize={13} labelColor="var(--override-red)" wrapText height="auto" /> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  window.QLCWizard = { open };
  window.QLCToolbarItems = (window.QLCToolbarItems || []).concat([WizardHost]);
  window.QLCMenuItems = (window.QLCMenuItems || []).concat([({ online }) => [
    '-',
    { label: 'Show Wizard…', fa: HAT, disabled: !online, onClick: open }
  ]]);
})();
