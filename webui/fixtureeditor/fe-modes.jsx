/**
 * fe-modes.jsx — Modes and Physical tabs of the Fixture Editor, mirroring
 * qmlui/qml/fixtureeditor/EditorView.qml (mode list), ModeEditor.qml (name, ordered channel list with
 * drag-reordering and "acts on", emitters / heads, global-or-override physical) and
 * PhysicalProperties.qml (bulb, lens, head(s) incl. layout, dimensions, electrical).
 *
 * The mode's slot list is always written as a whole (fixturedefs.mode.setChannels): one reorder /
 * insert / removal is one revision-gated call, see fixturedefs-notes.md.
 *
 * window.FE.ModesTab, FE.PhysicalTab, FE.PhysicalForm
 */
(function () {
  'use strict';
  const FE = window.FE;
  const { RobotoText, IconButton, GenericButton, CustomComboBox, CustomCheckBox, IconTextEntry, SectionBox } = window.PatchDesignSystem_5432c9;

  /* ---- PhysicalProperties.qml ---------------------------------------------------------------- */
  function Combo({ id, value, options, onCommit, disabled, ...rest }) {
    return <>
      <FE.Text value={value} list={id} disabled={disabled} onCommit={onCommit} width={200} {...rest} />
      <datalist id={id}>{options.map(o => <option key={o} value={o} />)}</datalist>
    </>;
  }
  function Group({ title, children }) {
    return (
      <div style={{ border: '1px solid var(--bg-light)', borderRadius: 4, padding: '4px 8px 8px', display: 'flex', flexDirection: 'column', gap: 2, minWidth: 330 }}>
        <RobotoText label={title} fontSize={14} fontBold height={24} />
        {children}
      </div>
    );
  }
  function PhysicalForm({ phy, disabled, onChange, idPrefix }) {
    const p = phy || {};
    const set = (field) => (v) => onChange({ [field]: v }, field);
    const pre = idPrefix || 'phy';
    return (
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(340px, 1fr))', gap: 10, opacity: disabled ? .55 : 1 }} data-fe={pre + '-form'}>
        <Group title="Bulb">
          <FE.Row label="Type"><Combo id={pre + '-bulb'} value={p.bulbType} options={FE.BULB_TYPES} disabled={disabled} onCommit={set('bulbType')} data-fe="phy-bulbType" /></FE.Row>
          <FE.Row label="Lumens"><FE.Num value={p.bulbLumens} disabled={disabled} onCommit={set('bulbLumens')} data-fe="phy-bulbLumens" /></FE.Row>
          <FE.Row label="Colour Temp (K)"><FE.Num value={p.bulbColourTemperature} disabled={disabled} onCommit={set('bulbColourTemperature')} data-fe="phy-bulbColourTemperature" /></FE.Row>
        </Group>
        <Group title="Lens">
          <FE.Row label="Type"><Combo id={pre + '-lens'} value={p.lensName} options={FE.LENS_TYPES} disabled={disabled} onCommit={set('lensName')} data-fe="phy-lensName" /></FE.Row>
          <FE.Row label="Min Degrees"><FE.Num value={p.lensDegreesMin} step={0.1} suffix="°" disabled={disabled} onCommit={set('lensDegreesMin')} data-fe="phy-lensDegreesMin" /></FE.Row>
          <FE.Row label="Max Degrees"><FE.Num value={p.lensDegreesMax} step={0.1} suffix="°" disabled={disabled} onCommit={set('lensDegreesMax')} data-fe="phy-lensDegreesMax" /></FE.Row>
        </Group>
        <Group title="Head(s)">
          <FE.Row label="Type"><Combo id={pre + '-focus'} value={p.focusType} options={FE.FOCUS_TYPES} disabled={disabled} onCommit={set('focusType')} data-fe="phy-focusType" /></FE.Row>
          <FE.Row label="Pan Max Degrees"><FE.Num value={p.focusPanMax} suffix="°" disabled={disabled} onCommit={set('focusPanMax')} data-fe="phy-focusPanMax" /></FE.Row>
          <FE.Row label="Tilt Max Degrees"><FE.Num value={p.focusTiltMax} suffix="°" disabled={disabled} onCommit={set('focusTiltMax')} data-fe="phy-focusTiltMax" /></FE.Row>
          <FE.Row label="Layout (cols x rows)">
            <FE.Num value={p.layoutWidth} width={70} min={1} max={999} disabled={disabled} onCommit={set('layoutWidth')} data-fe="phy-layoutWidth" />
            <RobotoText label="x" fontSize={14} height={24} />
            <FE.Num value={p.layoutHeight} width={70} min={1} max={999} disabled={disabled} onCommit={set('layoutHeight')} data-fe="phy-layoutHeight" />
          </FE.Row>
        </Group>
        <Group title="Dimensions">
          <FE.Row label="Weight"><FE.Num value={p.weight} step={0.1} suffix="kg" disabled={disabled} onCommit={set('weight')} data-fe="phy-weight" /></FE.Row>
          <FE.Row label="Width"><FE.Num value={p.width} suffix="mm" disabled={disabled} onCommit={set('width')} data-fe="phy-width" /></FE.Row>
          <FE.Row label="Height"><FE.Num value={p.height} suffix="mm" disabled={disabled} onCommit={set('height')} data-fe="phy-height" /></FE.Row>
          <FE.Row label="Depth"><FE.Num value={p.depth} suffix="mm" disabled={disabled} onCommit={set('depth')} data-fe="phy-depth" /></FE.Row>
        </Group>
        <Group title="Electrical">
          <FE.Row label="Power Consumption"><FE.Num value={p.powerConsumption} suffix="W" disabled={disabled} onCommit={set('powerConsumption')} data-fe="phy-powerConsumption" /></FE.Row>
          <FE.Row label="DMX Connector"><Combo id={pre + '-dmx'} value={p.dmxConnector} options={FE.DMX_CONNECTORS} disabled={disabled} onCommit={set('dmxConnector')} data-fe="phy-dmxConnector" /></FE.Row>
        </Group>
      </div>
    );
  }

  function PhysicalTab({ qlc, s }) {
    const sid = s.sessionId;
    return (
      <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 10 }} data-fe="physical-tab">
        <RobotoText label="Global physical properties (every mode uses these unless it overrides them in the Modes tab)" fontSize={13} labelColor="var(--fg-light)" height={26} />
        <PhysicalForm phy={s.definition.physical} idPrefix="global"
          onChange={(patch, field) => FE.act(qlc, 'fixturedefs.session.setPhysical', { sessionId: sid, physical: patch }, { key: 'phy:' + field })} />
      </div>
    );
  }

  /* ---- Modes tab ---------------------------------------------------------------------------- */
  function ModesTab({ qlc, s }) {
    const def = s.definition, sid = s.sessionId, ui = FE.ui(sid);
    const mode = FE.modeById(def, ui.modeId) || null;
    const add = () => {
      let name = 'Mode ' + (def.modes.length + 1), k = def.modes.length + 1;
      while (def.modes.some(m => m.name === name)) name = 'Mode ' + (++k);
      FE.act(qlc, 'fixturedefs.mode.add', { sessionId: sid, name }).then(r => { if (r) FE.setUi(sid, { modeId: r.modeId, slotSel: [], headSel: [] }); });
    };
    const remove = () => { if (mode) FE.act(qlc, 'fixturedefs.mode.remove', { sessionId: sid, modeId: mode.modeId }).then(r => { if (r) FE.setUi(sid, { modeId: null }); }); };
    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex' }} data-fe="modes-tab">
        <div style={{ width: 260, minWidth: 260, display: 'flex', flexDirection: 'column', borderRight: 'var(--border-dark)', background: 'var(--bg-strong)' }}>
          <FE.ListToolbar>
            <IconButton faSource="fa_plus" faColor="limegreen" size={28} tooltip="Add a new mode" onClick={add} data-fe="add-mode" />
            <IconButton faSource="fa_trash_can" faColor="crimson" size={28} disabled={!mode} tooltip="Remove the selected mode" onClick={remove} data-fe="remove-mode" />
          </FE.ListToolbar>
          <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 2 }} data-fe="mode-list">
            {def.modes.map(m => (
              <FE.ListRow key={m.modeId} selected={mode && m.modeId === mode.modeId} onClick={() => FE.setUi(sid, { modeId: m.modeId, slotSel: [], headSel: [] })} data-mode-id={m.modeId} data-mode-name={m.name}>
                <IconTextEntry faSource="fa_list_ul" faColor="var(--fg-main)" tLabel={m.name} tFontSize={14} height={26} style={{ flex: 1, minWidth: 0 }} />
                <RobotoText label={m.channels.length + ' ch' + (m.heads.length ? ', ' + m.heads.length + ' heads' : '')} fontSize={11} labelColor="var(--fg-light)" height={26} />
              </FE.ListRow>
            ))}
            {!def.modes.length ? <RobotoText label="No modes: without modes this fixture will not appear in the fixture list." fontSize={13} labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 8 }} /> : null}
          </div>
        </div>
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 10 }}>
          {mode ? <ModeEditor key={mode.modeId} qlc={qlc} s={s} mode={mode} /> : <RobotoText label="Select a mode on the left to edit it." fontSize={14} labelColor="var(--fg-medium)" height={30} />}
        </div>
      </div>
    );
  }

  function ModeEditor({ qlc, s, mode }) {
    const def = s.definition, sid = s.sessionId, ui = FE.ui(sid);
    const [open, setOpen] = React.useState({ channels: true, heads: true, physical: true });
    const [pick, setPick] = React.useState('');
    const [dragFrom, setDragFrom] = React.useState(null);
    const [dropAt, setDropAt] = React.useState(null);
    const slots = mode.channels;
    const slotSel = ui.slotSel.filter(i => i < slots.length);
    /* fn(currentSlots, currentDefinition) -> new slot list, applied at send time to the latest snapshot. */
    const setChannels = (fn) => FE.act(qlc, 'fixturedefs.mode.setChannels', { sessionId: sid, modeId: mode.modeId,
      $build: () => { const d = FE.session(sid).definition, m = FE.modeById(d, mode.modeId);
        return { channels: fn(m ? m.channels : [], d).map(sl => ({ channelId: sl.channelId, actsOnChannelId: sl.actsOnChannelId || null })) }; } })
      .then(r => { if (r) FE.setUi(sid, { slotSel: [] }); return r; });
    /** Move the slot at index `from` in front of index `to` (to = length: to the end), addressed by channel id. */
    const move = (from, to) => {
      if (to < 0 || to > slots.length || from === to || from + 1 === to) return;
      const id = slots[from].channelId, before = to < slots.length ? slots[to].channelId : null;
      setChannels(cur => {
        const it = cur.find(o => o.channelId === id); if (!it) return cur;
        const list = cur.filter(o => o !== it);
        const at = before ? list.findIndex(o => o.channelId === before) : -1;
        list.splice(at === -1 ? list.length : at, 0, it); return list;
      });
    };
    const notInMode = def.channels.filter(c => !slots.some(sl => sl.channelId === c.channelId));
    const pickId = notInMode.some(c => c.channelId === pick) ? pick : (notInMode[0] ? notInMode[0].channelId : '');
    const toggleSlot = (i) => FE.setUi(sid, { slotSel: slotSel.indexOf(i) === -1 ? slotSel.concat([i]).sort((a, b) => a - b) : slotSel.filter(x => x !== i) });
    const headSel = ui.headSel.filter(i => i < mode.heads.length);
    const toggle = (k) => setOpen(o => Object.assign({}, o, { [k]: !o[k] }));
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6, maxWidth: 1000 }} data-fe="mode-editor">
        <FE.Row label="Name" width={60}><FE.Text value={mode.name} onCommit={t => { if (t.trim()) FE.act(qlc, 'fixturedefs.mode.rename', { sessionId: sid, modeId: mode.modeId, name: t.trim() }); }} data-fe="mode-name" /></FE.Row>

        <SectionBox sectionLabel={'Channels (' + slots.length + ')'} isExpanded={open.channels} onToggle={() => toggle('channels')}>
          <FE.ListToolbar>
            <CustomComboBox width={220} height={26} disabled={!notInMode.length} currValue={pickId}
              model={notInMode.length ? notInMode.map(c => ({ mLabel: c.name, mValue: c.channelId, mIcon: FE.channelIcon(c) })) : [{ mLabel: 'every channel is in this mode', mValue: '' }]}
              onValueChanged={setPick} data-fe="mode-add-pick" />
            <GenericButton label="Add" width={50} height={26} fontSize={13} disabled={!pickId} onClick={() => setChannels(cur => cur.some(o => o.channelId === pickId) ? cur : cur.concat([{ channelId: pickId, actsOnChannelId: null }]))} data-fe="mode-add-channel" />
            <GenericButton label="Add all" width={64} height={26} fontSize={13} disabled={!notInMode.length} onClick={() => setChannels((cur, d) => cur.concat(d.channels.filter(c => !cur.some(o => o.channelId === c.channelId)).map(c => ({ channelId: c.channelId, actsOnChannelId: null }))))} data-fe="mode-add-all" />
            <span style={{ flex: 1 }} />
            <GenericButton label="Create emitter" width={110} height={26} fontSize={13} disabled={!slotSel.length} title="Create a head (emitter) from the ticked channels"
              onClick={() => FE.act(qlc, 'fixturedefs.mode.head.add', { sessionId: sid, modeId: mode.modeId, channelIds: slotSel.map(i => slots[i].channelId) }).then(r => { if (r) FE.setUi(sid, { slotSel: [] }); })} data-fe="mode-create-head" />
            <IconButton faSource="fa_trash_can" faColor="crimson" size={28} disabled={!slotSel.length} tooltip="Remove the ticked channels from this mode"
              onClick={() => { const ids = slotSel.map(i => slots[i].channelId); setChannels(cur => cur.filter(o => ids.indexOf(o.channelId) === -1)); }} data-fe="mode-remove-channels" />
          </FE.ListToolbar>
          <div style={{ display: 'flex', gap: 6, alignItems: 'center', height: 24, background: 'var(--section-header)', padding: '0 6px' }}>
            <span style={{ width: 22 }} />
            <RobotoText label="Channel" fontSize={13} fontBold height={24} style={{ flex: 1 }} />
            <RobotoText label="Acts on" fontSize={13} fontBold height={24} style={{ width: 200 }} />
            <span style={{ width: 90 }} />
          </div>
          <div onDragOver={e => e.preventDefault()} onDrop={e => { e.preventDefault(); if (dragFrom != null) move(dragFrom, slots.length); setDragFrom(null); setDropAt(null); }} data-fe="mode-slots">
            {slots.map((sl, i) => {
              const ch = FE.channelById(def, sl.channelId);
              const actsModel = [{ mLabel: 'None', mValue: '' }].concat(slots.filter(o => o.channelId !== sl.channelId).map(o => { const c = FE.channelById(def, o.channelId); return { mLabel: c ? c.name : o.channelId, mValue: o.channelId }; }));
              return (
                <div key={sl.channelId} draggable onDragStart={e => { setDragFrom(i); e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(i)); }}
                  onDragOver={e => { e.preventDefault(); e.stopPropagation(); const r = e.currentTarget.getBoundingClientRect(); setDropAt(e.clientY < r.top + r.height / 2 ? i : i + 1); }}
                  onDrop={e => { e.preventDefault(); e.stopPropagation(); if (dragFrom != null && dropAt != null) move(dragFrom, dropAt); setDragFrom(null); setDropAt(null); }}
                  onDragEnd={() => { setDragFrom(null); setDropAt(null); }}
                  style={{ display: 'flex', gap: 6, alignItems: 'center', minHeight: 30, padding: '0 6px', borderBottom: '1px solid var(--bg-medium)', cursor: 'grab',
                    borderTop: dropAt === i ? '2px solid var(--selection)' : '2px solid transparent', background: slotSel.indexOf(i) !== -1 ? 'var(--highlight)' : 'transparent' }}
                  data-slot-index={i} data-slot-channel={ch ? ch.name : sl.channelId}>
                  <CustomCheckBox checked={slotSel.indexOf(i) !== -1} size={20} onToggled={() => toggleSlot(i)} data-fe="slot-check" />
                  <IconTextEntry iSrc={FE.channelIcon(ch)} tLabel={(i + 1) + ': ' + (ch ? ch.name : '?')} tFontSize={14} height={26} style={{ flex: 1, minWidth: 0 }} />
                  <CustomComboBox width={200} height={26} currValue={sl.actsOnChannelId || ''} model={actsModel}
                    onValueChanged={v => setChannels(cur => cur.map(o => o.channelId === sl.channelId ? { channelId: o.channelId, actsOnChannelId: v || null } : o))} data-fe="slot-acts-on" />
                  <IconButton faSource="fa_chevron_up" faColor="var(--fg-main)" size={26} disabled={i === 0} tooltip="Move up" onClick={() => move(i, i - 1)} data-fe="slot-up" />
                  <IconButton faSource="fa_chevron_down" faColor="var(--fg-main)" size={26} disabled={i === slots.length - 1} tooltip="Move down" onClick={() => move(i, i + 2)} data-fe="slot-down" />
                </div>
              );
            })}
            {!slots.length ? <RobotoText label="No channels in this mode: add them from the picker above." fontSize={13} labelColor="var(--fg-medium)" height={40} leftMargin={8} /> : null}
            {slots.length && dropAt === slots.length ? <div style={{ height: 2, background: 'var(--selection)' }} /> : null}
          </div>
          <RobotoText label="Drag a row to reorder it, or use the arrows. Tick channels to remove them or to group them into an emitter." fontSize={11} labelColor="var(--fg-medium)" height={22} leftMargin={6} />
        </SectionBox>

        <SectionBox sectionLabel={'Emitters (' + mode.heads.length + ')'} isExpanded={open.heads} onToggle={() => toggle('heads')}>
          <FE.ListToolbar>
            <IconButton faSource="fa_trash_can" faColor="crimson" size={28} disabled={!headSel.length} tooltip="Remove the ticked emitter(s)"
              onClick={() => FE.act(qlc, 'fixturedefs.mode.head.remove', { sessionId: sid, modeId: mode.modeId, headIndexes: headSel }).then(r => { if (r) FE.setUi(sid, { headSel: [] }); })} data-fe="mode-remove-heads" />
          </FE.ListToolbar>
          <div data-fe="mode-heads">
            {mode.heads.map((h, i) => (
              <div key={i} style={{ display: 'flex', gap: 8, alignItems: 'flex-start', padding: 4, border: '1px solid var(--bg-light)', margin: 2, background: headSel.indexOf(i) !== -1 ? 'var(--highlight)' : 'transparent' }} data-head-index={i}>
                <CustomCheckBox checked={headSel.indexOf(i) !== -1} size={20} onToggled={() => FE.setUi(sid, { headSel: headSel.indexOf(i) === -1 ? headSel.concat([i]) : headSel.filter(x => x !== i) })} data-fe="head-check" />
                <RobotoText label={'#' + (i + 1)} fontSize={14} fontBold height={24} style={{ width: 36 }} />
                <div style={{ display: 'flex', flexDirection: 'column' }}>
                  {h.channelIds.map(cid => { const c = FE.channelById(def, cid); const idx = slots.findIndex(sl => sl.channelId === cid);
                    return <IconTextEntry key={cid} iSrc={FE.channelIcon(c)} tLabel={(idx + 1) + ': ' + (c ? c.name : cid)} tFontSize={13} height={22} />; })}
                </div>
              </div>
            ))}
            {!mode.heads.length ? <RobotoText label="No emitters. Tick channels above and press Create emitter (one per head of a multi-head fixture)." fontSize={13} labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} /> : null}
          </div>
        </SectionBox>

        <SectionBox sectionLabel="Physical" isExpanded={open.physical} onToggle={() => toggle('physical')}>
          <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 6 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <CustomCheckBox checked={mode.useGlobalPhysical} size={22} onToggled={() => { if (!mode.useGlobalPhysical) FE.act(qlc, 'fixturedefs.mode.setPhysical', { sessionId: sid, modeId: mode.modeId, useGlobalPhysical: true }); }} data-fe="mode-phy-global" />
              <RobotoText label="Use global settings" fontSize={14} height={26} />
              <CustomCheckBox checked={!mode.useGlobalPhysical} size={22} style={{ marginLeft: 14 }}
                onToggled={() => { if (mode.useGlobalPhysical) FE.act(qlc, 'fixturedefs.mode.setPhysical', { sessionId: sid, modeId: mode.modeId, useGlobalPhysical: false, physical: Object.assign({}, def.physical) }); }} data-fe="mode-phy-override" />
              <RobotoText label="Override global settings" fontSize={14} height={26} />
            </div>
            <PhysicalForm phy={mode.useGlobalPhysical ? def.physical : mode.physical} disabled={mode.useGlobalPhysical} idPrefix={'mode-' + mode.modeId}
              onChange={(patch, field) => FE.act(qlc, 'fixturedefs.mode.setPhysical', { sessionId: sid, modeId: mode.modeId, useGlobalPhysical: false,
                /* built at send time from the latest snapshot, so consecutive field edits never undo each other */
                $build: () => { const cur = FE.session(sid), d = cur && cur.definition, m = FE.modeById(d, mode.modeId);
                  return { physical: Object.assign({}, (m && m.physical) || (d && d.physical) || {}, patch) }; } }, { key: mode.modeId + ':phy:' + field })} />
          </div>
        </SectionBox>
      </div>
    );
  }

  Object.assign(FE, { ModesTab, PhysicalTab, PhysicalForm });
})();
