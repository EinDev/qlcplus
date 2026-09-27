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

  /* Parameter keys the network plugins accept in setParameter(), per patch direction, with the value
     each one parses (plugins/artnet/src/artnetplugin.cpp, plugins/E1.31/e131plugin.cpp,
     plugins/osc/oscplugin.cpp — any other key is rejected with a warning and not stored). Offered as
     suggestions; any key can still be typed for other plugins.
       'int'  -> QVariant::toUInt()/toInt()   'text' -> toString()   [..] -> exact strings the plugin compares
     ArtNet and E1.31 have no Feedback capability, hence no feedback keys. OSC sends feedback to
     feedbackIP/feedbackPort (OSCController::sendFeedback), not to outputIP/outputPort. */
  const SPEC = {
    ArtNet: {
      input: { inputUni: 'int' },
      output: { outputIP: 'text', outputUni: 'int', transmitMode: ['Standard', 'Full', 'Partial'] },
      feedback: {}
    },
    'E1.31': {
      input: { universe: 'int', multicast: 'int', mcastFullIP: 'text', mcastIP: 'text', ucastPort: 'int' },
      output: { universe: 'int', transmitMode: ['Full', 'Partial'], priority: 'int', multicast: 'int', mcastFullIP: 'text', mcastIP: 'text', ucastIP: 'text', ucastPort: 'int' },
      feedback: {}
    },
    OSC: {
      input: { inputPort: 'int', feedbackIP: 'text', feedbackPort: 'int' },
      output: { outputIP: 'text', outputPort: 'int' },
      feedback: { feedbackIP: 'text', feedbackPort: 'int' }
    }
  };
  const KNOWN = {};
  Object.keys(SPEC).forEach(p => { KNOWN[p] = {}; Object.keys(SPEC[p]).forEach(d => { KNOWN[p][d] = Object.keys(SPEC[p][d]); }); });
  const HINTS = {
    outputIP: 'Destination IP address; empty = broadcast', outputUni: 'Art-Net universe number (0-based; default = the QLC+ universe index)', inputUni: 'Art-Net universe to listen to (default = the QLC+ universe index)',
    universe: 'sACN universe (1-63999)', priority: 'sACN priority 0-200 (default 100)',
    multicast: '1 = multicast (default), 0 = unicast (ucastIP / ucastPort)', mcastFullIP: 'Full multicast address, e.g. 239.255.0.1', mcastIP: 'Legacy: last octet of 239.255.0.x (mcastFullIP replaces it)',
    ucastIP: 'Unicast destination IP', ucastPort: 'UDP port (default 5568)',
    inputPort: 'UDP port to listen on (default 7700 + universe index)', outputPort: 'Destination UDP port (default 9000 + universe index)',
    feedbackIP: 'Feedback destination IP', feedbackPort: 'Feedback destination UDP port (default 9000 + universe index)'
  };
  const PLUGIN_HINTS = {
    ArtNet: { transmitMode: '"Standard" (default: sends changed frames), "Full" (all 512 channels, always) or "Partial" (only the used channels)', outputIP: 'Destination IP address, e.g. 2.255.255.255 or one node; empty = broadcast (default)' },
    'E1.31': { universe: 'sACN universe 1-63999 (default = the QLC+ universe index + 1)', transmitMode: '"Full" (default: all 512 channels) or "Partial" (only the used channels)' },
    OSC: { outputIP: 'Destination IP address (the 127.0.0.1 line defaults to 127.0.0.1)', feedbackIP: 'Feedback destination IP address (the 127.0.0.1 line defaults to 127.0.0.1)' }
  };
  /* Keys QLC+ itself sets on every output line (Universe::dumpOutput re-sends it whenever the universe's
     channel count changes): shown, not editable. */
  const ENGINE_KEYS = { UniverseChannels: 'Set by QLC+ itself: how many channels this universe uses (Art-Net and SPI size their frames with it)' };
  const hintFor = (plugin, key) => (PLUGIN_HINTS[plugin] || {})[key] || HINTS[key] || '';

  /** Typed text -> the JSON value the server stores: keep the previous type when it still parses. */
  function coerce(text, previous) {
    const t = String(text).trim();
    if (typeof previous === 'boolean' || /^(true|false)$/i.test(t)) return /^true$/i.test(t);
    if ((typeof previous === 'number' || previous === undefined) && /^-?\d+(\.\d+)?$/.test(t)) return Number(t);
    return t;
  }
  /** Like coerce(), but a key the plugin is known to parse gets exactly that format ({value} or {error}):
      the plugins persist parameters as text and read them back with toInt()/toString(), so e.g. a
      boolean multicast=true would work live but reload from the .qxw as "true" -> 0 (unicast). */
  function coerceFor(plugin, direction, key, text, previous) {
    const spec = ((SPEC[plugin] || {})[direction] || {})[key];
    const t = String(text).trim();
    if (!spec) return { value: coerce(text, previous) };
    if (spec === 'int') return /^\d+$/.test(t) ? { value: Number(t) } : { error: key + ' must be a whole number' };
    if (Array.isArray(spec)) {
      const hit = spec.find(v => v.toLowerCase() === t.toLowerCase());
      return hit ? { value: hit } : { error: key + ' must be one of ' + spec.join(', ') };
    }
    return { value: t };
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

    const send = (parameters, label, setKey) => {
      setBusy(true);
      return withConflictRetry((details) => qlc.call('io.patch.setParameters', { universeId: universe.id, patchType: direction, index: index || 0, parameters, baseRevision: details && details.docRevision != null ? details.docRevision : qlc.docRevision() }))
        .then(r => {
          /* ArtNet / OSC drop a parameter that equals their default, and every network plugin ignores a
             key it does not know: say so instead of a plain "set" when the line did not keep it. */
          const kept = !setKey || !r || !r.parameters || Object.prototype.hasOwnProperty.call(r.parameters, setKey);
          const text = kept ? label : label + ' — ' + pluginName + ' did not keep it (that value is its default, or it does not accept "' + setKey + '")';
          setStatus({ text, error: false }); if (onStatus) onStatus(text, false); return true;
        },
          e => { setStatus({ text: isUnknownMethod(e) ? 'Not available: this server has no io.patch.setParameters' : label + ' failed: ' + errorText(e), error: true }); return false; })
        .finally(() => setBusy(false));
    };
    const setOne = (key, value) => { const p = {}; p[key] = value; return send(p, key + ' set to ' + JSON.stringify(value), key); };
    /** Typed text for `key` -> set it, or show why the plugin would not parse it. */
    const setTyped = (key, text, previous) => {
      const c = coerceFor(pluginName, direction, key, text, previous);
      if (c.error) { setStatus({ text: c.error, error: true }); return Promise.resolve(false); }
      if (c.value === previous) return Promise.resolve(true);
      return setOne(key, c.value);
    };
    const remove = (key) => { const p = {}; p[key] = null; return send(p, key + ' reset to the plugin default'); };
    const add = () => {
      const key = newKey.trim();
      if (!key) return;
      const typed = newValue;
      /* clear only what still holds the submitted text: the operator may already be typing the next one */
      setTyped(key, typed, undefined).then(ok => { if (ok) { setNewKey(k => k.trim() === key ? '' : k); setNewValue(v => v === typed ? '' : v); } });
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
              ENGINE_KEYS[k] ? (
              <Row key={k} label={k} width={130} title={ENGINE_KEYS[k]} style={{ minHeight: 28 }}>
                <RobotoText label={String(params[k])} fontSize={14} height={24} style={{ width: 260 }} data-engine-param={k} />
                <RobotoText label="set by QLC+" fontSize={12} labelColor={NOTE} height={24} title={ENGINE_KEYS[k]} />
              </Row>) :
              <Row key={k} label={k} width={130} title={hintFor(pluginName, k)} style={{ minHeight: 28 }}>
                {typeof params[k] === 'boolean'
                  ? <CustomCheckBox checked={!!params[k]} size={22} disabled={busy || setParamsUnsupported} onToggled={v => setOne(k, v)} data-param={k} />
                  : <TextField value={params[k]} width={260} disabled={busy || setParamsUnsupported} data-param={k} onCommit={(t) => { if (String(t).trim() !== String(params[k])) setTyped(k, t, params[k]); }} />}
                <RobotoText label={typeof params[k]} fontSize={12} labelColor={NOTE} height={24} style={{ width: 56 }} />
                <IconButton faSource="fa_trash_can" faColor="var(--bg-strong)" size={24} tooltip="Remove: revert to the plugin default" disabled={busy || setParamsUnsupported} onClick={() => remove(k)} data-remove-param={k} />
              </Row>
            )) : null}
            {keys.some(k => !ENGINE_KEYS[k]) ? null : <RobotoText label="No parameters set on this line — the plugin uses its defaults." fontSize={12} labelColor={NOTE} height={24} />}
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
          {newKey && hintFor(pluginName, newKey) ? <RobotoText label={hintFor(pluginName, newKey)} fontSize={12} labelColor={NOTE} wrapText height="auto" leftMargin={138} data-role="param-hint" /> : null}

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

  window.IOPatchProperties = { PatchPropertiesDialog, KNOWN_PARAMETERS: KNOWN, PARAMETER_SPEC: SPEC };
})();
