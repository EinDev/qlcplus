const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, QLCPlusFader, GenericButton, ShortcutHint, CustomSpinBox, SectionBox, SidePanel, CustomComboBox, CustomCheckBox } = window.PatchDesignSystem_5432c9;

function VCSlider({ w, onChange }) {
  return (
    <div style={{ width: 74, background: 'var(--bg-strong)', border: 'var(--border-control)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4, padding: '6px 0' }}>
      <QLCPlusFader value={w.value} onMoved={onChange} height={180} />
      <RobotoText label={String(w.value)} fontSize={14} labelColor="var(--fg-light)" height={18} textHAlign="center" style={{ width: '100%' }} />
      <RobotoText label={w.label} fontSize={14} height={20} textHAlign="center" style={{ width: '100%' }} />
    </div>
  );
}

function VCButton({ w, onToggle }) {
  return (
    <button type="button" onClick={onToggle}
      style={{
        width: 108, height: 60, cursor: 'pointer', padding: 4,
        background: w.on ? 'var(--highlight)' : 'var(--bg-control)',
        border: 'var(--border-control)', color: 'var(--fg-main)',
        font: '400 var(--text-size-small)/1.2 var(--font-roboto)'
      }}>{w.label}</button>
  );
}

function VirtualConsole() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [widgets, setWidgets] = React.useState(D.vcWidgets);
  const [edit, setEdit] = React.useState(false);
  const [panel, setPanel] = React.useState(false);

  /* vc.widget.list is structural only — id/type/caption/layout, no live value. This server
     build has no push (or pull) for a widget's current value at all yet, so a connected
     Virtual Console shows the real widget list but every value stays local-mock until the
     API grows a slider/button interaction method — see api/qlcplus-api.js's setWidget(). */
  React.useEffect(() => {
    if (!qlc.online) return;
    const offList = qlc.subscribeTo('getWidgetsList', (result) => {
      const next = (result.widgets || [])
        .filter(w => /slider|button/i.test(w.widgetType || ''))
        .map(w => ({
          id: w.id, label: (w.style && w.style.caption) || w.id,
          kind: /slider/i.test(w.widgetType) ? 'slider' : 'button',
          value: 0, on: false
        }));
      if (next.length) setWidgets(next);
    });
    return () => { offList(); };
  }, [qlc.online]);

  const set = (id, patch) => {
    setWidgets(p => p.map(w => w.id === id ? Object.assign({}, w, patch) : w));
    if (patch.value != null) qlc.setWidget(id, patch.value);
    if (patch.on != null) qlc.setWidget(id, patch.on ? 255 : 0);
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <ShortcutHint keys="Ctrl L" placement="corner">
          <IconButton imgSource={edit ? D.icon('unlock') : D.icon('lock')} size={26} checked={edit}
            onClick={() => setEdit(!edit)} tooltip={edit ? 'Lock editing' : 'Unlock editing'} />
        </ShortcutHint>
        <IconButton imgSource={D.icon('frame')} size={26} tooltip="Add frame" disabled={!edit} />
        <IconButton imgSource={D.icon('button')} size={26} tooltip="Add button" disabled={!edit} />
        <IconButton imgSource={D.icon('slider')} size={26} tooltip="Add slider" disabled={!edit} />
        <IconButton imgSource={D.icon('buttonmatrix')} size={26} tooltip="Add button matrix" disabled={!edit} />
        <IconButton imgSource={D.icon('xypad')} size={26} tooltip="Add XY pad" disabled={!edit} />
        <IconButton imgSource={D.icon('network')} size={26} disabled={!qlc.online}
          tooltip={qlc.online ? 'Reload widgets from the desk' : 'Not connected'}
          onClick={() => { const c = qlc.client(); if (c) c.getWidgetsList(); }} />
        <ToolbarSpacer />
        <RobotoText label={qlc.online ? widgets.length + ' widgets from the desk (values not live yet)' : 'Offline — local preview'} fontSize={14}
          labelColor={qlc.online ? 'var(--check-lime)' : 'var(--fg-medium)'} />
        <IconButton imgSource={D.icon('blackout')} size={26} tooltip="Blackout" />
        <IconButton imgSource={D.icon('stopall')} size={26} tooltip="Stop all functions" />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 12, background: 'var(--bg-medium)' }}>
          <div style={{ display: 'inline-flex', flexDirection: 'column', gap: 10, padding: 10, border: edit ? '2px dashed var(--bg-light)' : 'var(--border-control)', background: 'var(--bg-stronger)' }}>
            <RobotoText label="Main frame" fontSize={14} labelColor="var(--fg-medium)" height={20} />
            <div style={{ display: 'flex', gap: 8, alignItems: 'flex-start' }}>
              {widgets.filter(w => w.kind === 'slider').map(w => (
                <VCSlider key={w.id} w={w} onChange={v => set(w.id, { value: v })} />
              ))}
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2,auto)', gap: 6, alignContent: 'start' }}>
                {widgets.filter(w => w.kind === 'button').map(w => (
                  <VCButton key={w.id} w={w} onToggle={() => set(w.id, { on: !w.on })} />
                ))}
              </div>
            </div>
          </div>
        </div>

        <SidePanel isOpen={panel} alignment="right" rail={
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
            <IconButton imgSource={D.icon('configure')} checked={panel} onClick={() => setPanel(!panel)} tooltip="Widget properties" />
            <IconButton imgSource={D.icon('keybinding')} tooltip="Key bindings" />
            <IconButton imgSource={D.icon('inputoutput')} tooltip="External input" />
          </div>}>
          <div>
            <SectionBox sectionLabel="Slider" isExpanded>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 8, padding: 8 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <RobotoText label="Mode" fontSize={14} style={{ width: 62 }} />
                  <CustomComboBox width={110} currValue={0} model={['Level', 'Playback', 'Submaster']} />
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <RobotoText label="Low" fontSize={14} style={{ width: 62 }} />
                  <CustomSpinBox value={0} width={86} />
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <RobotoText label="High" fontSize={14} style={{ width: 62 }} />
                  <CustomSpinBox value={255} width={86} />
                </div>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <CustomCheckBox checked /><RobotoText label="Invert" fontSize={14} />
                </div>
              </div>
            </SectionBox>
            <SectionBox sectionLabel="Input" />
          </div>
        </SidePanel>
      </div>
    </div>
  );
}
Object.assign(window, { VirtualConsole, VCSlider, VCButton });
