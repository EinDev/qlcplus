const { ViewToolbar, ToolbarSpacer, IconButton, GenericButton, RobotoText, TreeNode, SidePanel, SectionBox, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomTextInput, QLCPlusFader, IconTextEntry, ShortcutHint, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

/* --- icon mapping for server-provided types ------------------------------------------------ */
const FUNCTION_ICONS = {
  Scene: 'scene', Chaser: 'chaser', Sequence: 'sequence', Collection: 'collection', EFX: 'efx',
  RGBMatrix: 'rgbmatrix', Script: 'script', Show: 'showmanager', Audio: 'audio', Video: 'video'
};
const FIXTURE_TYPE_ICONS = {
  'Moving Head': 'movinghead', 'Scanner': 'scanner', 'Smoke': 'smoke', 'Hazer': 'hazer', 'Laser': 'laser',
  'Flower': 'flower', 'Strobe': 'strobe', 'Dimmer': 'dimmer', 'Effect': 'effect',
  'LED Bar (Beams)': 'ledbar_beams', 'LED Bar (Pixels)': 'ledbar_pixels', 'Color Changer': 'fixture'
};
const CHANNEL_GROUP_ICONS = {
  Intensity: 'dimmer', Pan: 'pan', Tilt: 'tilt', Gobo: 'gobo', Colour: 'colorwheel', Shutter: 'shutter',
  Prism: 'prism', Beam: 'beam', Speed: 'speed', Effect: 'effect', Maintenance: 'other', Nothing: 'other'
};
const COLOUR_ICONS = { Red: 'red', Green: 'green', Blue: 'blue', White: 'white', Amber: 'amber', UV: 'uv',
  Cyan: 'cyan', Magenta: 'magenta', Yellow: 'yellow', Lime: 'lime', Indigo: 'indigo' };
function channelIcon(ch) {
  const D = window.QLCData;
  if (ch.group === 'Intensity' && ch.colour && COLOUR_ICONS[ch.colour]) return D.icon(COLOUR_ICONS[ch.colour]);
  return D.icon(CHANNEL_GROUP_ICONS[ch.group] || 'other');
}
window.QLCIcons = { FUNCTION_ICONS, FIXTURE_TYPE_ICONS, CHANNEL_GROUP_ICONS, COLOUR_ICONS, channelIcon };

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

/** Re-run `load` whenever the connection comes up and whenever one of `topics` fires. */
function useLiveList(qlc, load, topics) {
  const [data, setData] = React.useState(null);
  React.useEffect(() => {
    if (!qlc.online) { setData(null); return; }
    let alive = true, timer = null;
    const refresh = () => load().then(d => { if (alive) setData(d); }).catch(() => {});
    /* Coalesce bursts (a multi-fixture patch fires one event per fixture). */
    const debounced = () => { clearTimeout(timer); timer = setTimeout(refresh, 150); };
    refresh();
    const offs = topics.map(t => qlc.subscribeTo(t, debounced));
    return () => { alive = false; clearTimeout(timer); offs.forEach(f => f()); };
  }, [qlc.online]);
  return data;
}

function byName(a, b) { return a.name.localeCompare(b.name, undefined, { numeric: true }); }

/** Fixtures grouped by universe, from fixtures.list + io.universe.list. */
function useFixtureTree(qlc) {
  const D = window.QLCData;
  const data = useLiveList(qlc, () => Promise.all([qlc.call('fixtures.list'), qlc.call('io.universe.list')])
    .then(([f, u]) => ({ fixtures: f.fixtures || [], universes: u.universes || [] })),
    ['fixtures.patched', 'fixtures.unpatched', 'fixtures.updated', 'io.universe.created', 'core.project.loaded']);
  return React.useMemo(() => {
    if (!data) return null;
    const perUniverse = {};
    data.fixtures.forEach(f => { (perUniverse[f.universe] = perUniverse[f.universe] || []).push(f); });
    const universes = data.universes.slice().sort((a, b) => a.id - b.id);
    Object.keys(perUniverse).forEach(id => { if (!universes.some(u => u.id === Number(id))) universes.push({ id: Number(id), name: 'Universe ' + (Number(id) + 1) }); });
    const tree = universes.filter(u => perUniverse[u.id]).map(u => ({
      id: 'u' + u.id, name: u.name, icon: D.icon('uniview'),
      children: perUniverse[u.id].slice().sort((a, b) => a.address - b.address).map(f => ({
        id: 'fx' + f.id, kind: 'fixture', fixtureId: f.id, name: f.name,
        icon: D.icon(FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture'), summary: f
      }))
    }));
    return { tree, fixtures: data.fixtures, universes: data.universes };
  }, [data]);
}

/** Functions as a folder tree from their `path` ("a/b/c"), from functions.list. */
function useFunctionTree(qlc) {
  const D = window.QLCData;
  const data = useLiveList(qlc, () => qlc.call('functions.list').then(r => r.functions || []),
    ['functions.created', 'functions.deleted', 'functions.renamed', 'functions.moved', 'functions.updated', 'core.project.loaded']);
  return React.useMemo(() => {
    if (!data) return null;
    const root = { children: [], folders: {} };
    const folderFor = (path) => {
      if (!path) return root;
      let node = root, acc = '';
      path.split('/').forEach(seg => {
        acc = acc ? acc + '/' + seg : seg;
        if (!node.folders[seg]) {
          node.folders[seg] = { id: 'dir:' + acc, name: seg, icon: D.icon('folder'), children: [], folders: {} };
          node.children.push(node.folders[seg]);
        }
        node = node.folders[seg];
      });
      return node;
    };
    const visible = data.filter(f => !f.hidden);
    visible.forEach(f => folderFor(f.path).children.push({
      id: 'fn' + f.id, kind: 'function', functionId: f.id, name: f.name, type: f.type,
      icon: D.icon(FUNCTION_ICONS[f.type] || 'functions'), summary: f
    }));
    const finish = (node) => {
      node.children.sort((a, b) => (!!b.children) - (!!a.children) || byName(a, b));
      node.children.forEach(c => { if (c.children) finish(c); delete c.folders; });
    };
    finish(root);
    return { tree: root.children, functions: visible, total: data.length };
  }, [data]);
}

function filterTree(nodes, needle) {
  if (!needle) return nodes;
  const q = needle.toLowerCase();
  const walk = (list) => list.map(n => {
    if (n.children) {
      const kids = walk(n.children);
      return kids.length ? Object.assign({}, n, { children: kids }) : null;
    }
    return n.name.toLowerCase().indexOf(q) !== -1 ? n : null;
  }).filter(Boolean);
  return walk(nodes);
}
function collectIds(nodes, out) { nodes.forEach(n => { if (n.children) { out.push(n.id); collectIds(n.children, out); } }); return out; }

/* --- detail panes -------------------------------------------------------------------------- */

function Row({ label, children, width = 90 }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 8, minHeight: 26 }}>
      <RobotoText label={label} fontSize={14} style={{ width, flex: 'none' }} labelColor="var(--fg-light)" />
      {typeof children === 'string' || typeof children === 'number'
        ? <RobotoText label={String(children)} fontSize={14} />
        : children}
    </div>
  );
}

function FixtureDetail({ node, qlc, universes }) {
  const D = window.QLCData;
  const [detail, setDetail] = React.useState(null);
  React.useEffect(() => {
    setDetail(null);
    if (!qlc.online) return;
    let alive = true;
    const load = () => qlc.call('fixtures.get', { fixtureId: node.fixtureId }).then(d => { if (alive) setDetail(d); }).catch(() => {});
    load();
    const off = qlc.subscribeTo('fixtures.updated', (d) => { if (d && d.fixture && d.fixture.id === node.fixtureId) load(); });
    return () => { alive = false; off(); };
  }, [node.fixtureId, qlc.online]);
  const f = detail || node.summary;
  const uni = (universes || []).find(u => u.id === f.universe);
  const groups = {};
  (detail && detail.channelList || []).forEach(ch => { groups[ch.group] = (groups[ch.group] || 0) + 1; });
  return (
    <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, alignContent: 'start' }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <RobotoText label="Addressing" fontBold fontSize={14} />
        <Row label="Universe">{uni ? uni.name : 'Universe ' + (f.universe + 1)}</Row>
        <Row label="Address">{(f.address + 1) + ' – ' + (f.address + f.channels)}</Row>
        <Row label="Channels">{f.channels}</Row>
        <RobotoText label="Definition" fontBold fontSize={14} style={{ marginTop: 8 }} />
        <Row label="Manufacturer">{f.manufacturer || '—'}</Row>
        <Row label="Model">{f.model || '—'}</Row>
        <Row label="Mode">{f.mode || '—'}</Row>
        <Row label="Type">{f.fixtureType || (f.isGeneric ? 'Generic' : '—')}</Row>
        <RobotoText label="Re-addressing and mode changes are not available in the web UI yet (fixtures.update exists server-side)."
          fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" style={{ marginTop: 8 }} />
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <RobotoText label={'Channels' + (detail ? ' (' + detail.channelList.length + ')' : '')} fontBold fontSize={14} />
        {!detail ? <RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /> : null}
        {detail ? (
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4, marginBottom: 6 }}>
            {Object.keys(groups).map(g => (
              <span key={g} style={{ display: 'inline-flex', alignItems: 'center', gap: 4, padding: '2px 6px', background: 'var(--bg-strong)', borderRadius: 3 }}>
                <img src={D.icon(CHANNEL_GROUP_ICONS[g] || 'other')} alt="" style={{ width: 14, height: 14 }} />
                <RobotoText label={g + ' ×' + groups[g]} fontSize={12} height={16} />
              </span>
            ))}
          </div>
        ) : null}
        {detail ? detail.channelList.map(ch => (
          <div key={ch.index} style={{ display: 'flex', alignItems: 'center', gap: 6, height: 24 }}>
            <RobotoText label={String(ch.index + 1)} fontSize={12} labelColor="var(--fg-medium)" textHAlign="right" style={{ width: 24 }} height={24} />
            <IconTextEntry iSrc={channelIcon(ch)} tLabel={ch.name} tFontSize={14} height={24} style={{ flex: 1 }} />
            <RobotoText label={'DMX ' + (ch.absoluteAddress - f.universe * 512 + 1)} fontSize={12} labelColor="var(--fg-medium)" height={24} />
          </div>
        )) : null}
      </div>
    </div>
  );
}

function FunctionDetail({ node, qlc }) {
  const [detail, setDetail] = React.useState(null);
  React.useEffect(() => {
    setDetail(null);
    if (!qlc.online) return;
    let alive = true;
    const load = () => qlc.call('functions.get', { functionId: node.functionId }).then(d => { if (alive) setDetail(d); }).catch(() => {});
    load();
    const isMine = (d) => d && String(d.functionId) === String(node.functionId);
    const offs = ['functions.updated', 'functions.scene.valuesChanged', 'functions.scene.membersChanged',
      'functions.chaser.stepsChanged', 'functions.sequence.stepsChanged'].map(t => qlc.subscribeTo(t, d => { if (isMine(d)) load(); }));
    return () => { alive = false; offs.forEach(f => f()); };
  }, [node.functionId, qlc.online]);
  const f = detail || node.summary;
  const td = (detail && detail.typeDetail) || {};
  const ms = (v) => (v === 0 || v == null ? '0 ms' : v >= 1000 ? (v / 1000).toFixed(2).replace(/\.?0+$/, '') + ' s' : Math.round(v) + ' ms');
  return (
    <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, alignContent: 'start' }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <RobotoText label="Function" fontBold fontSize={14} />
        <Row label="Type">{f.type}</Row>
        <Row label="Folder">{f.path || '/'}</Row>
        <Row label="ID">{f.id}</Row>
        {detail ? <>
          <RobotoText label="Speed" fontBold fontSize={14} style={{ marginTop: 8 }} />
          <Row label="Fade in">{ms(detail.fadeInSpeed)}</Row>
          <Row label="Fade out">{ms(detail.fadeOutSpeed)}</Row>
          <Row label="Duration">{ms(detail.duration)}</Row>
          <Row label="Tempo">{detail.tempoType}</Row>
          <Row label="Run order">{detail.runOrder}</Row>
          <Row label="Direction">{detail.direction}</Row>
          <Row label="Blend">{detail.blendMode}</Row>
        </> : <RobotoText label={qlc.online ? 'Loading…' : 'Connect to see the full definition'} fontSize={14} labelColor="var(--fg-medium)" />}
      </div>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <RobotoText label="Contents" fontBold fontSize={14} />
        {f.type === 'Scene' && detail ? <>
          <Row label="Fixtures">{(td.fixtures || []).length}</Row>
          <Row label="Values">{Object.keys(td.values || {}).length}</Row>
          <Row label="Palettes">{(td.palettes || []).length}</Row>
        </> : null}
        {(f.type === 'Chaser' || f.type === 'Sequence') && detail ? <>
          <Row label="Steps">{(td.steps || []).length}</Row>
          <Row label="Fade in mode">{td.fadeInMode}</Row>
          <Row label="Fade out mode">{td.fadeOutMode}</Row>
          <Row label="Duration mode">{td.durationMode}</Row>
          {f.type === 'Sequence' ? <Row label="Bound scene">{td.boundSceneId}</Row> : null}
        </> : null}
        {f.type === 'Collection' && detail ? <Row label="Functions">{(td.functions || td.members || []).length}</Row> : null}
        {detail && (detail.attributes || []).length ? <>
          <RobotoText label="Attributes" fontBold fontSize={14} style={{ marginTop: 8 }} />
          {detail.attributes.map(a => <Row key={a.index} label={a.name}>{String(a.value)}</Row>)}
        </> : null}
        <RobotoText label="Editing (scene values, steps, EFX…) is not available in the web UI yet — the server implements functions.scene.* and functions.steps.*, the editors are missing here."
          fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" style={{ marginTop: 8 }} />
      </div>
    </div>
  );
}

/* --- the screen ---------------------------------------------------------------------------- */

function FixturesFunctions() {
  const D = window.QLCData;
  const qlc = useQLC();
  const live = qlc.online;
  const fixtureTree = useFixtureTree(qlc);
  const functionTree = useFunctionTree(qlc);
  const palettes = useLiveList(qlc, () => qlc.call('palette.list').then(r => r.palettes || []), ['palette.created', 'palette.deleted', 'palette.updated']);

  const [selected, setSelected] = React.useState(null);
  const [detail, setDetail] = React.useState(null);
  const [expanded, setExpanded] = React.useState(['g-front', 'g-back', 'g-cyc', 'fn-scenes', 'fn-chasers', 'fn-fx']);
  const [panel, setPanel] = React.useState(true);
  const [dlg, setDlg] = React.useState(false);
  const [confirmDelete, setConfirmDelete] = React.useState(false);
  const [search, setSearch] = React.useState('');
  const [searching, setSearching] = React.useState(false);
  const [running, setRunning] = React.useState([]);
  const [dimmer, setDimmer] = React.useState(255);

  /* Live: one root per side; mock: the prototype's flat groups. */
  const fixturesRoot = live && fixtureTree
    ? [{ id: 'root-fixtures', name: 'Fixtures', icon: D.icon('fixture'), children: fixtureTree.tree }]
    : D.fixtures;
  const functionsRoot = live && functionTree
    ? [{ id: 'root-functions', name: 'Functions', icon: D.icon('functions'), children: functionTree.tree }]
    : D.functions;
  const shownFixtures = React.useMemo(() => filterTree(fixturesRoot, search), [fixturesRoot, search]);
  const shownFunctions = React.useMemo(() => filterTree(functionsRoot, search), [functionsRoot, search]);
  /* While searching, every folder that survived the filter is shown open. */
  const effectiveExpanded = search ? collectIds(shownFixtures.concat(shownFunctions), []) : expanded;

  React.useEffect(() => {
    if (live) { setExpanded(['root-fixtures', 'root-functions']); setSelected(null); setDetail(null); setRunning([]); }
    else { setExpanded(['g-front', 'g-back', 'g-cyc', 'fn-scenes', 'fn-chasers', 'fn-fx']); setSelected('f1'); setDetail(D.fixtures[0].children[0]); }
  }, [live]);
  /* Forget a selection whose node vanished (deleted elsewhere). */
  React.useEffect(() => {
    if (!live || !detail) return;
    const all = [];
    const walk = (ns) => ns.forEach(n => { all.push(n.id); if (n.children) walk(n.children); });
    walk(fixturesRoot); walk(functionsRoot);
    if (all.indexOf(detail.id) === -1) { setDetail(null); setSelected(null); }
  }, [fixturesRoot, functionsRoot]);

  const toggle = (id) => setExpanded(p => p.indexOf(id) === -1 ? p.concat([id]) : p.filter(x => x !== id));
  /* Clicking a folder row toggles it (like qmlui's tree); clicking a leaf shows its detail. */
  const pick = (id, node) => { setSelected(id); if (node.children) toggle(id); else setDetail(node); };

  const isFunction = !!(detail && (detail.kind === 'function' || detail.type));
  const isFixture = !!(detail && (detail.kind === 'fixture' || detail.address));
  const detailId = detail ? (detail.functionId || detail.fixtureId || detail.id) : null;
  const isRunning = detail ? running.indexOf(detailId) !== -1 : false;
  const toggleRun = () => {
    if (!detail) return;
    if (isRunning) { if (live) qlc.call('functions.stop', { functionId: String(detailId) }).catch(() => {}); setRunning(p => p.filter(x => x !== detailId)); }
    else { if (live) qlc.call('functions.start', { functionId: String(detailId) }).catch(() => {}); setRunning(p => p.concat([detailId])); }
  };
  const rename = (name) => {
    if (!detail || !live || !name || name === detail.name) return;
    const rev = qlc.docRevision();
    if (isFunction) qlc.call('functions.rename', { functionId: String(detail.functionId), name, baseRevision: rev }).catch(() => {});
    else if (isFixture) qlc.call('fixtures.update', { fixtureId: String(detail.fixtureId), name, baseRevision: rev }).catch(() => {});
  };
  const doDelete = () => {
    setConfirmDelete(false);
    if (!detail || !live) return;
    const rev = qlc.docRevision();
    if (isFunction) qlc.call('functions.delete', { functionId: String(detail.functionId), baseRevision: rev }).catch(() => {});
    else if (isFixture) qlc.call('fixtures.unpatch', { fixtureId: String(detail.fixtureId), baseRevision: rev }).catch(() => {});
  };

  const summary = live
    ? (fixtureTree && functionTree
      ? fixtureTree.fixtures.length + ' fixtures · ' + fixtureTree.fixtures.reduce((s, f) => s + f.channels, 0) + ' channels · '
        + fixtureTree.universes.length + ' universes · ' + functionTree.functions.length + ' functions'
      : 'Loading workspace…')
    : '8 fixtures · 126 channels · 2 universes (mock)';

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0 }}>
      <ViewToolbar variant="sub">
        <ShortcutHint keys="A" placement="corner">
          <IconButton imgSource={D.icon('add')} size={26} onClick={() => setDlg(true)} disabled={live}
            tooltip={live ? 'Add fixture — not available: the server has no fixture-definition browsing (fixtures.defs.*) yet' : 'Add fixture (mock dialog)'} />
        </ShortcutHint>
        <IconButton imgSource={D.icon('group')} size={26} disabled tooltip="Add fixture group — not available in the web UI yet" />
        <IconButton imgSource={D.icon('remap')} size={26} disabled tooltip="Remap addresses — not available in the web UI yet" />
        <IconButton faSource="fa_trash_can" size={26} disabled={!live || !detail || !(isFunction || isFixture)}
          tooltip={live ? 'Delete selected function / unpatch selected fixture' : 'Delete — connect first'}
          onClick={() => setConfirmDelete(true)} />
        <ToolbarSpacer />
        <RobotoText label={summary} fontSize={14} labelColor={live ? 'var(--check-lime)' : 'var(--fg-light)'} />
        {searching ? (
          <span style={{ width: 180, height: 26, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px' }}>
            <CustomTextInput text={search} editing autoFocus placeholder="Search…" width="100%" height={22}
              onTextConfirmed={setSearch} onKeyDown={(e) => { if (e.key === 'Escape') { setSearch(''); setSearching(false); } else setTimeout(() => setSearch(e.target.value), 0); }} />
          </span>
        ) : null}
        <IconButton imgSource={D.icon('search')} size={26} tooltip="Search fixtures and functions" checked={searching}
          onClick={() => { if (searching) setSearch(''); setSearching(!searching); }} />
      </ViewToolbar>

      <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
        <div style={{ width: 280, minWidth: 280, background: 'var(--bg-stronger)', borderRight: 'var(--border-dark)', overflow: 'auto' }}>
          {shownFixtures.map(n => <TreeBranch key={n.id} node={n} selected={selected} onSelect={pick} expanded={effectiveExpanded} onToggle={toggle} />)}
          <div style={{ height: 1, background: 'var(--border-color-dark)', margin: '4px 0' }} />
          {shownFunctions.map(n => <TreeBranch key={n.id} node={n} selected={selected} onSelect={pick} expanded={effectiveExpanded} onToggle={toggle} />)}
          {live && !(fixtureTree && functionTree) ? <div style={{ padding: 8 }}><RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
        </div>

        <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', background: 'var(--bg-medium)' }}>
          {detail ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, height: 38, padding: '0 10px', background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)' }}>
              <img src={detail.icon} alt="" style={{ width: 24, height: 24 }} />
              <CustomTextInput key={detail.id} text={detail.name} allowDoubleClick width={300} onTextConfirmed={rename} />
              <ToolbarSpacer />
              {isFunction ? (
                <IconButton faSource={isRunning ? 'fa_pause' : 'fa_play'} size={26}
                  faColor={isRunning ? 'var(--override-red)' : 'var(--check-lime)'}
                  checked={isRunning} onClick={toggleRun}
                  tooltip={(isRunning ? 'Stop function' : 'Start function') + (live ? ' (running state is not reported by the server — shown as last sent)' : ' (mock)')} />
              ) : null}
              <RobotoText label={detail.mode || detail.type || (detail.summary && detail.summary.fixtureType) || ''} fontSize={14} labelColor="var(--fg-light)" />
            </div>
          ) : null}

          {!detail ? (
            <div style={{ padding: 20 }}>
              <RobotoText label={live ? 'Select a fixture or function in the tree.' : 'Select a fixture or function.'} fontSize={14} labelColor="var(--fg-medium)" />
            </div>
          ) : live && detail.kind === 'fixture' ? (
            <FixtureDetail node={detail} qlc={qlc} universes={fixtureTree ? fixtureTree.universes : []} />
          ) : live && detail.kind === 'function' ? (
            <FunctionDetail node={detail} qlc={qlc} />
          ) : (
            <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, alignContent: 'start' }}>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                <RobotoText label="Addressing (mock)" fontBold fontSize={14} />
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
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                <RobotoText label="Capabilities (mock)" fontBold fontSize={14} />
                {['dimmer', 'color', 'position', 'gobo', 'beam'].map(k => (
                  <IconTextEntry key={k} iSrc={D.icon(k)} tLabel={k[0].toUpperCase() + k.slice(1)} tFontSize={14} height={26} />
                ))}
              </div>
              <div style={{ gridColumn: '1 / -1', display: 'flex', gap: 16, alignItems: 'flex-start', paddingTop: 4, borderTop: 'var(--border-dark)' }}>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 4 }}>
                  <QLCPlusFader value={dimmer} onMoved={setDimmer} height={140} />
                  <RobotoText label={'Dimmer ' + dimmer} fontSize={14} labelColor="var(--fg-light)" textHAlign="center" style={{ width: 80 }} />
                </div>
              </div>
            </div>
          )}
        </div>

        <SidePanel isOpen={panel} alignment="right" rail={
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
            <IconButton imgSource={D.icon('palette')} checked={panel} onClick={() => setPanel(!panel)} tooltip="Palettes" />
            <IconButton imgSource={D.icon('fixture-editor')} disabled tooltip="Fixture editor — not available in the web UI" />
            <IconButton imgSource={D.icon('uniview')} disabled tooltip="Universe view — not available in the web UI" />
          </div>}>
          <div style={{ padding: 0 }}>
            {live ? (
              <SectionBox sectionLabel={'Palettes' + (palettes ? ' (' + palettes.length + ')' : '')} isExpanded>
                <div style={{ padding: 6, display: 'flex', flexDirection: 'column', gap: 2 }}>
                  {palettes && palettes.length ? palettes.map(p => (
                    <IconTextEntry key={p.id} iSrc={D.icon(p.type === 'Color' ? 'color' : p.type === 'Position' || p.type === 'Pan' || p.type === 'Tilt' ? 'position' : p.type === 'Dimmer' ? 'dimmer' : 'palette')}
                      tLabel={p.name + ' (' + p.type + ')'} tFontSize={14} height={26} />
                  )) : <RobotoText label={palettes ? 'No palettes in this workspace' : 'Loading…'} fontSize={14} labelColor="var(--fg-medium)" />}
                  <RobotoText label="Applying a palette to fixtures is not available: the API has no palette-apply / live fixture-control method yet."
                    fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" style={{ marginTop: 6 }} />
                </div>
              </SectionBox>
            ) : (
              <SectionBox sectionLabel="Colour (mock)" isExpanded>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4,1fr)', gap: 4, padding: 6 }}>
                  {['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'amber', 'white', 'uv', 'lime', 'indigo', 'colorwheel'].map(c => (
                    <img key={c} src={D.icon(c)} alt={c} title={c} style={{ width: '100%', aspectRatio: 1, cursor: 'pointer' }} />
                  ))}
                </div>
              </SectionBox>
            )}
          </div>
        </SidePanel>
      </div>

      <CustomPopupDialog open={dlg} title="Add fixture (mock)" width={360}
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

      <CustomPopupDialog open={confirmDelete} title={isFunction ? 'Delete function' : 'Unpatch fixture'} width={380}
        message={detail ? (isFunction ? 'Delete "' + detail.name + '" from the workspace? This cannot be undone from the web UI.' : 'Remove "' + detail.name + '" from the patch? Functions referencing it lose those channels.') : ''}
        standardButtons={['Cancel', 'Delete']}
        onClicked={(b) => { if (b === 'Delete') doDelete(); else setConfirmDelete(false); }} onClose={() => setConfirmDelete(false)} />
    </div>
  );
}
Object.assign(window, { FixturesFunctions, TreeBranch });
