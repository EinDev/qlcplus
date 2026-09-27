/**
 * ImportProject.jsx — "Import from project" (qmlui/qml/popup/PopupImportProject.qml): pick another
 * .qxw (a file on the QLC+ machine through window.ServerFileBrowser, or a file uploaded from this
 * browser), tick fixtures / fixture groups and functions in two trees, import them.
 *
 * Server: core.project.importList (the source's fixtures by universe, groups, palettes, functions
 * by folder, each function with its dependency closure) and core.project.import (the engine's
 * ProjectImporter, the same code as the desktop popup) in controlapi/src/domains/apiimportdomain.cpp.
 * Like the desktop popup, ticking a function also ticks everything it needs; unticking it leaves
 * those ticked. The server recomputes the closure anyway.
 *
 * Opened from the Actions menu through a 'qlc-import-open' window event caught by an invisible
 * toolbar item (the MediaActions.jsx pattern).
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, GenericButton, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };
  const open = () => window.dispatchEvent(new CustomEvent('qlc-import-open'));
  const TYPE_ICON = { Scene: 'scene', Chaser: 'chaser', Sequence: 'sequence', EFX: 'efx', Collection: 'collection', RGBMatrix: 'rgbmatrix', Script: 'script', Show: 'showmanager', Audio: 'audio', Video: 'video' };
  const add = (set, ids) => { const n = new Set(set); ids.forEach(i => n.add(String(i))); return n; };

  function Row({ checked, onToggle, label, sub, indent, icon, attr }) {
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, height: 24, paddingLeft: 4 + (indent || 0) * 16 }} data-import-row={attr}>
        {onToggle ? <CustomCheckBox checked={checked} onToggled={onToggle} size={18} /> : <span style={{ width: 18 }} />}
        {icon ? <img src={window.QLCData.icon(icon)} alt="" style={{ width: 16, height: 16 }} /> : null}
        <RobotoText label={label} fontSize={13} height={24} style={{ flex: 1, minWidth: 0, overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }} />
        {sub ? <RobotoText label={sub} fontSize={11} height={24} labelColor="var(--fg-medium)" /> : null}
      </div>
    );
  }

  function ImportHost({ qlc }) {
    const [isOpen, setOpen] = React.useState(false);
    const [source, setSource] = React.useState(null);          /* the CoreProjectImportSource in use */
    const [path, setPath] = React.useState('');
    const [contents, setContents] = React.useState(null);
    const [fx, setFx] = React.useState(new Set());
    const [groups, setGroups] = React.useState(new Set());
    const [fns, setFns] = React.useState(new Set());
    const [fxFilter, setFxFilter] = React.useState('');
    const [fnFilter, setFnFilter] = React.useState('');
    const [browse, setBrowse] = React.useState(false);
    const [busy, setBusy] = React.useState(false);
    const [error, setError] = React.useState('');
    const [result, setResult] = React.useState(null);
    const fileInput = React.useRef(null);

    const reset = () => { setSource(null); setContents(null); setFx(new Set()); setGroups(new Set()); setFns(new Set()); setError(''); setResult(null); setBusy(false); };
    React.useEffect(() => {
      const on = () => { reset(); setOpen(true); };
      window.addEventListener('qlc-import-open', on);
      return () => window.removeEventListener('qlc-import-open', on);
    }, []);
    const close = () => setOpen(false);

    const load = (src) => {
      setBusy(true); setError(''); setContents(null); setResult(null);
      setFx(new Set()); setGroups(new Set()); setFns(new Set());
      qlc.call('core.project.importList', src).then(r => { setSource(src); setContents(r); setBusy(false); })
        .catch(e => { setBusy(false); setError((e && e.message) || 'core.project.importList failed'); });
    };
    const loadPath = (p) => { if (p) load({ source: 'path', path: p }); };
    const loadFile = (file) => {
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        const url = String(reader.result || '');
        load({ source: 'upload', fileName: file.name, contentBase64: url.slice(url.indexOf(',') + 1) });
      };
      reader.onerror = () => setError('Could not read ' + file.name);
      reader.readAsDataURL(file);
    };

    const fnById = React.useMemo(() => {
      const m = {};
      ((contents && contents.functions) || []).forEach(f => { m[f.id] = f; });
      return m;
    }, [contents]);
    const groupById = React.useMemo(() => {
      const m = {};
      ((contents && contents.fixtureGroups) || []).forEach(g => { m[g.id] = g; });
      return m;
    }, [contents]);

    /* PopupImportProject: ticking a function ticks its dependencies too; unticking leaves them */
    const toggleFunction = (f, on) => {
      if (!on) { setFns(s => { const n = new Set(s); n.delete(f.id); return n; }); return; }
      const d = f.dependencies || {};
      setFns(s => add(s, [f.id].concat((d.functionIds || []).filter(id => fnById[id]))));
      setFx(s => add(s, d.fixtureIds || []));
      setGroups(s => add(s, d.fixtureGroupIds || []));
    };
    const toggleGroup = (g, on) => {
      setGroups(s => { const n = new Set(s); if (on) n.add(g.id); else n.delete(g.id); return n; });
      if (on) setFx(s => add(s, g.fixtureIds || []));
    };
    const toggleFixture = (id, on) => setFx(s => { const n = new Set(s); if (on) n.add(id); else n.delete(id); return n; });

    const apply = async () => {
      if (!source) return;
      setBusy(true); setError('');
      try {
        const r = await FF.mutate(qlc, 'core.project.import', Object.assign({}, source, {
          fixtureIds: Array.from(fx), fixtureGroupIds: Array.from(groups), functionIds: Array.from(fns)
        }));
        setResult(r);
      } catch (e) { setError((e && e.message) || 'Import failed'); }
      setBusy(false);
    };

    /* ---- trees ---- */
    const fixtureTree = () => {
      if (!contents) return null;
      const q = fxFilter.toLowerCase();
      const match = (name) => q.length < 2 || String(name).toLowerCase().indexOf(q) !== -1;
      const out = [];
      (contents.fixtureGroups || []).filter(g => match(g.name)).forEach(g => out.push(
        <Row key={'g' + g.id} attr={'group-' + g.name} checked={groups.has(g.id)} onToggle={(v) => toggleGroup(g, v)} label={g.name} sub={g.fixtureIds.length + ' fixtures'} icon="group" />
      ));
      (contents.universes || []).forEach(u => {
        const list = (contents.fixtures || []).filter(f => f.universe === u.universe && match(f.name));
        if (!list.length) return;
        const all = list.every(f => fx.has(f.id));
        out.push(<Row key={'u' + u.universe} attr={'universe-' + u.universe} checked={all} onToggle={(v) => setFx(s => { const n = new Set(s); list.forEach(f => v ? n.add(f.id) : n.delete(f.id)); return n; })} label={u.name} icon="uniview" />);
        list.forEach(f => {
          out.push(<Row key={'f' + f.id} attr={'fixture-' + f.name} indent={1} checked={fx.has(f.id)} onToggle={(v) => toggleFixture(f.id, v)} label={f.name}
            sub={(f.existsInProject ? 'exists — matched · ' : '') + (f.address + 1) + ' · ' + (f.model || '')} icon="fixture" />);
          (f.linked || []).forEach(l => out.push(<Row key={'l' + f.id + '.' + l.headIndex + '.' + l.linkedIndex} indent={2} label={l.name || ('Linked ' + l.linkedIndex)} sub="linked" />));
        });
      });
      return out;
    };
    const functionTree = () => {
      if (!contents) return null;
      const q = fnFilter.toLowerCase();
      const list = (contents.functions || []).filter(f => q.length < 2 || f.name.toLowerCase().indexOf(q) !== -1);
      const byPath = {};
      list.forEach(f => { (byPath[f.path || ''] = byPath[f.path || ''] || []).push(f); });
      const out = [];
      Object.keys(byPath).sort().forEach(p => {
        const items = byPath[p].slice().sort((a, b) => a.name.localeCompare(b.name));
        let indent = 0;
        if (p) {
          const all = items.every(f => fns.has(f.id));
          out.push(<Row key={'p' + p} attr={'folder-' + p} checked={all} onToggle={(v) => items.forEach(f => toggleFunction(f, v))} label={p} icon="folder" />);
          indent = 1;
        }
        items.forEach(f => out.push(<Row key={'fn' + f.id} attr={'function-' + f.name} indent={indent} checked={fns.has(f.id)} onToggle={(v) => toggleFunction(f, v)} label={f.name} sub={f.type} icon={TYPE_ICON[f.type] || 'functions'} />));
      });
      return out;
    };

    const nothing = !fx.size && !groups.size && !fns.size;
    const buttons = result ? ['Close'] : ['Cancel', 'Apply'];
    const FileBrowser = window.ServerFileBrowser;
    return (
      <>
        <CustomPopupDialog open={isOpen} title="Import from project" width={900} standardButtons={buttons} disabledButtons={!contents || nothing || busy ? ['Apply'] : []}
          onClose={close} onClicked={(b) => { if (b === 'Apply') apply(); else close(); }}>
          <div data-import="dialog" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <RobotoText label="Project" fontSize={13} height={24} />
              <input value={path} onChange={e => setPath(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') loadPath(path); }} placeholder="Path of a .qxw on the QLC+ machine" style={Object.assign({ flex: 1 }, inputStyle)} data-import="path" />
              <GenericButton label="Load" width={60} height={24} fontSize={12} disabled={!path || busy} onClick={() => loadPath(path)} data-import="load" />
              {FileBrowser ? <GenericButton label="Browse…" width={80} height={24} fontSize={12} onClick={() => setBrowse(true)} data-import="browse" /> : null}
              <GenericButton label="Upload…" width={80} height={24} fontSize={12} onClick={() => fileInput.current && fileInput.current.click()} data-import="upload" />
              <input ref={fileInput} type="file" accept=".qxw,application/xml,text/xml" style={{ display: 'none' }} data-import="file" onChange={e => { loadFile(e.target.files && e.target.files[0]); e.target.value = ''; }} />
            </div>
            {contents ? <RobotoText label={'Importing from ' + (contents.fileName || 'the uploaded project') + ': ' + contents.fixtures.length + ' fixtures, ' + contents.functions.length + ' functions'} fontSize={12} height={18} labelColor="var(--fg-light)" data-import="loaded" /> : null}
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
              {[['Fixtures', fxFilter, setFxFilter, fixtureTree, 'fixtures'], ['Functions', fnFilter, setFnFilter, functionTree, 'functions']].map(([title, filter, setFilter, tree, key]) => (
                <div key={key} style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
                  <RobotoText label={title} fontBold fontSize={14} height={22} />
                  <input value={filter} onChange={e => setFilter(e.target.value)} placeholder="Search…" style={Object.assign({ width: '100%' }, inputStyle)} data-import={'search-' + key} />
                  <div style={{ height: 380, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }} data-import={'tree-' + key}>
                    {!contents ? <RobotoText label={busy ? 'Loading…' : 'Load a project first'} fontSize={13} labelColor="var(--fg-medium)" leftMargin={6} /> : tree()}
                  </div>
                </div>
              ))}
            </div>
            <FF.Note text="Fixtures, groups and palettes whose name already exists in this project are reused; everything else is copied with new ids and every reference follows. Not undoable from the web UI." />
            {result ? <RobotoText data-import="result" fontSize={13} labelColor="var(--check-lime)" wrapText height="auto"
              label={'Imported ' + Object.keys(result.functionIdMap).length + ' functions, ' + (result.createdFixtureIds || []).length + ' new fixtures (' + (Object.keys(result.fixtureIdMap).length - (result.createdFixtureIds || []).length) + ' matched by name), ' + Object.keys(result.fixtureGroupIdMap).length + ' groups, ' + Object.keys(result.paletteIdMap).length + ' palettes' + ((result.skippedFixtureIds || []).length ? '; ' + result.skippedFixtureIds.length + ' fixtures skipped (definition missing or no free address)' : '') + '.'} /> : null}
            {error ? <RobotoText label={error} fontSize={13} labelColor="var(--override-red)" wrapText height="auto" /> : null}
          </div>
        </CustomPopupDialog>
        {FileBrowser && browse ? <FileBrowser open={browse} qlc={qlc} title="Import from project" filters={[{ label: 'QLC+ projects', patterns: ['*.qxw'] }, { label: 'All files', patterns: [] }]}
          onClose={() => setBrowse(false)} onPick={(p) => { setBrowse(false); setPath(p); loadPath(p); }} /> : null}
      </>
    );
  }

  window.QLCToolbarItems = (window.QLCToolbarItems || []).concat([ImportHost]);
  window.QLCMenuItems = (window.QLCMenuItems || []).concat([({ online }) => [
    { label: 'Import from project…', icon: 'import', disabled: !online, onClick: open }
  ]]);
})();
