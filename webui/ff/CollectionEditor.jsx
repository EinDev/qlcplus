/**
 * CollectionEditor.jsx — the Collection editor, modelled on
 * qmlui/qml/fixturesfunctions/CollectionEditor.qml: an ordered member list with add (function
 * picker over the loaded function list, excluding the collection itself and current members),
 * remove, move up / down, plus the shared timing block.
 *
 * Server: functions.collection.addFunction / removeFunction / setMembers
 * (controlapi/src/domains/apiefxcollectiondomain.cpp). Edits are optimistic like every other
 * editor (see FunctionEditors.jsx): the local typeDetail is patched first, the mutation goes
 * through FF.mutate, a rejection reloads. The server rejects self-membership and loops
 * (a member that already contains this collection) with INVALID_PARAMS; that surfaces through
 * the toolbar error and the list snaps back on reload.
 *
 * Registered into window.QLCEditors.Collection (see ff-core / FixturesFunctions.jsx editorFor).
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton } = window.PatchDesignSystem_5432c9;
  const Icons = window.QLCIcons;

  function CollectionEditor({ qlc, detail, reload, setDetail, functions }) {
    const D = window.QLCData;
    const td = detail.typeDetail || {};
    const members = (td.functions || []).map(String);
    const fid = String(detail.id);
    const [sel, setSel] = React.useState([]);        /* selected member ids */
    const [picker, setPicker] = React.useState(false);
    React.useEffect(() => { setSel(s => s.filter(id => members.indexOf(id) !== -1)); }, [members.join(',')]);

    FF.useForeignEvents(qlc, ['functions.collection.membersChanged'], (topic, d) => {
      if (d && String(d.functionId) === fid) reload();
    }, [fid]);

    const patchMembers = (next) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { functions: next }) }));
    const add = (ids) => {
      const fresh = ids.map(String).filter(id => members.indexOf(id) === -1 && id !== fid);
      if (!fresh.length) return;
      patchMembers(members.concat(fresh));
      FF.mutateSeq(qlc, fresh.map(id => ['functions.collection.addFunction', { functionId: fid, memberFunctionId: id }])).catch(() => reload());
    };
    const removeSelected = () => {
      if (!sel.length) return;
      const gone = sel.slice();
      patchMembers(members.filter(id => gone.indexOf(id) === -1));
      setSel([]);
      FF.mutateSeq(qlc, gone.map(id => ['functions.collection.removeFunction', { functionId: fid, memberFunctionId: id }])).catch(() => reload());
    };
    const move = (dir) => {
      if (sel.length !== 1) return;
      const from = members.indexOf(sel[0]), to = from + dir;
      if (from < 0 || to < 0 || to >= members.length) return;
      const next = members.slice(); const [m] = next.splice(from, 1); next.splice(to, 0, m);
      patchMembers(next);
      FF.mutate(qlc, 'functions.collection.setMembers', { functionId: fid, functions: next }, { key: 'collection:' + fid + ':order' }).catch(() => reload());
    };
    const fnOf = (id) => functions.find(x => String(x.id) === String(id));
    const funcItems = functions.filter(f => String(f.id) !== fid && members.indexOf(String(f.id)) === -1 && f.type !== 'Show')
      .map(f => ({ id: String(f.id), name: f.name, icon: D.icon(Icons.FUNCTION_ICONS[f.type] || 'functions'), hint: f.type }));
    const onRowClick = (e, id) => setSel(e.ctrlKey || e.metaKey ? (sel.indexOf(id) !== -1 ? sel.filter(x => x !== id) : sel.concat([id]))
      : e.shiftKey && sel.length ? (() => { const a = members.indexOf(sel[0]), b = members.indexOf(id); return members.slice(Math.min(a, b), Math.max(a, b) + 1); })() : [id]);
    const selIndex = sel.length === 1 ? members.indexOf(sel[0]) : -1;

    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }} data-e2e="collection-editor">
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 8px', background: 'var(--bg-strong)', flex: 'none' }}>
          <span data-e2e="collection-add"><IconButton faSource="fa_plus" size={26} tooltip="Add functions to the collection" onClick={() => setPicker(true)} /></span>
          <span data-e2e="collection-up"><IconButton faSource={FF.GLYPH.arrowUp} size={26} tooltip="Move the selected function up" disabled={selIndex <= 0} onClick={() => move(-1)} /></span>
          <span data-e2e="collection-down"><IconButton faSource={FF.GLYPH.arrowDown} size={26} tooltip="Move the selected function down" disabled={selIndex < 0 || selIndex >= members.length - 1} onClick={() => move(1)} /></span>
          <span data-e2e="collection-remove"><IconButton faSource={FF.GLYPH.minus} size={26} tooltip="Remove the selected functions" disabled={!sel.length} onClick={removeSelected} /></span>
          <div style={{ flex: 1 }} />
          <RobotoText label={members.length + ' functions'} fontSize={13} labelColor="var(--fg-light)" />
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div style={{ flex: 1, minWidth: 0, overflow: 'auto' }}>
            <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--font-roboto)', fontSize: 13, color: 'var(--fg-main)' }}>
              <thead>
                <tr style={{ background: 'var(--bg-strong)', height: 24, textAlign: 'left' }}>
                  <th style={{ width: 34, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>#</th>
                  <th style={{ padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Function</th>
                  <th style={{ width: 110, padding: '0 6px', fontWeight: 400, color: 'var(--fg-light)' }}>Type</th>
                </tr>
              </thead>
              <tbody data-e2e="collection-members">
                {members.map((id, i) => {
                  const f = fnOf(id), on = sel.indexOf(id) !== -1;
                  return (
                    <tr key={id} data-e2e-member={id} onClick={(e) => onRowClick(e, id)}
                      style={{ height: 28, background: on ? 'var(--highlight)' : (i % 2 ? 'var(--bg-medium)' : 'var(--bg-stronger)'), cursor: 'pointer' }}>
                      <td style={{ padding: '0 6px', color: 'var(--fg-light)' }}>{i + 1}</td>
                      <td style={{ padding: '0 6px' }}>
                        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
                          <img src={D.icon(f ? (Icons.FUNCTION_ICONS[f.type] || 'functions') : 'functions')} alt="" style={{ width: 16, height: 16 }} />
                          <span style={{ color: f ? 'var(--fg-main)' : 'var(--fg-medium)' }}>{f ? f.name : 'Function ' + id + ' (missing)'}</span>
                        </span>
                      </td>
                      <td style={{ padding: '0 6px', color: 'var(--fg-light)' }}>{f ? f.type : '—'}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
            {!members.length ? <div style={{ padding: 12 }}><RobotoText label="No functions. Use + to add some; they all start together when the collection starts." fontSize={14} labelColor="var(--fg-medium)" /></div> : null}
          </div>
          <div style={{ width: 300, flex: 'none', overflow: 'auto', borderLeft: 'var(--border-dark)', padding: 10, display: 'flex', flexDirection: 'column', gap: 4 }}>
            <FF.TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} showRun={false} />
            <FF.Note text="A Collection starts every member at once and stops when the last one stops; the fade times above are passed down to members that use the collection's speed." style={{ marginTop: 6 }} />
          </div>
        </div>
        <FF.PickerDialog open={picker} title="Add functions to the collection" items={funcItems} onPick={add} onClose={() => setPicker(false)} />
      </div>
    );
  }

  window.QLCEditors = Object.assign(window.QLCEditors || {}, { Collection: CollectionEditor });
  Object.assign(FF, { CollectionEditor });
})();
