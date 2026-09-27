/**
 * MediaActions.jsx — the fork's media store entries of the Actions menu (qmlui/qml/ActionsMenu.qml,
 * MainView.qml): Collect media into project, Reload changed media, Remove unused media.
 *
 * Server: functions.media.status (what each entry would act on), functions.media.collect,
 * functions.media.reload (looped per changed Audio/Video, so the dialog can show each outcome) and
 * functions.media.removeUnused (controlapi/src/domains/apimediadomain.cpp).
 *
 * The menu entries (window.QLCMenuItems) open one dialog hosted by an invisible toolbar item
 * (window.QLCToolbarItems), signalled through a 'qlc-media-dialog' window event.
 */
(function () {
  'use strict';
  const { RobotoText, CustomPopupDialog } = window.PatchDesignSystem_5432c9;
  const open = (kind) => window.dispatchEvent(new CustomEvent('qlc-media-dialog', { detail: kind }));
  const TITLES = { collect: 'Collect media into project', reload: 'Reload changed media', remove: 'Remove unused media' };
  const size = (b) => b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : b >= 1024 ? Math.round(b / 1024) + ' kB' : (b || 0) + ' B';
  const base = (p) => String(p || '').split(/[\\/]/).pop();

  function MediaDialogHost({ qlc }) {
    const FF = window.FF;
    const [kind, setKind] = React.useState(null);
    const [status, setStatus] = React.useState(null);
    const [error, setError] = React.useState('');
    const [result, setResult] = React.useState(null);
    const [outcomes, setOutcomes] = React.useState({});   /* reload: functionId -> status text */
    const [checked, setChecked] = React.useState([]);     /* remove: selected paths */
    const [busy, setBusy] = React.useState(false);
    const load = () => qlc.call('functions.media.status').then(s => { setStatus(s); setChecked((s.unused || []).map(f => f.path)); }).catch(e => setError((e && e.message) || 'functions.media.status failed'));
    React.useEffect(() => {
      const on = (e) => { setKind(e.detail); setStatus(null); setError(''); setResult(null); setOutcomes({}); setBusy(false); };
      window.addEventListener('qlc-media-dialog', on);
      return () => window.removeEventListener('qlc-media-dialog', on);
    }, []);
    React.useEffect(() => { if (kind && qlc.online) load(); }, [kind, qlc.online]);
    const close = () => setKind(null);

    const run = async () => {
      if (!status || busy) return;
      setBusy(true); setError('');
      try {
        if (kind === 'collect') {
          const r = await FF.mutate(qlc, 'functions.media.collect', {});
          setResult('Copied ' + r.copied + (r.queued ? ', ' + r.queued + ' copying in the background' : '') + (r.failed ? ', ' + r.failed + ' failed' + (r.error ? ' (' + r.error + ')' : '') : '') + ' — into ' + r.storeDir);
        } else if (kind === 'reload') {
          const out = {};
          for (const f of status.changed) {
            try {
              const r = await FF.mutate(qlc, 'functions.media.reload', { functionId: String(f.functionId) });
              out[f.functionId] = r && r.status ? r.status : 'done';
            } catch (e) { out[f.functionId] = 'failed: ' + ((e && e.message) || e); }
            setOutcomes(Object.assign({}, out));
          }
          setResult('Reloaded ' + Object.values(out).filter(s => s === 'reloaded').length + ' of ' + status.changed.length);
        } else if (kind === 'remove') {
          const r = await qlc.call('functions.media.removeUnused', { files: checked });
          setResult('Removed ' + r.removed + ' file' + (r.removed === 1 ? '' : 's') + (r.complete ? '' : ' — ' + (r.error || 'some files were refused')));
        }
        await load();
      } catch (e) { setError((e && e.message) || 'failed'); }
      setBusy(false);
    };

    const list = !status ? [] : kind === 'collect' ? status.external : kind === 'remove' ? status.unused : status.changed;
    const empty = status && !list.length;
    const verb = kind === 'collect' ? 'Collect' : kind === 'reload' ? 'Reload all' : 'Remove';
    const note = kind === 'collect' ? (status && status.staging ? 'The project is not saved yet: the copies go to a staging folder and move next to the project on its first save.' : 'Every Audio / Video file outside the project\'s media folder is copied into it and the functions are relinked to the copies.')
      : kind === 'reload' ? 'Audio / Video whose original file changed on disk since it was copied into the project. Running functions are skipped.'
      : 'Files in the project\'s media folder that no function uses any more.';
    return (
      <CustomPopupDialog open={!!kind} title={TITLES[kind] || ''} width={520} standardButtons={result || empty ? ['Close'] : ['Cancel', verb]}
        onClicked={(b) => { if (b === verb) run(); else close(); }} onClose={close}>
        <div data-e2e={'media-dialog-' + kind} style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          <RobotoText label={note} fontSize={13} labelColor="var(--fg-light)" wrapText height="auto" />
          {!status && !error ? <RobotoText label="Loading…" fontSize={14} labelColor="var(--fg-medium)" /> : null}
          {empty ? <RobotoText label={kind === 'collect' ? 'Nothing to collect: every media file is already in the project.' : kind === 'reload' ? 'No media changed on disk.' : 'No unused media files.'} fontSize={14} /> : null}
          {list.length ? (
            <div style={{ maxHeight: 260, overflow: 'auto', background: 'var(--bg-stronger)', border: 'var(--border-dark)' }}>
              {list.map((f, i) => (
                <div key={f.path || f.functionId || i} style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 6px' }} title={f.path || ''}>
                  {kind === 'remove' ? <input type="checkbox" checked={checked.indexOf(f.path) !== -1} onChange={e => setChecked(c => e.target.checked ? c.concat([f.path]) : c.filter(x => x !== f.path))} /> : null}
                  <img src={window.QLCData.icon(kind === 'reload' ? (f.type === 'Video' ? 'video' : 'audio') : 'audio')} alt="" style={{ width: 16, height: 16 }} />
                  <RobotoText label={kind === 'reload' ? f.name : base(f.path)} fontSize={13} height={26} style={{ flex: 1 }} />
                  <RobotoText label={kind === 'reload' ? (outcomes[f.functionId] || (f.running ? 'running' : 'changed')) : size(f.size)} fontSize={12} height={26} labelColor="var(--fg-light)" />
                </div>
              ))}
            </div>
          ) : null}
          {result ? <RobotoText label={result} fontSize={13} labelColor="var(--check-lime)" wrapText height="auto" /> : null}
          {error ? <RobotoText label={error} fontSize={13} labelColor="var(--override-red)" wrapText height="auto" /> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  window.QLCToolbarItems = (window.QLCToolbarItems || []).concat([MediaDialogHost]);
  window.QLCMenuItems = (window.QLCMenuItems || []).concat([({ online }) => [
    '-',
    { label: 'Collect media into project', icon: 'import', disabled: !online, onClick: () => open('collect') },
    { label: 'Reload changed media', icon: 'redo', disabled: !online, onClick: () => open('reload') },
    { label: 'Remove unused media', fa: 'fa_trash_can', disabled: !online, onClick: () => open('remove') }
  ]]);
})();
