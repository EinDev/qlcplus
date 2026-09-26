const { ViewToolbar, ToolbarSpacer, IconButton, GenericButton, RobotoText, TreeNode, SidePanel, SectionBox, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomTextInput, QLCPlusFader, IconTextEntry, ShortcutHint, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

function TreeBranch({ node, selected, onSelect, expanded, onToggle, depth = 0 }) {
  const isOpen = expanded.indexOf(node.id) !== -1;
  return (
    <TreeNode textLabel={node.name} itemIcon={node.icon} depth={depth}
      hasChildren={!!node.children} isExpanded={isOpen} onToggle={() => onToggle(node.id)}
      isSelected={selected === node.id} onSelect={() => onSelect(node.id, node)}>
      {isOpen && node.children ? node.children.map(c => (
        <TreeBranch key={c.id} node={c} selected={selected} onSelect={onSelect}
          expanded={expanded} onToggle={onToggle} depth={depth + 1} />
      )) : null}
    </TreeNode>
  );
}

function FixturesFunctions() {
  const D = window.QLCData;
  const qlc = useQLC();
  const [selected, setSelected] = React.useState('f1');
  const [detail, setDetail] = React.useState(D.fixtures[0].children[0]);
  const [expanded, setExpanded] = React.useState(['g-front', 'g-back', 'g-cyc', 'fn-scenes', 'fn-chasers', 'fn-fx']);
  const [panel, setPanel] = React.useState(true);
  const [dlg, setDlg] = React.useState(false);
  const [dimmer, setDimmer] = React.useState(255);
  const [running, setRunning] = React.useState([]);
  const isFunction = !!detail.type;
  const isRunning = running.indexOf(detail.id) !== -1;
  const toggleRun = () => {
    if (isRunning) { qlc.stopFunction(detail.id); setRunning(p => p.filter(x => x !== detail.id)); }
    else { qlc.startFunction(detail.id); setRunning(p => p.concat([detail.id])); }
  };
  const toggle = (id) => setExpanded(p => p.indexOf(id) === -1 ? p.concat([id]) : p.filter(x => x !== id));
  const pick = (id, node) => { setSelected(id); if (!node.children) setDetail(node); };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <ShortcutHint keys="A" placement="corner"><IconButton imgSource={D.icon('add')} size={26} tooltip="Add fixture" onClick={() => setDlg(true)} /></ShortcutHint>
        <IconButton imgSource={D.icon('group')} size={26} tooltip="Add fixture group" />
        <IconButton imgSource={D.icon('remap')} size={26} tooltip="Remap addresses" />
        <IconButton faSource="fa_trash_can" size={26} tooltip="Delete selected" />
        <ToolbarSpacer />
        <RobotoText label={qlc.online ? 'Live — connected to ' + qlc.host : '8 fixtures · 126 channels · 2 universes'}
          fontSize={14} labelColor={qlc.online ? 'var(--check-lime)' : 'var(--fg-light)'} />
        <IconButton imgSource={D.icon('search')} size={26} tooltip="Search" />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ width: 260, minWidth: 260, background: 'var(--bg-stronger)', borderRight: 'var(--border-dark)', overflow: 'auto' }}>
          {D.fixtures.map(n => <TreeBranch key={n.id} node={n} selected={selected} onSelect={pick} expanded={expanded} onToggle={toggle} />)}
          <div style={{ height: 1, background: 'var(--border-color-dark)', margin: '4px 0' }} />
          {D.functions.map(n => <TreeBranch key={n.id} node={n} selected={selected} onSelect={pick} expanded={expanded} onToggle={toggle} />)}
        </div>

        <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', background: 'var(--bg-medium)' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, height: 38, padding: '0 10px', background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)' }}>
            <img src={detail.icon} alt="" style={{ width: 24, height: 24 }} />
            <CustomTextInput text={detail.name} allowDoubleClick width={240} />
            <ToolbarSpacer />
            {isFunction ? (
              <IconButton faSource={isRunning ? 'fa_pause' : 'fa_play'} size={26}
                faColor={isRunning ? 'var(--override-red)' : 'var(--check-lime)'}
                checked={isRunning} onClick={toggleRun}
                tooltip={isRunning ? 'Stop function' : 'Start function'} />
            ) : null}
            <RobotoText label={detail.mode || detail.type || ''} fontSize={14} labelColor="var(--fg-light)" />
          </div>

          <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, alignContent: 'start' }}>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              <RobotoText label="Addressing" fontBold fontSize={14} />
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <RobotoText label="Universe" fontSize={14} style={{ width: 90 }} />
                <CustomComboBox width={170} currValue={1} model={D.universes.map(u => ({ mLabel: u.name, mValue: u.id }))} />
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <RobotoText label="Address" fontSize={14} style={{ width: 90 }} />
                <CustomSpinBox value={Number((detail.address || '1.001').split('.')[1])} from={1} to={512} width={110} />
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <RobotoText label="Channels" fontSize={14} style={{ width: 90 }} />
                <CustomSpinBox value={detail.channels || 8} from={1} to={512} width={110} />
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <CustomCheckBox checked />
                <RobotoText label="Add to Front Truss group" fontSize={14} />
              </div>
            </div>

            <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              <RobotoText label="Capabilities" fontBold fontSize={14} />
              {['dimmer', 'color', 'position', 'gobo', 'beam'].map(k => (
                <IconTextEntry key={k} iSrc={D.icon(k)} tLabel={k[0].toUpperCase() + k.slice(1)} tFontSize={14} height={26} />
              ))}
            </div>

            <div style={{ gridColumn: '1 / -1', display: 'flex', gap: 16, alignItems: 'flex-start', paddingTop: 4, borderTop: 'var(--border-dark)' }}>
              <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }}>
                <QLCPlusFader value={dimmer} onMoved={setDimmer} height={140} />
                <RobotoText label={'Dimmer ' + dimmer} fontSize={14} labelColor="var(--fg-light)" textHAlign="center" style={{ width: 80 }} />
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                <GenericButton label="Create scene from selection" iconSource={D.icon('scene')} width={250} />
                <GenericButton label="Create chaser" iconSource={D.icon('chaser')} width={250} />
                <GenericButton label="Create RGB matrix" iconSource={D.icon('rgbmatrix')} width={250} />
              </div>
            </div>
          </div>
        </div>

        <SidePanel isOpen={panel} alignment="right" rail={
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
            <IconButton imgSource={D.icon('palette')} checked={panel} onClick={() => setPanel(!panel)} tooltip="Palettes" />
            <IconButton imgSource={D.icon('fixture-editor')} tooltip="Fixture editor" />
            <IconButton imgSource={D.icon('uniview')} tooltip="Universe view" />
          </div>}>
          <div style={{ padding: 0 }}>
            <SectionBox sectionLabel="Colour" isExpanded>
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4,1fr)', gap: 4, padding: 6 }}>
                {['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'amber', 'white', 'uv', 'lime', 'indigo', 'colorwheel'].map(c => (
                  <img key={c} src={D.icon(c)} alt={c} title={c} style={{ width: '100%', aspectRatio: 1, cursor: 'pointer' }} />
                ))}
              </div>
            </SectionBox>
            <SectionBox sectionLabel="Position">
              <div style={{ padding: 6 }}><RobotoText label="No position palettes" fontSize={14} labelColor="var(--fg-medium)" /></div>
            </SectionBox>
            <SectionBox sectionLabel="Gobo">
              <div style={{ padding: 6 }}><RobotoText label="No gobo palettes" fontSize={14} labelColor="var(--fg-medium)" /></div>
            </SectionBox>
          </div>
        </SidePanel>
      </div>

      <CustomPopupDialog open={dlg} title="Add fixture" width={360}
        standardButtons={['Cancel', 'OK']} onClicked={() => setDlg(false)} onClose={() => setDlg(false)}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Manufacturer" fontSize={14} style={{ width: 100 }} />
            <CustomComboBox width={200} currValue={0} model={['Robe', 'Martin', 'Chauvet', 'Generic']} />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Model" fontSize={14} style={{ width: 100 }} />
            <CustomComboBox width={200} currValue={0} model={['Pointe', 'Spikie', 'MegaPointe']} />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Quantity" fontSize={14} style={{ width: 100 }} />
            <CustomSpinBox value={4} from={1} to={64} width={90} />
          </div>
        </div>
      </CustomPopupDialog>
    </div>
  );
}
Object.assign(window, { FixturesFunctions, TreeBranch });
