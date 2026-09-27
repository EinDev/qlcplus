/**
 * RgbMatrixEditor.jsx — the RGB Matrix function editor, modelled on
 * qmlui/qml/fixturesfunctions/RGBMatrixEditor.qml + RGBMatrixPreview.qml (backed by
 * qmlui/rgbmatrixeditor.cpp). Registers itself as window.QLCEditors.RGBMatrix.
 *
 * Server side: controlapi/src/domains/apirgbmatrixdomain.cpp — functions.rgbmatrix.listAlgorithms /
 * getScriptProperties / setConfig / setScriptProperty / getPreview, and functions.get's typeDetail
 * {config: FunctionsRgbMatrixConfig}. setConfig accepts a partial config (absent keys unchanged),
 * so every control sends only what it changed, optimistically patched into the local detail and
 * queued through FF.mutate (revision-gated, key-coalesced). Structural switches (algorithm, fixture
 * group) reload the detail afterwards because the engine derives state from them (a script's
 * property list and colour read-back, the group's size).
 *
 * Preview: the Qt editor animates in-process (RGBMatrixEditor::slotPreviewTimeout at MasterTimer's
 * tick). Here one frame is fetched per step with getPreview and the step is advanced client-side
 * on the function's own step duration (run order / direction honoured like RGBMatrixStep::
 * checkNextStep), capped at 10 fps; Beats tempo has no beat feed in the browser and advances at a
 * fixed 500 ms. Polling stops on unmount. Cells without a head in the group are outlined only,
 * like the QML preview leaves them transparent.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomTextInput } = window.PatchDesignSystem_5432c9;

  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 14, padding: '0 6px' };
  const PREVIEW_PERIOD = 100;   /* ms between polls, 10 fps cap */
  const BEAT_PERIOD = 500;      /* Beats tempo: no beat feed in the browser, 120 BPM assumed */

  /* Same order as RGBMatrixEditor.qml's combos */
  const CONTROL_MODES = [['rgb', 'RGB'], ['white', 'White'], ['amber', 'Amber'], ['uv', 'UV'], ['dimmer', 'Dimmer'], ['shutter', 'Shutter']];
  const BLEND_MODES = [['Normal', 'Default (HTP)'], ['Mask', 'Mask'], ['Additive', 'Additive'], ['Subtractive', 'Subtractive']];
  const TEXT_ANIMATIONS = [['staticLetters', 'Letters'], ['horizontal', 'Horizontal'], ['vertical', 'Vertical']];
  const IMAGE_ANIMATIONS = [['static', 'Static'], ['horizontal', 'Horizontal'], ['vertical', 'Vertical'], ['animation', 'Animation']];
  const comboModel = (pairs) => pairs.map(([v, l]) => ({ mLabel: l, mValue: v }));

  /* ---- server catalogs, cached per connection ------------------------------------------------ */
  const algorithmCache = new WeakMap();      /* client -> Promise<algorithms[]> */
  const propertyDefCache = new WeakMap();    /* client -> Map<scriptName, Promise<defs[]>> */
  function listAlgorithms(qlc) {
    const client = qlc.client && qlc.client();
    if (!client) return Promise.resolve([]);
    if (!algorithmCache.has(client)) {
      algorithmCache.set(client, qlc.call('functions.rgbmatrix.listAlgorithms').then(r => r.algorithms || []).catch(e => { algorithmCache.delete(client); throw e; }));
    }
    return algorithmCache.get(client);
  }
  function scriptPropertyDefs(qlc, scriptName) {
    const client = qlc.client && qlc.client();
    if (!client || !scriptName) return Promise.resolve([]);
    if (!propertyDefCache.has(client)) propertyDefCache.set(client, new Map());
    const m = propertyDefCache.get(client);
    if (!m.has(scriptName)) m.set(scriptName, qlc.call('functions.rgbmatrix.getScriptProperties', { scriptName }).then(r => r.properties || []).catch(e => { m.delete(scriptName); throw e; }));
    return m.get(scriptName);
  }

  /* ---- small inputs -------------------------------------------------------------------------- */
  /** Text field that commits on Enter / blur only (never per keystroke). */
  function CommitText({ value, onCommit, width = '100%', placeholder, mono, testId }) {
    const [text, setText] = React.useState(value == null ? '' : String(value));
    React.useEffect(() => { setText(value == null ? '' : String(value)); }, [value]);
    const commit = () => { if (text !== (value == null ? '' : String(value))) onCommit(text); };
    return <input value={text} placeholder={placeholder} data-rgb={testId} onChange={e => setText(e.target.value)} onBlur={commit}
      onKeyDown={e => { if (e.key === 'Enter') { commit(); e.target.blur(); } else if (e.key === 'Escape') setText(value == null ? '' : String(value)); }}
      style={Object.assign({}, inputStyle, { width, fontFamily: mono ? 'var(--font-mono)' : 'var(--font-roboto)' })} />;
  }

  /** One of the up-to-5 colour slots: swatch picker + hex field, X resets slots >= 1 to "unset". */
  function ColorSlot({ index, value, onChange }) {
    const set = !!value;
    const hex = set ? value : '#000000';
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
        <RobotoText label={'Color ' + (index + 1)} fontSize={13} labelColor="var(--fg-light)" style={{ width: 50 }} />
        <input type="color" value={hex} data-rgb={'color' + index} onChange={e => onChange(e.target.value)} title={set ? value : 'Not set - the algorithm picks its own'}
          style={{ width: 34, height: 24, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)', opacity: set ? 1 : 0.35, cursor: 'pointer' }} />
        <CommitText value={set ? value : ''} placeholder="auto" mono width={78} testId={'hex' + index}
          onCommit={t => { const c = FF.parseHex(t); if (c) onChange(FF.hex(c)); else if (!t.trim() && index > 0) onChange(null); }} />
        {index > 0 ? <IconButton faSource="fa_xmark" size={22} tooltip="Reset this color (let the algorithm choose)" disabled={!set} onClick={() => onChange(null)} /> : null}
      </div>
    );
  }

  /* ---- preview ------------------------------------------------------------------------------- */
  /** Poll getPreview and step through the algorithm on the function's own timing. */
  function usePreview(qlc, fid, detail, configKey, enabled) {
    const [frame, setFrame] = React.useState(null);
    const st = React.useRef({ step: 0, forward: true, elapsed: 0, busy: false, alive: false, steps: 0 });
    const fetchRef = React.useRef(null);
    const runOrder = detail.runOrder, direction = detail.direction, tempoType = detail.tempoType, duration = detail.duration;
    React.useEffect(() => {
      if (!enabled) { setFrame(null); return undefined; }
      const s = st.current;
      s.alive = true; s.forward = direction !== 'Backward'; s.step = s.forward ? 0 : -1; s.elapsed = 0;
      const fetch = async () => {
        if (s.busy || !s.alive) return;
        s.busy = true;
        try {
          const r = await qlc.call('functions.rgbmatrix.getPreview', { functionId: fid, step: s.step });
          if (!s.alive) return;
          s.steps = r.stepsCount || 0;
          s.step = r.step != null ? r.step : s.step;
          setFrame(r);
        } catch (e) { /* editor stays on the last frame; errors surface through FF.reportError elsewhere */ }
        finally { s.busy = false; }
      };
      fetchRef.current = fetch;
      fetch();
      const stepMs = tempoType === 'Beats' ? BEAT_PERIOD : Number(duration);
      const animate = stepMs > 0 && stepMs < FF.INFINITE;
      const timer = setInterval(() => {
        if (!animate) return;
        s.elapsed += PREVIEW_PERIOD;
        if (s.elapsed < Math.max(stepMs, PREVIEW_PERIOD)) return;
        s.elapsed = 0;
        const n = s.steps;
        if (n <= 1) return;
        /* RGBMatrixStep::checkNextStep */
        if (runOrder === 'Random') s.step = Math.floor(Math.random() * n);
        else if (runOrder === 'SingleShot') { if (s.forward ? s.step < n - 1 : s.step > 0) s.step += s.forward ? 1 : -1; else return; }
        else if (runOrder === 'PingPong') {
          if (s.forward && s.step + 1 >= n) { s.forward = false; s.step = Math.max(0, n - 2); }
          else if (!s.forward && s.step - 1 < 0) { s.forward = true; s.step = Math.min(1, n - 1); }
          else s.step += s.forward ? 1 : -1;
        }
        else s.step = ((s.step + (s.forward ? 1 : -1)) % n + n) % n;
        fetch();
      }, PREVIEW_PERIOD);
      return () => { s.alive = false; clearInterval(timer); fetchRef.current = null; };
    }, [fid, enabled, runOrder, direction, tempoType, duration]);
    /* a config change re-renders the current step right away (colours, properties, text...) */
    React.useEffect(() => { if (fetchRef.current) fetchRef.current(); }, [configKey]);
    return frame;
  }

  function PreviewGrid({ frame, heads, width = 400, maxHeight = 260 }) {
    const ref = React.useRef(null);
    const cols = frame ? frame.width : 0, rows = frame ? frame.height : 0;
    const cell = cols && rows ? Math.max(3, Math.floor(Math.min(width / cols, maxHeight / rows))) : 0;
    const cw = cell * cols, ch = cell * rows;
    React.useEffect(() => {
      const c = ref.current; if (!c || !frame || !cell) return;
      const g = c.getContext('2d');
      g.fillStyle = '#000'; g.fillRect(0, 0, cw, ch);
      g.lineWidth = 1;
      for (let y = 0; y < rows; y++) {
        const row = frame.pixels[y] || [];
        for (let x = 0; x < cols; x++) {
          const hasHead = !heads || heads.has(x + ',' + y);
          const v = row[x] || 0;
          const px = x * cell + 0.5, py = y * cell + 0.5, s = cell - 1;
          if (hasHead) {
            g.fillStyle = '#' + ('000000' + v.toString(16)).slice(-6);
            g.fillRect(px, py, s, s);
            g.strokeStyle = '#555';
          } else {
            g.strokeStyle = '#2a2a2a';
          }
          g.strokeRect(px, py, s, s);
        }
      }
    }, [frame, heads, cell, cw, ch]);
    if (!frame || !cols || !rows) {
      return <div data-rgb="preview-empty" style={{ width, height: 80, background: '#000', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        <RobotoText label={frame ? 'Fixture group is empty (0 x 0)' : 'No preview: pick a fixture group'} fontSize={13} labelColor="var(--fg-medium)" /></div>;
    }
    return (
      <div style={{ width, background: '#000', display: 'flex', justifyContent: 'center', padding: '5px 0' }}>
        <canvas ref={ref} width={cw} height={ch} data-rgb="preview" data-step={frame.step} data-steps={frame.stepsCount} style={{ width: cw, height: ch, display: 'block' }} />
      </div>
    );
  }

  /* ---- script property editors --------------------------------------------------------------- */
  function ScriptProperties({ defs, values, onSet }) {
    if (!defs) return <RobotoText label="Loading script properties…" fontSize={13} labelColor="var(--fg-medium)" />;
    if (!defs.length) return <FF.Note text="This script exposes no properties." />;
    const val = (name) => { const p = (values || []).find(v => v.name === name); return p ? p.value : ''; };
    return (
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }} data-rgb="script-properties">
        {defs.map(d => (
          <FF.Row key={d.name} label={d.displayName || d.name} width={140}>
            {d.type === 'list' ? (
              <CustomComboBox width={170} height={24} currValue={val(d.name)} onValueChanged={v => onSet(d.name, String(v))}
                model={(d.listValues || []).map(v => ({ mLabel: v, mValue: v }))} />
            ) : d.type === 'range' ? (
              <CustomSpinBox value={Math.round(Number(val(d.name)) || 0)} from={d.rangeMin != null ? d.rangeMin : 0} to={d.rangeMax != null ? d.rangeMax : 255} width={90} height={24}
                onValueModified={v => onSet(d.name, String(v))} />
            ) : d.type === 'float' ? (
              <CommitText value={val(d.name)} width={90} mono testId={'prop-' + d.name} onCommit={t => { const n = Number(t); if (!Number.isNaN(n)) onSet(d.name, String(n)); }} />
            ) : (
              <CommitText value={val(d.name)} width={200} testId={'prop-' + d.name} onCommit={t => onSet(d.name, t)} />
            )}
          </FF.Row>
        ))}
      </div>
    );
  }

  /* ---- the editor ---------------------------------------------------------------------------- */
  function RgbMatrixEditor({ qlc, detail, reload, setDetail }) {
    const D = window.QLCData;
    const fid = String(detail.id);
    const td = detail.typeDetail || {};
    const config = td.config || null;
    const algorithm = (config && config.algorithm) || { type: 'plain' };
    const colors = (config && config.colors) || [null, null, null, null, null];
    const groupId = config && config.fixtureGroupId != null ? String(config.fixtureGroupId) : '';

    const [algorithms, setAlgorithms] = React.useState(null);
    const [groups, setGroups] = React.useState([]);
    const [heads, setHeads] = React.useState(null);
    const [propDefs, setPropDefs] = React.useState(null);
    const unsupported = qlc.isUnsupported('functions.rgbmatrix.setConfig') || qlc.isUnsupported('functions.rgbmatrix.getPreview');

    const loadGroups = () => qlc.call('fixtures.group.list').then(r => setGroups(r.groups || [])).catch(() => setGroups([]));
    React.useEffect(() => { if (!qlc.online) return; listAlgorithms(qlc).then(setAlgorithms).catch(() => setAlgorithms([])); loadGroups(); }, [qlc.online]);
    FF.useForeignEvents(qlc, ['fixtures.group.created', 'fixtures.group.deleted', 'fixtures.group.renamed', 'fixtures.group.updated'], () => { loadGroups(); setHeads(null); }, []);
    FF.useForeignEvents(qlc, ['functions.rgbmatrix.configChanged', 'functions.rgbmatrix.scriptPropertyChanged'], (t, d) => { if (d && String(d.functionId) === fid) reload(); }, [fid]);

    /* the head layout of the bound group, for the preview's empty cells */
    React.useEffect(() => {
      setHeads(null);
      if (!qlc.online || !groupId) return;
      let alive = true;
      qlc.call('fixtures.group.get', { groupId }).then(r => { if (alive) setHeads(new Set((r.heads || []).map(h => h.x + ',' + h.y))); }).catch(() => { });
      return () => { alive = false; };
    }, [qlc.online, groupId, groups.length]);

    /* the current script's property definitions */
    const scriptName = algorithm.type === 'script' ? algorithm.scriptName : null;
    React.useEffect(() => {
      setPropDefs(null);
      if (!qlc.online || !scriptName) return;
      let alive = true;
      scriptPropertyDefs(qlc, scriptName).then(d => { if (alive) setPropDefs(d); }).catch(() => { if (alive) setPropDefs([]); });
      return () => { alive = false; };
    }, [qlc.online, scriptName]);

    /* ---- mutations: optimistic local patch + queued server call ---- */
    const patchConfig = (p) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { config: Object.assign({}, (d.typeDetail || {}).config, p) }) }));
    const sendConfig = (cfg, key, structural) => {
      const promise = FF.mutate(qlc, 'functions.rgbmatrix.setConfig', { functionId: fid, config: cfg }, { key: 'rgbmatrix:' + fid + ':' + key });
      return promise.then(() => { if (structural) reload(); }).catch(() => reload());
    };
    const setConfig = (p, structural) => { patchConfig(p); return sendConfig(p, Object.keys(p).join(','), structural); };
    const setAlgo = (p) => {
      const next = Object.assign({}, algorithm, p);
      patchConfig({ algorithm: next });
      const cfg = { algorithm: Object.assign({ type: algorithm.type }, algorithm.type === 'script' ? { scriptName: algorithm.scriptName } : {}, p) };
      return sendConfig(cfg, 'algorithm:' + Object.keys(p).join(','), false);
    };
    const chooseAlgorithm = (name) => {
      const a = (algorithms || []).find(x => x.name === name);
      if (!a) return;
      const algo = a.type === 'script' ? { type: 'script', scriptName: a.name } : { type: a.type };
      patchConfig({ algorithm: Object.assign({ name: a.name, acceptedColors: a.acceptedColors }, algo) });
      sendConfig({ algorithm: algo }, 'algorithm', true);
    };
    const setColor = (i, value) => {
      const next = colors.slice(); while (next.length < 5) next.push(null); next[i] = value;
      setConfig({ colors: next });
    };
    const setScriptProperty = (name, value) => {
      const props = (algorithm.scriptProperties || []).some(p => p.name === name) ? algorithm.scriptProperties.map(p => p.name === name ? { name, value } : p) : (algorithm.scriptProperties || []).concat([{ name, value }]);
      patchConfig({ algorithm: Object.assign({}, algorithm, { scriptProperties: props }) });
      /* reload afterwards: a script may derive state from its properties (plasma.js flips
         acceptColors between 0 and 5 on one), and the engine re-reads the script's colours */
      FF.mutate(qlc, 'functions.rgbmatrix.setScriptProperty', { functionId: fid, propertyName: name, value: String(value) }, { key: 'rgbprop:' + fid + ':' + name }).then(() => reload()).catch(() => reload());
    };

    const configKey = JSON.stringify(config);
    const frame = usePreview(qlc, fid, detail, configKey, !!(qlc.online && config && groupId && !unsupported));

    if (!config) {
      return (
        <div style={{ flex: 1, minHeight: 0, display: 'flex' }}>
          <div style={{ flex: 1, padding: 10 }}>
            <FF.Note text={qlc.online ? 'This server reports no RGB Matrix configuration (functions.get returned no typeDetail.config) - it predates the functions.rgbmatrix.* methods.' : 'Connect to a QLC+ instance to edit this RGB Matrix.'} />
          </div>
          <div style={{ width: 300, flex: 'none', borderLeft: 'var(--border-dark)', padding: 10 }}>
            <FF.TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} />
          </div>
        </div>
      );
    }

    const acceptedColors = algorithm.acceptedColors != null ? algorithm.acceptedColors
      : (() => { const a = (algorithms || []).find(x => x.name === algorithm.name || (algorithm.type === 'script' && x.name === algorithm.scriptName) || (algorithm.type !== 'script' && x.type === algorithm.type)); return a && a.acceptedColors != null ? a.acceptedColors : 0; })();
    const builtins = (algorithms || []).filter(a => a.type !== 'script');
    const scripts = (algorithms || []).filter(a => a.type === 'script');
    const currentAlgoName = algorithm.type === 'script' ? algorithm.scriptName : (algorithm.name || (builtins.find(b => b.type === algorithm.type) || {}).name || '');
    const groupModel = [{ mLabel: groupId ? '(none)' : 'Pick a fixture group…', mValue: '' }].concat(groups.map(g => ({ mLabel: g.name + (g.size ? '  ' + g.size.columns + ' x ' + g.size.rows : ''), mValue: String(g.id) })));
    const offsetRow = (
      <FF.Row label="Offset" width={90}>
        <RobotoText label="X" fontSize={13} labelColor="var(--fg-light)" />
        <CustomSpinBox value={algorithm.xOffset || 0} from={-255} to={255} width={70} height={24} onValueModified={v => setAlgo({ xOffset: v })} />
        <RobotoText label="Y" fontSize={13} labelColor="var(--fg-light)" />
        <CustomSpinBox value={algorithm.yOffset || 0} from={-255} to={255} width={70} height={24} onValueModified={v => setAlgo({ yOffset: v })} />
      </FF.Row>
    );

    return (
      <div style={{ flex: 1, minHeight: 0, display: 'flex' }} data-rgb="editor">
        <div style={{ flex: 1, minWidth: 0, overflow: 'auto', padding: 10, display: 'flex', flexDirection: 'column', gap: 6 }}>
          <FF.Row label="Fixture group" width={110}>
            <CustomComboBox width={290} height={24} currValue={groupId} model={groupModel} onValueChanged={v => { if (v !== groupId) setConfig({ fixtureGroupId: v ? String(v) : null }, true); }} />
            <img src={D.icon('group')} alt="" style={{ width: 18, height: 18, opacity: 0.7 }} />
          </FF.Row>
          <PreviewGrid frame={frame} heads={heads} />
          {frame && frame.stepsCount ? <RobotoText label={'Step ' + (frame.step + 1) + ' / ' + frame.stepsCount + '  ·  ' + frame.width + ' x ' + frame.height + (detail.tempoType === 'Beats' ? '  ·  Beats: preview steps every 500 ms' : '')} fontSize={12} labelColor="var(--fg-light)" height={16} /> : null}

          <FF.Row label="Algorithm" width={110}>
            <select value={currentAlgoName} data-rgb="algorithm" disabled={!algorithms || unsupported} onChange={e => chooseAlgorithm(e.target.value)} style={Object.assign({}, inputStyle, { width: 290 })}>
              {!algorithms ? <option value="">Loading…</option> : null}
              {algorithms && currentAlgoName && !algorithms.some(a => a.name === currentAlgoName) ? <option value={currentAlgoName}>{currentAlgoName + ' (not installed here)'}</option> : null}
              <optgroup label="Built-in">{builtins.map(a => <option key={a.name} value={a.name}>{a.name}</option>)}</optgroup>
              <optgroup label="Scripts">{scripts.map(a => <option key={a.name} value={a.name}>{a.name}</option>)}</optgroup>
            </select>
            {algorithm.type === 'script' ? (() => { const a = scripts.find(x => x.name === algorithm.scriptName); return a ? <RobotoText label={'v' + a.apiVersion + (a.author ? ' · ' + a.author : '')} fontSize={12} labelColor="var(--fg-light)" /> : null; })() : null}
          </FF.Row>
          <FF.Row label="Blend mode" width={110}>
            <CustomComboBox width={170} height={24} currValue={config.blendMode || 'Normal'} model={comboModel(BLEND_MODES)} onValueChanged={v => { if (v !== config.blendMode) setConfig({ blendMode: v }); }} />
          </FF.Row>
          <FF.Row label="Control mode" width={110}>
            <CustomComboBox width={170} height={24} currValue={config.controlMode || 'rgb'} model={comboModel(CONTROL_MODES)} onValueChanged={v => { if (v !== config.controlMode) setConfig({ controlMode: v }); }} />
          </FF.Row>

          {acceptedColors > 0 ? <>
            <FF.Heading text="Colors" style={{ marginTop: 4 }} />
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 18px' }}>
              {Array.from({ length: Math.min(5, acceptedColors) }, (_, i) => <ColorSlot key={i} index={i} value={colors[i] || null} onChange={v => setColor(i, v)} />)}
            </div>
          </> : <FF.Note text="This algorithm generates its own colors." style={{ marginTop: 4 }} />}

          {algorithm.type === 'script' ? <>
            <FF.Heading text="Script properties" style={{ marginTop: 6 }} />
            <ScriptProperties defs={propDefs} values={algorithm.scriptProperties} onSet={setScriptProperty} />
          </> : null}

          {algorithm.type === 'text' ? <>
            <FF.Heading text="Text" style={{ marginTop: 6 }} />
            <FF.Row label="Text" width={90}>
              <input value={algorithm.text || ''} data-rgb="text" onChange={e => setAlgo({ text: e.target.value })} style={Object.assign({}, inputStyle, { width: 290 })} />
            </FF.Row>
            <FF.Row label="Font" width={90}>
              <CommitText value={(algorithm.font || {}).family || ''} width={150} placeholder="Family" testId="font-family" onCommit={t => setAlgo({ font: Object.assign({}, algorithm.font, { family: t }) })} />
              <CustomSpinBox value={(algorithm.font || {}).pointSize || 12} from={4} to={200} width={70} height={24} suffix="pt" onValueModified={v => setAlgo({ font: Object.assign({}, algorithm.font, { pointSize: v }) })} />
              <CustomCheckBox checked={!!(algorithm.font || {}).bold} size={22} onToggled={b => setAlgo({ font: Object.assign({}, algorithm.font, { bold: b }) })} tooltip="Bold" />
              <RobotoText label="Bold" fontSize={13} labelColor="var(--fg-light)" />
              <CustomCheckBox checked={!!(algorithm.font || {}).italic} size={22} onToggled={b => setAlgo({ font: Object.assign({}, algorithm.font, { italic: b }) })} tooltip="Italic" />
              <RobotoText label="Italic" fontSize={13} labelColor="var(--fg-light)" />
            </FF.Row>
            <FF.Row label="Animation" width={90}>
              <CustomComboBox width={170} height={24} currValue={algorithm.animationStyle || 'staticLetters'} model={comboModel(TEXT_ANIMATIONS)} onValueChanged={v => { if (v !== algorithm.animationStyle) setAlgo({ animationStyle: v }); }} />
            </FF.Row>
            {offsetRow}
          </> : null}

          {algorithm.type === 'image' ? <>
            <FF.Heading text="Image" style={{ marginTop: 6 }} />
            <FF.Row label="Image" width={90}>
              <CommitText value={algorithm.imagePath || ''} width={340} placeholder="Path on the QLC+ machine (png, bmp, jpg, gif)" testId="image-path" onCommit={t => setAlgo({ imagePath: t })} />
            </FF.Row>
            <FF.Note text="The file is read by QLC+ itself, so this is a path on the machine running QLC+, not a browser upload." />
            <FF.Row label="Animation" width={90}>
              <CustomComboBox width={170} height={24} currValue={algorithm.animationStyle || 'static'} model={comboModel(IMAGE_ANIMATIONS)} onValueChanged={v => { if (v !== algorithm.animationStyle) setAlgo({ animationStyle: v }); }} />
            </FF.Row>
            {offsetRow}
          </> : null}

          {algorithm.type === 'audio' ? <FF.Note text="Audio Spectrum reacts to the audio input of the QLC+ machine while the function runs; the preview shows one static frame." style={{ marginTop: 6 }} /> : null}
          {unsupported ? <FF.Note text="This server has no functions.rgbmatrix.* methods; the controls above are read-only." style={{ marginTop: 6 }} /> : null}
        </div>
        <div style={{ width: 300, flex: 'none', borderLeft: 'var(--border-dark)', padding: 10, overflow: 'auto' }}>
          <FF.TimingEditor qlc={qlc} detail={detail} setDetail={setDetail} reload={reload} />
        </div>
      </div>
    );
  }

  window.QLCEditors = Object.assign(window.QLCEditors || {}, { RGBMatrix: RgbMatrixEditor });
})();
