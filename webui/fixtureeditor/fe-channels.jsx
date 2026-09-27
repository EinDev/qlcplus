/**
 * fe-channels.jsx — Channels and Aliases tabs of the Fixture Editor, mirroring
 * qmlui/qml/fixtureeditor/EditorView.qml (channel list + toolbar), ChannelEditor.qml (name, preset,
 * type, role, default value, capabilities with range / description / warnings, capability preset
 * with its colours / values / picture, automatic colour assignment), AliasEditor.qml and
 * qmlui/qml/popup/PopupChannelWizard.qml (both faces: preset channels and capabilities).
 *
 * window.FE.ChannelsTab, FE.AliasesTab, FE.WizardDialog
 */
(function () {
  'use strict';
  const FE = window.FE;
  const { RobotoText, IconButton, GenericButton, CustomComboBox, CustomSpinBox, CustomCheckBox, CustomPopupDialog, IconTextEntry } = window.PatchDesignSystem_5432c9;

  const groupModel = () => FE.GROUPS.map(g => ({ mLabel: g, mValue: g }));
  const colourModel = () => FE.COLOURS.map(c => ({ mLabel: c === 'Generic' ? 'None (generic)' : c, mValue: c, mIcon: c !== 'Generic' && window.QLCIcons && window.QLCIcons.COLOUR_ICONS[c] ? window.QLCData.icon(window.QLCIcons.COLOUR_ICONS[c]) : undefined }));
  const channelPresetModel = () => FE.CHANNEL_PRESETS.map(p => ({ mLabel: p === 'Custom' ? 'Custom' : FE.humanize(p), mValue: p }));
  const capPresetModel = () => FE.CAPABILITY_PRESETS.map(p => ({ mLabel: p === 'Custom' ? 'Custom (no preset)' : FE.humanize(p), mValue: p }));
  const WARNING_TEXT = { EmptyName: 'Empty description provided', Overlapping: 'Overlapping with another capability' };

  /* ---- Channels tab -------------------------------------------------------------------------- */
  function ChannelsTab({ qlc, s }) {
    const D = window.QLCData;
    const def = s.definition, ui = FE.ui(s.sessionId);
    const [addPreset, setAddPreset] = React.useState('Custom');
    const [wizard, setWizard] = React.useState(false);
    const sid = s.sessionId;
    const current = FE.channelById(def, ui.channelId);
    const sel = ui.chanSel.filter(id => FE.channelById(def, id));
    const click = (ch, e) => {
      if (e.ctrlKey || e.metaKey) {
        const next = sel.indexOf(ch.channelId) === -1 ? sel.concat([ch.channelId]) : sel.filter(x => x !== ch.channelId);
        FE.setUi(sid, { chanSel: next });
      } else FE.setUi(sid, { channelId: ch.channelId, chanSel: [ch.channelId], capIndex: -1 });
    };
    const add = () => {
      const params = { sessionId: sid };
      if (addPreset !== 'Custom') params.preset = addPreset;
      FE.act(qlc, 'fixturedefs.channel.add', params).then(r => { if (r) FE.setUi(sid, { channelId: r.channelId, chanSel: [r.channelId], capIndex: -1 }); });
    };
    const remove = () => {
      if (!sel.length) return;
      FE.act(qlc, 'fixturedefs.channel.remove', { sessionId: sid, channelIds: sel }).then(r => { if (r) FE.setUi(sid, { chanSel: [], channelId: null, capIndex: -1 }); });
    };
    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex' }} data-fe="channels-tab">
        <div style={{ width: 300, minWidth: 300, display: 'flex', flexDirection: 'column', borderRight: 'var(--border-dark)', background: 'var(--bg-strong)' }}>
          <FE.ListToolbar>
            <CustomComboBox width={150} height={26} currValue={addPreset} model={channelPresetModel()} onValueChanged={setAddPreset} data-fe="add-channel-preset" />
            <IconButton faSource="fa_plus" faColor="limegreen" size={28} tooltip="Add a new channel (from the preset on the left, or Custom)" onClick={add} data-fe="add-channel" />
            <IconButton faSource="fa_trash_can" faColor="crimson" size={28} disabled={!sel.length} tooltip="Remove the selected channel(s) - also removed from every mode" onClick={remove} data-fe="remove-channels" />
            <span style={{ flex: 1 }} />
            <GenericButton label="Wizard" width={70} height={26} fontSize={13} onClick={() => setWizard(true)} title="Channel wizard: create several preset channels at once" data-fe="channel-wizard" />
          </FE.ListToolbar>
          <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 2 }} data-fe="channel-list">
            {def.channels.length ? def.channels.map(ch => (
              <FE.ListRow key={ch.channelId} selected={sel.indexOf(ch.channelId) !== -1} onClick={(e) => click(ch, e)} data-channel-id={ch.channelId} data-channel-name={ch.name}
                style={ch.channelId === ui.channelId ? { boxShadow: 'inset 3px 0 0 var(--selection)' } : null}>
                <IconTextEntry iSrc={FE.channelIcon(ch)} tLabel={ch.name} tFontSize={14} height={26} style={{ flex: 1, minWidth: 0 }} />
                <RobotoText label={ch.capabilities.length + ' cap'} fontSize={11} labelColor="var(--fg-light)" height={26} />
              </FE.ListRow>
            )) : <RobotoText label="No channels yet: add one with + or create several with the wizard." fontSize={13} labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 8 }} />}
          </div>
          <RobotoText label="Click to edit, Ctrl+click to select several for removal." fontSize={11} labelColor="var(--fg-medium)" height={22} leftMargin={6} />
        </div>
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 10 }}>
          {current ? <ChannelEditor qlc={qlc} s={s} ch={current} /> : <RobotoText label="Select a channel on the left to edit it." fontSize={14} labelColor="var(--fg-medium)" height={30} />}
        </div>
        <WizardDialog open={wizard} kind="channel" qlc={qlc} s={s} onClose={() => setWizard(false)} />
      </div>
    );
  }

  /* ---- Channel editor (ChannelEditor.qml) ------------------------------------------------------ */
  function ChannelEditor({ qlc, s, ch }) {
    const sid = s.sessionId, ui = FE.ui(sid);
    const [wizard, setWizard] = React.useState(false);
    const upd = (patch, key) => FE.act(qlc, 'fixturedefs.channel.update', Object.assign({ sessionId: sid, channelId: ch.channelId }, patch), key ? { key: ch.channelId + ':' + key } : undefined);
    const preset = ch.preset !== 'Custom';
    const capIndex = ui.capIndex >= 0 && ui.capIndex < ch.capabilities.length ? ui.capIndex : -1;
    const addCap = () => FE.act(qlc, 'fixturedefs.channel.capability.add', { sessionId: sid, channelId: ch.channelId }).then(r => { if (r) FE.setUi(sid, { capIndex: r.capabilityIndex }); });
    const removeCap = () => { if (capIndex < 0) return; FE.act(qlc, 'fixturedefs.channel.capability.remove', { sessionId: sid, channelId: ch.channelId, capabilityIndex: capIndex }).then(() => FE.setUi(sid, { capIndex: -1 })); };
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4, maxWidth: 900 }} data-fe="channel-editor">
        <FE.Row label="Name"><FE.Text value={ch.name} onCommit={t => { if (t.trim()) upd({ name: t.trim() }); }} data-fe="channel-name" /></FE.Row>
        <FE.Row label="Preset"><CustomComboBox width={320} height={26} currValue={ch.preset} model={channelPresetModel()} onValueChanged={v => upd({ preset: v })} data-fe="channel-preset" /></FE.Row>
        <FE.Row label="Type" title={preset ? 'Set by the preset' : ''}>
          <CustomComboBox width={200} height={26} disabled={preset} currValue={ch.group} model={groupModel()} onValueChanged={v => upd({ group: v })} data-fe="channel-group" />
          {ch.group === 'Intensity' ? <>
            <RobotoText label="Colour" fontSize={14} labelColor="var(--fg-light)" height={26} style={{ marginLeft: 12 }} />
            <CustomComboBox width={170} height={26} disabled={preset} currValue={ch.colour} model={colourModel()} onValueChanged={v => upd({ colour: v })} data-fe="channel-colour" />
          </> : null}
        </FE.Row>
        <FE.Row label="Role">
          <CustomCheckBox checked={ch.controlByte !== 'LSB'} size={22} disabled={preset} onToggled={() => upd({ controlByte: 'MSB' })} data-fe="role-msb" />
          <RobotoText label="Coarse (MSB)" fontSize={14} height={26} />
          <CustomCheckBox checked={ch.controlByte === 'LSB'} size={22} disabled={preset} onToggled={() => upd({ controlByte: 'LSB' })} style={{ marginLeft: 10 }} data-fe="role-lsb" />
          <RobotoText label="Fine (LSB)" fontSize={14} height={26} />
        </FE.Row>
        <FE.Row label="Default value">
          <CustomSpinBox value={ch.defaultValue} from={0} to={255} width={90} height={26} onValueModified={v => upd({ defaultValue: v }, 'default')} data-fe="channel-default" />
        </FE.Row>

        <div style={{ display: 'flex', alignItems: 'center', gap: 4, marginTop: 8 }}>
          <RobotoText label={'Capabilities (' + ch.capabilities.length + ')'} fontSize={15} fontBold height={28} style={{ flex: 1 }} />
          <IconButton faSource="fa_plus" faColor="limegreen" size={28} disabled={preset} tooltip="Add a capability (next free range)" onClick={addCap} data-fe="add-capability" />
          <IconButton faSource="fa_trash_can" faColor="crimson" size={28} disabled={preset || capIndex < 0} tooltip="Delete the selected capability" onClick={removeCap} data-fe="remove-capability" />
          {ch.group === 'Colour' ? <GenericButton label="Auto colours" width={100} height={26} fontSize={13} disabled={preset} title="Automatic color assignment: detect colour names in the descriptions"
            onClick={() => FE.act(qlc, 'fixturedefs.channel.capability.autoPatchColors', { sessionId: sid, channelId: ch.channelId })} data-fe="auto-colours" /> : null}
          <GenericButton label="Wizard" width={70} height={26} fontSize={13} disabled={preset} onClick={() => setWizard(true)} title="Capability wizard: create several ranges at once" data-fe="capability-wizard" />
        </div>
        {preset ? <RobotoText label="Capabilities of a preset channel are generated by the preset; switch the preset to Custom to edit them." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" /> : null}
        <div style={{ border: 'var(--border-dark)', background: 'var(--bg-strong)', opacity: preset ? .6 : 1 }} data-fe="capability-table">
          <div style={{ display: 'flex', gap: 6, alignItems: 'center', height: 26, background: 'var(--section-header)', padding: '0 6px' }}>
            <RobotoText label="From" fontSize={13} fontBold height={26} style={{ width: 80 }} />
            <RobotoText label="To" fontSize={13} fontBold height={26} style={{ width: 80 }} />
            <RobotoText label="Description" fontSize={13} fontBold height={26} style={{ flex: 1 }} />
            <RobotoText label="Preset" fontSize={13} fontBold height={26} style={{ width: 150 }} />
          </div>
          {ch.capabilities.map((cap, i) => (
            <FE.ListRow key={i} selected={i === capIndex} onClick={() => FE.setUi(sid, { capIndex: i })} data-cap-index={i}>
              <span onClick={e => e.stopPropagation()} style={{ width: 80 }}><FE.Num value={cap.min} width={64} min={0} max={255} disabled={preset}
                onCommit={v => FE.act(qlc, 'fixturedefs.channel.capability.update', { sessionId: sid, channelId: ch.channelId, capabilityIndex: i, min: v })} data-fe="cap-min" /></span>
              <span onClick={e => e.stopPropagation()} style={{ width: 80 }}><FE.Num value={cap.max} width={64} min={0} max={255} disabled={preset}
                onCommit={v => FE.act(qlc, 'fixturedefs.channel.capability.update', { sessionId: sid, channelId: ch.channelId, capabilityIndex: i, max: v })} data-fe="cap-max" /></span>
              <span onClick={e => { e.stopPropagation(); FE.setUi(sid, { capIndex: i }); }} style={{ flex: 1, minWidth: 0 }}><FE.Text value={cap.name} disabled={preset}
                onCommit={t => FE.act(qlc, 'fixturedefs.channel.capability.update', { sessionId: sid, channelId: ch.channelId, capabilityIndex: i, name: t })} data-fe="cap-name" /></span>
              <span style={{ width: 150, display: 'flex', alignItems: 'center', gap: 4 }}>
                <CapSwatch cap={cap} />
                <RobotoText label={cap.preset === 'Custom' ? '' : FE.humanize(cap.preset)} fontSize={12} labelColor="var(--fg-light)" height={24} />
                {cap.warning && cap.warning !== 'NoWarning' ? <span title={WARNING_TEXT[cap.warning] || cap.warning} style={{ color: 'yellow', fontWeight: 700 }} data-fe="cap-warning">!</span> : null}
              </span>
            </FE.ListRow>
          ))}
        </div>
        {capIndex >= 0 && !preset ? <CapabilityDetail qlc={qlc} s={s} ch={ch} capIndex={capIndex} /> : null}
        <WizardDialog open={wizard} kind="capability" qlc={qlc} s={s} ch={ch} onClose={() => setWizard(false)} />
      </div>
    );
  }

  function CapSwatch({ cap }) {
    const t = FE.capabilityPresetType(cap.preset);
    if (t !== 'SingleColor' && t !== 'DoubleColor') return null;
    const a = cap.resources[0] || '#000000', b = cap.resources[1] || a;
    return <span style={{ width: 18, height: 18, flex: 'none', border: '1px solid var(--bg-lighter)', background: 'linear-gradient(90deg,' + a + ' 50%,' + b + ' 50%)' }} />;
  }

  /* ---- Capability preset + resources + aliases ------------------------------------------------- */
  function CapabilityDetail({ qlc, s, ch, capIndex }) {
    const cap = ch.capabilities[capIndex];
    const sid = s.sessionId;
    const type = FE.capabilityPresetType(cap.preset);
    const units = FE.capabilityPresetUnits(cap.preset);
    const upd = (patch) => FE.act(qlc, 'fixturedefs.channel.capability.update', Object.assign({ sessionId: sid, channelId: ch.channelId, capabilityIndex: capIndex }, patch));
    const res = cap.resources || [];
    const setRes = (i, v) => { const n = res.slice(); n[i] = v; if (type === 'DoubleColor' && n.length < 2) n[1 - i] = n[1 - i] || '#000000'; upd({ resources: n }); };
    return (
      <div style={{ marginTop: 8, padding: 8, border: '1px solid var(--bg-light)', borderRadius: 4, display: 'flex', flexDirection: 'column', gap: 4 }} data-fe="capability-detail">
        <RobotoText label={'Capability [' + cap.min + ' - ' + cap.max + '] ' + cap.name} fontSize={14} fontBold height={24} />
        <FE.Row label="Preset"><CustomComboBox width={320} height={26} currValue={cap.preset} model={capPresetModel()} onValueChanged={v => upd({ preset: v })} data-fe="cap-preset" /></FE.Row>
        {type === 'SingleColor' || type === 'DoubleColor' ? (
          <FE.Row label={type === 'DoubleColor' ? 'Colours' : 'Colour'}>
            <input type="color" value={res[0] || '#000000'} onChange={e => setRes(0, e.target.value)} style={{ width: 48, height: 26 }} data-fe="cap-color1" />
            {type === 'DoubleColor' ? <input type="color" value={res[1] || '#000000'} onChange={e => setRes(1, e.target.value)} style={{ width: 48, height: 26 }} data-fe="cap-color2" /> : null}
            <RobotoText label={res.join('  ')} fontSize={12} labelColor="var(--fg-light)" height={26} />
          </FE.Row>
        ) : null}
        {type === 'SingleValue' || type === 'DoubleValue' ? (
          <FE.Row label="Value(s)">
            <FE.Num value={res[0] != null ? res[0] : 0} step={0.1} min={-1000} max={1000} suffix={units} onCommit={v => setRes(0, v)} data-fe="cap-value1" />
            {type === 'DoubleValue' ? <FE.Num value={res[1] != null ? res[1] : 0} step={0.1} min={-1000} max={1000} suffix={units} onCommit={v => setRes(1, v)} data-fe="cap-value2" /> : null}
          </FE.Row>
        ) : null}
        {type === 'Picture' ? (
          <FE.Row label="Picture" title="Path of the gobo picture on the QLC+ machine (e.g. a file in its Gobos folder)">
            <FE.Text value={res[0] || ''} placeholder="Gobos/Others/gobo00001.svg" onCommit={t => upd({ resources: t ? [t] : [] })} data-fe="cap-picture" />
          </FE.Row>
        ) : null}
        {type === 'Picture' ? <RobotoText label="Uploading a picture from this browser is not available: type the path of a gobo picture that exists on the QLC+ machine." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" /> : null}
        {cap.preset === 'Alias' ? <AliasEditor qlc={qlc} s={s} ch={ch} capIndex={capIndex} /> : null}
      </div>
    );
  }

  /* ---- AliasEditor.qml --------------------------------------------------------------------------- */
  function AliasEditor({ qlc, s, ch, capIndex }) {
    const def = s.definition, sid = s.sessionId;
    const cap = ch.capabilities[capIndex];
    const modes = def.modes.filter(m => m.channels.some(sl => sl.channelId === ch.channelId)).map(m => m.name);
    const channels = def.channels.filter(c => c.channelId !== ch.channelId).map(c => c.name);
    const base = { sessionId: sid, channelId: ch.channelId, capabilityIndex: capIndex };
    const add = () => { if (modes.length && channels.length) FE.act(qlc, 'fixturedefs.channel.capability.alias.add', Object.assign({ targetMode: modes[0], targetChannel: channels[0] }, base)); };
    return (
      <div style={{ marginTop: 6, display: 'flex', flexDirection: 'column', gap: 4 }} data-fe="alias-editor">
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <RobotoText label="Aliases" fontSize={14} fontBold height={26} style={{ flex: 1 }} />
          <IconButton faSource="fa_plus" faColor="limegreen" size={26} disabled={!modes.length || !channels.length} tooltip="Add an alias" onClick={add} data-fe="alias-add" />
          <GenericButton label="Apply to all modes" width={140} height={26} fontSize={13} disabled={!modes.length}
            onClick={() => FE.act(qlc, 'fixturedefs.channel.capability.alias.applyToAllModes', base)} data-fe="alias-apply-all" />
        </div>
        <RobotoText label="While this capability is active, the source channel is replaced by the target channel in the selected mode." fontSize={12} fontItalic labelColor="var(--fg-medium)" wrapText height="auto" />
        {!modes.length ? <RobotoText label="Add this channel to a mode first (Modes tab): aliases are defined per mode." fontSize={12} labelColor="var(--override-red)" wrapText height="auto" /> : null}
        <div style={{ display: 'flex', gap: 6, alignItems: 'center', height: 24, background: 'var(--section-header)', padding: '0 6px' }}>
          <RobotoText label="In mode" fontSize={13} fontBold height={24} style={{ width: 190 }} />
          <RobotoText label="Replace" fontSize={13} fontBold height={24} style={{ width: 140 }} />
          <RobotoText label="With" fontSize={13} fontBold height={24} style={{ flex: 1 }} />
        </div>
        {(cap.aliases || []).map((a, i) => {
          const warn = modes.indexOf(a.targetMode) === -1 ? 'Mode "' + a.targetMode + '" does not contain this channel (renamed or removed?)'
            : channels.indexOf(a.targetChannel) === -1 ? 'Channel "' + a.targetChannel + '" does not exist' : '';
          return (
            <div key={i} style={{ display: 'flex', gap: 6, alignItems: 'center' }} data-alias-index={i}>
              <CustomComboBox width={190} height={26} currValue={a.targetMode} model={(modes.indexOf(a.targetMode) === -1 ? [a.targetMode] : []).concat(modes).map(m => ({ mLabel: m, mValue: m }))}
                onValueChanged={v => FE.act(qlc, 'fixturedefs.channel.capability.alias.update', Object.assign({ aliasIndex: i, targetMode: v }, base))} data-fe="alias-mode" />
              <RobotoText label={ch.name} fontSize={14} height={26} style={{ width: 140 }} />
              <CustomComboBox width={200} height={26} currValue={a.targetChannel} model={(channels.indexOf(a.targetChannel) === -1 ? [a.targetChannel] : []).concat(channels).map(c => ({ mLabel: c, mValue: c }))}
                onValueChanged={v => FE.act(qlc, 'fixturedefs.channel.capability.alias.update', Object.assign({ aliasIndex: i, targetChannel: v }, base))} data-fe="alias-target" />
              {warn ? <span title={warn} style={{ color: 'yellow', fontWeight: 700 }}>!</span> : null}
              <IconButton faSource="fa_trash_can" faColor="crimson" size={24} tooltip="Remove this alias"
                onClick={() => FE.act(qlc, 'fixturedefs.channel.capability.alias.remove', Object.assign({ aliasIndex: i }, base))} data-fe="alias-remove" />
            </div>
          );
        })}
      </div>
    );
  }

  /* ---- Aliases tab (EditorView.qml's alias section) --------------------------------------------- */
  function AliasesTab({ qlc, s }) {
    const list = FE.aliasCapabilities(s.definition);
    const sid = s.sessionId;
    return (
      <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 10, maxWidth: 900 }} data-fe="aliases-tab">
        {!list.length ? <RobotoText label="Set a capability preset to 'Alias' in the channel editor to make it appear here." fontSize={14} labelColor="var(--fg-medium)" wrapText height="auto" /> : null}
        {list.map(({ ch, cap, capIndex }) => (
          <div key={ch.channelId + ':' + capIndex} style={{ marginBottom: 10 }}>
            <FE.ListRow onClick={() => FE.setUi(sid, { tab: 'channels', channelId: ch.channelId, chanSel: [ch.channelId], capIndex })} data-fe="alias-capability">
              <IconTextEntry iSrc={FE.channelIcon(ch)} tLabel={ch.name + ' [' + cap.min + ' - ' + cap.max + '] ' + cap.name + ' (' + (cap.aliases || []).length + ')'} tFontSize={14} height={26} style={{ flex: 1 }} />
              <RobotoText label="edit in Channels" fontSize={11} labelColor="var(--fg-light)" height={26} />
            </FE.ListRow>
            <AliasEditor qlc={qlc} s={s} ch={ch} capIndex={capIndex} />
          </div>
        ))}
      </div>
    );
  }

  /* ---- PopupChannelWizard.qml --------------------------------------------------------------------- */
  function WizardDialog({ open, kind, qlc, s, ch, onClose }) {
    const D = window.QLCData;
    const cap = kind === 'capability';
    const [type, setType] = React.useState('Red');
    const [amount, setAmount] = React.useState(1);
    const [start, setStart] = React.useState(0);
    const [width, setWidth] = React.useState(1);
    const [label, setLabel] = React.useState(cap ? 'Capability #' : 'Channel #');
    React.useEffect(() => { if (open) { setAmount(1); setStart(0); setWidth(1); setLabel(cap ? 'Capability #' : 'Channel #'); } }, [open]);
    if (!open) return null;
    const items = [];
    let overlapping = false, outOfRange = false;
    for (let i = 0; i < amount && items.length < 2000; i++) {
      const n = label.replace(/#/g, String(i + 1));
      if (cap) {
        const lo = start + width * i, hi = lo + width - 1;
        if (hi > 255) outOfRange = true;
        if (ch && ch.capabilities.some(c => !(hi < c.min || lo > c.max))) overlapping = true;
        items.push('[' + lo + ' - ' + hi + '] ' + n);
      } else if (FE.WIZARD_COMPOUND[type]) FE.WIZARD_COMPOUND[type].forEach(c => items.push(c + ' ' + (i + 1)));
      else items.push(n);
    }
    const taken = !cap ? items.filter(n => s.definition.channels.some(c => c.name === n)) : [];
    const blocked = overlapping || outOfRange || taken.length > 0 || !label.trim();
    const ok = () => {
      if (blocked) return;
      const params = cap ? { sessionId: s.sessionId, channelId: ch.channelId, start, width, amount, label }
        : { sessionId: s.sessionId, type, amount, label };
      FE.act(qlc, cap ? 'fixturedefs.channel.capability.wizard' : 'fixturedefs.channel.wizard', params).then(r => { if (r) onClose(); });
    };
    return (
      <CustomPopupDialog open title="Fixture Editor Wizard" width={560} standardButtons={['Cancel', 'Ok']} disabledButtons={blocked ? ['Ok'] : []}
        onClicked={b => { if (b === 'Ok') ok(); else onClose(); }} onClose={onClose}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }} data-fe={cap ? 'capability-wizard-dialog' : 'channel-wizard-dialog'}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
            {cap ? <>
              <RobotoText label="Start" fontSize={14} height={26} />
              <CustomSpinBox value={start} from={0} to={254} width={80} height={26} onValueModified={setStart} data-fe="wizard-start" />
              <RobotoText label="Width" fontSize={14} height={26} />
              <CustomSpinBox value={width} from={1} to={255} width={80} height={26} onValueModified={setWidth} data-fe="wizard-width" />
            </> : null}
            <RobotoText label="Amount" fontSize={14} height={26} />
            <CustomSpinBox value={amount} from={1} to={1000} width={80} height={26} onValueModified={setAmount} data-fe="wizard-amount" />
            {!cap ? <>
              <RobotoText label="Type" fontSize={14} height={26} />
              <CustomComboBox width={170} height={26} currValue={type} model={FE.WIZARD_TYPES.map(t => ({ mLabel: t[0], mValue: t[0], mIcon: D.icon(t[1]) }))} onValueChanged={setType} data-fe="wizard-type" />
            </> : null}
          </div>
          {overlapping ? <RobotoText label="Overlapping range detected. Adjust the parameters or make space" fontSize={13} labelColor="var(--override-red)" height={22} /> : null}
          {outOfRange ? <RobotoText label="The ranges do not fit into 0 - 255" fontSize={13} labelColor="var(--override-red)" height={22} /> : null}
          {taken.length ? <RobotoText label={'Channel name(s) already in use: ' + taken.slice(0, 5).join(', ')} fontSize={13} labelColor="var(--override-red)" wrapText height="auto" /> : null}
          <FE.Row label="Label" width={60}>
            <input value={label} onChange={e => setLabel(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') ok(); }} style={Object.assign({}, FE.inputStyle, { width: '100%' })} data-fe="wizard-label" />
          </FE.Row>
          {!cap && FE.WIZARD_COMPOUND[type] ? <RobotoText label="Compound types name each component '<Colour> N'; the label is not used." fontSize={12} labelColor="var(--fg-medium)" height={20} /> : null}
          <RobotoText label="Preview" fontSize={14} fontBold height={22} />
          <div style={{ height: 160, overflow: 'auto', background: 'var(--bg-strong)', border: 'var(--border-dark)', padding: 4 }} data-fe="wizard-preview">
            {items.map((n, i) => <RobotoText key={i} label={n} fontSize={13} height={22} />)}
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  Object.assign(FE, { ChannelsTab, AliasesTab, WizardDialog });
})();
