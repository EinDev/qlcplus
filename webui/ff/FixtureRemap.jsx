/**
 * FixtureRemap.jsx — the Fixture Remap tool (qmlui FixtureRemap.qml / RemapRowDelegate.qml) as
 * a dialog: pick source fixtures of the project, give each a target definition / mode / universe /
 * address / name (or clone it 1:1), let the server suggest the channel map
 * (fixtures.remap.suggestChannelMap), adjust single channel connections, optionally mark
 * unmapped sources for deletion, and apply everything in one fixtures.remap.apply call — Scenes,
 * Sequences, EFX, fixture groups, channel groups, monitor placement and Virtual Console widgets
 * follow the new fixtures.
 *
 * window.FF.FixtureRemapDialog; opened from the Remap toolbar button in FixturesFunctions.jsx.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };
  const GENERIC = '__generic__';
  let nextKey = 1;

  /** First free block of `channels` in `universe` given the occupancy list [{universe, address, channels}]. */
  function firstFree(occupied, universe, channels) {
    const used = new Array(512).fill(false);
    occupied.filter(o => o.universe === universe).forEach(o => { for (let i = 0; i < o.channels; i++) if (o.address + i < 512) used[o.address + i] = true; });
    for (let a = 0; a + channels <= 512; a++) {
      let ok = true;
      for (let i = 0; i < channels; i++) if (used[a + i]) { ok = false; break; }
      if (ok) return a;
    }
    return -1;
  }

  /** One target row: definition pickers + patch + channel map. */
  function TargetRow({ qlc, row, source, universes, manufacturers, onChange, onRemove, onAuto, sourceDetail }) {
    const [models, setModels] = React.useState([]);
    const [modes, setModes] = React.useState([]);
    const [showMap, setShowMap] = React.useState(false);
    React.useEffect(() => {
      if (!row.manufacturer || row.manufacturer === GENERIC) { setModels([]); return undefined; }
      let alive = true;
      qlc.call('fixtures.defs.listModels', { manufacturer: row.manufacturer }).then(r => { if (alive) setModels(((r && r.models) || []).map(m => typeof m === 'string' ? m : m.model)); }).catch(() => {});
      return () => { alive = false; };
    }, [row.manufacturer]);
    React.useEffect(() => {
      if (!row.manufacturer || row.manufacturer === GENERIC || !row.model) { setModes([]); return undefined; }
      let alive = true;
      qlc.call('fixtures.defs.getModel', { manufacturer: row.manufacturer, model: row.model }).then(r => {
        if (!alive) return;
        const list = (r && r.modes) || [];
        setModes(list);
        if (list.length && !list.some(m => m.name === row.mode)) onChange({ mode: list[0].name, channels: list[0].channelCount, channelMap: [] });
        else if (list.length) { const m = list.find(x => x.name === row.mode); if (m && m.channelCount !== row.channels) onChange({ channels: m.channelCount }); }
      }).catch(() => {});
      return () => { alive = false; };
    }, [row.manufacturer, row.model]);
    const isGeneric = row.manufacturer === GENERIC;
    const mapped = row.channelMap.length;
    const srcChannels = source ? source.channels : 0;
    const mfModel = [{ mLabel: 'Generic dimmer', mValue: GENERIC }].concat((manufacturers || []).map(m => ({ mLabel: m, mValue: m })));
    return (
      <div data-remap-row={row.key} data-source-id={row.sourceId} style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 6, background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexWrap: 'wrap' }}>
          <RobotoText label={(source ? source.name : '?') + ' →'} fontBold fontSize={13} style={{ minWidth: 120 }} />
          <CustomComboBox width={150} currValue={row.manufacturer || GENERIC} model={mfModel} onValueChanged={v => onChange({ manufacturer: v, model: '', mode: '', channels: v === GENERIC ? (row.channels || srcChannels || 1) : 0, channelMap: [] })} />
          {isGeneric ? (
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}><RobotoText label="Channels" fontSize={12} labelColor="var(--fg-light)" />
              <CustomSpinBox value={row.channels || 1} from={1} to={512} width={64} height={24} onValueModified={v => onChange({ channels: v, channelMap: [] })} /></span>
          ) : <>
            <CustomComboBox width={170} currValue={row.model} model={models.map(m => ({ mLabel: m, mValue: m }))} onValueChanged={v => onChange({ model: v, mode: '', channelMap: [] })} />
            <CustomComboBox width={150} currValue={row.mode} model={modes.map(m => ({ mLabel: m.name + ' (' + m.channelCount + ')', mValue: m.name }))} onValueChanged={v => { const m = modes.find(x => x.name === v); onChange({ mode: v, channels: m ? m.channelCount : 0, channelMap: [] }); }} />
          </>}
          <IconButton faSource="fa_xmark" size={24} tooltip="Remove this target (the source fixture stays as it is)" onClick={onRemove} />
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexWrap: 'wrap' }}>
          <RobotoText label="Universe" fontSize={12} labelColor="var(--fg-light)" />
          <CustomComboBox width={120} currValue={row.universe} model={(universes || []).map(u => ({ mLabel: u.name, mValue: u.id }))} onValueChanged={v => onChange({ universe: v })} />
          <RobotoText label="Address" fontSize={12} labelColor="var(--fg-light)" />
          <CustomSpinBox value={row.address + 1} from={1} to={512} width={70} height={24} onValueModified={v => onChange({ address: v - 1 })} />
          <RobotoText label="Name" fontSize={12} labelColor="var(--fg-light)" />
          <input value={row.name} onChange={e => onChange({ name: e.target.value })} data-remap-name="1" style={Object.assign({ width: 160 }, inputStyle)} />
          <GenericButton label="Auto-connect" width={100} height={24} disabled={!(isGeneric || (row.model && row.mode))} onClick={onAuto} />
          <span title="Show / edit the channel connections" onClick={() => setShowMap(!showMap)} style={{ cursor: 'pointer' }} data-remap-map-toggle="1">
            <RobotoText label={mapped + ' / ' + srcChannels + ' channels connected' + (row.error ? ' · ' + row.error : '')} fontSize={12} labelColor={mapped ? 'var(--check-lime)' : 'var(--override-red)'} />
          </span>
        </div>
        {showMap ? (
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(230px, 1fr))', gap: 3, padding: 4, background: 'var(--bg-medium)' }} data-remap-map="1">
            {Array.from({ length: srcChannels }, (_, s) => {
              const entry = row.channelMap.find(e => e.sourceChannel === s);
              const srcCh = sourceDetail && sourceDetail.channelList ? sourceDetail.channelList[s] : null;
              return (
                <div key={s} style={{ display: 'flex', alignItems: 'center', gap: 4, height: 22 }}>
                  <RobotoText label={(s + 1) + ' ' + (srcCh ? srcCh.name : '')} fontSize={12} style={{ width: 120, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} />
                  <select value={entry ? entry.targetChannel : -1} data-src-channel={s} onChange={e => {
                    const t = Number(e.target.value);
                    const rest = row.channelMap.filter(x => x.sourceChannel !== s);
                    onChange({ channelMap: t < 0 ? rest : rest.concat([{ sourceChannel: s, targetChannel: t }]).sort((a, b) => a.sourceChannel - b.sourceChannel) });
                  }} style={Object.assign({ width: 90 }, inputStyle)}>
                    <option value={-1}>—</option>
                    {Array.from({ length: row.channels || 0 }, (_, t) => <option key={t} value={t}>{'→ ' + (t + 1)}</option>)}
                  </select>
                </div>
              );
            })}
          </div>
        ) : null}
      </div>
    );
  }

  function FixtureRemapDialog({ open, qlc, universes, fixtures, selectedFixtureIds, onClose }) {
    const D = window.QLCData, Icons = window.QLCIcons;
    const [rows, setRows] = React.useState([]);
    const [deleteIds, setDeleteIds] = React.useState([]);
    const [manufacturers, setManufacturers] = React.useState([]);
    const [busy, setBusy] = React.useState(false);
    const [error, setError] = React.useState('');
    const [filter, setFilter] = React.useState('');
    const sourceIds = React.useMemo(() => (fixtures || []).map(f => String(f.id)), [fixtures]);
    const details = FF.useFixtureDetails(qlc, open ? rows.map(r => r.sourceId) : []);
    React.useEffect(() => {
      if (!open) return undefined;
      setRows([]); setDeleteIds([]); setError('');
      let alive = true;
      qlc.call('fixtures.defs.listManufacturers').then(r => { if (alive) setManufacturers((r && r.manufacturers) || []); }).catch(() => {});
      return () => { alive = false; };
    }, [open]);

    const byId = (id) => (fixtures || []).find(f => String(f.id) === String(id));
    const occupancy = (exceptKey) => {
      const consumed = new Set(rows.map(r => r.sourceId).concat(deleteIds));
      const list = (fixtures || []).filter(f => !consumed.has(String(f.id))).map(f => ({ universe: f.universe, address: f.address, channels: f.channels }));
      rows.filter(r => r.key !== exceptKey).forEach(r => list.push({ universe: r.universe, address: r.address, channels: r.channels || 0 }));
      return list;
    };
    const updateRow = (key, fields) => setRows(rs => rs.map(r => r.key === key ? Object.assign({}, r, fields, { error: '' }) : r));
    const autoConnect = (row) => {
      const target = row.manufacturer === GENERIC ? { generic: { channels: row.channels || 1 } } : { manufacturer: row.manufacturer, model: row.model, mode: row.mode };
      return qlc.call('fixtures.remap.suggestChannelMap', { sourceFixtureId: row.sourceId, target }).then(r => updateRow(row.key, { channelMap: (r && r.channelMap) || [] })).catch(e => updateRow(row.key, { error: e && e.message }));
    };
    /** "Clone": a target row with the same definition, at the first free address of its universe. */
    const addTarget = (id, clone) => {
      const f = byId(id);
      if (!f || rows.some(r => r.sourceId === String(id))) return;
      const key = nextKey++;
      const generic = f.isGeneric || !f.manufacturer;
      const row = { key, sourceId: String(id), manufacturer: generic ? GENERIC : f.manufacturer, model: generic ? '' : f.model, mode: generic ? '' : f.mode,
        channels: f.channels, universe: f.universe, address: 0, name: f.name, channelMap: clone ? Array.from({ length: f.channels }, (_, i) => ({ sourceChannel: i, targetChannel: i })) : [], error: '' };
      const occ = occupancy(key).filter(o => !(o.universe === f.universe && o.address === f.address && o.channels === f.channels));
      const free = firstFree(occ, f.universe, f.channels);
      row.address = free >= 0 ? free : f.address;
      setDeleteIds(d => d.filter(x => x !== String(id)));
      setRows(rs => rs.concat([row]));
    };
    const apply = () => {
      const mappings = rows.map(r => ({
        sourceFixtureId: r.sourceId, universe: r.universe, address: r.address, name: r.name,
        definition: r.manufacturer === GENERIC ? { generic: { channels: r.channels || 1 } } : { manufacturer: r.manufacturer, model: r.model, mode: r.mode },
        channelMap: r.channelMap
      }));
      if (!mappings.length && !deleteIds.length) { setError('Add at least one target (or mark sources for deletion) first.'); return; }
      if (!mappings.length) { setError('Deleting sources needs at least one mapping in the same remap; unpatch them from the tree instead.'); return; }
      setBusy(true); setError('');
      FF.mutate(qlc, 'fixtures.remap.apply', { mappings, unmappedSourceFixtureIds: deleteIds }).then(() => { setBusy(false); onClose(); })
        .catch(e => { setBusy(false); setError((e && e.message) || 'Remap failed'); });
    };
    const shownSources = (fixtures || []).filter(f => !filter || f.name.toLowerCase().indexOf(filter.toLowerCase()) !== -1).slice().sort((a, b) => a.universe - b.universe || a.address - b.address);
    return (
      <CustomPopupDialog open={open} title="Fixture Remap" width={980} standardButtons={['Cancel', busy ? 'Applying…' : 'Apply remap']}
        onClicked={(b) => { if (b === 'Cancel') onClose(); else if (!busy) apply(); }} onClose={onClose}>
        <div data-ff-remap-dialog="1" style={{ display: 'flex', gap: 10, height: 520 }}>
          <div style={{ width: 300, flex: 'none', display: 'flex', flexDirection: 'column', gap: 4, minHeight: 0 }}>
            <RobotoText label="Source — current project fixtures" fontBold fontSize={13} />
            <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="Filter…" style={Object.assign({ width: '100%' }, inputStyle)} />
            <div style={{ display: 'flex', gap: 4 }}>
              <GenericButton label={'Clone selected (' + (selectedFixtureIds || []).length + ')'} width={150} height={24} disabled={!(selectedFixtureIds || []).length} onClick={() => (selectedFixtureIds || []).forEach(id => addTarget(id, true))} />
            </div>
            <div style={{ flex: 1, minHeight: 0, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
              {shownSources.map(f => {
                const hasRow = rows.some(r => r.sourceId === String(f.id));
                const del = deleteIds.indexOf(String(f.id)) !== -1;
                return (
                  <div key={f.id} data-remap-source={f.id} style={{ display: 'flex', alignItems: 'center', gap: 4, height: 26, padding: '0 4px', opacity: del ? 0.5 : 1 }}>
                    <img src={D.icon(Icons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture')} alt="" style={{ width: 16, height: 16 }} />
                    <RobotoText label={f.name} fontSize={12} height={26} style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} />
                    <RobotoText label={'U' + (f.universe + 1) + '.' + (f.address + 1)} fontSize={11} labelColor="var(--fg-medium)" height={26} />
                    {hasRow ? <RobotoText label="mapped" fontSize={11} labelColor="var(--check-lime)" height={26} /> : <>
                      <IconButton faSource={FF.GLYPH.clone} size={22} tooltip="Clone: a 1:1 target with the same definition at the next free address" onClick={() => addTarget(f.id, true)} data-action="clone" />
                      <IconButton faSource="fa_plus" size={22} tooltip="New target for this fixture (pick a definition on the right)" onClick={() => addTarget(f.id, false)} data-action="target" />
                      <span title="Delete this fixture in the same remap (no replacement)"><CustomCheckBox checked={del} size={14} onToggled={v => setDeleteIds(d => v ? d.concat([String(f.id)]) : d.filter(x => x !== String(f.id)))} /></span>
                    </>}
                  </div>
                );
              })}
            </div>
            <FF.Note text="Clone keeps definition and channel order; a new target lets you pick another fixture definition or mode. The checkbox deletes an unmapped source with the remap." />
          </div>
          <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', gap: 4, minHeight: 0 }}>
            <RobotoText label={'Target — ' + rows.length + ' fixture' + (rows.length === 1 ? '' : 's') + ' to create' + (deleteIds.length ? ', ' + deleteIds.length + ' to delete' : '')} fontBold fontSize={13} />
            <div style={{ flex: 1, minHeight: 0, overflow: 'auto', display: 'flex', flexDirection: 'column', gap: 6 }}>
              {!rows.length ? <FF.Note text="Add targets from the source list on the left. Every function, group and Virtual Console widget referencing a source fixture follows its connected channels." /> : null}
              {rows.map(r => <TargetRow key={r.key} qlc={qlc} row={r} source={byId(r.sourceId)} sourceDetail={details[r.sourceId]} universes={universes} manufacturers={manufacturers}
                onChange={fields => updateRow(r.key, fields)} onRemove={() => setRows(rs => rs.filter(x => x.key !== r.key))} onAuto={() => autoConnect(r)} />)}
            </div>
            {error ? <RobotoText label={error} fontSize={13} labelColor="var(--override-red)" /> : null}
            <FF.Note text="Apply is one atomic change: the source fixtures are replaced, Scenes / Sequences / EFX / groups / channel groups / 2D positions / VC widgets are rewritten, unconnected channels lose their values." />
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  FF.FixtureRemapDialog = FixtureRemapDialog;
})();
