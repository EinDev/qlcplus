const { ViewToolbar, ToolbarSpacer, IconButton, GenericButton, RobotoText, TreeNode, SidePanel, SectionBox, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomTextInput, QLCPlusFader, IconTextEntry, ShortcutHint, CustomPopupDialog, FaIcon } = window.PatchDesignSystem_5432c9;

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

/* The editors/tools live in webui/ff/*.jsx, which Babel compiles *after* this file — resolve them
   at render time, never at file scope. */
function ff(name) { return (window.FF && window.FF[name]) || null; }

/* Function editor registry. `window.QLCEditors[<Function type as the server spells it>]` wins over
   the built-in editors below (Scene / Chaser / Sequence / Collection live in ff/FunctionEditors.jsx).
   A new editor registers itself from its own file, loaded after this one in index.html:
     window.QLCEditors = Object.assign(window.QLCEditors || {}, { EFX: EfxEditor });
   Props received: qlc, detail (functions.get result), reload, setDetail, functions, fixtures,
   selectedFixtureIds, palettes, onSelectFixtures. */
function editorFor(type) {
  const reg = window.QLCEditors || {};
  if (reg[type]) return reg[type];
  if (type === 'Scene') return ff('SceneEditor');
  if (type === 'Chaser' || type === 'Sequence') return ff('ChaserEditor');
  if (type === 'Collection') return ff('CollectionEditor');
  return null;
}

function TreeBranch({ node, selected, onSelect, expanded, onToggle, depth = 0, decorate, checkable, onContextMenu }) {
  const isOpen = expanded.indexOf(node.id) !== -1;
  const isSel = selected.indexOf(node.id) !== -1;
  const label = decorate ? decorate(node) : node.name;
  return (
    <TreeNode textLabel={label} itemIcon={node.icon} depth={depth}
      hasChildren={!!node.children} isExpanded={isOpen} onToggle={() => onToggle(node.id)}
      isSelected={isSel} onSelect={(e) => onSelect(node.id, node, e)}
      isCheckable={checkable && !node.children} isChecked={isSel} onCheck={() => onSelect(node.id, node, { ctrlKey: true })}
      onContextMenu={(e) => { if (onContextMenu) { e.preventDefault(); e.stopPropagation(); onContextMenu(node, e); } }}>
      {isOpen && node.children ? node.children.map(c => (
        <TreeBranch key={c.id} node={c} selected={selected} onSelect={onSelect} expanded={expanded} onToggle={onToggle}
          depth={depth + 1} decorate={decorate} checkable={checkable} onContextMenu={onContextMenu} />
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
    /* core.history.changed also follows this client's own mutations (see FF.isOwnHistory). */
    const offs = topics.map(t => qlc.subscribeTo(t, t === 'core.history.changed' ? (d) => { if (!(window.FF && window.FF.isOwnHistory(d))) debounced(); } : debounced));
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
    ['fixtures.patched', 'fixtures.unpatched', 'fixtures.updated', 'io.universe.created', 'core.project.loaded', 'core.history.changed']);
  return React.useMemo(() => {
    if (!data) return null;
    const perUniverse = {};
    data.fixtures.forEach(f => { (perUniverse[f.universe] = perUniverse[f.universe] || []).push(f); });
    const universes = data.universes.slice().sort((a, b) => a.id - b.id);
    Object.keys(perUniverse).forEach(id => { if (!universes.some(u => u.id === Number(id))) universes.push({ id: Number(id), name: 'Universe ' + (Number(id) + 1) }); });
    const tree = universes.filter(u => perUniverse[u.id]).map(u => ({
      id: 'u' + u.id, kind: 'universe', name: u.name, icon: D.icon('uniview'), universeId: u.id,
      children: perUniverse[u.id].slice().sort((a, b) => a.address - b.address).map(f => ({
        id: 'fx' + f.id, kind: 'fixture', fixtureId: f.id, name: f.name,
        icon: D.icon(FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture'), summary: f
      }))
    }));
    return { tree, fixtures: data.fixtures, universes: data.universes };
  }, [data]);
}

/** Functions as a folder tree from their `path` ("a/b/c"), from functions.list. */
function useFunctionTree(qlc, extraFolders) {
  const D = window.QLCData;
  const data = useLiveList(qlc, () => qlc.call('functions.list').then(r => r.functions || []),
    ['functions.created', 'functions.deleted', 'functions.renamed', 'functions.moved', 'functions.updated', 'core.project.loaded', 'core.history.changed']);
  return React.useMemo(() => {
    if (!data) return null;
    const root = { children: [], folders: {} };
    const folderFor = (path) => {
      if (!path) return root;
      let node = root, acc = '';
      path.split('/').forEach(seg => {
        acc = acc ? acc + '/' + seg : seg;
        if (!node.folders[seg]) {
          node.folders[seg] = { id: 'dir:' + acc, kind: 'folder', path: acc, name: seg, icon: D.icon('folder'), children: [], folders: {} };
          node.children.push(node.folders[seg]);
        }
        node = node.folders[seg];
      });
      return node;
    };
    const visible = data.filter(f => !f.hidden);
    (extraFolders || []).forEach(folderFor);
    visible.forEach(f => folderFor(f.path).children.push({
      id: 'fn' + f.id, kind: 'function', functionId: f.id, name: f.name, type: f.type, path: f.path,
      icon: D.icon(FUNCTION_ICONS[f.type] || 'functions'), summary: f
    }));
    const paths = [];
    const finish = (node) => {
      node.children.sort((a, b) => (!!b.children) - (!!a.children) || byName(a, b));
      node.children.forEach(c => { if (c.children) { paths.push(c.path); finish(c); } delete c.folders; });
    };
    finish(root);
    return { tree: root.children, functions: visible, all: data, total: data.length, paths: paths.sort() };
  }, [data, (extraFolders || []).join('\n')]);
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
function flatten(nodes, out) { nodes.forEach(n => { out.push(n); if (n.children) flatten(n.children, out); }); return out; }
/** Leaves in display order (for shift-range selection). */
function visibleLeaves(nodes, expanded, out) {
  nodes.forEach(n => {
    if (n.children) { if (expanded.indexOf(n.id) !== -1) visibleLeaves(n.children, expanded, out); }
    else out.push(n);
  });
  return out;
}

/* --- function running / paused state ----------------------------------------------------------- */
/**
 * running/paused per function id. Seeded from functions.list items (servers that report it) and
 * updated by functions.status.changed {functionId|id, running, paused}. `reported` tells whether
 * this server has ever reported state at all; without it the UI falls back to "last sent".
 */
function useFunctionStatus(qlc, functions) {
  const [status, setStatus] = React.useState({});
  const [reported, setReported] = React.useState(false);
  React.useEffect(() => {
    if (!functions) return;
    const seed = {};
    let any = false;
    functions.forEach(f => { if (f.running !== undefined || f.paused !== undefined) { any = true; seed[f.id] = { running: !!f.running, paused: !!f.paused }; } });
    if (any) { setReported(true); setStatus(s => Object.assign({}, s, seed)); }
  }, [functions]);
  React.useEffect(() => {
    if (!qlc.online) { setStatus({}); setReported(false); return undefined; }
    const off = qlc.subscribeTo('functions.status.changed', (d) => {
      if (!d) return;
      const id = d.functionId != null ? d.functionId : d.id;
      if (id == null) return;
      setReported(true);
      setStatus(s => {
        const cur = s[String(id)] || {};
        const next = { running: d.running !== undefined ? !!d.running : cur.running, paused: d.paused !== undefined ? !!d.paused : cur.paused };
        if (cur.running === next.running && cur.paused === next.paused) return s; /* elapsed-only ticks: no re-render */
        return Object.assign({}, s, { [String(id)]: next });
      });
    });
    return off;
  }, [qlc.online]);
  const setLocal = (id, patch) => setStatus(s => Object.assign({}, s, { [String(id)]: Object.assign({ running: false, paused: false }, s[String(id)] || {}, patch) }));
  return { status, reported, setLocal };
}

/* --- detail panes -------------------------------------------------------------------------- */

function Row(props) { const R = ff('Row'); return R ? <R {...props} /> : null; }
function Note(props) { const N = ff('Note'); return N ? <N {...props} /> : null; }

function FixtureDetail({ node, qlc, universes }) {
  const D = window.QLCData;
  const FF = window.FF;
  const detail = FF.useFixtureDetail(qlc, node.fixtureId);
  const f = detail || node.summary;
  const uni = (universes || []).find(u => u.id === f.universe);
  const [univ, setUniv] = React.useState(f.universe);
  const [addr, setAddr] = React.useState(f.address + 1);
  React.useEffect(() => { setUniv(f.universe); setAddr(f.address + 1); }, [f.universe, f.address, node.fixtureId]);
  const dirty = univ !== f.universe || addr !== f.address + 1;
  const apply = () => FF.mutate(qlc, 'fixtures.update', { fixtureId: String(f.id), universe: univ, address: addr - 1 }).catch(() => {});
  const groups = {};
  (detail && detail.channelList || []).forEach(ch => { groups[ch.group] = (groups[ch.group] || 0) + 1; });
  const universeModel = (universes || []).map(u => ({ mLabel: u.name, mValue: u.id }));
  return (
    <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, alignContent: 'start' }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <RobotoText label="Addressing" fontBold fontSize={14} />
        <Row label="Universe">
          <CustomComboBox width={170} currValue={univ} model={universeModel} onValueChanged={setUniv} />
        </Row>
        <Row label="Address">
          <CustomSpinBox value={addr} from={1} to={Math.max(1, 513 - f.channels)} width={110} onValueModified={setAddr} />
          <RobotoText label={'– ' + (addr + f.channels - 1)} fontSize={14} labelColor="var(--fg-light)" />
        </Row>
        <Row label="Channels">{f.channels}</Row>
        {dirty ? (
          <div style={{ display: 'flex', gap: 6, marginTop: 4 }}>
            <GenericButton label="Apply address" width={120} height={26} onClick={apply} />
            <GenericButton label="Revert" width={80} height={26} onClick={() => { setUniv(f.universe); setAddr(f.address + 1); }} />
          </div>
        ) : null}
        <RobotoText label="Definition" fontBold fontSize={14} style={{ marginTop: 8 }} />
        <Row label="Manufacturer">{f.manufacturer || '—'}</Row>
        <Row label="Model">{f.model || '—'}</Row>
        <Row label="Mode">{f.mode || '—'}</Row>
        <Row label="Type">{f.fixtureType || (f.isGeneric ? 'Generic' : '—')}</Row>
        <Note text="Changing the mode of a patched fixture is not available: fixtures.update only takes universe, address and name. Unpatch and add the fixture again in the wanted mode." style={{ marginTop: 8 }} />
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

/** Live function detail (functions.get), refreshed on foreign-origin change events. */
function useFunctionDetail(qlc, functionId) {
  const [detail, setDetail] = React.useState(null);
  const FF = window.FF;
  const load = React.useCallback(() => qlc.call('functions.get', { functionId: String(functionId) }).then(setDetail).catch(() => {}), [functionId, qlc.online]);
  React.useEffect(() => { setDetail(null); if (qlc.online) load(); }, [functionId, qlc.online]);
  /* Undo/redo (from this client too) emits only core.history.changed — always refetch on it. */
  React.useEffect(() => qlc.subscribeTo('core.history.changed', (d) => { if (!FF.isOwnHistory(d)) load(); }), [functionId, qlc.online]);
  const topics = ['functions.updated', 'functions.renamed', 'functions.moved', 'functions.scene.valuesChanged', 'functions.scene.membersChanged',
    'functions.chaser.stepsChanged', 'functions.chaser.changed', 'functions.sequence.stepsChanged', 'functions.sequence.changed', 'functions.collection.membersChanged'];
  FF.useForeignEvents(qlc, topics, (topic, d) => {
    const id = d && (d.functionId != null ? d.functionId : (d.functionIds && d.functionIds.indexOf(String(functionId)) !== -1 ? functionId : null));
    if (id != null && String(id) === String(functionId)) load();
  }, [functionId]);
  return [detail, load, setDetail];
}

function FunctionDetail({ node, qlc, functions, fixtures, selectedFixtureIds, palettes, onSelectFixtures }) {
  const FF = window.FF;
  const [detail, reload, setDetail] = useFunctionDetail(qlc, node.functionId);
  const f = detail || node.summary;
  const type = f.type;
  const Editor = editorFor(type);
  const Timing = ff('TimingEditor');
  if (!detail) return <div style={{ padding: 20 }}><RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /></div>;
  return (
    <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      {Editor ? (
        <Editor qlc={qlc} detail={detail} reload={reload} setDetail={setDetail} functions={functions} fixtures={fixtures}
          selectedFixtureIds={selectedFixtureIds} palettes={palettes} onSelectFixtures={onSelectFixtures} />
      ) : (
        <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, alignContent: 'start' }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            {Timing ? <Timing qlc={qlc} detail={detail} setDetail={setDetail} /> : null}
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
            <RobotoText label="Contents" fontBold fontSize={14} />
            {(detail.attributes || []).map(a => <Row key={a.index} label={a.name}>{String(a.value)}</Row>)}
            <Note text={'The ' + type + ' editor is not available in the web UI: the server has no functions.' + type.toLowerCase() + '.* methods yet. Start/stop, rename, move, delete and timing work.'} style={{ marginTop: 8 }} />
          </div>
        </div>
      )}
    </div>
  );
}

/** Tree root / universe row: a summary of what is below it. */
function BranchDetail({ node, onSelectFixtures }) {
  const leaves = flatten(node.children || [], []);
  const fixtures = leaves.filter(n => n.kind === 'fixture');
  const functions = leaves.filter(n => n.kind === 'function');
  const channels = fixtures.reduce((s, n) => s + ((n.summary && n.summary.channels) || 0), 0);
  return (
    <div style={{ padding: 20, display: 'flex', flexDirection: 'column', gap: 8 }}>
      <RobotoText label={node.name} fontBold fontSize={14} />
      {fixtures.length ? <RobotoText label={fixtures.length + ' fixtures · ' + channels + ' channels'} fontSize={14} labelColor="var(--fg-light)" /> : null}
      {functions.length ? <RobotoText label={functions.length + ' functions'} fontSize={14} labelColor="var(--fg-light)" /> : null}
      {!fixtures.length && !functions.length ? <RobotoText label="None" fontSize={14} labelColor="var(--fg-medium)" /> : null}
      {fixtures.length ? <GenericButton label={'Select ' + fixtures.length + ' fixtures'} width={150} height={26} onClick={() => onSelectFixtures(fixtures.map(n => n.fixtureId))} /> : null}
    </div>
  );
}

function FolderDetail({ node, count, onNew }) {
  return (
    <div style={{ padding: 20, display: 'flex', flexDirection: 'column', gap: 8 }}>
      <RobotoText label={'Folder ' + node.path} fontBold fontSize={14} />
      <RobotoText label={count + ' function' + (count === 1 ? '' : 's') + ' inside'} fontSize={14} labelColor="var(--fg-light)" />
      <GenericButton label="New function here" width={150} height={26} onClick={onNew} />
      <Note text="Folders are just the shared prefix of the functions' paths. An empty folder exists only in this browser until a function is created in it." />
    </div>
  );
}

/* --- the screen ---------------------------------------------------------------------------- */

function FixturesFunctions() {
  const D = window.QLCData;
  const FF = window.FF;
  const qlc = useQLC();
  const live = qlc.online;
  const [extraFolders, setExtraFolders] = React.useState([]);
  const fixtureTree = useFixtureTree(qlc);
  const functionTree = useFunctionTree(qlc, extraFolders);
  const palettes = useLiveList(qlc, () => qlc.call('palette.list').then(r => r.palettes || []), ['palette.created', 'palette.deleted', 'palette.updated', 'core.project.loaded', 'core.history.changed']);
  const fnStatus = useFunctionStatus(qlc, functionTree ? functionTree.all : null);
  const lastError = FF ? FF.useLastError() : null;

  const [selected, setSelected] = React.useState([]);     /* node ids, in click order */
  const [detail, setDetail] = React.useState(null);       /* primary node (last clicked) */
  const [expanded, setExpanded] = React.useState(['g-front', 'g-back', 'g-cyc', 'fn-scenes', 'fn-chasers', 'fn-fx']);
  const [multi, setMulti] = React.useState(false);
  const [panel, setPanel] = React.useState('tools');      /* 'tools' | 'palettes' | 'groups' | null */
  const [dlg, setDlg] = React.useState(null);              /* 'addFixture' | 'addGroup' | 'move' | 'newFolder' | 'delete' */
  const [menu, setMenu] = React.useState(null);            /* {x, y, kind: 'new'|'node', node?} */
  const [search, setSearch] = React.useState('');
  const [searching, setSearching] = React.useState(false);
  const [lastSent, setLastSent] = React.useState([]);      /* fallback running guess for servers without status */
  const [dimmer, setDimmer] = React.useState(255);
  const [moveTarget, setMoveTarget] = React.useState('');
  const [renaming, setRenaming] = React.useState(false); /* toolbar/menu Rename puts the header name into edit mode */
  React.useEffect(() => {
    if (!renaming) return;
    const el = document.querySelector('[data-ff-name] input');
    if (el) { el.focus(); el.select(); }
  }, [renaming]);
  const [newFolderName, setNewFolderName] = React.useState('');

  /* Live: one root per side; mock: the prototype's flat groups. */
  const fixturesRoot = live && fixtureTree
    ? [{ id: 'root-fixtures', kind: 'root', name: 'Fixtures', icon: D.icon('fixture'), children: fixtureTree.tree }]
    : D.fixtures;
  const functionsRoot = live && functionTree
    ? [{ id: 'root-functions', kind: 'root', name: 'Functions', icon: D.icon('functions'), children: functionTree.tree }]
    : D.functions;
  const shownFixtures = React.useMemo(() => filterTree(fixturesRoot, search), [fixturesRoot, search]);
  const shownFunctions = React.useMemo(() => filterTree(functionsRoot, search), [functionsRoot, search]);
  /* While searching, every folder that survived the filter is shown open. */
  const effectiveExpanded = search ? collectIds(shownFixtures.concat(shownFunctions), []) : expanded;

  React.useEffect(() => {
    if (live) { setExpanded(['root-fixtures', 'root-functions']); setSelected([]); setDetail(null); setLastSent([]); }
    else { setExpanded(['g-front', 'g-back', 'g-cyc', 'fn-scenes', 'fn-chasers', 'fn-fx']); setSelected(['f1']); setDetail(D.fixtures[0].children[0]); }
  }, [live]);
  /* Forget selections whose nodes vanished (deleted elsewhere); refresh the primary node's summary. */
  React.useEffect(() => {
    if (!live) return;
    const all = flatten(fixturesRoot.concat(functionsRoot), []);
    const ids = all.map(n => n.id);
    setSelected(s => s.filter(id => ids.indexOf(id) !== -1));
    if (detail) {
      const fresh = all.find(n => n.id === detail.id);
      if (!fresh) setDetail(null);
      else if (fresh !== detail) setDetail(fresh);
    }
  }, [fixturesRoot, functionsRoot]);

  const toggle = (id) => setExpanded(p => p.indexOf(id) === -1 ? p.concat([id]) : p.filter(x => x !== id));
  /* Click = select (folders also toggle); Ctrl/Cmd or multi mode = add/remove; Shift = range. */
  const pick = (id, node, e) => {
    setRenaming(false);
    const add = multi || (e && (e.ctrlKey || e.metaKey));
    const range = e && e.shiftKey && detail && !node.children;
    if (range) {
      const leaves = visibleLeaves(shownFixtures.concat(shownFunctions), effectiveExpanded, []);
      const a = leaves.findIndex(n => n.id === detail.id), b = leaves.findIndex(n => n.id === id);
      if (a !== -1 && b !== -1) {
        const [lo, hi] = a < b ? [a, b] : [b, a];
        const ids = leaves.slice(lo, hi + 1).filter(n => n.kind === detail.kind).map(n => n.id);
        setSelected(s => s.filter(x => ids.indexOf(x) === -1).concat(ids));
        return;
      }
    }
    if (add && !node.children) {
      /* Adding to the selection keeps the open editor: ctrl-clicking fixtures while a Scene is
         open is how Fixture Tools get their "Scene" target, like qmlui's left panel selection. */
      setSelected(s => s.indexOf(id) !== -1 ? s.filter(x => x !== id) : s.concat([id]));
      if (!detail) setDetail(node);
      return;
    }
    setSelected([id]);
    /* Expanding a universe/root row while a function editor is open must not close the editor
       (that is how fixtures get picked for Fixture Tools with target "Scene"). */
    const keepEditor = node.children && detail && detail.kind === 'function' && (node.kind === 'universe' || node.kind === 'root');
    if (!keepEditor) setDetail(node);
    if (node.children) toggle(id);
  };
  const allNodes = React.useMemo(() => flatten(fixturesRoot.concat(functionsRoot), []), [fixturesRoot, functionsRoot]);
  const selectedNodes = selected.map(id => allNodes.find(n => n.id === id)).filter(Boolean);
  const selectedFixtureIds = selectedNodes.filter(n => n.kind === 'fixture').map(n => n.fixtureId);
  const selectedFunctionIds = selectedNodes.filter(n => n.kind === 'function').map(n => n.functionId);
  /* Select fixtures in the tree (from an editor or a group); an open function editor stays open. */
  const selectFixtures = (ids) => {
    const wanted = ids.map(String);
    const nodes = allNodes.filter(n => n.kind === 'fixture' && wanted.indexOf(String(n.fixtureId)) !== -1);
    setSelected(nodes.map(n => n.id));
    setExpanded(p => p.concat(['root-fixtures'].concat(nodes.map(n => 'u' + n.summary.universe))));
    if (nodes.length && !(detail && detail.kind === 'function')) setDetail(nodes[nodes.length - 1]);
  };

  const isFunction = !!(detail && (detail.kind === 'function' || detail.type));
  const isFixture = !!(detail && (detail.kind === 'fixture' || detail.address));
  const isFolder = !!(detail && detail.kind === 'folder');
  const detailId = detail ? (detail.functionId || detail.fixtureId || detail.id) : null;

  /* Running / paused for the primary function: server-reported when available, else last sent. */
  const st = isFunction ? (fnStatus.status[String(detailId)] || null) : null;
  const isRunning = isFunction ? (fnStatus.reported ? !!(st && st.running) : lastSent.indexOf(detailId) !== -1) : false;
  const isPaused = !!(st && st.paused);
  const startStop = () => {
    if (!detail || !isFunction) return;
    const id = String(detailId);
    if (isRunning) { if (live) qlc.call('functions.stop', { functionId: id }).catch(() => {}); setLastSent(p => p.filter(x => x !== detailId)); if (!fnStatus.reported) fnStatus.setLocal(id, { running: false, paused: false }); }
    else { if (live) qlc.call('functions.start', { functionId: id }).catch(() => {}); setLastSent(p => p.concat([detailId])); }
  };
  const pauseResume = () => {
    if (!detail || !isFunction || !live) return;
    const id = String(detailId), paused = !isPaused;
    const method = qlc.isUnsupported('functions.setPause') ? 'functions.pause' : 'functions.setPause';
    qlc.call(method, { functionId: id, id, paused }).then(() => { if (!fnStatus.reported) fnStatus.setLocal(id, { paused }); })
      .catch(e => { if (e && e.code === 'NOT_FOUND' && method === 'functions.setPause') qlc.call('functions.pause', { functionId: id, id, paused }).catch(() => {}); });
  };
  const rename = (name) => {
    setRenaming(false);
    if (!detail || !live || !name || name === detail.name) return;
    if (isFunction) FF.mutate(qlc, 'functions.rename', { functionId: String(detail.functionId), name }).catch(() => {});
    else if (isFixture) FF.mutate(qlc, 'fixtures.update', { fixtureId: String(detail.fixtureId), name }).catch(() => {});
  };
  const doDelete = () => {
    setDlg(null);
    if (!live) return;
    /* Act on the tree selection when there is one; the open item alone only when nothing is selected. */
    const anySel = selectedFunctionIds.length + selectedFixtureIds.length > 0;
    const fns = anySel ? selectedFunctionIds : (isFunction ? [detail.functionId] : []);
    const fxs = anySel ? selectedFixtureIds : (isFixture ? [detail.fixtureId] : []);
    if (fns.length) FF.mutateSeq(qlc, fns.map(id => ['functions.delete', { functionId: String(id) }])).catch(() => {});
    if (fxs.length) FF.mutate(qlc, 'fixtures.unpatch', { fixtureIds: fxs.map(String) }).catch(() => {});
    setSelected([]); setDetail(null);
  };
  /* Folder the "new function" menu creates into: the selected folder, or the selected function's. */
  const targetPath = isFolder ? detail.path : (isFunction ? (detail.path || '') : '');
  const createFunction = (type) => {
    setMenu(null);
    if (!live) return;
    FF.mutate(qlc, 'functions.create', { type, path: targetPath }).then(r => {
      if (r && r.functionId != null) {
        /* Select it and open its folder; the effect below shows its detail once the refreshed
           list has produced the node. */
        setSelected(['fn' + r.functionId]);
        setExpanded(p => p.concat(['root-functions'].concat(targetPath ? targetPath.split('/').map((s, i, a) => 'dir:' + a.slice(0, i + 1).join('/')) : [])));
      }
    }).catch(() => {});
  };
  React.useEffect(() => {
    /* Follow a single freshly created/selected function id to its node once it exists. */
    if (selected.length === 1 && (!detail || detail.id !== selected[0])) {
      const n = allNodes.find(x => x.id === selected[0]);
      if (n && !n.children) setDetail(n);
    }
  }, [allNodes, selected]);
  const moveFunctions = () => {
    setDlg(null);
    const ids = selectedFunctionIds.length ? selectedFunctionIds : (isFunction ? [detail.functionId] : []);
    if (!ids.length || !live) return;
    FF.mutate(qlc, 'functions.move', { functionIds: ids.map(String), path: moveTarget.replace(/^\/+|\/+$/g, '') }).catch(() => {});
  };
  const addFolder = () => {
    setDlg(null);
    const name = newFolderName.trim().replace(/^\/+|\/+$/g, '');
    if (!name) return;
    const path = targetPath ? targetPath + '/' + name : name;
    setExtraFolders(p => p.indexOf(path) === -1 ? p.concat([path]) : p);
    setExpanded(p => p.concat(['root-functions', 'dir:' + path]));
    setSelected(['dir:' + path]);
    setNewFolderName('');
  };

  const summary = live
    ? (fixtureTree && functionTree
      ? fixtureTree.fixtures.length + ' fixtures · ' + fixtureTree.fixtures.reduce((s, f) => s + f.channels, 0) + ' channels · '
        + fixtureTree.universes.length + ' universes · ' + functionTree.functions.length + ' functions'
        + (selected.length > 1 ? ' · ' + selected.length + ' selected' : '')
      : 'Loading workspace…')
    : '8 fixtures · 126 channels · 2 universes (mock)';

  /* Tree row decoration: running functions get a lime play glyph, paused an amber pause glyph. */
  const decorate = (node) => {
    if (node.kind !== 'function') return node.name;
    const s = fnStatus.status[String(node.functionId)];
    const running = fnStatus.reported ? !!(s && s.running) : lastSent.indexOf(node.functionId) !== -1;
    if (!running) return node.name;
    return <span style={{ display: 'inline-flex', alignItems: 'center', gap: 5 }}>
      <FaIcon name={s && s.paused ? 'fa_pause' : 'fa_play'} size={10} color={s && s.paused ? 'var(--selection)' : 'var(--check-lime)'} />{node.name}
    </span>;
  };
  const onNodeMenu = (node, e) => {
    if (!live) return;
    if (selected.indexOf(node.id) === -1) { setSelected([node.id]); setDetail(node); }
    setMenu({ x: e.clientX, y: e.clientY, kind: 'node', node });
  };

  const PopupMenu = ff('PopupMenu'), MenuItem = ff('MenuItem');
  const FixtureTools = ff('FixtureTools'), PalettePanel = ff('PalettePanel'), GroupsPanel = ff('FixtureGroupsPanel'), AddFixtureDialog = ff('AddFixtureDialog');
  const canDelete = live && (selectedFunctionIds.length + selectedFixtureIds.length > 0 || isFunction || isFixture);
  const folderModel = functionTree ? [{ mLabel: '/ (top level)', mValue: '' }].concat(functionTree.paths.map(p => ({ mLabel: p, mValue: p }))) : [];

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', minHeight: 0, position: 'relative' }}>
      <ViewToolbar variant="sub">
        <ShortcutHint keys="A" placement="corner">
          <IconButton imgSource={D.icon('fixture')} size={26} onClick={() => setDlg('addFixture')} disabled={!live}
            tooltip={live ? 'Add fixtures' : 'Add fixtures — connect first'} />
        </ShortcutHint>
        <IconButton imgSource={D.icon('group')} size={26} disabled={!live} checked={panel === 'groups'} onClick={() => setPanel(panel === 'groups' ? null : 'groups')} tooltip="Fixture Groups" />
        <IconButton imgSource={D.icon('remap')} size={26} disabled tooltip="Remap addresses — not available: the server has no fixtures.remap.* yet" />
        <div style={{ width: 1, height: 20, background: 'var(--border-color-dark)', margin: '0 3px' }} />
        <IconButton imgSource={D.icon('add')} size={26} disabled={!live} tooltip={'Add a new function' + (targetPath ? ' in ' + targetPath : '')}
          onClick={(e) => { const r = e.currentTarget.getBoundingClientRect(); setMenu({ x: r.left, y: r.bottom + 2, kind: 'new' }); }} />
        <IconButton imgSource={D.icon('rename')} size={26} disabled={!live || !detail || isFolder} tooltip="Rename the selected item (or double-click its name)"
          onClick={() => setRenaming(true)} />
        <IconButton imgSource={D.icon('folder')} size={26} disabled={!live || !(isFunction || selectedFunctionIds.length)} tooltip="Move the selected functions to a folder"
          onClick={() => { setMoveTarget(targetPath); setDlg('move'); }} />
        <IconButton faSource="fa_trash_can" size={26} disabled={!canDelete}
          tooltip={live ? 'Delete the selected functions / unpatch the selected fixtures' : 'Delete — connect first'}
          onClick={() => setDlg('delete')} />
        <IconButton imgSource={D.icon('multiple')} size={26} checked={multi} onClick={() => setMulti(!multi)} tooltip="Toggle multiple item selection (Ctrl-click / Shift-click also work)" />
        <IconButton imgSource={D.icon('selectall')} size={26} disabled={!live} tooltip="Select/Deselect all fixtures"
          onClick={() => { const all = allNodes.filter(n => n.kind === 'fixture'); if (selectedFixtureIds.length === all.length) { setSelected([]); } else { setSelected(all.map(n => n.id)); if (all.length) setDetail(all[0]); } }} />
        <ToolbarSpacer />
        {lastError ? <RobotoText label={(lastError.method ? lastError.method + ': ' : '') + lastError.message} fontSize={13} labelColor="var(--override-red)" style={{ maxWidth: 420 }} /> : null}
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
        <div data-ff-tree="1" style={{ width: 280, minWidth: 280, background: 'var(--bg-stronger)', borderRight: 'var(--border-dark)', overflow: 'auto' }}>
          {shownFixtures.map(n => <TreeBranch key={n.id} node={n} selected={selected} onSelect={pick} expanded={effectiveExpanded} onToggle={toggle} decorate={decorate} checkable={multi} onContextMenu={onNodeMenu} />)}
          <div style={{ height: 1, background: 'var(--border-color-dark)', margin: '4px 0' }} />
          {shownFunctions.map(n => <TreeBranch key={n.id} node={n} selected={selected} onSelect={pick} expanded={effectiveExpanded} onToggle={toggle} decorate={decorate} checkable={multi} onContextMenu={onNodeMenu} />)}
          {live && !(fixtureTree && functionTree) ? <div style={{ padding: 8 }}><RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
        </div>

        <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', background: 'var(--bg-medium)' }}>
          {detail && (isFunction || isFixture) ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, height: 38, padding: '0 10px', background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)', flex: 'none' }}>
              <img src={detail.icon} alt="" style={{ width: 24, height: 24 }} />
              <span data-ff-name="1" style={{ display: 'inline-flex' }} onKeyDown={(e) => { if (e.key === 'Escape') setRenaming(false); }}>
                <CustomTextInput key={detail.id} text={detail.name} allowDoubleClick width={300} editing={renaming ? true : undefined} onTextConfirmed={rename} />
              </span>
              {selected.length > 1 ? <RobotoText label={'+' + (selected.length - 1) + ' more'} fontSize={13} labelColor="var(--fg-light)" /> : null}
              <ToolbarSpacer />
              {isFunction ? <>
                <IconButton faSource={isRunning ? FF.GLYPH.stop : 'fa_play'} size={26}
                  faColor={isRunning ? 'var(--override-red)' : 'var(--check-lime)'} checked={isRunning} onClick={startStop}
                  tooltip={(isRunning ? 'Stop function' : 'Start function') + (live ? (fnStatus.reported ? '' : ' — this server does not report running state, shown as last sent') : ' (mock)')} />
                <IconButton faSource="fa_pause" size={26} checked={isPaused} disabled={!live || !isRunning} onClick={pauseResume}
                  faColor={isPaused ? 'var(--selection)' : 'var(--bg-strong)'}
                  tooltip={isPaused ? 'Resume function' : 'Pause function'} />
                {fnStatus.reported && st ? <RobotoText label={st.running ? (st.paused ? 'Paused' : 'Running') : 'Stopped'} fontSize={13} labelColor={st.running ? (st.paused ? 'var(--selection)' : 'var(--check-lime)') : 'var(--fg-light)'} /> : null}
              </> : null}
              <RobotoText label={detail.mode || detail.type || (detail.summary && detail.summary.fixtureType) || ''} fontSize={14} labelColor="var(--fg-light)" />
            </div>
          ) : null}

          {!detail ? (
            <div style={{ padding: 20 }}>
              <RobotoText label={live ? 'Select a fixture or function in the tree. Right-click a row for actions.' : 'Select a fixture or function.'} fontSize={14} labelColor="var(--fg-medium)" />
            </div>
          ) : live && isFolder ? (
            <FolderDetail node={detail} count={flatten(detail.children || [], []).filter(n => n.kind === 'function').length}
              onNew={() => setMenu({ x: 320, y: 120, kind: 'new' })} />
          ) : live && (detail.kind === 'root' || detail.kind === 'universe') ? (
            <BranchDetail node={detail} onSelectFixtures={selectFixtures} />
          ) : live && detail.kind === 'fixture' ? (
            <FixtureDetail node={detail} qlc={qlc} universes={fixtureTree ? fixtureTree.universes : []} />
          ) : live && detail.kind === 'function' ? (
            <FunctionDetail key={detail.functionId} node={detail} qlc={qlc} functions={functionTree ? functionTree.functions : []}
              fixtures={fixtureTree ? fixtureTree.fixtures : []} selectedFixtureIds={selectedFixtureIds} palettes={palettes || []} onSelectFixtures={selectFixtures} />
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

        <SidePanel isOpen={!!panel} alignment="right" expandedWidth={panel === 'tools' ? 344 : 300} rail={
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, padding: 4 }}>
            <IconButton imgSource={D.icon('intensity')} checked={panel === 'tools'} onClick={() => setPanel(panel === 'tools' ? null : 'tools')} tooltip="Fixture tools: intensity, colour, position, presets" />
            <IconButton imgSource={D.icon('palette')} checked={panel === 'palettes'} onClick={() => setPanel(panel === 'palettes' ? null : 'palettes')} tooltip="Palettes" />
            <IconButton imgSource={D.icon('group')} checked={panel === 'groups'} onClick={() => setPanel(panel === 'groups' ? null : 'groups')} tooltip="Fixture Groups" />
            <IconButton imgSource={D.icon('fixture-editor')} disabled tooltip="Fixture editor — not available in the web UI" />
            <IconButton imgSource={D.icon('uniview')} disabled tooltip="Universe view — not available in the web UI" />
          </div>}>
          <div style={{ height: '100%', overflow: 'auto' }}>
            {!live ? (
              <SectionBox sectionLabel="Colour (mock)" isExpanded>
                <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4,1fr)', gap: 4, padding: 6 }}>
                  {['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'amber', 'white', 'uv', 'lime', 'indigo', 'colorwheel'].map(c => (
                    <img key={c} src={D.icon(c)} alt={c} title={c} style={{ width: '100%', aspectRatio: 1, cursor: 'pointer' }} />
                  ))}
                </div>
              </SectionBox>
            ) : panel === 'tools' && FixtureTools ? (
              <FixtureTools qlc={qlc} fixtureIds={selectedFixtureIds} fixtures={fixtureTree ? fixtureTree.fixtures : []}
                sceneId={isFunction && detail.type === 'Scene' ? detail.functionId : null} sceneName={isFunction && detail.type === 'Scene' ? detail.name : null} />
            ) : panel === 'palettes' && PalettePanel ? (
              <PalettePanel qlc={qlc} palettes={palettes} fixtureIds={selectedFixtureIds} fixtures={fixtureTree ? fixtureTree.fixtures : []} />
            ) : panel === 'groups' && GroupsPanel ? (
              <GroupsPanel qlc={qlc} fixtureIds={selectedFixtureIds} fixtures={fixtureTree ? fixtureTree.fixtures : []} onSelectFixtures={selectFixtures} />
            ) : <div style={{ padding: 8 }}><Note text="This panel is not loaded (webui/ff/*.jsx missing)." /></div>}
          </div>
        </SidePanel>
      </div>

      {PopupMenu && menu ? (
        <PopupMenu open x={menu.x} y={menu.y} onClose={() => setMenu(null)} width={210}>
          {menu.kind === 'new' ? <>
            <MenuItem icon="folder" text="New folder" onClick={() => { setMenu(null); setNewFolderName(''); setDlg('newFolder'); }} />
            <div style={{ height: 1, background: 'var(--border-color-dark)', margin: '2px 0' }} />
            {FF.FUNCTION_TYPES.map(t => <MenuItem key={t.type} icon={t.icon} text={t.label} onClick={() => createFunction(t.type)} />)}
          </> : menu.node.kind === 'function' ? <>
            <MenuItem glyph="fa_play" text="Start" onClick={() => { setMenu(null); qlc.call('functions.start', { functionId: String(menu.node.functionId) }).catch(() => {}); setLastSent(p => p.concat([menu.node.functionId])); }} />
            <MenuItem glyph={FF.GLYPH.stop} text="Stop" onClick={() => { setMenu(null); qlc.call('functions.stop', { functionId: String(menu.node.functionId) }).catch(() => {}); setLastSent(p => p.filter(x => x !== menu.node.functionId)); }} />
            <MenuItem icon="rename" text="Rename" onClick={() => { setMenu(null); setRenaming(true); }} />
            <MenuItem icon="folder" text="Move to folder…" onClick={() => { setMenu(null); setMoveTarget(menu.node.path || ''); setDlg('move'); }} />
            <MenuItem glyph="fa_trash_can" text={selected.length > 1 ? 'Delete ' + selected.length + ' items' : 'Delete'} onClick={() => { setMenu(null); setDlg('delete'); }} />
          </> : menu.node.kind === 'fixture' ? <>
            <MenuItem icon="rename" text="Rename" onClick={() => { setMenu(null); setRenaming(true); }} />
            <MenuItem icon="intensity" text="Fixture tools" onClick={() => { setMenu(null); setPanel('tools'); }} />
            <MenuItem icon="scene" text="New Scene with selected fixtures" onClick={() => { setMenu(null); FF.mutate(qlc, 'functions.create', { type: 'Scene', fixtures: (selectedFixtureIds.length ? selectedFixtureIds : [menu.node.fixtureId]).map(String) }).catch(() => {}); }} />
            <MenuItem glyph="fa_trash_can" text={selectedFixtureIds.length > 1 ? 'Unpatch ' + selectedFixtureIds.length + ' fixtures' : 'Unpatch'} onClick={() => { setMenu(null); setDlg('delete'); }} />
          </> : menu.node.kind === 'folder' ? <>
            <MenuItem icon="add" text="New function here…" onClick={() => setMenu({ x: menu.x, y: menu.y, kind: 'new' })} />
            <MenuItem icon="folder" text="New subfolder…" onClick={() => { setMenu(null); setNewFolderName(''); setDlg('newFolder'); }} />
          </> : <MenuItem text="No actions" disabled />}
        </PopupMenu>
      ) : null}

      {AddFixtureDialog ? <AddFixtureDialog open={dlg === 'addFixture'} qlc={qlc} universes={fixtureTree ? fixtureTree.universes : []} fixtures={fixtureTree ? fixtureTree.fixtures : []} onClose={() => setDlg(null)} /> : null}

      <CustomPopupDialog open={dlg === 'move'} title="Move to folder" width={380}
        standardButtons={['Cancel', 'Move']} onClicked={(b) => { if (b === 'Move') moveFunctions(); else setDlg(null); }} onClose={() => setDlg(null)}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <RobotoText label={(selectedFunctionIds.length || 1) + ' function' + (selectedFunctionIds.length > 1 ? 's' : '')} fontSize={14} labelColor="var(--fg-light)" />
          <Row label="Folder" width={70}><CustomComboBox width={250} currValue={folderModel.some(m => m.mValue === moveTarget) ? moveTarget : '__custom'} model={folderModel.concat([{ mLabel: 'Other…', mValue: '__custom' }])} onValueChanged={(v) => setMoveTarget(v === '__custom' ? moveTarget : v)} /></Row>
          <Row label="Path" width={70}>
            <input value={moveTarget} onChange={e => setMoveTarget(e.target.value)} placeholder="Folder/Subfolder (empty = top level)"
              style={{ width: 250, height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' }} />
          </Row>
        </div>
      </CustomPopupDialog>

      <CustomPopupDialog open={dlg === 'newFolder'} title="New folder" width={360}
        standardButtons={['Cancel', 'Create']} onClicked={(b) => { if (b === 'Create') addFolder(); else setDlg(null); }} onClose={() => setDlg(null)}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <RobotoText label={'Inside ' + (targetPath || '/ (top level)')} fontSize={14} labelColor="var(--fg-light)" />
          <input autoFocus value={newFolderName} onChange={e => setNewFolderName(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') addFolder(); }} placeholder="Folder name"
            style={{ width: '100%', height: 26, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' }} />
        </div>
      </CustomPopupDialog>

      <CustomPopupDialog open={dlg === 'delete'} title={selectedFunctionIds.length && selectedFixtureIds.length ? 'Delete items' : (selectedFunctionIds.length || (!selectedFixtureIds.length && isFunction)) ? 'Delete functions' : 'Unpatch fixtures'} width={400}
        message={(() => {
          const anySel = selectedFunctionIds.length + selectedFixtureIds.length > 0;
          const nf = anySel ? selectedFunctionIds.length : (isFunction ? 1 : 0), nx = anySel ? selectedFixtureIds.length : (isFixture ? 1 : 0);
          const parts = [];
          if (nf) parts.push(nf === 1 ? 'the function "' + (selectedNodes.find(n => n.kind === 'function') || detail || {}).name + '"' : nf + ' functions');
          if (nx) parts.push(nx === 1 ? 'the fixture "' + (selectedNodes.find(n => n.kind === 'fixture') || detail || {}).name + '"' : nx + ' fixtures');
          return 'Are you sure you want to delete ' + parts.join(' and ') + '? Functions referencing an unpatched fixture lose those channels.';
        })()}
        standardButtons={['Cancel', 'Delete']}
        onClicked={(b) => { if (b === 'Delete') doDelete(); else setDlg(null); }} onClose={() => setDlg(null)} />
    </div>
  );
}
Object.assign(window, { FixturesFunctions, TreeBranch });
