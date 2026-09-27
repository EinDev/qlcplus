/**
 * AudioDevices.jsx — the Input/Output screen's "Audio" section (AudioCardsList.qml / AudioIOItem.qml):
 * the QLC+ host's audio input and output device pickers over io.audio.listDevices / io.audio.setDevice.
 * These select devices on the machine running QLC+ (what the desktop UI does), not the browser's.
 *
 * window.IOAudioDevices = { AudioDevices }
 */
(function () {
  'use strict';
  const { RobotoText, CustomComboBox, IconTextEntry } = window.PatchDesignSystem_5432c9;
  const { Row, isUnknownMethod, errorText, NOTE } = window.IOShared;

  function AudioDevices({ qlc, onStatus }) {
    const D = window.QLCData;
    const [state, setState] = React.useState({ devices: null, unsupported: false, error: '' });
    React.useEffect(() => {
      if (!qlc.online) { setState({ devices: null, unsupported: false, error: '' }); return; }
      let alive = true;
      const load = () => qlc.call('io.audio.listDevices', {}).then(r => { if (alive) setState({ devices: r, unsupported: false, error: '' }); })
        .catch(e => { if (alive) setState({ devices: null, unsupported: isUnknownMethod(e), error: errorText(e) }); });
      load();
      const off = qlc.subscribeTo('io.audio.deviceChanged', load);
      return () => { alive = false; off(); };
    }, [qlc.online]);

    const set = (direction, privateName) => {
      qlc.call('io.audio.setDevice', { direction, privateName })
        .then(() => onStatus && onStatus('Audio ' + direction + ' device set', false), e => onStatus && onStatus('Audio ' + direction + ' device failed: ' + errorText(e), true));
    };
    const model = (list) => (list || []).map(d => ({ mLabel: d.name, mValue: d.privateName }));

    if (!qlc.online) return <IconTextEntry iSrc={D.icon('audiocard')} tLabel="Audio devices: connect to a server first" tFontSize={12} tLabelColor={NOTE} height={26} />;
    if (state.unsupported) return <IconTextEntry iSrc={D.icon('audiocard')} tLabel="Not available: this server has no io.audio.listDevices" tFontSize={12} tLabelColor={NOTE} height={26} />;
    if (!state.devices) return <RobotoText label={state.error ? 'io.audio.listDevices failed: ' + state.error : 'Loading…'} fontSize={12} labelColor={state.error ? 'var(--override-red)' : NOTE} wrapText height="auto" />;
    const d = state.devices;
    const setOff = qlc.isUnsupported('io.audio.setDevice');
    return (
      <div data-role="audio-devices" style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <Row label="Input" width={52} title="Audio input used for beat detection and Audio Triggers on the QLC+ host">
          <CustomComboBox width="100%" height={24} currValue={d.inputDevice} model={model(d.inputs)} disabled={setOff} onValueChanged={v => { if (v !== d.inputDevice) set('input', v); }} data-role="audio-input" />
        </Row>
        <Row label="Output" width={52} title="Audio output for Audio functions on the QLC+ host">
          <CustomComboBox width="100%" height={24} currValue={d.outputDevice} model={model(d.outputs)} disabled={setOff} onValueChanged={v => { if (v !== d.outputDevice) set('output', v); }} data-role="audio-output" />
        </Row>
        <RobotoText label={(d.inputs.length - 1) + ' input / ' + (d.outputs.length - 1) + ' output devices on the QLC+ host. Sample rate, channels and buffer size are still desktop-only.'} fontSize={12} labelColor={NOTE} wrapText height="auto" />
      </div>
    );
  }

  window.IOAudioDevices = { AudioDevices };
})();
