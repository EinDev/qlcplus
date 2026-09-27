/**
 * FunctionActions.jsx — the function-side actions of the Fixtures & Functions screen that are not
 * part of one editor, modelled on qmlui/qml/fixturesfunctions/RightPanel.qml, UsageList.qml and
 * popup/PopupRenameItems.qml:
 *
 *  - detail header (window.QLCFunctionHeaderItems): an Intensity slider for the function's
 *    Intensity attribute (functions.adjustAttribute), the autostart toggle
 *    (core.project.setStartupFunction), Usage and Clone buttons.
 *  - tree context menu (window.QLCFFMenuItems): Clone (every selected function), Usage…,
 *    Set / unset autostart, Select fixtures in function(s), Rename with numbering… (functions or
 *    fixtures, when several are selected).
 *  - dialogs (window.QLCFFOverlays): the usage list and the rename-with-numbering popup, opened
 *    through the FF local bus ('ff.usage', 'ff.renameNumbered').
 *
 * "Function Preview" in the Qt editors is Function::start/stop with a different parent
 * (FunctionEditor::setPreviewEnabled) — the header Start/Stop button is its parity here.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomPopupDialog, CustomSlider, CustomSpinBox } = window.PatchDesignSystem_5432c9;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' };

  /* ---- shared state ---------------------------------------------------------------------------- */
  /** core.project.get startupFunctionId, kept current by core.project.startupFunctionChanged. */
  function useStartupFunction(qlc) {
    const [id, setId] = React.useState(null);
    React.useEffect(() => {
      if (!qlc.online) { setId(null); return undefined; }
      let alive = true;
      const load = () => qlc.call('core.project.get').then(p => { if (alive) setId(p && p.startupFunctionId != null ? String(p.startupFunctionId) : null); }).catch(() => {});
      load();
      const offs = [qlc.subscribeTo('core.project.startupFunctionChanged', d => setId(d && d.startupFunctionId != null ? String(d.startupFunctionId) : null)),
        qlc.subscribeTo('core.project.loaded', load), qlc.subscribeTo('core.history.changed', load)];
      return () => { alive = false; offs.forEach(f => f()); };
    }, [qlc.online]);
    return id;
  }
  const setStartup = (qlc, functionId) => FF.mutate(qlc, 'core.project.setStartupFunction', { functionId: functionId == null ? null : String(functionId) }).catch(() => {});
  const cloneFunctions = (qlc, ids) => ids.length ? FF.mutate(qlc, 'functions.clone', { functionIds: ids.map(String) }).catch(() => {}) : null;

  /** Fixture ids a function acts on (Scene members, Sequence step channels, EFX heads, RGB Matrix group). */
  async function fixturesOf(qlc, functionId) {
    const d = await qlc.call('functions.get', { functionId: String(functionId) });
    const td = (d && d.typeDetail) || {};
    const ids = new Set();
    (td.fixtures || []).forEach(f => ids.add(String(typeof f === 'object' ? (f.fixture != null ? f.fixture : f.fixtureId) : f)));
    Object.keys(td.values || {}).forEach(k => ids.add(k.split('.')[0]));
    (td.steps || []).forEach(s => Object.keys(s.values || {}).forEach(k => ids.add(k.split('.')[0])));
    if (d.type === 'Sequence' && td.boundSceneId) (await fixturesOf(qlc, td.boundSceneId)).forEach(i => ids.add(i));
    const gid = td.config && td.config.fixtureGroupId;
    if (gid != null) {
      const g = await qlc.call('fixtures.group.get', { groupId: String(gid) }).catch(() => null);
      ((g && g.heads) || []).forEach(h => ids.add(String(h.fixtureId)));
    }
    return Array.from(ids);
  }

  /* ---- header items ------------------------------------------------------------------------------ */
  /** The Intensity attribute of the open function (functions.adjustAttribute), 0-100 %. */
  function IntensityControl({ qlc, node }) {
    const fid = String(node.functionId);
    const [value, setValue] = React.useState(null);
    React.useEffect(() => {
      let alive = true;
      setValue(null);
      qlc.call('functions.get', { functionId: fid }).then(d => {
        const a = ((d && d.attributes) || []).find(x => x.name === 'Intensity');
        if (alive && a) setValue(a.value);
      }).catch(() => {});
      const off = qlc.subscribeTo('functions.attributeChanged', d => { if (d && String(d.functionId) === fid && d.attributeName === 'Intensity') setValue(d.value); });
      return () => { alive = false; off(); };
    }, [fid, qlc.online]);
    if (value == null) return null;
    const pct = Math.round(value * 100);
    const send = (p) => { setValue(p / 100); FF.live('adjust:' + fid, () => qlc.call('functions.adjustAttribute', { functionId: fid, attributeName: 'Intensity', value: p / 100 }).catch(e => FF.reportError(e, 'functions.adjustAttribute'))); };
    return (
      <span data-e2e="fn-intensity" title="Function intensity (the Intensity attribute; scales the function's output while it runs)" style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
        <img src={window.QLCData.icon('intensity')} alt="" style={{ width: 18, height: 18 }} />
        <CustomSlider value={pct} from={0} to={100} length={110} onMoved={send} />
        <RobotoText label={pct + '%'} fontSize={13} labelColor="var(--fg-light)" style={{ width: 36 }} />
      </span>
    );
  }

  function FunctionHeaderActions({ qlc, node }) {
    const startup = useStartupFunction(qlc);
    const fid = String(node.functionId);
    const isStartup = startup === fid;
    return (
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 4 }}>
        <IntensityControl qlc={qlc} node={node} />
        <IconButton imgSource={window.QLCData.icon('autostart')} size={26} checked={isStartup} data-e2e="fn-autostart"
          tooltip={isStartup ? 'This function starts automatically in Operate mode — click to unset' : 'Set as the autostart function (started when the project enters Operate mode)'}
          onClick={() => setStartup(qlc, isStartup ? null : fid)} />
        <IconButton faSource={FF.GLYPH.eye} size={26} tooltip="Usage: functions and widgets using this function" data-e2e="fn-usage"
          onClick={() => FF.notifyLocal('ff.usage', { functionId: fid, name: node.name })} />
        <IconButton faSource={FF.GLYPH.clone} size={26} tooltip="Clone this function" data-e2e="fn-clone" onClick={() => cloneFunctions(qlc, [fid])} />
      </span>
    );
  }

  /* ---- context menu items ------------------------------------------------------------------------- */
  let startupCache = null; /* the menu is a plain function, not a component: read the last known value */
  function menuItems({ qlc, node, selectedNodes, selectFixtures, close }) {
    const sel = (selectedNodes || []).filter(n => n.kind === node.kind);
    const many = sel.length > 1 ? sel : [node];
    const items = [];
    if (node.kind === 'function') {
      const ids = many.map(n => String(n.functionId));
      const isStartup = startupCache === String(node.functionId);
      items.push({ glyph: FF.GLYPH.clone, text: ids.length > 1 ? 'Clone ' + ids.length + ' functions' : 'Clone', onClick: () => { close(); cloneFunctions(qlc, ids); } });
      items.push({ glyph: FF.GLYPH.eye, text: 'Usage…', onClick: () => { close(); FF.notifyLocal('ff.usage', { functionId: String(node.functionId), name: node.name }); } });
      items.push({ icon: 'autostart', text: isStartup ? 'Unset autostart' : 'Set as autostart', onClick: () => { close(); setStartup(qlc, isStartup ? null : node.functionId); } });
      items.push({ glyph: FF.GLYPH.crosshairs, text: ids.length > 1 ? 'Select fixtures in ' + ids.length + ' functions' : 'Select fixtures in function', onClick: () => {
        close();
        Promise.all(ids.map(id => fixturesOf(qlc, id).catch(() => []))).then(lists => selectFixtures(Array.from(new Set([].concat.apply([], lists)))));
      } });
    }
    if (many.length > 1 && (node.kind === 'function' || node.kind === 'fixture'))
      items.push({ icon: 'rename', text: 'Rename ' + many.length + ' with numbering…', onClick: () => { close(); FF.notifyLocal('ff.renameNumbered', { nodes: many }); } });
    return items;
  }

  /* ---- dialogs ---------------------------------------------------------------------------------- */
  function UsageDialog({ qlc, onOpenWidget }) {
    const [target, setTarget] = React.useState(null);
    const [usage, setUsage] = React.useState(null);
    const [error, setError] = React.useState('');
    FF.useLocalEvents('ff.usage', (d) => { setTarget(d); setUsage(null); setError(''); });
    React.useEffect(() => {
      if (!target) return;
      qlc.call('functions.usage', { functionId: target.functionId }).then(setUsage).catch(e => setError((e && e.message) || 'functions.usage failed'));
    }, [target]);
    const D = window.QLCData, Icons = window.QLCIcons;
    const row = (key, icon, label, hint) => (
      <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 6px' }}>
        <img src={D.icon(icon)} alt="" style={{ width: 18, height: 18 }} />
        <RobotoText label={label} fontSize={14} height={26} style={{ flex: 1 }} />
        {hint ? <RobotoText label={hint} fontSize={12} height={26} labelColor="var(--fg-light)" /> : null}
      </div>
    );
    const W_ICON = { Button: 'button', Slider: 'slider', Knob: 'knob', CueList: 'cuelist', Frame: 'frame', SoloFrame: 'soloframe', Speed: 'speed', XYPad: 'xypad', Clock: 'clock', AudioTriggers: 'audiotriggers', Animation: 'animation' };
    const none = usage && !usage.functions.length && !usage.widgets.length;
    return (
      <CustomPopupDialog open={!!target} title={target ? 'Usage of "' + target.name + '"' : ''} width={440} standardButtons={['Close']}
        onClicked={() => setTarget(null)} onClose={() => setTarget(null)}>
        <div data-e2e="usage-list" style={{ display: 'flex', flexDirection: 'column', gap: 4, maxHeight: 360, overflow: 'auto' }}>
          {error ? <RobotoText label={error} fontSize={14} labelColor="var(--override-red)" /> : null}
          {!usage && !error ? <RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /> : null}
          {usage && usage.isStartupFunction ? row('startup', 'autostart', 'Project autostart function', '') : null}
          {usage && usage.functions.length ? <RobotoText label="Functions" fontBold fontSize={13} height={20} /> : null}
          {usage ? usage.functions.map((f, i) => row('f' + i, (Icons.FUNCTION_ICONS[f.type] || 'functions'), f.name, (f.type === 'Collection' ? 'member ' : f.type === 'Show' ? 'track ' : 'step ') + (f.position + 1))) : null}
          {usage && usage.widgets.length ? <RobotoText label="Virtual Console widgets" fontBold fontSize={13} height={20} /> : null}
          {usage ? usage.widgets.map((w, i) => row('w' + i, W_ICON[w.widgetType] || 'virtualconsole', w.caption || (w.widgetType + ' ' + w.id), w.widgetType + ' #' + w.id)) : null}
          {none ? <RobotoText label="<None> — nothing uses this function." fontSize={14} labelColor="var(--fg-medium)" /> : null}
          {usage && !usage.vcAvailable ? <FF.Note text="This server has no Virtual Console host, widgets are not listed." /> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  /** PopupRenameItems.qml: new base name + start number + digits, applied to the selection in order. */
  function RenameNumberedDialog({ qlc }) {
    const [nodes, setNodes] = React.useState(null);
    const [base, setBase] = React.useState('');
    const [start, setStart] = React.useState(1);
    const [digits, setDigits] = React.useState(1);
    FF.useLocalEvents('ff.renameNumbered', (d) => {
      const list = (d && d.nodes) || [];
      setNodes(list);
      setBase(list.length ? list[0].name.replace(/\s*\d+$/, '') : '');
      setStart(1); setDigits(1);
    });
    const nameFor = (i) => base + ' ' + String(start + i).padStart(digits, '0');
    const apply = () => {
      const list = nodes || [];
      setNodes(null);
      if (!base.trim()) return;
      FF.mutateSeq(qlc, list.map((n, i) => n.kind === 'function'
        ? ['functions.rename', { functionId: String(n.functionId), name: nameFor(i) }]
        : ['fixtures.update', { fixtureId: String(n.fixtureId), name: nameFor(i) }])).catch(() => {});
    };
    return (
      <CustomPopupDialog open={!!nodes} title={nodes ? 'Rename ' + nodes.length + ' items' : ''} width={420} standardButtons={['Cancel', 'Rename']}
        onClicked={(b) => { if (b === 'Rename') apply(); else setNodes(null); }} onClose={() => setNodes(null)}>
        <div data-e2e="rename-numbered" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          <FF.Row label="New name" width={100}>
            <input autoFocus value={base} onChange={e => setBase(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') apply(); }} style={Object.assign({ width: 240 }, inputStyle)} data-e2e="rename-base" />
          </FF.Row>
          <FF.Row label="Start number" width={100}><CustomSpinBox value={start} from={0} to={99999} width={100} height={24} onValueModified={setStart} /></FF.Row>
          <FF.Row label="Digits" width={100}><CustomSpinBox value={digits} from={1} to={6} width={100} height={24} onValueModified={setDigits} /></FF.Row>
          <RobotoText label="Preview" fontBold fontSize={13} height={20} />
          <div style={{ maxHeight: 160, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)', padding: '2px 6px' }}>
            {(nodes || []).slice(0, 50).map((n, i) => (
              <div key={n.id} style={{ display: 'flex', gap: 8 }}>
                <RobotoText label={n.name} fontSize={13} height={20} labelColor="var(--fg-light)" style={{ width: 170 }} />
                <RobotoText label={'→ ' + nameFor(i)} fontSize={13} height={20} />
              </div>
            ))}
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  function FunctionActionsOverlays({ qlc }) {
    /* keep the context menu's autostart label current */
    const startup = useStartupFunction(qlc);
    React.useEffect(() => { startupCache = startup; }, [startup]);
    return <><UsageDialog qlc={qlc} /><RenameNumberedDialog qlc={qlc} /></>;
  }

  window.QLCFunctionHeaderItems = (window.QLCFunctionHeaderItems || []).concat([FunctionHeaderActions]);
  window.QLCFFMenuItems = (window.QLCFFMenuItems || []).concat([menuItems]);
  window.QLCFFOverlays = (window.QLCFFOverlays || []).concat([FunctionActionsOverlays]);
  Object.assign(FF, { fixturesOf, useStartupFunction });
})();
