/**
 * FixtureDialogs.jsx — Add Fixture dialog (FixtureBrowser.qml), Fixture Groups panel
 * (FixtureGroupManager.qml) and the Palettes panel (PaletteManager.qml) for the web UI.
 *
 * Add Fixture browses fixtures.defs.listManufacturers → listModels → getModel (modes) and patches
 * with fixtures.patch {universe, address, definition, name, quantity, gap}; the next free address
 * comes from fixtures.findAvailableAddress. Without fixtures.defs.* on the server only a generic
 * dimmer can be patched and the dialog says so.
 * Palettes: palette.list/get/create/update/delete; "apply to selection" is client-side (there is
 * no server-side palette apply) via FF.paletteValues → io.simpleDesk.setChannels.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, SectionBox, CustomSpinBox, CustomComboBox, GenericButton, IconButton, CustomTextInput, CustomPopupDialog } = window.PatchDesignSystem_5432c9;
  const Icons = window.QLCIcons;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' };
  const GENERIC = '__generic__';

  /* ---- Add fixture -------------------------------------------------------------------------- */
  function AddFixtureDialog({ open, qlc, universes, fixtures, onClose }) {
    const D = window.QLCData;
    const [manufacturers, setManufacturers] = React.useState(null); /* null loading, [] none, false unsupported */
    const [manufacturer, setManufacturer] = React.useState('');
    const [models, setModels] = React.useState([]);
    const [model, setModel] = React.useState('');
    const [modelInfo, setModelInfo] = React.useState(null);
    const [mode, setMode] = React.useState('');
    const [search, setSearch] = React.useState('');
    const [universe, setUniverse] = React.useState(0);
    const [address, setAddress] = React.useState(1);
    const [quantity, setQuantity] = React.useState(1);
    const [gap, setGap] = React.useState(0);
    const [name, setName] = React.useState('');
    const [genericChannels, setGenericChannels] = React.useState(1);
    const [free, setFree] = React.useState(null);
    const [busy, setBusy] = React.useState(false);
    const [error, setError] = React.useState('');

    React.useEffect(() => {
      if (!open || !qlc.online) return undefined;
      let alive = true;
      setError(''); setFree(null);
      setUniverse(universes.length ? universes[0].id : 0);
      qlc.call('fixtures.defs.listManufacturers').then(r => { if (alive) setManufacturers((r && r.manufacturers) || []); })
        .catch(() => { if (alive) setManufacturers(false); });
      return () => { alive = false; };
    }, [open, qlc.online]);
    React.useEffect(() => {
      setModels([]); setModel(''); setModelInfo(null); setMode('');
      if (!manufacturer || manufacturer === GENERIC || !qlc.online) return undefined;
      let alive = true;
      qlc.call('fixtures.defs.listModels', { manufacturer }).then(r => {
        if (!alive) return;
        /* models: [string] plus modelDetails: [{model, isUser}] on the merged server; older builds return [{model, isUser}] directly. */
        const details = {};
        ((r && r.modelDetails) || []).forEach(d => { details[d.model] = d; });
        setModels(((r && r.models) || []).map(m => typeof m === 'string' ? Object.assign({ model: m }, details[m] || {}) : m));
      }).catch(() => {});
      return () => { alive = false; };
    }, [manufacturer]);
    React.useEffect(() => {
      setModelInfo(null); setMode('');
      if (!manufacturer || manufacturer === GENERIC || !model || !qlc.online) return undefined;
      let alive = true;
      qlc.call('fixtures.defs.getModel', { manufacturer, model }).then(r => { if (!alive) return; setModelInfo(r); const modes = (r && r.modes) || []; if (modes.length) setMode(modes[0].name); setName(model); }).catch(() => {});
      return () => { alive = false; };
    }, [manufacturer, model]);

    const isGeneric = manufacturer === GENERIC;
    const modeInfo = modelInfo ? ((modelInfo.modes || []).find(m => m.name === mode) || null) : null;
    const channels = isGeneric ? genericChannels : (modeInfo ? modeInfo.channelCount : 0);
    const definition = isGeneric ? { generic: { channels: genericChannels } } : (manufacturer && model && mode ? { manufacturer, model, mode } : null);
    const footprint = quantity * channels + Math.max(0, quantity - 1) * gap;

    /* Next free address: server-side when available, else a client-side scan of the patch. */
    React.useEffect(() => {
      if (!open || !channels || !qlc.online) { setFree(null); return undefined; }
      let alive = true;
      const local = () => {
        const used = new Array(512).fill(false);
        fixtures.filter(f => f.universe === universe).forEach(f => { for (let i = f.address; i < f.address + f.channels && i < 512; i++) used[i] = true; });
        const fits = (start) => { for (let n = 0; n < quantity; n++) { const a = start + n * (channels + gap); for (let i = a; i < a + channels; i++) if (i >= 512 || used[i]) return false; } return true; };
        if (fits(address - 1)) return { available: true, address: address - 1 };
        for (let s = 0; s + footprint <= 512; s++) if (fits(s)) return { available: false, address: s };
        return { available: false };
      };
      if (qlc.isUnsupported('fixtures.findAvailableAddress')) { setFree(local()); return undefined; }
      qlc.call('fixtures.findAvailableAddress', { universe, channels, quantity, gap, requestedAddress: address - 1 })
        .then(r => { if (alive) setFree(r); }).catch(() => { if (alive) setFree(local()); });
      return () => { alive = false; };
    }, [open, universe, address, quantity, gap, channels]);

    const patch = () => {
      if (!definition || busy) return;
      setBusy(true); setError('');
      FF.mutate(qlc, 'fixtures.patch', { universe, address: address - 1, definition, name: name || undefined, quantity, gap })
        .then(() => { setBusy(false); onClose(); })
        .catch(e => { setBusy(false); setError((e && e.message) || 'Patch failed'); });
    };
    const manuModel = manufacturers === false ? [] : (manufacturers || []).filter(m => !search || m.toLowerCase().indexOf(search.toLowerCase()) !== -1);
    const modelList = models.filter(m => !search || search.length < 2 || m.model.toLowerCase().indexOf(search.toLowerCase()) !== -1);
    const universeModel = universes.map(u => ({ mLabel: u.name, mValue: u.id }));
    const canPatch = !!definition && channels > 0 && free && free.available !== false && !busy;

    return (
      <CustomPopupDialog open={open} title="Add Fixtures" width={720} standardButtons={['Cancel', 'Add']}
        onClicked={(b) => { if (b === 'Add') patch(); else onClose(); }} onClose={onClose}>
        <div style={{ display: 'grid', gridTemplateColumns: '220px 220px 1fr', gap: 10, minHeight: 320 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
            <input value={search} onChange={e => setSearch(e.target.value)} placeholder="Search manufacturers / models…" style={Object.assign({ width: '100%' }, inputStyle)} />
            <RobotoText label="Manufacturer" fontBold fontSize={13} height={20} />
            <div style={{ height: 260, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
              <div onClick={() => setManufacturer(GENERIC)} style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 6px', cursor: 'pointer', background: isGeneric ? 'var(--highlight)' : 'transparent' }}>
                <img src={D.icon('dimmer')} alt="" style={{ width: 16, height: 16 }} /><RobotoText label="Generic dimmer" fontSize={13} height={26} />
              </div>
              {manufacturers === null ? <div style={{ padding: 6 }}><RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
              {manufacturers === false ? <div style={{ padding: 6 }}><FF.Note text="Browsing fixture definitions is not available: this server has no fixtures.defs.listManufacturers yet. Only a generic dimmer can be patched." /></div> : null}
              {manuModel.map(m => (
                <div key={m} onClick={() => setManufacturer(m)} style={{ height: 24, padding: '0 6px', cursor: 'pointer', background: manufacturer === m ? 'var(--highlight)' : 'transparent' }}>
                  <RobotoText label={m} fontSize={13} height={24} />
                </div>
              ))}
            </div>
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
            <div style={{ height: 24 }} />
            <RobotoText label={isGeneric ? 'Channels' : 'Model'} fontBold fontSize={13} height={20} />
            {isGeneric ? (
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                <CustomSpinBox value={genericChannels} from={1} to={512} width={100} onValueModified={setGenericChannels} />
                <FF.Note text="A generic dimmer with this many intensity channels (Fixture::genericDimmerDef)." />
              </div>
            ) : (
              <div style={{ height: 260, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
                {!manufacturer ? <div style={{ padding: 6 }}><RobotoText label="Pick a manufacturer" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
                {modelList.map(m => (
                  <div key={m.model} onClick={() => setModel(m.model)} style={{ display: 'flex', alignItems: 'center', height: 24, padding: '0 6px', cursor: 'pointer', background: model === m.model ? 'var(--highlight)' : 'transparent' }}>
                    <RobotoText label={m.model} fontSize={13} height={24} style={{ flex: 1 }} />
                    {m.isUser ? <RobotoText label="user" fontSize={11} height={24} labelColor="var(--fg-light)" /> : null}
                  </div>
                ))}
              </div>
            )}
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
            <RobotoText label="Properties" fontBold fontSize={13} height={20} />
            {!isGeneric ? <FF.Row label="Type" width={70}>{modelInfo ? (modelInfo.fixtureType || modelInfo.type || '—') : '—'}</FF.Row> : null}
            {!isGeneric ? <FF.Row label="Mode" width={70}>
              <CustomComboBox width={200} currValue={mode} model={((modelInfo && modelInfo.modes) || []).map(m => ({ mLabel: m.name + ' (' + m.channelCount + ' ch)', mValue: m.name }))} onValueChanged={setMode} disabled={!modelInfo} />
            </FF.Row> : null}
            <FF.Row label="Name" width={70}><input value={name} onChange={e => setName(e.target.value)} placeholder={isGeneric ? 'Generic Dimmer' : model || 'Fixture name'} style={Object.assign({ width: 200 }, inputStyle)} /></FF.Row>
            <FF.Row label="Universe" width={70}><CustomComboBox width={200} currValue={universe} model={universeModel} onValueChanged={setUniverse} /></FF.Row>
            <FF.Row label="Address" width={70}><CustomSpinBox value={address} from={1} to={512} width={100} onValueModified={setAddress} /></FF.Row>
            <FF.Row label="Quantity" width={70}><CustomSpinBox value={quantity} from={1} to={512} width={100} onValueModified={setQuantity} /></FF.Row>
            <FF.Row label="Gap" width={70}><CustomSpinBox value={gap} from={0} to={511} width={100} onValueModified={setGap} /></FF.Row>
            <FF.Row label="Channels" width={70}>{channels ? channels + (quantity > 1 ? ' × ' + quantity + ' = ' + footprint + ' (' + (address) + '–' + (address + footprint - 1) + ')' : ' (' + address + '–' + (address + channels - 1) + ')') : '—'}</FF.Row>
            {free && channels ? (free.available
              ? <RobotoText label="Address range is free" fontSize={13} labelColor="var(--check-lime)" height={22} />
              : free.address != null
                ? <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}><RobotoText label={'Address ' + address + ' is in use. Next free: ' + (free.address + 1)} fontSize={13} labelColor="var(--override-red)" height={22} /><GenericButton label="Use" width={44} height={22} onClick={() => setAddress(free.address + 1)} /></div>
                : <RobotoText label="No free range of that size in this universe" fontSize={13} labelColor="var(--override-red)" height={22} />) : null}
            {error ? <RobotoText label={'Cannot patch fixture: ' + error} fontSize={13} labelColor="var(--override-red)" wrapText height="auto" /> : null}
            {!canPatch && definition && free && free.available === false ? <FF.Note text="Add is blocked while the range overlaps another fixture." /> : null}
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- Fixture groups ------------------------------------------------------------------------ */
  function FixtureGroupsPanel({ qlc, fixtureIds, fixtures, onSelectFixtures }) {
    const D = window.QLCData;
    const [groups, setGroups] = React.useState(null);
    const [current, setCurrent] = React.useState(null);
    const [detail, setDetail] = React.useState(null);
    const [creating, setCreating] = React.useState(false);
    const [newName, setNewName] = React.useState('New group');
    const [confirm, setConfirm] = React.useState(false);
    const loadList = () => qlc.call('fixtures.group.list').then(r => setGroups((r && r.groups) || [])).catch(() => {});
    const loadDetail = (id) => qlc.call('fixtures.group.get', { groupId: String(id) }).then(setDetail).catch(() => setDetail(null));
    React.useEffect(() => {
      if (!qlc.online) { setGroups(null); return undefined; }
      loadList();
      const offs = ['fixtures.group.created', 'fixtures.group.deleted', 'fixtures.group.renamed', 'fixtures.group.updated', 'core.project.loaded', 'core.history.changed'].map(t => qlc.subscribeTo(t, () => { loadList(); if (current != null) loadDetail(current); }));
      return () => offs.forEach(f => f());
    }, [qlc.online, current]);
    React.useEffect(() => { setDetail(null); if (current != null && qlc.online) loadDetail(current); }, [current, qlc.online]);
    const memberIds = detail ? Array.from(new Set((detail.heads || []).map(h => String(h.fixtureId)))) : [];
    const addable = fixtureIds.map(String).filter(id => memberIds.indexOf(id) === -1);
    const create = () => {
      setCreating(false);
      FF.mutate(qlc, 'fixtures.group.create', { name: newName || 'New group' }).then(r => { if (r && r.groupId != null) setCurrent(String(r.groupId)); loadList(); }).catch(() => {});
    };
    const rename = (name) => { if (current == null || !name) return; FF.mutate(qlc, 'fixtures.group.rename', { groupId: String(current), name }).catch(() => {}); };
    const remove = () => { setConfirm(false); if (current == null) return; FF.mutate(qlc, 'fixtures.group.delete', { groupId: String(current) }).then(() => setCurrent(null)).catch(() => {}); };
    const addSelected = () => FF.mutateSeq(qlc, addable.map(id => ['fixtures.group.assignFixture', { groupId: String(current), fixtureId: id }])).then(() => loadDetail(current)).catch(() => loadDetail(current));
    const unassign = (id) => FF.mutate(qlc, 'fixtures.group.unassignFixture', { groupId: String(current), fixtureId: String(id) }).then(() => loadDetail(current)).catch(() => {});
    const g = (groups || []).find(x => String(x.id) === String(current));
    return (
      <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 6px', background: 'var(--bg-strong)' }}>
          <RobotoText label="Fixture Groups" fontBold fontSize={14} style={{ flex: 1 }} />
          <IconButton faSource="fa_plus" size={24} tooltip="Add a new fixture group" onClick={() => { setNewName('New group'); setCreating(true); }} />
          <IconButton faSource="fa_trash_can" size={24} tooltip="Delete the selected group" disabled={current == null} onClick={() => setConfirm(true)} />
        </div>
        <div style={{ maxHeight: 220, overflow: 'auto', borderBottom: 'var(--border-dark)' }}>
          {groups === null ? <div style={{ padding: 6 }}><RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
          {groups && !groups.length ? <div style={{ padding: 6 }}><RobotoText label="No fixture groups" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
          {(groups || []).map(gr => (
            <div key={gr.id} onClick={() => setCurrent(String(gr.id))} style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 6px', cursor: 'pointer', background: String(gr.id) === String(current) ? 'var(--highlight)' : 'transparent' }}>
              <img src={D.icon('group')} alt="" style={{ width: 18, height: 18 }} />
              <RobotoText label={gr.name} fontSize={13} height={26} style={{ flex: 1 }} />
              <RobotoText label={gr.headCount + ' heads'} fontSize={12} height={26} labelColor="var(--fg-light)" />
            </div>
          ))}
        </div>
        {g ? (
          <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 6, flex: 1, minHeight: 0 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
              <CustomTextInput key={g.id} text={g.name} allowDoubleClick width={140} onTextConfirmed={rename} />
              <RobotoText label={g.size.columns + '×' + g.size.rows} fontSize={12} labelColor="var(--fg-light)" />
            </div>
            <div style={{ display: 'flex', gap: 4 }}>
              <GenericButton label={'Add ' + addable.length + ' selected'} width={120} height={24} disabled={!addable.length} onClick={addSelected} />
              <IconButton faSource={FF.GLYPH.crosshairs} size={24} tooltip="Select the group's fixtures in the tree" disabled={!memberIds.length} onClick={() => onSelectFixtures(memberIds)} />
            </div>
            <div style={{ flex: 1, minHeight: 0, overflow: 'auto' }}>
              {!detail ? <RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /> : memberIds.map(id => {
                const f = fixtures.find(x => String(x.id) === id);
                return (
                  <div key={id} style={{ display: 'flex', alignItems: 'center', gap: 4, height: 24 }}>
                    <img src={D.icon(f ? (Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture') : 'other')} alt="" style={{ width: 16, height: 16 }} />
                    <RobotoText label={f ? f.name : 'Fixture ' + id} fontSize={13} height={24} style={{ flex: 1 }} />
                    <IconButton faSource="fa_xmark" size={20} tooltip="Remove from group" onClick={() => unassign(id)} />
                  </div>
                );
              })}
              {detail && !memberIds.length ? <RobotoText label="No fixtures in this group" fontSize={13} labelColor="var(--fg-medium)" /> : null}
            </div>
            <FF.Note text="Head layout editing (the grid) is not available in the web UI; fixtures are appended to the grid on assignment." />
          </div>
        ) : <div style={{ padding: 6 }}><FF.Note text="Pick a group to see and edit its members. Fixtures selected in the tree can be added to it." /></div>}
        <CustomPopupDialog open={creating} title="New fixture group" width={340} standardButtons={['Cancel', 'Create']} onClicked={(b) => { if (b === 'Create') create(); else setCreating(false); }} onClose={() => setCreating(false)}>
          <input autoFocus value={newName} onChange={e => setNewName(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') create(); }} style={Object.assign({ width: '100%' }, inputStyle)} />
        </CustomPopupDialog>
        <CustomPopupDialog open={confirm} title="Delete fixture group" width={360} message={g ? 'Are you sure you want to delete the group "' + g.name + '"? The fixtures themselves stay patched.' : ''}
          standardButtons={['Cancel', 'Delete']} onClicked={(b) => { if (b === 'Delete') remove(); else setConfirm(false); }} onClose={() => setConfirm(false)} />
      </div>
    );
  }

  /* ---- Palettes ------------------------------------------------------------------------------ */
  const PALETTE_TYPES = ['Dimmer', 'Color', 'Pan', 'Tilt', 'PanTilt'];
  function PaletteValueEditor({ type, values, onChange }) {
    const v = values || [];
    if (type === 'Dimmer') return <FF.Row label="Level" width={60}><CustomSpinBox value={Math.round(Number(v[0]) || 0)} from={0} to={100} suffix="%" width={90} onValueModified={x => onChange([x])} /></FF.Row>;
    if (type === 'Color') {
      const c = FF.parsePaletteColour(v[0] || '#ffffff') || { rgb: { r: 255, g: 255, b: 255 } };
      return <FF.Row label="Colour" width={60}>
        <input type="color" value={FF.hex(c.rgb)} onChange={e => onChange([e.target.value])} style={{ width: 40, height: 26, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)' }} />
        <input value={v[0] || ''} onChange={e => onChange([e.target.value])} spellCheck={false} style={Object.assign({ width: 120, fontFamily: 'var(--font-mono)' }, inputStyle)} placeholder="#rrggbb[wwaauv]" />
      </FF.Row>;
    }
    if (type === 'Pan' || type === 'Tilt') return <FF.Row label={type} width={60}><CustomSpinBox value={Math.round(Number(v[0]) || 0)} from={0} to={255} width={90} onValueModified={x => onChange([x])} /></FF.Row>;
    if (type === 'PanTilt') return <>
      <FF.Row label="Pan" width={60}><CustomSpinBox value={Math.round(Number(v[0]) || 0)} from={0} to={255} width={90} onValueModified={x => onChange([x, Number(v[1]) || 0])} /></FF.Row>
      <FF.Row label="Tilt" width={60}><CustomSpinBox value={Math.round(Number(v[1]) || 0)} from={0} to={255} width={90} onValueModified={x => onChange([Number(v[0]) || 0, x])} /></FF.Row>
    </>;
    return <FF.Note text={'Editing ' + type + ' values is not available (positional values without documented meaning).'} />;
  }

  function PalettePanel({ qlc, palettes, fixtureIds, fixtures }) {
    const D = window.QLCData;
    const items = FF.useToolFixtures(qlc, fixtureIds);
    const [current, setCurrent] = React.useState(null);
    const [detail, setDetail] = React.useState(null);
    const [draft, setDraft] = React.useState(null);   /* {type, name, values} for create */
    const [confirm, setConfirm] = React.useState(false);
    const [applied, setApplied] = React.useState('');
    React.useEffect(() => {
      setDetail(null);
      if (current == null || !qlc.online) return undefined;
      let alive = true;
      qlc.call('palette.get', { paletteId: Number(current) }).then(d => { if (alive) setDetail(d); }).catch(() => {});
      return () => { alive = false; };
    }, [current, qlc.online, JSON.stringify(palettes || [])]);
    const apply = (p) => {
      if (!items.length) { setApplied('Select fixtures in the tree first'); return; }
      let n = 0, unsupported = false;
      items.forEach(it => { const w = FF.paletteValues(p, it.channels); if (w === null) { unsupported = true; return; } if (w.length) { n++; FF.writeLive(qlc, it.detail, w, 'pal:' + it.detail.id); } });
      setApplied(unsupported ? 'Applying ' + p.type + ' palettes is not available (no channel mapping)' : 'Applied "' + p.name + '" to ' + n + ' fixture' + (n === 1 ? '' : 's') + ' (live output)');
    };
    const applyCurrent = () => { if (detail) apply(detail); };
    const update = (p) => { setDetail(d => Object.assign({}, d, p)); FF.mutate(qlc, 'palette.update', Object.assign({ paletteId: Number(current) }, p), { key: 'pal:' + current + ':' + Object.keys(p).join(',') }).catch(() => {}); };
    const create = () => { const d = draft; setDraft(null); FF.mutate(qlc, 'palette.create', { type: d.type, name: d.name || d.type + ' palette', values: d.values }).then(r => { if (r && r.paletteId != null) setCurrent(String(r.paletteId)); }).catch(() => {}); };
    const remove = () => { setConfirm(false); if (current == null) return; FF.mutate(qlc, 'palette.delete', { paletteId: Number(current) }).then(() => setCurrent(null)).catch(() => {}); };
    const defaultValues = (t) => t === 'Dimmer' ? [100] : t === 'Color' ? ['#ffffff'] : t === 'PanTilt' ? [127, 127] : [127];
    return (
      <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 6px', background: 'var(--bg-strong)' }}>
          <RobotoText label={'Palettes' + (palettes ? ' (' + palettes.length + ')' : '')} fontBold fontSize={14} style={{ flex: 1 }} />
          <IconButton faSource="fa_plus" size={24} tooltip="Create a palette" onClick={() => setDraft({ type: 'Color', name: '', values: ['#ffffff'] })} />
          <IconButton faSource="fa_trash_can" size={24} tooltip="Delete the selected palette" disabled={current == null} onClick={() => setConfirm(true)} />
        </div>
        <div style={{ maxHeight: 240, overflow: 'auto', borderBottom: 'var(--border-dark)' }}>
          {!palettes ? <div style={{ padding: 6 }}><RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
          {palettes && !palettes.length ? <div style={{ padding: 6 }}><RobotoText label="No palettes in this workspace" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
          {(palettes || []).map(p => (
            <div key={p.id} onClick={() => setCurrent(String(p.id))} onDoubleClick={() => qlc.call('palette.get', { paletteId: Number(p.id) }).then(apply).catch(() => {})}
              title="Click to edit, double-click to apply to the selected fixtures"
              style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 6px', cursor: 'pointer', background: String(p.id) === String(current) ? 'var(--highlight)' : 'transparent' }}>
              <img src={D.icon(FF.PALETTE_ICON[p.type] || 'palette')} alt="" style={{ width: 18, height: 18 }} />
              <RobotoText label={p.name} fontSize={13} height={26} style={{ flex: 1 }} />
              <RobotoText label={p.type} fontSize={12} height={26} labelColor="var(--fg-light)" />
            </div>
          ))}
        </div>
        <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 6 }}>
          {detail ? <>
            <CustomTextInput key={detail.id} text={detail.name} allowDoubleClick width={170} onTextConfirmed={n => n && n !== detail.name && update({ name: n })} />
            <PaletteValueEditor type={detail.type} values={detail.values} onChange={vals => update({ values: vals })} />
            <GenericButton label={'Apply to ' + fixtureIds.length + ' selected'} width={170} height={24} disabled={!fixtureIds.length} onClick={applyCurrent} />
          </> : <FF.Note text="Click a palette to edit it, double-click to apply it to the selected fixtures (live output). Applying is done by the browser: Color sets RGB/CMY/WAUV channels, Dimmer the intensity, Pan/Tilt the position." />}
          {applied ? <RobotoText label={applied} fontSize={12} labelColor="var(--fg-light)" wrapText height="auto" /> : null}
          <FF.Note text="The API has no palette-apply / live fixture-control method; Shutter, Gobo, Zoom and Position3D palettes cannot be applied." />
        </div>
        <CustomPopupDialog open={!!draft} title="New palette" width={380} standardButtons={['Cancel', 'Create']} onClicked={(b) => { if (b === 'Create') create(); else setDraft(null); }} onClose={() => setDraft(null)}>
          {draft ? <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <FF.Row label="Type" width={60}><CustomComboBox width={160} currValue={draft.type} model={PALETTE_TYPES.map(t => ({ mLabel: t, mValue: t }))} onValueChanged={t => setDraft({ type: t, name: draft.name, values: defaultValues(t) })} /></FF.Row>
            <FF.Row label="Name" width={60}><input value={draft.name} onChange={e => setDraft(Object.assign({}, draft, { name: e.target.value }))} placeholder="Palette name" style={Object.assign({ width: 200 }, inputStyle)} /></FF.Row>
            <PaletteValueEditor type={draft.type} values={draft.values} onChange={vals => setDraft(Object.assign({}, draft, { values: vals }))} />
          </div> : null}
        </CustomPopupDialog>
        <CustomPopupDialog open={confirm} title="Delete palette" width={340} message={detail ? 'Are you sure you want to delete the palette "' + detail.name + '"?' : ''}
          standardButtons={['Cancel', 'Delete']} onClicked={(b) => { if (b === 'Delete') remove(); else setConfirm(false); }} onClose={() => setConfirm(false)} />
      </div>
    );
  }

  Object.assign(FF, { AddFixtureDialog, FixtureGroupsPanel, PalettePanel });
})();
