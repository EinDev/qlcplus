/**
 * ScriptEditor.jsx — editor for Script functions, modelled on
 * qmlui/qml/fixturesfunctions/ScriptEditor.qml: a monospace text editor with line numbers, the
 * "add a method call at cursor" menu (from functions.script.listCommands' snippets), fixture /
 * function ID pickers (the QML editor's side trees + drag-and-drop), a syntax check
 * (functions.script.validate, error lines marked in the gutter) and Save (functions.script.setSource,
 * also Ctrl+S). The QML editor auto-saves 500 ms after every keystroke; here the text stays local
 * until Save so a half-typed line never reaches the engine — a dirty marker shows the difference.
 *
 * Registered as window.QLCEditors.Script (see FixturesFunctions.jsx editorFor()).
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton } = window.PatchDesignSystem_5432c9;

  const SNIPPET_GLYPH = { play: 'fa_play', stop: FF.GLYPH.stop, sliders: '', hourglass: '', dice: '', moon: '', terminal: '' };

  function ScriptEditor({ qlc, detail, reload, setDetail, functions, fixtures }) {
    const D = window.QLCData;
    const fid = String(detail.id);
    const td = detail.typeDetail || {};
    const serverSource = td.source || '';
    const [text, setText] = React.useState(serverSource);
    const [dirty, setDirty] = React.useState(false);
    const [errors, setErrors] = React.useState(td.syntaxErrors || []);
    const [checked, setChecked] = React.useState(null);   /* 'ok' | 'errors' | null after last Validate */
    const [snippets, setSnippets] = React.useState([]);
    const [menu, setMenu] = React.useState(null);          /* {x, y} */
    const [picker, setPicker] = React.useState(null);      /* 'functions' | 'fixtures' */
    const [caret, setCaret] = React.useState({ line: 1, col: 1 });
    const taRef = React.useRef(null);
    const gutterRef = React.useRef(null);

    /* Server text changed underneath us (own save echo, another client, undo): adopt it unless we
       hold unsaved edits. */
    React.useEffect(() => { if (!dirty) { setText(serverSource); setErrors(td.syntaxErrors || []); setChecked(null); } }, [serverSource, fid]);
    React.useEffect(() => { setDirty(false); setText(serverSource); setErrors(td.syntaxErrors || []); setChecked(null); }, [fid]);
    React.useEffect(() => {
      if (!qlc.online) return;
      qlc.call('functions.script.listCommands', {}).then(r => setSnippets((r && r.snippets) || [])).catch(() => setSnippets([]));
    }, [qlc.online]);
    FF.useForeignEvents(qlc, ['functions.script.sourceChanged'], (topic, d) => { if (d && String(d.functionId) === fid) reload(); }, [fid]);

    const lines = text.split('\n');
    const errorLines = {};
    errors.forEach(e => { if (e && e.line) errorLines[e.line] = e.message; });

    const insertAtCursor = (str, caretOffset) => {
      const ta = taRef.current;
      const start = ta ? ta.selectionStart : text.length, end = ta ? ta.selectionEnd : text.length;
      const next = text.slice(0, start) + str + text.slice(end);
      setText(next); setDirty(true);
      const pos = start + str.length - (caretOffset || 0);
      requestAnimationFrame(() => { if (ta) { ta.focus(); ta.setSelectionRange(pos, pos); } });
    };
    const insertSnippet = (s) => { insertAtCursor(s.insert + '\n', (s.caretOffset || 0) + 1); setMenu(null); };
    const save = () => {
      if (!dirty) return;
      setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { source: text }) }));
      setDirty(false);
      FF.mutate(qlc, 'functions.script.setSource', { functionId: fid, source: text }, { key: 'script:' + fid })
        .then(() => validate(true)).catch(() => { setDirty(true); reload(); });
    };
    const revert = () => { setText(serverSource); setDirty(false); setErrors(td.syntaxErrors || []); setChecked(null); };
    /* The server validates what it holds, so unsaved text is saved first. */
    const validate = (afterSave) => {
      if (dirty && !afterSave) { save(); return; }
      qlc.call('functions.script.validate', { functionId: fid }).then(r => {
        const errs = (r && r.syntaxErrors) || [];
        setErrors(errs); setChecked(errs.length ? 'errors' : 'ok');
      }).catch(e => FF.reportError(e, 'functions.script.validate'));
    };
    const goToLine = (n) => {
      const ta = taRef.current; if (!ta) return;
      let pos = 0; for (let i = 0; i < n - 1 && i < lines.length; i++) pos += lines[i].length + 1;
      ta.focus(); ta.setSelectionRange(pos, pos + (lines[n - 1] || '').length);
      const lh = 18; ta.scrollTop = Math.max(0, (n - 4) * lh);
    };
    const updateCaret = () => {
      const ta = taRef.current; if (!ta) return;
      const before = ta.value.slice(0, ta.selectionStart).split('\n');
      setCaret({ line: before.length, col: before[before.length - 1].length + 1 });
    };
    const onKeyDown = (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); save(); return; }
      if (e.key === 'Tab') { e.preventDefault(); insertAtCursor('    ', 0); }
    };
    const funcItems = (functions || []).filter(f => String(f.id) !== fid).map(f => ({ id: String(f.id), name: f.name, icon: D.icon(window.QLCIcons.FUNCTION_ICONS[f.type] || 'functions'), hint: f.type + ' · ID ' + f.id }));
    const fixtureItems = (fixtures || []).map(f => ({ id: String(f.id), name: f.name, icon: D.icon(window.QLCIcons.FIXTURE_TYPE_ICONS[f.fixtureType] || 'fixture'), hint: 'ID ' + f.id }));
    const lineHeight = 18;

    return (
      <div className="qlc-script-editor" style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4, padding: '4px 8px', background: 'var(--bg-strong)', flex: 'none' }}>
          <IconButton faSource="fa_plus" size={26} tooltip="Add a method call at cursor position" onClick={(e) => { const r = e.currentTarget.getBoundingClientRect(); setMenu({ x: r.left, y: r.bottom + 2 }); }} />
          <IconButton imgSource={D.icon('functions')} size={26} tooltip="Insert a function ID at the cursor" onClick={() => setPicker('functions')} />
          <IconButton imgSource={D.icon('fixture')} size={26} tooltip="Insert a fixture ID at the cursor" onClick={() => setPicker('fixtures')} />
          <div style={{ width: 1, height: 20, background: 'var(--border-color-dark)', margin: '0 3px' }} />
          <GenericButton label="Check syntax" width={110} height={26} onClick={() => validate(false)} />
          <GenericButton label={dirty ? 'Save (Ctrl+S)' : 'Saved'} width={110} height={26} disabled={!dirty} onClick={save} />
          <GenericButton label="Revert" width={70} height={26} disabled={!dirty} onClick={revert} />
          <div style={{ flex: 1 }} />
          <RobotoText label={(dirty ? 'unsaved · ' : '') + 'Ln ' + caret.line + ', Col ' + caret.col + ' · ' + lines.length + ' lines'} fontSize={13} labelColor={dirty ? 'var(--selection, #f0a030)' : 'var(--fg-light)'} />
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex', background: 'var(--bg-stronger)' }}>
          <div ref={gutterRef} style={{ width: 46, flex: 'none', overflow: 'hidden', borderRight: 'var(--border-dark)', background: 'var(--bg-strong)', paddingTop: 6 }}>
            {lines.map((_, i) => {
              const n = i + 1, bad = !!errorLines[n];
              return <div key={n} title={bad ? errorLines[n] : ''} onClick={() => goToLine(n)} style={{ height: lineHeight, lineHeight: lineHeight + 'px', textAlign: 'right', paddingRight: 6, fontFamily: 'var(--font-mono, Consolas, monospace)', fontSize: 12, color: bad ? '#fff' : 'var(--fg-medium)', background: bad ? 'var(--check-red, #c03030)' : 'transparent', cursor: 'pointer' }}>{n}</div>;
            })}
          </div>
          <textarea ref={taRef} value={text} spellCheck={false} wrap="off"
            onChange={e => { setText(e.target.value); setDirty(true); setChecked(null); }}
            onKeyDown={onKeyDown} onKeyUp={updateCaret} onClick={updateCaret}
            onScroll={e => { if (gutterRef.current) gutterRef.current.scrollTop = e.target.scrollTop; if (gutterRef.current) gutterRef.current.style.paddingTop = (6 - e.target.scrollTop) + 'px'; }}
            style={{ flex: 1, minWidth: 0, resize: 'none', border: 'none', outline: 'none', padding: '6px 8px', margin: 0, background: 'transparent', color: 'var(--fg-main)', fontFamily: 'var(--font-mono, Consolas, monospace)', fontSize: 13, lineHeight: lineHeight + 'px', tabSize: 4, whiteSpace: 'pre', overflow: 'auto' }} />
        </div>
        <div style={{ flex: 'none', maxHeight: 120, overflow: 'auto', borderTop: 'var(--border-dark)', background: 'var(--bg-strong)', padding: '4px 8px' }}>
          {checked === 'ok' ? <RobotoText label="No errors found." fontSize={13} labelColor="var(--check-lime)" height={20} /> : null}
          {errors.map((e, i) => (
            <div key={i} onClick={() => e.line && goToLine(e.line)} style={{ display: 'flex', gap: 6, alignItems: 'center', height: 20, cursor: e.line ? 'pointer' : 'default' }}>
              <RobotoText label={e.line ? 'Line ' + e.line : 'Script'} fontSize={13} labelColor="var(--check-red, #e05050)" height={20} style={{ width: 60, flex: 'none' }} />
              <RobotoText label={e.message || ''} fontSize={13} height={20} style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} />
            </div>
          ))}
          {!errors.length && checked !== 'ok' ? <FF.Note text="Scripts are JavaScript run by the engine: call Engine.startFunction(id), Engine.setFixture(id, channel, value), Engine.waitTime(ms), … — use + to insert a call, Check syntax to validate on the server, Save to store it." /> : null}
        </div>
        <FF.PopupMenu open={!!menu} x={menu ? menu.x : 0} y={menu ? menu.y : 0} onClose={() => setMenu(null)} width={220}>
          {snippets.map(s => <FF.MenuItem key={s.label} glyph={SNIPPET_GLYPH[s.icon] || 'fa_plus'} text={s.label} onClick={() => insertSnippet(s)} />)}
          {!snippets.length ? <FF.MenuItem text="(no snippets from this server)" disabled onClick={() => {}} /> : null}
        </FF.PopupMenu>
        <FF.PickerDialog open={picker === 'functions'} title="Insert a function ID" items={funcItems} multi={false} confirmLabel="Insert"
          onPick={ids => { if (ids.length) insertAtCursor(String(ids[0]), 0); }} onClose={() => setPicker(null)} />
        <FF.PickerDialog open={picker === 'fixtures'} title="Insert a fixture ID" items={fixtureItems} multi={false} confirmLabel="Insert"
          onPick={ids => { if (ids.length) insertAtCursor(String(ids[0]), 0); }} onClose={() => setPicker(null)} />
      </div>
    );
  }

  window.QLCEditors = Object.assign(window.QLCEditors || {}, { Script: ScriptEditor });
  FF.ScriptEditor = ScriptEditor;
})();
