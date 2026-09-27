/**
 * FixtureEditor.jsx — the Fixture Editor screen (Ctrl+6), mirroring qmlui/qml/fixtureeditor/
 * FixtureEditor.qml (toolbar, one tab per open definition with a modified marker and a
 * save-before-close prompt) and EditorView.qml (General / Physical / Channels / Modes / Aliases),
 * on top of the fixturedefs.* Control API domain. Pieces live in webui/fixtureeditor/*.jsx
 * (window.FE); this file is the frame, the file operations and the General tab.
 *
 * File operations, as a browser can do them:
 *   New                 fixturedefs.session.create
 *   Open                manufacturer -> model picker over fixtures.defs.listManufacturers +
 *                       fixturedefs.list({manufacturer}) (the unfiltered list force-loads the whole
 *                       library, ~9 s, so it is never used), then fixturedefs.session.open
 *   Save                fixturedefs.save into the QLC+ host's user fixture folder; a bundled
 *                       definition must be forked first ("Save as user copy" = session.forkToUser)
 *   Import / Export     a .qxf picked in / downloaded to this browser (base64 over the socket)
 *   Delete              a user definition (library copy + file) via fixturedefs.delete
 * The desktop's free "Save as <path>" has no browser equivalent: definitions always land in the
 * host's user fixture folder under <Manufacturer>-<Model>.qxf, which is where QLC+ looks for them.
 *
 * Entry point from elsewhere: window.QLCOpenFixtureEditor(manufacturer?, model?) switches to this
 * screen and opens that definition (or a new one when called without arguments).
 */
(function () {
  'use strict';
  const { ViewToolbar, ToolbarSpacer, MenuBarEntry, IconButton, RobotoText, GenericButton, CustomPopupDialog, IconTextEntry } = window.PatchDesignSystem_5432c9;
  const FE = window.FE;

  const TABS = [['general', 'General'], ['channels', 'Channels'], ['modes', 'Modes'], ['physical', 'Physical'], ['aliases', 'Aliases']];
  const label = (s) => (s.definition.manufacturer || 'Unknown') + ' - ' + (s.definition.model || 'Unknown');

  /** Called from the Fixtures & Functions add-fixture browser (or anywhere): open and switch. */
  window.QLCOpenFixtureEditor = function (manufacturer, model) {
    FE.store.pending = manufacturer && model ? { manufacturer, model } : { create: true };
    FE.emit();
    window.dispatchEvent(new CustomEvent('qlc-set-context', { detail: 'fxeditor' }));
  };

  /* ---- Open picker (FixtureEditor.qml's file dialog, as a library browser) ---------------------- */
  function OpenDialog({ open, qlc, onClose, onOpen, onDelete }) {
    const D = window.QLCData;
    const store = FE.store;
    const [manufacturers, setManufacturers] = React.useState(null);
    const [manufacturer, setManufacturer] = React.useState('');
    const [models, setModels] = React.useState(null);
    const [model, setModel] = React.useState('');
    const [search, setSearch] = React.useState('');
    const [err, setErr] = React.useState('');
    React.useEffect(() => {
      if (!open) return undefined;
      let alive = true; setErr('');
      qlc.call('fixtures.defs.listManufacturers', {}).then(r => { if (alive) setManufacturers(r.manufacturers || []); }).catch(e => { if (alive) { setManufacturers([]); setErr(FE.errorText(e)); } });
      return () => { alive = false; };
    }, [open, store.libraryVersion]);
    React.useEffect(() => {
      setModels(null); setModel('');
      if (!open || !manufacturer) return undefined;
      let alive = true;
      qlc.call('fixturedefs.list', { manufacturer }).then(r => { if (alive) setModels((r.entries || []).slice().sort((a, b) => a.model.localeCompare(b.model))); })
        .catch(e => { if (alive) { setModels([]); setErr(FE.errorText(e)); } });
      return () => { alive = false; };
    }, [open, manufacturer, store.libraryVersion]);
    if (!open) return null;
    const q = search.trim().toLowerCase();
    const manuList = (manufacturers || []).filter(m => !q || m.toLowerCase().indexOf(q) !== -1 || m === manufacturer);
    const entry = (models || []).find(m => m.model === model) || null;
    return (
      <CustomPopupDialog open title="Open a fixture definition" width={760} standardButtons={['Cancel', 'Delete', 'Open']}
        disabledButtons={[].concat(entry ? [] : ['Open'], entry && entry.isUser ? [] : ['Delete'])}
        onClicked={b => { if (b === 'Open' && entry) onOpen(entry); else if (b === 'Delete' && entry) onDelete(entry); else if (b === 'Cancel') onClose(); }} onClose={onClose}>
        <div style={{ display: 'grid', gridTemplateColumns: '260px 1fr', gap: 10 }} data-fe="open-dialog">
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
            <input value={search} onChange={e => setSearch(e.target.value)} placeholder="Search manufacturers…" style={Object.assign({}, FE.inputStyle, { width: '100%' })} data-fe="open-search" />
            <div style={{ height: 340, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }} data-fe="open-manufacturers">
              {manufacturers === null ? <RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" leftMargin={6} /> : null}
              {manuList.map(m => (
                <FE.ListRow key={m} selected={m === manufacturer} onClick={() => setManufacturer(m)} data-manufacturer={m}><RobotoText label={m} fontSize={13} height={24} /></FE.ListRow>
              ))}
            </div>
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 4, minWidth: 0 }}>
            <RobotoText label={manufacturer ? 'Models of ' + manufacturer : 'Pick a manufacturer'} fontSize={13} fontBold height={24} />
            <div style={{ height: 250, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }} data-fe="open-models">
              {manufacturer && models === null ? <RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" leftMargin={6} /> : null}
              {(models || []).map(m => (
                <FE.ListRow key={m.model} selected={m.model === model} onClick={() => setModel(m.model)} onDoubleClick={() => onOpen(m)} data-model={m.model}>
                  <img src={FE.typeIcon(m.type)} alt="" style={{ width: 18, height: 18 }} />
                  <RobotoText label={m.model} fontSize={13} height={24} style={{ flex: 1, minWidth: 0 }} />
                  <span style={{ padding: '1px 6px', borderRadius: 3, font: '11px var(--font-roboto)', background: m.isUser ? 'var(--keypad-enter-pressed)' : 'var(--bg-control)', color: 'var(--fg-main)' }}
                    data-fe="origin-badge">{m.isUser ? 'user' : 'system'}</span>
                </FE.ListRow>
              ))}
            </div>
            {entry ? (
              <div style={{ display: 'flex', flexDirection: 'column' }} data-fe="open-entry">
                <FE.Row label="Type" width={80}><RobotoText label={entry.type} fontSize={13} height={22} /></FE.Row>
                <FE.Row label="Author" width={80}><RobotoText label={entry.author || '-'} fontSize={13} height={22} /></FE.Row>
                <FE.Row label="Contents" width={80}><RobotoText label={entry.channelCount + ' channels, ' + entry.modeCount + ' modes'} fontSize={13} height={22} /></FE.Row>
                <RobotoText label={entry.isUser ? 'User definition: editable and deletable.' : 'Bundled definition: opens read-only; "Save as user copy" makes an editable copy in the user folder that overrides it.'}
                  fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" />
              </div>
            ) : null}
            {err ? <RobotoText label={err} fontSize={12} labelColor="var(--override-red)" wrapText height="auto" /> : null}
          </div>
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- General tab (EditorView.qml "General") ------------------------------------------------ */
  function GeneralTab({ qlc, s, onValidate }) {
    const D = window.QLCData;
    const def = s.definition, sid = s.sessionId;
    const upd = (patch) => FE.act(qlc, 'fixturedefs.session.update', Object.assign({ sessionId: sid }, patch));
    const warnings = FE.store.validation[sid];
    return (
      <div style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 10, display: 'flex', gap: 20, flexWrap: 'wrap', alignContent: 'flex-start' }} data-fe="general-tab">
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4, width: 480 }}>
          <FE.Row label="Manufacturer"><FE.Text value={def.manufacturer} onCommit={t => upd({ manufacturer: t.trim() })} data-fe="general-manufacturer" /></FE.Row>
          <FE.Row label="Model"><FE.Text value={def.model} onCommit={t => upd({ model: t.trim() })} data-fe="general-model" /></FE.Row>
          <FE.Row label="Author"><FE.Text value={def.author} onCommit={t => upd({ author: t })} data-fe="general-author" /></FE.Row>
          <FE.Row label="Origin"><RobotoText label={s.isUser ? 'User definition' : 'Bundled (system) definition - read-only until saved as a user copy'} fontSize={13} height={24} labelColor={s.isUser ? 'var(--fg-main)' : 'var(--selection)'} /></FE.Row>
          <FE.Row label="File"><RobotoText label={def.sourceFile || 'not saved yet'} fontSize={12} height={24} labelColor="var(--fg-light)" style={{ overflow: 'hidden' }} /></FE.Row>
          <FE.Row label="Library revision"><RobotoText label={s.baseRevision == null ? '- (not in the library)' : String(s.baseRevision)} fontSize={13} height={24} labelColor="var(--fg-light)" /></FE.Row>
          <FE.Row label="Contents"><RobotoText label={def.channels.length + ' channels, ' + def.modes.length + ' modes'} fontSize={13} height={24} labelColor="var(--fg-light)" /></FE.Row>
          <div style={{ marginTop: 10, display: 'flex', alignItems: 'center', gap: 8 }}>
            <RobotoText label="Validation" fontSize={15} fontBold height={28} style={{ flex: 1 }} />
            <GenericButton label="Validate" width={90} height={26} fontSize={13} onClick={onValidate} data-fe="general-validate" />
          </div>
          <div data-fe="validation-list">
            {warnings == null ? <RobotoText label="Press Validate to check the definition (also run automatically on save)." fontSize={12} labelColor="var(--fg-medium)" wrapText height="auto" />
              : warnings.length ? warnings.map((w, i) => <RobotoText key={i} label={'• ' + w} fontSize={13} labelColor="var(--selection)" wrapText height="auto" data-fe="validation-warning" />)
                : <RobotoText label="No problems found." fontSize={13} labelColor="var(--check-lime)" height={24} data-fe="validation-ok" />}
          </div>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          <RobotoText label="Type" fontSize={14} labelColor="var(--fg-light)" height={24} />
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 110px)', gap: 4 }} data-fe="general-type">
            {FE.FIXTURE_TYPES.map(([t, icon]) => (
              <button key={t} type="button" onClick={() => { if (t !== def.type) upd({ type: t }); }} data-type={t} title={t}
                style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2, padding: 4, cursor: 'pointer', borderRadius: 4,
                  background: def.type === t ? 'var(--highlight)' : 'var(--bg-control)', border: '1px solid var(--bg-strong)', color: 'var(--fg-main)' }}>
                <img src={D.icon(icon)} alt="" style={{ width: 40, height: 40 }} />
                <span style={{ font: '12px var(--font-roboto)', whiteSpace: 'nowrap' }}>{t}</span>
              </button>
            ))}
          </div>
        </div>
      </div>
    );
  }

  /* ---- the screen ------------------------------------------------------------------------------ */
  function FixtureEditor() {
    const D = window.QLCData;
    const qlc = useQLC();
    const store = FE.useStore();
    const [dialog, setDialog] = React.useState(null); // {kind:'open'|'close'|'message'|'overwrite'|'fork'|'delete'|'validate', ...}
    const [ready, setReady] = React.useState(false);
    const fileInput = React.useRef(null);
    const s = FE.activeSession();

    /* events + resync on every (re)connect; then honour a pending QLCOpenFixtureEditor request */
    React.useEffect(() => {
      if (!qlc.online) { setReady(false); return undefined; }
      let alive = true;
      const off = FE.bindEvents(qlc);
      FE.resync(qlc).catch(e => FE.say('Could not list the open sessions: ' + FE.errorText(e), true)).then(() => {
        if (!alive) return;
        setReady(true);
        const p = FE.store.pending; FE.store.pending = null;
        if (p && p.create) newDefinition();
        else if (p) openDefinition(p.manufacturer, p.model);
      });
      return () => { alive = false; off(); };
    }, [qlc.online]);
    React.useEffect(() => { if (ready && FE.store.pending) { const p = FE.store.pending; FE.store.pending = null; if (p.create) newDefinition(); else openDefinition(p.manufacturer, p.model); } }, [ready, store.version]);

    /* Ctrl+S on this screen saves the definition, not the project (capture phase, before App's handler). */
    React.useEffect(() => {
      const k = (e) => { if ((e.ctrlKey || e.metaKey) && !e.shiftKey && e.key.toLowerCase() === 's') { e.preventDefault(); e.stopPropagation(); const cur = FE.activeSession(); if (cur) save(cur); } };
      window.addEventListener('keydown', k, true);
      return () => window.removeEventListener('keydown', k, true);
    });

    const fail = (what) => (e) => { FE.say(what + ': ' + FE.errorText(e), true); };
    function newDefinition() {
      qlc.call('fixturedefs.session.create', {}).then(r => { FE.putSession(r, true); FE.setUi(r.sessionId, { tab: 'general' }); FE.say('New definition'); }).catch(fail('New definition'));
    }
    function openDefinition(manufacturer, model) {
      const open = Object.values(FE.store.sessions).find(x => x.definition.manufacturer === manufacturer && x.definition.model === model);
      if (open) { FE.store.active = open.sessionId; FE.emit(); return; }
      qlc.call('fixturedefs.session.open', { manufacturer, model }).then(r => {
        FE.putSession(Object.assign({ isModified: false }, r), true);
        FE.say('Opened ' + manufacturer + ' - ' + model + (r.isUser ? '' : ' (bundled, read-only until saved as a user copy)'));
      }).catch(fail('Open'));
    }
    async function save(cur, overwriteRevision) {
      const def = cur.definition;
      if (!def.manufacturer || !def.model) { setDialog({ kind: 'message', title: '!! Warning !!', text: 'Manufacturer or model cannot be empty!' }); return; }
      if (!cur.isUser) { setDialog({ kind: 'fork', sessionId: cur.sessionId }); return; }
      const attempt = (base) => FE.mutate(qlc, 'fixturedefs.save', { sessionId: cur.sessionId, $build: () => ({ baseRevision: base === undefined ? FE.session(cur.sessionId).baseRevision : base }) }, { plain: true });
      try {
        let r;
        try { r = await attempt(overwriteRevision); }
        catch (e) {
          if (!(e && e.code === 'CONFLICT')) throw e;
          const lib = e.details ? e.details.defRevision : null;
          /* null: the library has no entry for the (possibly renamed) manufacturer/model - nothing to
             overwrite, save against null. Anything else: someone saved it since we opened it. */
          if (lib == null) r = await attempt(null);
          else { setDialog({ kind: 'overwrite', sessionId: cur.sessionId, revision: lib }); return; }
        }
        const x = FE.session(cur.sessionId);
        if (x) { x.baseRevision = r.defRevision; x.isModified = false; }
        FE.store.validation[cur.sessionId] = r.warnings || [];
        FE.say('Saved ' + label(cur) + (r.warnings && r.warnings.length ? ' - ' + r.warnings.length + ' warning(s): ' + r.warnings.join('; ') : ''));
      } catch (e) {
        if (e && e.code === 'FIXTUREDEFS_SYSTEM_READONLY') setDialog({ kind: 'fork', sessionId: cur.sessionId });
        else FE.say('Save: ' + FE.errorText(e), true);
      }
    }
    function forkToUser(sid) {
      FE.act(qlc, 'fixturedefs.session.forkToUser', { sessionId: sid }).then(r => {
        if (!r) return;
        const x = FE.session(sid); if (x) x.isUser = true;
        FE.say('This definition is now a user copy: Save writes it into the user fixture folder, overriding the bundled file.');
      });
    }
    function closeSession(sid, force) {
      const x = FE.session(sid);
      if (x && x.isModified && !force) { setDialog({ kind: 'close', sessionId: sid }); return; }
      qlc.call('fixturedefs.session.close', { sessionId: sid }).then(() => FE.dropSession(sid)).catch(fail('Close'));
    }
    function exportDefinition(cur) {
      FE.mutate(qlc, 'fixturedefs.export', { sessionId: cur.sessionId }, { plain: true })
        .then(r => { FE.downloadBase64(r.fileName, r.qxfBase64); FE.say('Exported ' + r.fileName); }).catch(fail('Export'));
    }
    function importFile(file) {
      if (!file) return;
      file.arrayBuffer().then(buf => qlc.call('fixturedefs.session.import', { fileName: file.name, qxfBase64: FE.bytesToBase64(buf) }))
        .then(r => { FE.putSession(r, true); FE.say('Imported ' + file.name + ' - save it to add it to the user library'); }).catch(fail('Import'));
    }
    async function deleteDefinition(manufacturer, model) {
      try {
        const list = await qlc.call('fixturedefs.list', { manufacturer });
        const entry = (list.entries || []).find(e => e.model === model);
        if (!entry) { FE.say('Delete: ' + manufacturer + ' - ' + model + ' is not in the library', true); return; }
        await qlc.call('fixturedefs.delete', { manufacturer, model, baseRevision: entry.defRevision });
        Object.values(FE.store.sessions).forEach(x => { if (x.definition.manufacturer === manufacturer && x.definition.model === model) { x.baseRevision = null; x.isModified = true; } });
        FE.store.libraryVersion = (FE.store.libraryVersion || 0) + 1;
        FE.say('Deleted ' + manufacturer + ' - ' + model + ' from the user library');
      } catch (e) {
        if (e && e.code === 'FIXTUREDEFS_IN_USE') FE.say('Delete: the definition is used by patched fixture(s) ' + ((e.details && e.details.fixtureIds) || []).join(', ') + ' in the open project', true);
        else FE.say('Delete: ' + FE.errorText(e), true);
      }
    }
    function validate(cur) {
      FE.mutate(qlc, 'fixturedefs.session.validate', { sessionId: cur.sessionId }, { plain: true }).then(r => {
        FE.store.validation[cur.sessionId] = r.warnings || []; FE.emit();
        setDialog({ kind: 'validate', warnings: r.warnings || [] });
      }).catch(fail('Validate'));
    }

    if (!qlc.online) {
      return (
        <div style={{ height: '100%', display: 'grid', placeItems: 'center', background: 'var(--bg-medium)' }}>
          <RobotoText label="Fixture Editor: connect to a QLC+ server first (definitions are edited on the server)." fontSize={16} labelColor="var(--fg-light)" height={40} />
        </div>
      );
    }
    const ui = s ? FE.ui(s.sessionId) : null;
    const Tab = s ? { general: null, channels: FE.ChannelsTab, modes: FE.ModesTab, physical: FE.PhysicalTab, aliases: FE.AliasesTab }[ui.tab] : null;
    const canDelete = !!s && s.isUser && s.baseRevision != null;
    return (
      <div style={{ height: '100%', display: 'flex', flexDirection: 'column', minHeight: 0, background: 'var(--bg-medium)', position: 'relative' }} data-fe="screen">
        <ViewToolbar variant="main">
          <MenuBarEntry imgSource={D.icon('filenew')} entryText="New definition" onClick={newDefinition} data-fe="tb-new" />
          <MenuBarEntry imgSource={D.icon('fileopen')} entryText="Open definition" onClick={() => setDialog({ kind: 'open' })} data-fe="tb-open" />
          <MenuBarEntry imgSource={D.icon('filesave')} entryText="Save definition" disabled={!s} onClick={() => s && save(s)} data-fe="tb-save" />
          <MenuBarEntry imgSource={D.icon('filesaveas')} entryText="Save as user copy" disabled={!s || s.isUser} onClick={() => s && forkToUser(s.sessionId)} data-fe="tb-fork"
            title="Turn a bundled definition into an editable copy in the user fixture folder" />
          <MenuBarEntry imgSource={D.icon('import')} entryText="Import" onClick={() => fileInput.current && fileInput.current.click()} data-fe="tb-import" />
          <MenuBarEntry faSource="fa_chevron_down" faColor="var(--fg-main)" entryText="Export" disabled={!s} onClick={() => s && exportDefinition(s)} data-fe="tb-export" />
          <MenuBarEntry faSource="fa_trash_can" faColor="crimson" entryText="Delete" disabled={!canDelete} onClick={() => setDialog({ kind: 'delete', manufacturer: s.definition.manufacturer, model: s.definition.model })} data-fe="tb-delete" />
          <MenuBarEntry faSource="fa_check" faColor="var(--check-lime)" entryText="Validate" disabled={!s} onClick={() => s && validate(s)} data-fe="tb-validate" />
          <ToolbarSpacer />
          <input ref={fileInput} type="file" accept=".qxf,application/xml,text/xml" style={{ display: 'none' }} data-fe="import-file"
            onChange={e => { importFile(e.target.files && e.target.files[0]); e.target.value = ''; }} />
        </ViewToolbar>
        <ViewToolbar variant="sub" style={{ overflowX: 'auto' }} data-fe="session-tabs">
          {store.order.map(sid => {
            const x = store.sessions[sid];
            return (
              <span key={sid} style={{ display: 'inline-flex', alignItems: 'center', flex: 'none' }} data-session-id={sid} data-session-label={label(x)}>
                <MenuBarEntry imgSource={x.isModified ? D.icon('filesave') : undefined} entryText={label(x) + (x.isUser ? '' : ' (system)')} checked={sid === store.active}
                  onClick={() => { FE.store.active = sid; FE.emit(); }} title={x.isModified ? 'Modified - not saved yet' : 'Saved'} />
                <IconButton faSource="fa_xmark" faColor="var(--fg-main)" bgColor="transparent" borderWidth={0} size={24} tooltip="Close this definition" onClick={() => closeSession(sid)} data-fe="session-close" />
              </span>
            );
          })}
          {!store.order.length ? <RobotoText label="No definition open - New, Open or Import one." fontSize={13} labelColor="var(--fg-medium)" leftMargin={8} height={30} /> : null}
        </ViewToolbar>
        {s && !s.isUser ? (
          <div style={{ padding: '4px 10px', background: 'var(--section-header-div)', display: 'flex', alignItems: 'center', gap: 10 }} data-fe="system-banner">
            <RobotoText label="You are editing a bundled fixture definition. Save it as a user copy to store it in the user fixture folder, where it overrides the bundled file." fontSize={13} labelColor="var(--selection)" wrapText height="auto" style={{ flex: 1 }} />
            <GenericButton label="Save as user copy" width={140} height={26} fontSize={13} onClick={() => forkToUser(s.sessionId)} data-fe="banner-fork" />
          </div>
        ) : null}
        {s ? (
          <div style={{ display: 'flex', gap: 2, padding: '0 6px', background: 'var(--bg-strong)', borderBottom: 'var(--border-dark)', flex: 'none' }} data-fe="section-tabs">
            {TABS.map(([id, text]) => {
              const count = id === 'channels' ? s.definition.channels.length : id === 'modes' ? s.definition.modes.length : id === 'aliases' ? FE.aliasCapabilities(s.definition).length : null;
              return <MenuBarEntry key={id} entryText={text + (count != null ? ' (' + count + ')' : '')} checked={ui.tab === id} height={30} onClick={() => FE.setUi(s.sessionId, { tab: id })} data-fe-tab={id} />;
            })}
          </div>
        ) : null}
        <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
          {!s ? (
            <div style={{ padding: 20, display: 'flex', flexDirection: 'column', gap: 10, maxWidth: 640 }}>
              <RobotoText label="Fixture Editor" fontSize={20} fontBold height={30} />
              <RobotoText label="Create a new fixture definition, open one from the library (user or bundled), or import a .qxf file from this computer. Definitions are saved into the user fixture folder of the QLC+ machine and become available for patching right away." fontSize={14} labelColor="var(--fg-light)" wrapText height="auto" />
              <div style={{ display: 'flex', gap: 8 }}>
                <GenericButton label="New definition" width={150} height={30} onClick={newDefinition} data-fe="empty-new" />
                <GenericButton label="Open definition" width={150} height={30} onClick={() => setDialog({ kind: 'open' })} />
              </div>
            </div>
          ) : ui.tab === 'general' ? <GeneralTab qlc={qlc} s={s} onValidate={() => validate(s)} /> : Tab ? <Tab qlc={qlc} s={s} /> : null}
        </div>
        <div style={{ height: 24, flex: 'none', display: 'flex', alignItems: 'center', padding: '0 8px', background: 'var(--bg-strong)', borderTop: 'var(--border-dark)' }} data-fe="status">
          <RobotoText label={store.status.text || (s ? 'Session revision ' + s.sessionRevision + (s.isModified ? ' - modified' : ' - saved') : '')} fontSize={12}
            labelColor={store.status.error ? 'var(--override-red)' : 'var(--fg-light)'} height={24} style={{ overflow: 'hidden', whiteSpace: 'nowrap' }} />
        </div>

        <OpenDialog open={!!dialog && dialog.kind === 'open'} qlc={qlc} onClose={() => setDialog(null)}
          onOpen={(e) => { setDialog(null); openDefinition(e.manufacturer, e.model); }}
          onDelete={(e) => setDialog({ kind: 'delete', manufacturer: e.manufacturer, model: e.model, back: 'open' })} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'close'} title="Warning" width={460}
          message={dialog && dialog.kind === 'close' && FE.session(dialog.sessionId) ? 'Do you wish to save the following definition first? ' + label(FE.session(dialog.sessionId)) + ' - changes will be lost if you don\'t save them.' : ''}
          standardButtons={['Cancel', 'Discard', 'Save']} onClose={() => setDialog(null)}
          onClicked={b => {
            const sid = dialog.sessionId; setDialog(null);
            if (b === 'Discard') closeSession(sid, true);
            else if (b === 'Save') { const x = FE.session(sid); if (x) save(x).then(() => { const y = FE.session(sid); if (y && !y.isModified) closeSession(sid, true); }); }
          }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'overwrite'} title="The library copy changed" width={460}
          message="Someone saved this definition since you opened it. Overwrite their version with yours?" standardButtons={['Cancel', 'Overwrite']} onClose={() => setDialog(null)}
          onClicked={b => { const d = dialog; setDialog(null); if (b === 'Overwrite') { const x = FE.session(d.sessionId); if (x) save(x, d.revision); } }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'fork'} title="Bundled definition" width={480}
          message="This is a bundled fixture definition, which cannot be overwritten. Save it as a user copy? The copy is stored in the user fixture folder of the QLC+ machine and overrides the bundled file."
          standardButtons={['Cancel', 'Save as user copy']} onClose={() => setDialog(null)}
          onClicked={b => { const d = dialog; setDialog(null); if (b === 'Save as user copy') forkToUser(d.sessionId); }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'delete'} title="Delete fixture definition" width={460}
          message={dialog && dialog.kind === 'delete' ? 'Delete the user definition ' + dialog.manufacturer + ' - ' + dialog.model + '? Its .qxf file is removed from the user fixture folder of the QLC+ machine.' : ''}
          standardButtons={['Cancel', 'Delete']} onClose={() => setDialog(null)}
          onClicked={b => { const d = dialog; setDialog(d.back ? { kind: d.back } : null); if (b === 'Delete') deleteDefinition(d.manufacturer, d.model); }} />
        <CustomPopupDialog open={!!dialog && dialog.kind === 'validate'} title="Validation" width={520} standardButtons={['Close']} onClose={() => setDialog(null)} onClicked={() => setDialog(null)}>
          <div data-fe="validate-dialog">
            {dialog && dialog.kind === 'validate' ? (dialog.warnings.length ? <>
              <RobotoText label="The following errors have been detected:" fontSize={14} height={26} />
              {dialog.warnings.map((w, i) => <RobotoText key={i} label={'• ' + w} fontSize={13} labelColor="var(--selection)" wrapText height="auto" />)}
            </> : <RobotoText label="No problems found." fontSize={14} labelColor="var(--check-lime)" height={26} />) : null}
          </div>
        </CustomPopupDialog>
        <CustomPopupDialog open={!!dialog && dialog.kind === 'message'} title={dialog && dialog.title} width={420} message={dialog && dialog.text}
          standardButtons={['Close']} onClose={() => setDialog(null)} onClicked={() => setDialog(null)} />
      </div>
    );
  }

  window.QLCScreens = Object.assign(window.QLCScreens || {}, {
    fxeditor: { id: 'fxeditor', icon: 'fixture-editor', label: 'Fixture Editor', keys: 'Ctrl 6', hotkey: '6', component: FixtureEditor, order: 20 }
  });
  Object.assign(window, { FixtureEditor });
})();
