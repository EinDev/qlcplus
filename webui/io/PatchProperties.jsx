/**
 * PatchProperties.jsx — per-patch plugin parameters for the Input/Output screen (the browser-side
 * replacement for IORightPanel.qml's "Open the plugin configuration"): a dialog listing the
 * parameters the plugin currently holds for one patched line (io.universe.get's
 * inputPatch/outputPatches[].parameters), editing them through io.patch.setParameters
 * (null = revert to the plugin default), plus "Configure plugin" (io.plugin.configure — opens the
 * plugin's native dialog on the machine running QLC+) and "Rescan lines" (io.plugin.rescan).
 *
 * window.IOPatchProperties = { PatchPropertiesDialog }
 */
(function () {
  'use strict';
  const { RobotoText, IconButton, GenericButton, CustomCheckBox, CustomPopupDialog, CustomComboBox } = window.PatchDesignSystem_5432c9;
  const { Row, TextField, isUnknownMethod, errorText, NOTE, withConflictRetry } = window.IOShared;

  /* Parameter keys the network plugins read (plugins/artnet/src/artnetplugin.h, plugins/E1.31/e131plugin.h,
     plugins/osc/oscplugin.h) — offered as suggestions; any key can still be typed. */
  const KNOWN = {
    ArtNet: { input: ['inputUni'], output: ['outputIP', 'outputUni', 'transmitMode'], feedback: ['outputIP', 'outputUni', 'transmitMode'] },
    'E1.31': { input: ['universe', 'multicast', 'mcastIP', 'ucastPort'], output: ['universe', 'transmitMode', 'priority', 'multicast', 'mcastIP', 'mcastFullIP', 'ucastIP', 'ucastPort'], feedback: [] },
    OSC: { input: ['inputPort', 'feedbackIP', 'feedbackPort'], output: ['outputIP', 'outputPort'], feedback: ['outputIP', 'outputPort'] }
  };
  const HINTS = {
    outputIP: 'Destination IP, e.g. 2.0.0.255 (broadcast) or one node', outputUni: 'Art-Net universe number (0-based)', inputUni: 'Art-Net universe to listen to',
    transmitMode: '"Full" sends all 512 channels, "Partial" only the used ones', universe: 'sACN universe (1-63999)', priority: 'sACN priority 0-200 (default 100)',
    multicast: '1 = multicast (default), 0 = unicast to ucastIP', mcastIP: 'Multicast address override', mcastFullIP: 'Full multicast address', ucastIP: 'Unicast destination IP', ucastPort: 'UDP port (default 5568)',
    inputPort: 'UDP port to listen on', outputPort: 'Destination UDP port', feedbackIP: 'Feedback destination IP', feedbackPort: 'Feedback destination UDP port'
  };

  /** Typed text -> the JSON value the server stores: keep the previous type when it still parses. */
  function coerce(text, previous) {
    const t = String(text).trim();
    if (typeof previous === 'boolean' || /^(true|false)$/i.test(t)) return /^true$/i.test(t);
    if ((typeof previous === 'number' || previous === undefined) && /^-?\d+(\.\d+)?$/.test(t)) return Number(t);
    return t;
  }

  function PatchPropertiesDialog({ open, qlc, universe, direction, index, patch, plugin, onClose, onStatus }) {
    const D = window.QLCData;
    const [status, setStatus] = React.useState({ text: '', error: false });
    const [newKey, setNewKey] = React.useState('');
    const [newValue, setNewValue] = React.useState('');
    const [busy, setBusy] = React.useState(false);
    React.useEffect(() => { if (open) { setStatus({ text: '', error: false }); setNewKey(''); setNewValue(''); } }, [open, universe && universe.id, direction, index]);
    if (!open || !universe || !patch) return null;

    const params = patch.parameters || {};
    const keys = Object.keys(params).sort();
    const pluginName = patch.pluginName;
    const lineName = direction === 'input' ? patch.inputName : patch.outputName;
    const known = (KNOWN[pluginName] || {})[direction] || [];
    const suggestions = known.filter(k => keys.indexOf(k) === -1);
    const setParamsUnsupported = qlc.isUnsupported('io.patch.setParameters');

    const send = (parameters, label) => {
      setBusy(true);
      return withConflictRetry((details) => qlc.call('io.patch.setParameters', { universeId: universe.id, patchType: direction, index: index || 0, parameters, baseRevision: details && details.docRevision != null ? details.docRevision : qlc.docRevision() }))
        .then(() => { setStatus({ text: label, error: false }); if (onStatus) onStatus(label, false); return true; },
          e => { setStatus({ text: isUnknownMethod(e) ? 'Not available: this server has no io.patch.setParameters' : label + ' failed: ' + errorText(e), error: true }); return false; })
        .finally(() => setBusy(false));
    };
    const setOne = (key, value) => { const p = {}; p[key] = value; return send(p, key + ' set to ' + JSON.stringify(value)); };
    const remove = (key) => { const p = {}; p[key] = null; return send(p, key + ' reset to the plugin default'); };
    const add = () => {
      const key = newKey.trim();
      if (!key) return;
      setOne(key, coerce(newValue, undefined)).then(ok => { if (ok) { setNewKey(''); setNewValue(''); } });
    };
    const configure = () => {
      setBusy(true);
      qlc.call('io.plugin.configure', { pluginName })
        .then(r => setStatus({ text: r && r.openedOnHost ? pluginName + ' configuration dialog opened on the QLC+ host (not in this browser); it closed with the response' : 'Configuration requested', error: false }),
          e => setStatus({ text: isUnknownMethod(e) ? 'Not available: this server has no io.plugin.configure' : 'Configure failed: ' + errorText(e), error: true }))
        .finally(() => setBusy(false));
    };
    const rescan = () => {
      setBusy(true);
      qlc.call('io.plugin.rescan', { pluginName })
        .then(() => setStatus({ text: pluginName + ' re-enumerated its lines', error: false }),
          e => setStatus({ text: isUnknownMethod(e) ? 'Not available: this server has no io.plugin.rescan' : 'Rescan failed: ' + errorText(e), error: true }))
        .finally(() => setBusy(false));
    };

    const title = (direction === 'input' ? 'Input' : direction === 'feedback' ? 'Feedback' : 'Output ' + ((index || 0) + 1)) + ' patch properties — ' + universe.name;
    return (
      <CustomPopupDialog open={open} title={title} width={560} standardButtons={['Close']} onClicked={onClose} onClose={onClose}>
        <div data-role="patch-properties" style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <img src={D.icon(window.IOPluginIcon ? window.IOPluginIcon(pluginName) : 'inputoutput')} alt="" style={{ width: 26, height: 26 }} />
            <RobotoText label={pluginName + ' — ' + (lineName || 'line ' + (direction === 'input' ? patch.input : patch.output))} fontSize={14} fontBold height={26} />
            <RobotoText label={'line ' + (direction === 'input' ? patch.input : patch.output)} fontSize={12} labelColor={NOTE} height={26} />
          </div>

          <RobotoText label="Plugin parameters for this line (QLCIOPlugin::setParameter): IP addresses, ports, universe numbers, transmission mode… They are saved with the workspace. A removed key falls back to the plugin default." fontSize={12} labelColor={NOTE} wrapText height="auto" />

          <div style={{ border: '1px solid var(--bg-light)', borderRadius: 4, background: 'var(--bg-stronger)', padding: 6, display: 'flex', flexDirection: 'column', gap: 4 }}>
            {keys.length ? keys.map(k => (
              <Row key={k} label={k} width={130} title={HINTS[k] || ''} style={{ minHeight: 28 }}>
                {typeof params[k] === 'boolean'
                  ? <CustomCheckBox checked={!!params[k]} size={22} disabled={busy || setParamsUnsupported} onToggled={v => setOne(k, v)} data-param={k} />
                  : <TextField value={params[k]} width={260} disabled={busy || setParamsUnsupported} data-param={k} onCommit={(t) => { const v = coerce(t, params[k]); if (v !== params[k]) setOne(k, v); }} />}
                <RobotoText label={typeof params[k]} fontSize={12} labelColor={NOTE} height={24} style={{ width: 56 }} />
                <IconButton faSource="fa_trash_can" faColor="var(--bg-strong)" size={24} tooltip="Remove: revert to the plugin default" disabled={busy || setParamsUnsupported} onClick={() => remove(k)} data-remove-param={k} />
              </Row>
            )) : <RobotoText label="No parameters set on this line — the plugin uses its defaults." fontSize={12} labelColor={NOTE} height={24} />}
          </div>

          <Row label="Add parameter" width={130}>
            <span style={{ width: 150 }}>
              {suggestions.length
                ? <CustomComboBox width={150} height={24} currValue={newKey || '__custom__'} model={[{ mLabel: newKey && suggestions.indexOf(newKey) === -1 ? newKey : 'key…', mValue: '__custom__' }].concat(suggestions.map(k => ({ mLabel: k, mValue: k })))}
                  onValueChanged={v => setNewKey(v === '__custom__' ? '' : v)} data-role="param-key-pick" />
                : null}
            </span>
            <TextField value={newKey} width={150} placeholder="key" onLive={setNewKey} onCommit={setNewKey} data-role="param-key" />
            <TextField value={newValue} width={120} placeholder="value" onLive={setNewValue} onCommit={(t) => { setNewValue(t); }} data-role="param-value" />
            <GenericButton label="Set" width={54} height={24} disabled={busy || !newKey.trim() || setParamsUnsupported} onClick={add} data-role="param-add" />
          </Row>
          {newKey && HINTS[newKey] ? <RobotoText label={HINTS[newKey]} fontSize={12} labelColor={NOTE} height={20} leftMargin={138} /> : null}

          <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 4 }}>
            <GenericButton label="Configure plugin…" width={150} height={26} disabled={busy || !plugin || !plugin.canConfigure || qlc.isUnsupported('io.plugin.configure')} onClick={configure} data-role="configure-plugin" />
            <GenericButton label="Rescan lines" width={110} height={26} disabled={busy || qlc.isUnsupported('io.plugin.rescan')} onClick={rescan} data-role="rescan-plugin" />
            <RobotoText label={plugin && plugin.canConfigure ? 'The native dialog appears on the QLC+ host machine.' : 'This plugin has no configuration dialog; use the parameters above.'} fontSize={12} labelColor={NOTE} wrapText height="auto" style={{ flex: 1 }} />
          </div>
          {status.text ? <RobotoText label={status.text} fontSize={12} labelColor={status.error ? 'var(--override-red)' : 'var(--check-lime)'} wrapText height="auto" data-role="patch-status" /> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  window.IOPatchProperties = { PatchPropertiesDialog, KNOWN_PARAMETERS: KNOWN };
})();
