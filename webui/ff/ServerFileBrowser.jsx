/**
 * ServerFileBrowser.jsx — a file picker for files that live on the machine QLC+ runs on, over
 * core.fs.list (read-only directory listing). The browser equivalent of
 * qmlui/qml/popup/PopupFolderBrowser.qml: places bar (home + drives), path crumbs, dirs-first
 * listing, a name-filter combo with glob patterns, double-click / Open to pick.
 *
 * Exported as window.ServerFileBrowser (and FF.ServerFileBrowser) so any screen can use it —
 * the Audio / Video editors' "Replace file" buttons do, App.jsx's Open-project dialog can.
 *
 * Props:
 *   open, onClose()                 — dialog visibility
 *   qlc                             — the connection object (qlc.call)
 *   title                           — dialog title
 *   filters                         — [{label: 'Audio files', patterns: ['*.mp3', '*.wav']}, ...];
 *                                     a filter with no patterns means "All files". Optional.
 *   initialPath                     — directory to start in; defaults to the last one used, then
 *                                     the roots list
 *   pickDirectories                 — true = folder picker (files hidden, Open picks the current dir)
 *   onPick(path)                    — absolute host path of the chosen entry
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomComboBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

  let lastPath = '';

  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };

  function fmtSize(n) {
    if (n == null) return '';
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB';
    if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + ' MB';
    return (n / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
  }
  function fmtTime(ms) {
    if (!ms) return '';
    const d = new Date(ms);
    const p = (n) => (n < 10 ? '0' : '') + n;
    return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate()) + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }
  /** "C:/Users/x/Music" → [{name:'C:/', path:'C:/'}, {name:'Users', path:'C:/Users'}, ...] */
  function crumbs(path) {
    if (!path) return [];
    const parts = path.split('/').filter((p, i) => p !== '' || i === 0);
    const out = [];
    let acc = '';
    parts.forEach((p, i) => {
      if (i === 0) { acc = p === '' ? '/' : p + '/'; out.push({ name: p === '' ? '/' : p + '/', path: acc }); return; }
      acc = acc.replace(/\/$/, '') + '/' + p;
      out.push({ name: p, path: acc });
    });
    return out;
  }

  function ServerFileBrowser({ open, onClose, qlc, title, filters, initialPath, pickDirectories, onPick }) {
    const D = window.QLCData;
    const fl = (filters && filters.length) ? filters : [{ label: 'All files', patterns: [] }];
    const [filterIdx, setFilterIdx] = React.useState(0);
    const [listing, setListing] = React.useState(null);   /* core.fs.list result */
    const [path, setPath] = React.useState('');
    const [typed, setTyped] = React.useState('');
    const [selected, setSelected] = React.useState(null); /* entry */
    const [error, setError] = React.useState(null);
    const [loading, setLoading] = React.useState(false);
    const seq = React.useRef(0);

    const load = React.useCallback((p, fIdx) => {
      const idx = fIdx == null ? filterIdx : fIdx;
      const patterns = (fl[idx] && fl[idx].patterns) || [];
      const my = ++seq.current;
      setLoading(true); setError(null);
      const params = { path: p || '' };
      if (patterns.length) params.extensions = patterns;
      if (pickDirectories) params.includeFiles = false;
      return qlc.call('core.fs.list', params).then(r => {
        if (my !== seq.current) return;
        setListing(r); setPath(r.path || ''); setTyped(r.path || ''); setSelected(null); setLoading(false);
        if (r.path) lastPath = r.path;
      }).catch(e => {
        if (my !== seq.current) return;
        setLoading(false); setError((e && e.message) || String(e));
      });
    }, [qlc, filterIdx, pickDirectories, fl.map(f => f.patterns.join(' ')).join('|')]);

    React.useEffect(() => { if (open) { setSelected(null); load(initialPath || lastPath || ''); } }, [open]);

    const entries = (listing && listing.entries) || [];
    const roots = (listing && listing.roots) || [];
    const pick = (entry) => { if (entry) { lastPath = path; onPick(entry.path); onClose(); } };
    const openSelected = () => {
      if (pickDirectories) { if (selected && selected.isDir) { onPick(selected.path); onClose(); } else if (path) { onPick(path); onClose(); } return; }
      if (selected && !selected.isDir) pick(selected);
      else if (selected && selected.isDir) load(selected.path);
    };
    const canOpen = pickDirectories ? !!(path || (selected && selected.isDir)) : !!(selected);
    const row = (entry) => {
      const sel = selected && selected.path === entry.path;
      return (
        <div key={entry.path} onClick={() => setSelected(entry)} onDoubleClick={() => entry.isDir ? load(entry.path) : pick(entry)}
          style={{ display: 'flex', alignItems: 'center', gap: 6, height: 24, padding: '0 6px', cursor: 'pointer', background: sel ? 'var(--highlight)' : 'transparent' }}>
          <FF.Glyph g={entry.isDir ? FF.GLYPH.folder : FF.GLYPH.file} size={13} color={entry.isDir ? 'var(--check-lime)' : 'var(--fg-light)'} />
          <RobotoText label={entry.name} fontSize={13} height={24} style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} />
          <RobotoText label={entry.isDir ? '' : fmtSize(entry.size)} fontSize={11} height={24} labelColor="var(--fg-medium)" style={{ width: 70, textAlign: 'right' }} />
          <RobotoText label={fmtTime(entry.mtime)} fontSize={11} height={24} labelColor="var(--fg-medium)" style={{ width: 110, textAlign: 'right' }} />
        </div>
      );
    };

    return (
      <CustomPopupDialog open={open} title={title || (pickDirectories ? 'Select a folder' : 'Select a file')} width={720}
        standardButtons={['Cancel', 'Open']} disabledButtons={canOpen ? [] : ['Open']}
        onClicked={(b) => { if (b === 'Open') openSelected(); else onClose(); }} onClose={onClose}>
        <div className="qlc-file-browser" style={{ display: 'flex', flexDirection: 'column', gap: 6, height: 440 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
            <IconButton faSource={FF.GLYPH.arrowUp} size={24} tooltip="Parent folder" disabled={!(listing && listing.parent)} onClick={() => load(listing.parent)} />
            <IconButton faSource={FF.GLYPH.retweet} size={24} tooltip="Refresh" onClick={() => load(path)} />
            <div style={{ flex: 1, display: 'flex', alignItems: 'center', gap: 2, overflow: 'hidden', minWidth: 0 }}>
              {path ? crumbs(path).map((c, i) => (
                <React.Fragment key={c.path}>
                  {i ? <RobotoText label="›" fontSize={13} labelColor="var(--fg-medium)" height={24} /> : null}
                  <button type="button" onClick={() => load(c.path)} title={c.path}
                    style={{ height: 22, padding: '0 6px', border: 'none', background: 'var(--bg-control)', color: 'var(--fg-main)', fontFamily: 'var(--font-roboto)', fontSize: 13, cursor: 'pointer', whiteSpace: 'nowrap' }}>{c.name}</button>
                </React.Fragment>
              )) : <RobotoText label="Drives and places" fontSize={13} labelColor="var(--fg-light)" height={24} />}
            </div>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
            <RobotoText label="Path" fontSize={13} labelColor="var(--fg-light)" style={{ width: 40, flex: 'none' }} />
            <input className="qlc-fb-path" value={typed} onChange={e => setTyped(e.target.value)} placeholder="Type a folder on the QLC+ machine and press Enter"
              onKeyDown={e => { if (e.key === 'Enter') load(typed.trim()); }} style={Object.assign({ flex: 1 }, inputStyle)} />
          </div>
          <div style={{ flex: 1, minHeight: 0, display: 'flex', gap: 6 }}>
            <div style={{ width: 150, flex: 'none', overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
              {roots.map(r => (
                <div key={r.path} onClick={() => load(r.path)} title={r.path}
                  style={{ display: 'flex', alignItems: 'center', gap: 6, height: 24, padding: '0 6px', cursor: 'pointer', background: path && path.toLowerCase().indexOf(r.path.toLowerCase()) === 0 && r.name !== 'Home' ? 'var(--bg-strong)' : 'transparent' }}>
                  <FF.Glyph g={r.name === 'Home' ? FF.GLYPH.house : FF.GLYPH.hdd} size={13} color="var(--fg-light)" />
                  <RobotoText label={r.name} fontSize={13} height={24} />
                </div>
              ))}
            </div>
            <div className="qlc-fb-list" style={{ flex: 1, minWidth: 0, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
              {error ? <div style={{ padding: 8 }}><RobotoText label={error} fontSize={13} labelColor="var(--check-red, #e05050)" wrapText height="auto" /></div> : null}
              {!error && loading && !entries.length ? <div style={{ padding: 8 }}><RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
              {!error && !loading && path && !entries.length ? <div style={{ padding: 8 }}><RobotoText label="Empty folder (or nothing matching the filter)" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
              {!error && !path && !loading ? <div style={{ padding: 8 }}><RobotoText label="Pick a drive or place on the left, or type a path." fontSize={13} labelColor="var(--fg-medium)" /></div> : null}
              {entries.map(row)}
            </div>
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <RobotoText label="Filter" fontSize={13} labelColor="var(--fg-light)" style={{ width: 40, flex: 'none' }} />
            <CustomComboBox width={260} height={24} currValue={filterIdx}
              model={fl.map((f, i) => ({ mLabel: f.label + (f.patterns.length ? ' (' + f.patterns.join(' ') + ')' : ''), mValue: i }))}
              onValueChanged={(v) => { setFilterIdx(v); load(path, v); }} />
            <RobotoText label={selected ? selected.path : ''} fontSize={12} labelColor="var(--fg-light)" height={24} style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} />
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  /** "*.mp3" glob → a filter entry; used by the editors to build the combo from listCapabilities. */
  ServerFileBrowser.filter = function (label, patterns) { return { label, patterns: patterns || [] }; };

  FF.GLYPH.file = FF.GLYPH.file || '\uf15b';
  FF.GLYPH.house = FF.GLYPH.house || '\uf015';
  FF.GLYPH.hdd = FF.GLYPH.hdd || '\uf0a0';

  window.ServerFileBrowser = ServerFileBrowser;
  FF.ServerFileBrowser = ServerFileBrowser;
})();
