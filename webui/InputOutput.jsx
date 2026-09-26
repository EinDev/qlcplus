const { ViewToolbar, ToolbarSpacer, IconButton, RobotoText, CustomComboBox, CustomCheckBox, GenericButton, IconTextEntry, SectionBox } = window.PatchDesignSystem_5432c9;

function InputOutput() {
  const D = window.QLCData;
  const [rows, setRows] = React.useState(D.universes);
  const [sel, setSel] = React.useState(1);
  const set = (id, patch) => setRows(p => p.map(u => u.id === id ? Object.assign({}, u, patch) : u));
  const head = { display: 'flex', alignItems: 'center', height: 30, background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)', padding: '0 6px' };
  const cell = (w) => ({ width: w, minWidth: w, padding: '0 6px' });

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <IconButton imgSource={D.icon('add')} size={26} tooltip="Add universe" />
        <IconButton faSource="fa_trash_can" size={26} tooltip="Remove universe" />
        <IconButton imgSource={D.icon('network')} size={26} tooltip="Network settings" />
        <ToolbarSpacer />
        <RobotoText label="4 universes · 3 plugins active" fontSize={14} labelColor="var(--fg-light)" />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto' }}>
          <div style={head}>
            <RobotoText label="Universe" fontSize={14} fontBold style={cell(150)} />
            <RobotoText label="Input" fontSize={14} fontBold style={cell(190)} />
            <RobotoText label="Output" fontSize={14} fontBold style={cell(210)} />
            <RobotoText label="Feedback" fontSize={14} fontBold style={cell(170)} />
            <RobotoText label="Passthrough" fontSize={14} fontBold style={cell(100)} />
          </div>
          {rows.map((u, i) => (
            <div key={u.id} onClick={() => setSel(u.id)}
              style={{
                display: 'flex', alignItems: 'center', height: 38, cursor: 'pointer', padding: '0 6px',
                background: sel === u.id ? 'var(--highlight)' : (i % 2 ? 'var(--bg-strong)' : 'var(--bg-stronger)'),
                borderBottom: 'var(--border-dark)'
              }}>
              <div style={cell(150)}><IconTextEntry iSrc={D.icon('uniview')} tLabel={u.name} tFontSize={14} height={26} /></div>
              <div style={cell(190)}><CustomComboBox width={175} height={26} currValue={u.input} onValueChanged={v => set(u.id, { input: v })}
                model={[{ mLabel: 'None', mValue: 'None' }, { mLabel: 'MIDI Controller', mValue: 'MIDI Controller' }, { mLabel: 'OSC 9000', mValue: 'OSC 9000' }]} /></div>
              <div style={cell(210)}><CustomComboBox width={195} height={26} currValue={u.output} onValueChanged={v => set(u.id, { output: v })}
                model={[{ mLabel: 'None', mValue: 'None' }, { mLabel: 'ArtNet 2.0.0.1', mValue: 'ArtNet 2.0.0.1' }, { mLabel: 'E1.31 239.255.0.2', mValue: 'E1.31 239.255.0.2' }, { mLabel: 'DMX USB Pro', mValue: 'DMX USB Pro' }]} /></div>
              <div style={cell(170)}><RobotoText label={u.feedback} fontSize={14} labelColor="var(--fg-light)" height={26} /></div>
              <div style={Object.assign({}, cell(100), { display: 'flex', justifyContent: 'center' })}>
                <CustomCheckBox checked={u.passthrough} onToggled={v => set(u.id, { passthrough: v })} />
              </div>
            </div>
          ))}
        </div>

        <div style={{ width: 240, minWidth: 240, borderLeft: 'var(--border-dark)', background: 'var(--bg-strong)', overflow: 'auto' }}>
          <SectionBox sectionLabel="Plugins" isExpanded>
            <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
              {[['artnetplugin', 'ArtNet'], ['e131plugin', 'E1.31'], ['dmxusbplugin', 'DMX USB'], ['midiplugin', 'MIDI'], ['oscplugin', 'OSC'], ['hidplugin', 'HID'], ['olaplugin', 'OLA']].map(([ic, n]) => (
                <IconTextEntry key={n} iSrc={D.icon(ic)} tLabel={n} tFontSize={14} height={26} />
              ))}
            </div>
          </SectionBox>
          <SectionBox sectionLabel="Audio">
            <div style={{ padding: 6 }}><IconTextEntry iSrc={D.icon('audiocard')} tLabel="Default output" tFontSize={14} height={26} /></div>
          </SectionBox>
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { InputOutput });
