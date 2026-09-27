/**
 * Virtual Console — layout slice: Frame / Solo Frame / Label configuration, PIN prompts and the
 * edit-mode layout tools. Everything here plugs into the registries the shared VC files expose, so
 * this file owns its features end to end:
 *
 *  - window.QLCVCBodies.Frame / .SoloFrame: VCFrameBodyEx replaces the built-in frame body with one
 *    that honours VcFrameConfig (showHeader, showEnable + the Enable button, isCollapsed + the
 *    collapse button, page labels on the multipage pager) and locks a PIN-protected frame behind a
 *    PIN prompt (vc.frame.validatePin - a correct PIN unlocks for this browser session only).
 *  - window.QLCVCProperties.Frame / .SoloFrame / .Label: VCFrameProperties.qml section by section
 *    (Header, Solo Frame Options, Pages incl. "Clone first page widgets", Shortcuts = page labels) plus
 *    the PIN setup (vc.frame.setPin); a Label has no type-specific config by design.
 *  - window.QLCVCEditTools: VCLayoutTools, the toolbar block for align (left/hcenter/right/top/vcenter/
 *    bottom, reference = first selected widget), distribute (horizontal/vertical, 3+ widgets), "Add
 *    widgets from functions" (vc.widget.createFromFunctions), "Create a widget matrix"
 *    (vc.widget.createMatrix) and the Usage popup of the selected widget's function (vc.widget.usage).
 *  - window.VCPinDialog: the 4-digit prompt VirtualConsole.jsx also uses for PIN-protected pages.
 *
 * Loaded after vc-edit.jsx (uses its exported PropRow/CheckRow/TextField/FunctionPicker/VCUsageDialog)
 * and before VirtualConsole.jsx (which reads the registries when it renders).
 */
const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomCheckBox, CustomComboBox, CustomTextInput, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

const VC_NO_FUNCTION_ID = '4294967295';
/* Font Awesome 7 Solid codepoints missing from the bundle's FA map (FaIcon renders a raw glyph): lock-open, power-off. */
const VC_FA_LOCK_OPEN = '';
const VC_FA_POWER_OFF = '';

/* ---------------------------------------------------------------- PIN dialogs */
/** 4-digit PIN prompt (PopupPINRequest.qml). onSubmit(value) resolves true when accepted. */
function VCPinDialog({ open, title, onClose, onSubmit }) {
  const [value, setValue] = React.useState('');
  const [wrong, setWrong] = React.useState(false);
  React.useEffect(() => { if (open) { setValue(''); setWrong(false); } }, [open]);
  const submit = () => {
    if (!/^\d{4}$/.test(value)) { setWrong(true); return; }
    Promise.resolve(onSubmit(value)).then(ok => { if (!ok) { setWrong(true); setValue(''); } }).catch(() => setWrong(true));
  };
  return (
    <CustomPopupDialog open={open} title={title || 'Enter the PIN'} width={340} standardButtons={['Cancel', 'Unlock']} onClose={onClose}
      onClicked={(b) => { if (b === 'Unlock') submit(); else onClose(); }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }} data-vc-pin-dialog="">
        <input type="password" inputMode="numeric" pattern="[0-9]*" maxLength={4} value={value} autoFocus placeholder="4 digits" data-vc-pin-input=""
          onChange={(e) => { setValue(e.target.value.replace(/\D/g, '').slice(0, 4)); setWrong(false); }}
          onKeyDown={(e) => { if (e.key === 'Enter') submit(); }}
          style={{ height: 30, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', color: 'var(--fg-main)', padding: '0 8px', fontSize: 18, letterSpacing: 6, textAlign: 'center', fontFamily: 'var(--font-mono)' }} />
        {wrong ? <RobotoText label="The entered PIN is invalid or incorrect" fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" /> : null}
      </div>
    </CustomPopupDialog>
  );
}

/** PIN setup (PopupPINSetup.qml): current PIN when one is set, new PIN + confirmation; empty new PIN clears. */
function VCPinSetupDialog({ open, hasPin, onClose, onApply }) {
  const [cur, setCur] = React.useState('');
  const [next, setNext] = React.useState('');
  const [confirm, setConfirm] = React.useState('');
  const [err, setErr] = React.useState('');
  React.useEffect(() => { if (open) { setCur(''); setNext(''); setConfirm(''); setErr(''); } }, [open]);
  const field = (label, v, set, name) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
      <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 110px' }} />
      <input type="password" inputMode="numeric" maxLength={4} value={v} data-vc-pin-field={name} onChange={(e) => { set(e.target.value.replace(/\D/g, '').slice(0, 4)); setErr(''); }}
        style={{ flex: 1, height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', color: 'var(--fg-main)', padding: '0 8px', fontFamily: 'var(--font-mono)', letterSpacing: 4 }} />
    </div>
  );
  const apply = () => {
    if (next !== confirm) { setErr('The new PIN and its confirmation differ'); return; }
    if (next && !/^\d{4}$/.test(next)) { setErr('A PIN is exactly 4 digits (leave empty to remove it)'); return; }
    Promise.resolve(onApply(cur, next)).then(() => onClose()).catch(e => setErr((e && e.message) || 'The current PIN is incorrect'));
  };
  return (
    <CustomPopupDialog open={open} title={hasPin ? 'Change the frame PIN' : 'Set a frame PIN'} width={380} standardButtons={['Cancel', 'Apply']} onClose={onClose}
      onClicked={(b) => { if (b === 'Apply') apply(); else onClose(); }}>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }} data-vc-pin-setup="">
        {hasPin ? field('Current PIN', cur, setCur, 'current') : null}
        {field(hasPin ? 'New PIN' : 'PIN', next, setNext, 'new')}
        {field('Confirm', confirm, setConfirm, 'confirm')}
        <RobotoText label={hasPin ? 'Leave the new PIN empty to remove the protection.' : 'Operators must enter this PIN before the frame contents are shown.'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" />
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" wrapText /> : null}
      </div>
    </CustomPopupDialog>
  );
}

/* ---------------------------------------------------------------- Frame body */
function VCFrameBodyEx({ w, header = true, children }) {
  const vc = useVC();
  const D = window.QLCData;
  const solo = w.widgetType === 'SoloFrame';
  const style = w.style || {};
  const cfg = w.typeConfig || {};
  const fg = style.foregroundColor || 'var(--fg-main)';
  const fr = vc.live.frames[w.id] || {};
  const showHeader = typeof cfg.showHeader === 'boolean' ? cfg.showHeader : header;
  const pages = Number(cfg.totalPagesNumber) || Number(fr.pages) || 0;
  const current = Number(fr.currentPage) || 0;
  const multipage = cfg.multiPageMode === true || fr.multipage === true || pages > 1;
  const labels = cfg.pageLabels || [];
  const labelOf = (p) => { const e = labels.find(l => Number(l.pageIndex) === p); return e && e.label ? e.label : 'Page ' + (p + 1); };
  const canGoto = !vc.unsupported(VC_METHODS.FRAME_GOTO);
  const goto = (p) => { if (p >= 0 && (!pages || p < pages)) vc.act.frameGoto(w.id, p); };
  const locked = !!cfg.hasPin && vc.pin && !vc.pin.isUnlocked('frame', w.id);
  const [askPin, setAskPin] = React.useState(false);
  const stop = (e) => e.stopPropagation();
  const structural = (method, params) => vcStructural(vc.qlc, method, Object.assign({ widgetId: String(w.id) }, params)).then(() => vc.refresh && vc.refresh()).catch(e => vc.notice(method + ': ' + ((e && e.message) || 'failed')));
  const pager = multipage ? (
    <span data-vc-frame-pager="" style={{ display: 'inline-flex', alignItems: 'center', gap: 2, flex: 'none', pointerEvents: vc.edit ? 'none' : 'auto', background: showHeader ? 'transparent' : 'var(--section-header)', borderRadius: showHeader ? 0 : 'var(--radius-spin)' }}>
      <IconButton faSource="fa_chevron_left" size={22} tooltip="Previous page" disabled={!canGoto || current <= 0} onClick={() => goto(current - 1)} onPointerDown={stop} />
      <RobotoText label={labelOf(current) + (pages ? ' (' + (current + 1) + '/' + pages + ')' : '')} fontSize="var(--text-size-menubar)" height={22} style={{ minWidth: 52, textAlign: 'center', maxWidth: 160, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} textHAlign="center" title={labelOf(current)} />
      <IconButton faSource="fa_chevron_right" size={22} tooltip="Next page" disabled={!canGoto || (pages > 0 && current >= pages - 1)} onClick={() => goto(current + 1)} onPointerDown={stop} />
    </span>
  ) : null;
  /* VCFrameItem.qml header buttons: Enable (when showEnable) and collapse - both persisted (§4a). */
  const enableButton = cfg.showEnable ? (
    <IconButton faSource={VC_FA_POWER_OFF} size={22} checked={!w.isDisabled} tooltip={w.isDisabled ? 'Enable this frame' : 'Disable this frame'} data-vc-frame-enable=""
      onClick={() => structural('vc.widget.update', { isDisabled: !w.isDisabled })} onPointerDown={stop} style={{ pointerEvents: vc.edit ? 'none' : 'auto' }} />
  ) : null;
  const collapseButton = (
    <IconButton faSource={cfg.isCollapsed ? 'fa_chevron_down' : 'fa_chevron_up'} size={22} tooltip={cfg.isCollapsed ? 'Expand' : 'Collapse'} data-vc-frame-collapse=""
      onClick={() => structural('vc.widget.setConfig', { config: { isCollapsed: !cfg.isCollapsed } })} onPointerDown={stop} style={{ pointerEvents: vc.edit ? 'none' : 'auto' }} />
  );
  return (
    <div data-vc-frame-body="" data-vc-frame-locked={locked ? 'true' : undefined} style={{ position: 'absolute', inset: 0, background: style.backgroundColor || 'var(--bg-strong)',
      border: '2px solid ' + (solo ? 'var(--override-red)' : 'var(--border-color-dark)'), overflow: 'hidden' }}>
      {showHeader ? (
        <div style={{ position: 'absolute', left: 0, top: 0, right: 0, height: 'var(--list-item-height)', background: 'var(--section-header)', display: 'flex', alignItems: 'center', gap: 4, padding: '0 4px 0 6px', zIndex: 2 }}>
          <img src={D.icon(solo ? 'soloframe' : 'frame')} alt="" style={{ width: 16, height: 16, flex: 'none' }} />
          <span style={Object.assign({ color: fg, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', flex: 1, minWidth: 0 }, vcFontCss(style), { fontSize: 14 })}>{style.caption || ''}</span>
          {cfg.hasPin ? <img src={D.icon(locked ? 'lock' : 'unlock')} alt="" title={locked ? 'PIN protected' : 'Unlocked for this session'} style={{ width: 14, height: 14, flex: 'none', opacity: .8 }} onError={(e) => { e.target.style.display = 'none'; }} /> : null}
          {pager}
          {enableButton}
          {collapseButton}
        </div>
      ) : (pager || enableButton ? <div style={{ position: 'absolute', right: 2, top: 2, zIndex: 3, display: 'flex', gap: 2 }}>{pager}{enableButton}</div> : null)}
      {locked ? null : children}
      {locked ? (
        <div data-vc-frame-lock="" style={{ position: 'absolute', left: 0, right: 0, top: showHeader ? 'var(--list-item-height)' : 0, bottom: 0, background: 'var(--bg-stronger)', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 8, zIndex: 4, pointerEvents: 'auto' }}>
          <RobotoText label="PIN protected" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="auto" />
          <GenericButton label="Unlock" width={90} height={26} fontSize="var(--text-size-small)" onClick={() => setAskPin(true)} onPointerDown={stop} />
          <VCPinDialog open={askPin} title={'Frame "' + (style.caption || w.widgetType + ' #' + w.id) + '" is PIN protected'} onClose={() => setAskPin(false)}
            onSubmit={(value) => vc.pin.validateFrame(w.id, value).then(ok => { if (ok) { vc.pin.unlock('frame', w.id); setAskPin(false); } return ok; })} />
        </div>
      ) : null}
      {w.isDisabled ? <div style={{ position: 'absolute', inset: 0, background: 'var(--disabled-veil-soft)', zIndex: 5, pointerEvents: 'none' }} /> : null}
    </div>
  );
}

/* ---------------------------------------------------------------- Frame / Label properties */
function VCFrameProps({ w, cfg, setConfig, section }) {
  const vc = useVC();
  const PropRow = window.VCPropRow, CheckRow = window.VCCheckRow, TextField = window.VCTextField;
  const solo = w.widgetType === 'SoloFrame';
  const pages = Number(cfg.totalPagesNumber) || 1;
  const labels = cfg.pageLabels || [];
  const [pinSetup, setPinSetup] = React.useState(false);
  const setLabel = (i, label) => setConfig({ pageLabels: [{ pageIndex: i, label }] });
  return (
    <>
      {section('fheader', 'Header', (
        <div>
          <CheckRow label="Show header" checked={cfg.showHeader !== false} onToggle={(b) => setConfig({ showHeader: b })} />
          <CheckRow label="Show enable button" checked={!!cfg.showEnable} onToggle={(b) => setConfig({ showEnable: b })} />
          <CheckRow label="Collapsed" checked={!!cfg.isCollapsed} onToggle={(b) => setConfig({ isCollapsed: b })} />
        </div>
      ))}
      {solo ? section('fsolo', 'Solo Frame Options', (
        <div>
          <CheckRow label="Exclude monitored functions" checked={!!cfg.excludeMonitoredFunctions} onToggle={(b) => setConfig({ excludeMonitoredFunctions: b })} />
          <CheckRow label="Mix (fade) between functions" checked={!!cfg.soloframeMixing} onToggle={(b) => setConfig({ soloframeMixing: b })} />
        </div>
      )) : null}
      {section('fpages', 'Pages', (
        <div>
          <CheckRow label="Enable pages" checked={!!cfg.multiPageMode} onToggle={(b) => setConfig({ multiPageMode: b })} />
          <CheckRow label="Circular pages scrolling" checked={!!cfg.pagesLoop} onToggle={(b) => setConfig({ pagesLoop: b })} disabled={!cfg.multiPageMode} />
          <PropRow label="Pages number">
            <CustomSpinBox value={pages} from={1} to={100} width={70} height={24} disabled={!cfg.multiPageMode} onValueModified={(v) => { if (v !== pages) setConfig({ totalPagesNumber: v }); }} data-vc-frame-pages="" />
          </PropRow>
          <div style={{ padding: '2px 6px' }}>
            <GenericButton label="Clone first page widgets" width="100%" height={24} fontSize="var(--text-size-menubar)" disabled={pages < 2} data-vc-frame-clone=""
              onClick={() => vc.editApi.cloneFirstPage && vc.editApi.cloneFirstPage(w.id)} />
          </div>
        </div>
      ))}
      {pages > 1 ? section('fshort', 'Shortcuts', (
        <div>
          {Array.from({ length: pages }, (_, i) => {
            const e = labels.find(l => Number(l.pageIndex) === i);
            return (
              <PropRow key={i} label={'Page ' + (i + 1)}>
                <TextField value={e ? e.label : ''} placeholder={'Page ' + (i + 1)} onCommit={(t) => setLabel(i, t)} />
              </PropRow>
            );
          })}
        </div>
      )) : null}
      {section('fpin', 'Security', (
        <div style={{ padding: '2px 6px', display: 'flex', flexDirection: 'column', gap: 4 }}>
          <RobotoText label={cfg.hasPin ? 'This frame is PIN protected.' : 'No PIN set - the frame is visible to everyone.'} fontSize="var(--text-size-small)" labelColor={cfg.hasPin ? 'var(--fg-main)' : 'var(--fg-medium)'} height="auto" wrapText />
          <GenericButton label={cfg.hasPin ? 'Change / remove PIN' : 'Set a PIN'} width="100%" height={24} fontSize="var(--text-size-menubar)" onClick={() => setPinSetup(true)} data-vc-frame-pin="" />
          <VCPinSetupDialog open={pinSetup} hasPin={!!cfg.hasPin} onClose={() => setPinSetup(false)}
            onApply={(cur, next) => vc.editApi.frameSetPin(w.id, cur, next).then(() => { if (!next) vc.pin.unlock('frame', w.id); vc.notice(next ? 'PIN set' : 'PIN removed'); })} />
        </div>
      ))}
    </>
  );
}

function VCLabelProps({ section }) {
  return section('label', 'Label', (
    <RobotoText label="A label has no settings beyond its text, colours and font - see Basic properties above (VCLabel adds nothing to VCWidget)." fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" wrapText height="auto" style={{ padding: 6 }} />
  ));
}

/* ---------------------------------------------------------------- Layout tools (toolbar) */
/** Where a bulk creator puts its widgets: the single selected Frame/SoloFrame, else the page root; below the existing siblings. */
function vcCreationTarget(vc) {
  const sel = vc.selection.map(id => vc.byId[id]).filter(Boolean);
  const frame = sel.length === 1 && (sel[0].widgetType === 'Frame' || sel[0].widgetType === 'SoloFrame') ? sel[0] : null;
  const parentKey = frame ? frame.id : null;
  let bottom = frame ? 26 : 0;
  vc.widgets.forEach(x => { if ((x.parentId || null) === parentKey && x.geometry) bottom = Math.max(bottom, (x.geometry.y || 0) + (x.geometry.height || 0)); });
  const params = { position: { x: Math.round(VC_SNAP), y: Math.round(bottom + VC_SNAP) } };
  if (frame) params.parentId = String(frame.id);
  return { params, label: frame ? ((frame.style && frame.style.caption) || 'Frame #' + frame.id) : 'page root' };
}

function VCFunctionsDialog({ open, onClose }) {
  const vc = useVC();
  const D = window.QLCData;
  const [picked, setPicked] = React.useState({});
  const [hint, setHint] = React.useState('button');
  const [needle, setNeedle] = React.useState('');
  const [err, setErr] = React.useState('');
  React.useEffect(() => { if (open) { setPicked({}); setErr(''); setNeedle(''); } }, [open]);
  const n = needle.trim().toLowerCase();
  const list = vc.functions.filter(f => !f.hidden && (!n || f.name.toLowerCase().indexOf(n) !== -1) && (hint !== 'cueList' || f.type === 'Chaser')).slice(0, 200);
  const ids = Object.keys(picked).filter(k => picked[k]);
  const target = vcCreationTarget(vc);
  const add = () => {
    if (!ids.length) { setErr('Pick at least one function'); return; }
    vc.editApi.createFromFunctions(Object.assign({ functionIds: ids, widgetHint: hint }, target.params))
      .then(r => { vc.notice((r.widgetIds || []).length + ' widget' + ((r.widgetIds || []).length === 1 ? '' : 's') + ' added into ' + target.label); onClose(); })
      .catch(e => setErr((e && e.message) || 'vc.widget.createFromFunctions failed'));
  };
  return (
    <CustomPopupDialog open={open} title="Add widgets from functions" width={440} standardButtons={['Cancel', 'Add']} onClose={onClose} onClicked={(b) => { if (b === 'Add') add(); else onClose(); }}>
      <div data-vc-fn-dialog="" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <RobotoText label="Create" fontSize="var(--text-size-small)" height="auto" />
          <CustomComboBox width={220} height={24} currValue={hint} onValueChanged={(v) => { setHint(v); setPicked({}); }}
            model={[{ mLabel: 'a Button per function', mValue: 'button' }, { mLabel: 'an Adjust slider per function', mValue: 'adjustSlider' }, { mLabel: 'a Cue List per Chaser', mValue: 'cueList' }]} />
          <RobotoText label={'in ' + target.label} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" style={{ flex: 1 }} />
        </div>
        <span style={{ display: 'flex', alignItems: 'center', height: 26, background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', gap: 4 }}>
          <img src={D.icon('search')} alt="" style={{ width: 14, height: 14 }} />
          <CustomTextInput text={needle} editing placeholder="Search functions" width="100%" height={22} onTextConfirmed={setNeedle} onChange={(e) => setNeedle(e.target.value)} style={{ fontSize: 'var(--text-size-small)' }} />
        </span>
        <div style={{ maxHeight: 280, overflow: 'auto', border: 'var(--border-dark)' }} data-vc-fn-list="">
          {list.length ? list.map(f => (
            <div key={f.id} role="button" data-vc-fn-row={f.id} onClick={() => setPicked(p => Object.assign({}, p, { [f.id]: !p[f.id] }))}
              style={{ display: 'flex', alignItems: 'center', gap: 6, height: 'var(--list-item-height)', padding: '0 6px', cursor: 'pointer', background: picked[f.id] ? 'var(--highlight)' : 'transparent' }}>
              <CustomCheckBox checked={!!picked[f.id]} size={18} onToggled={() => setPicked(p => Object.assign({}, p, { [f.id]: !p[f.id] }))} />
              <RobotoText label={f.name} fontSize="var(--text-size-small)" height="100%" style={{ flex: 1 }} />
              <RobotoText label={f.type} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="100%" />
            </div>
          )) : <RobotoText label={hint === 'cueList' ? 'No Chaser matches' : 'No function matches'} fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--list-item-height)" leftMargin={6} />}
        </div>
        <RobotoText label={ids.length + ' selected'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" height="auto" />
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" wrapText /> : null}
      </div>
    </CustomPopupDialog>
  );
}

function VCMatrixDialog({ open, onClose }) {
  const vc = useVC();
  const [type, setType] = React.useState('Button');
  const [cols, setCols] = React.useState(3);
  const [rows, setRows] = React.useState(3);
  const [width, setWidth] = React.useState(64);
  const [height, setHeight] = React.useState(64);
  const [solo, setSolo] = React.useState(false);
  const [err, setErr] = React.useState('');
  React.useEffect(() => { if (open) setErr(''); }, [open]);
  const target = vcCreationTarget(vc);
  const create = () => {
    vc.editApi.createMatrix(Object.assign({ matrixType: type, matrixSize: { columns: cols, rows }, widgetSize: { width, height }, soloFrame: solo }, target.params))
      .then(r => { vc.notice(((r.widgetIds || []).length - 1) + ' ' + type.toLowerCase() + 's created in a new ' + (solo ? 'solo frame' : 'frame') + ' in ' + target.label); onClose(); })
      .catch(e => setErr((e && e.message) || 'vc.widget.createMatrix failed'));
  };
  const row = (label, body) => (
    <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
      <RobotoText label={label} fontSize="var(--text-size-small)" height="auto" style={{ flex: '0 0 120px' }} />
      {body}
    </div>
  );
  return (
    <CustomPopupDialog open={open} title="Create a widget matrix" width={380} standardButtons={['Cancel', 'Create']} onClose={onClose} onClicked={(b) => { if (b === 'Create') create(); else onClose(); }}>
      <div data-vc-matrix-dialog="" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {row('Widget type', <CustomComboBox width={160} height={24} currValue={type} onValueChanged={(v) => { setType(v); if (v === 'Slider') { setWidth(57); setHeight(151); } else { setWidth(64); setHeight(64); } }}
          model={[{ mLabel: 'Buttons', mValue: 'Button' }, { mLabel: 'Sliders', mValue: 'Slider' }]} />)}
        {row('Columns x rows', <><CustomSpinBox value={cols} from={1} to={100} width={64} height={24} onValueModified={setCols} data-vc-matrix-cols="" /><RobotoText label="x" fontSize="var(--text-size-small)" height="auto" /><CustomSpinBox value={rows} from={1} to={100} width={64} height={24} onValueModified={setRows} data-vc-matrix-rows="" /></>)}
        {row('Widget size (px)', <><CustomSpinBox value={width} from={1} to={1000} width={64} height={24} onValueModified={setWidth} /><RobotoText label="x" fontSize="var(--text-size-small)" height="auto" /><CustomSpinBox value={height} from={1} to={1000} width={64} height={24} onValueModified={setHeight} /></>)}
        {row('Container', <><CustomCheckBox checked={solo} size={20} onToggled={setSolo} /><RobotoText label="Solo frame (one function at a time)" fontSize="var(--text-size-small)" height="auto" /></>)}
        <RobotoText label={'Placed in ' + target.label + ' (select a single frame first to fill it instead).'} fontSize="var(--text-size-menubar)" labelColor="var(--fg-light)" wrapText height="auto" />
        {err ? <RobotoText label={err} fontSize="var(--text-size-small)" labelColor="var(--selection)" height="auto" wrapText /> : null}
      </div>
    </CustomPopupDialog>
  );
}

/** The Function a widget references, for the Usage tool (VirtualConsole::usageList()'s own rules). */
function vcWidgetFunctionId(w) {
  if (!w) return null;
  const c = w.typeConfig || {};
  const id = w.widgetType === 'Button' ? c.functionID : w.widgetType === 'Slider' ? c.controlledFunction : w.widgetType === 'CueList' ? c.chaserID : null;
  return id != null && String(id) !== VC_NO_FUNCTION_ID && String(id) !== '' ? String(id) : null;
}

function VCLayoutTools() {
  const vc = useVC();
  const D = window.QLCData;
  const [dialog, setDialog] = React.useState(null);
  const sel = vc.selection.map(id => vc.byId[id]).filter(Boolean);
  const sameParent = sel.length > 0 && sel.every(x => (x.parentId || null) === (sel[0].parentId || null));
  const canAlign = sel.length >= 2 && sameParent;
  const canDistribute = sel.length >= 3 && sameParent;
  const usageFn = sel.length === 1 ? vcWidgetFunctionId(sel[0]) : null;
  const usageName = usageFn ? (vc.functions.find(f => String(f.id) === usageFn) || {}).name : null;
  const align = (a) => vc.editApi.align(vc.selection, vc.selection[0], a);
  const distribute = (d) => vc.editApi.distribute(vc.selection, d);
  const alignTip = canAlign ? ' to the first selected widget' : sel.length < 2 ? ' - select 2 or more widgets' : ' - the selected widgets must share one frame';
  const tool = (icon, tip, disabled, onClick, attr) => (
    <IconButton imgSource={D.icon(icon)} size={26} tooltip={tip} disabled={disabled} onClick={onClick} {...attr} />
  );
  return (
    <>
      <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
      {tool('align-left', 'Align left' + alignTip, !canAlign, () => align('left'), { 'data-vc-tool': 'align-left' })}
      {tool('align-right', 'Align right' + alignTip, !canAlign, () => align('right'), { 'data-vc-tool': 'align-right' })}
      {tool('align-top', 'Align top' + alignTip, !canAlign, () => align('top'), { 'data-vc-tool': 'align-top' })}
      {tool('align-bottom', 'Align bottom' + alignTip, !canAlign, () => align('bottom'), { 'data-vc-tool': 'align-bottom' })}
      {tool('distribute-x', 'Distribute horizontally' + (canDistribute ? '' : ' - select 3 or more widgets in one frame'), !canDistribute, () => distribute('horizontal'), { 'data-vc-tool': 'distribute-x' })}
      {tool('distribute-y', 'Distribute vertically' + (canDistribute ? '' : ' - select 3 or more widgets in one frame'), !canDistribute, () => distribute('vertical'), { 'data-vc-tool': 'distribute-y' })}
      <span style={{ width: 1, alignSelf: 'stretch', margin: '6px 2px', background: 'var(--border-color-dark)' }} />
      {tool('functions', 'Add widgets from functions (a button, adjust slider or cue list per function)', !vc.functions.length, () => setDialog('functions'), { 'data-vc-tool': 'from-functions' })}
      {tool('buttonmatrix', 'Create a matrix of buttons or sliders', false, () => setDialog('matrix'), { 'data-vc-tool': 'matrix' })}
      <IconButton faSource="fa_circle_info" size={26} tooltip={usageFn ? 'Which widgets use "' + (usageName || 'function #' + usageFn) + '"' : 'Usage - select a widget with a function attached'} disabled={!usageFn} onClick={() => setDialog('usage')} data-vc-tool="usage" />
      <VCFunctionsDialog open={dialog === 'functions'} onClose={() => setDialog(null)} />
      <VCMatrixDialog open={dialog === 'matrix'} onClose={() => setDialog(null)} />
      {dialog === 'usage' && usageFn ? <VCUsageDialog functionId={usageFn} functionName={usageName} onClose={() => setDialog(null)} /> : null}
    </>
  );
}

/* ---------------------------------------------------------------- registrations */
window.QLCVCBodies = Object.assign(window.QLCVCBodies || {}, { Frame: VCFrameBodyEx, SoloFrame: VCFrameBodyEx });
window.QLCVCProperties = Object.assign(window.QLCVCProperties || {}, { Frame: VCFrameProps, SoloFrame: VCFrameProps, Label: VCLabelProps });
window.QLCVCEditTools = (window.QLCVCEditTools || []).concat([VCLayoutTools]);
Object.assign(window, { VCPinDialog, VCPinSetupDialog, VCFrameBodyEx, VCLayoutTools, VC_FA_LOCK_OPEN });
