/**
 * AudioDevices.jsx — the Input/Output screen's "Audio" section (AudioCardsList.qml / AudioIOItem.qml /
 * PopupAudioConfiguration.qml): the QLC+ host's audio input and output device pickers over
 * io.audio.listDevices / io.audio.setDevice, plus the input sample rate / channels and the output
 * buffer size over io.audio.setConfig. These configure the machine running QLC+ (what the desktop UI
 * does), not the browser's.
 *
 * window.IOAudioDevices = { AudioDevices }
 */
(function () {
  'use strict';
  const { RobotoText, CustomComboBox, CustomSpinBox, IconTextEntry, IconButton } = window.PatchDesignSystem_5432c9;
  const { Row, isUnknownMethod, errorText, NOTE } = window.IOShared;

  /* PopupAudioConfiguration.qml's choices. */
  const SAMPLE_RATES = [8000, 11025, 22050, 32000, 44100, 48000].map(r => ({ mLabel: r + ' Hz', mValue: r }));
  const CHANNELS = [{ mLabel: 'Mono', mValue: 1 }, { mLabel: 'Stereo', mValue: 2 }];

  /** PopupAudioConfiguration.qml's "Signal level" check: the QLC+ host's audio input level (0..0x7FFF)
      while the preview runs (io.audio.inputPreview.set + io.audio.inputLevel, this client only). */
  function InputLevel({ qlc, onStatus }) {
    const [on, setOn] = React.useState(false);
    const [level, setLevel] = React.useState(0);
    const onRef = React.useRef(false);
    React.useEffect(() => {
      const off = qlc.subscribeTo('io.audio.inputLevel', (d) => { if (onRef.current && d) setLevel(Number(d.level) || 0); });
      /* leaving the screen ends this client's preview (the host stops capturing when nobody previews) */
      return () => { off(); if (onRef.current) qlc.call('io.audio.inputPreview.set', { enabled: false }).catch(() => {}); };
    }, []);
    const toggle = () => {
      const next = !on;
      qlc.call('io.audio.inputPreview.set', { enabled: next })
        .then(() => { onRef.current = next; setOn(next); if (!next) setLevel(0); },
          e => onStatus && onStatus('Audio input level check failed: ' + errorText(e), true));
    };
    const frac = Math.min(1, level / 32767);
    return (
      <Row label="Signal level" width={96} title="Start / stop the audio input signal level check on the QLC+ host">
        <IconButton faSource={on ? 'fa_stop' : 'fa_play'} faColor="var(--fg-main)" size={24} checked={on} onClick={toggle} data-role="audio-level-toggle"
          tooltip={on ? 'Stop the audio input signal level check' : 'Start the audio input signal level check'} />
        <div data-role="audio-level" data-level={level} style={{ flex: 1, height: 22, borderRadius: 3, background: 'var(--bg-light)', border: '1px solid var(--bg-strong)', position: 'relative', overflow: 'hidden' }}>
          <div style={{ position: 'absolute', left: 2, top: 2, bottom: 2, width: 'calc((100% - 4px) * ' + frac + ')', borderRadius: 2,
            background: 'linear-gradient(to right, green 0%, yellow 70%, red 100%)', backgroundSize: frac > 0 ? (100 / frac) + '% 100%' : '100% 100%' }} />
        </div>
      </Row>
    );
  }

  function AudioDevices({ qlc, onStatus }) {
    const D = window.QLCData;
    const [state, setState] = React.useState({ devices: null, unsupported: false, error: '' });
    const [buffer, setBuffer] = React.useState(null); // draft of the buffer spin box while it is edited
    const bufferTimer = React.useRef(null);
    React.useEffect(() => {
      if (!qlc.online) { setState({ devices: null, unsupported: false, error: '' }); return; }
      let alive = true;
      const load = () => qlc.call('io.audio.listDevices', {}).then(r => { if (alive) setState({ devices: r, unsupported: false, error: '' }); })
        .catch(e => { if (alive) setState({ devices: null, unsupported: isUnknownMethod(e), error: errorText(e) }); });
      load();
      const offs = [qlc.subscribeTo('io.audio.deviceChanged', load),
        qlc.subscribeTo('io.audio.configChanged', (d) => { if (alive && d) setState(s => s.devices ? Object.assign({}, s, { devices: Object.assign({}, s.devices, d) }) : s); })];
      return () => { alive = false; offs.forEach(f => f()); clearTimeout(bufferTimer.current); };
    }, [qlc.online]);

    const set = (direction, privateName) => {
      qlc.call('io.audio.setDevice', { direction, privateName })
        .then(() => onStatus && onStatus('Audio ' + direction + ' device set', false), e => onStatus && onStatus('Audio ' + direction + ' device failed: ' + errorText(e), true));
    };
    const setConfig = (fields, what) => {
      qlc.call('io.audio.setConfig', fields)
        .then(() => {
          setState(s => s.devices ? Object.assign({}, s, { devices: Object.assign({}, s.devices, fields) }) : s);
          if (onStatus) onStatus(what + ' set', false);
        }, e => onStatus && onStatus(what + ' failed: ' + errorText(e), true));
    };
    /* The spin box reports every step: send the last value once the operator stops. */
    const setBufferSoon = (v) => {
      setBuffer(v);
      clearTimeout(bufferTimer.current);
      bufferTimer.current = setTimeout(() => { setConfig({ outputBufferMs: v }, 'Audio output buffer'); setBuffer(null); }, 400);
    };
    const model = (list) => (list || []).map(d => ({ mLabel: d.name, mValue: d.privateName }));

    if (!qlc.online) return <IconTextEntry iSrc={D.icon('audiocard')} tLabel="Audio devices: connect to a server first" tFontSize={12} tLabelColor={NOTE} height={26} />;
    if (state.unsupported) return <IconTextEntry iSrc={D.icon('audiocard')} tLabel="Not available: this server has no io.audio.listDevices" tFontSize={12} tLabelColor={NOTE} height={26} />;
    if (!state.devices) return <RobotoText label={state.error ? 'io.audio.listDevices failed: ' + state.error : 'Loading…'} fontSize={12} labelColor={state.error ? 'var(--override-red)' : NOTE} wrapText height="auto" />;
    const d = state.devices;
    const setOff = qlc.isUnsupported('io.audio.setDevice');
    /* An older server lists devices without the format fields and has no io.audio.setConfig. */
    const hasConfig = d.inputSampleRate != null && !qlc.isUnsupported('io.audio.setConfig');
    return (
      <div data-role="audio-devices" style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <Row label="Input" width={96} title="Audio input used for beat detection and Audio Triggers on the QLC+ host">
          <CustomComboBox width="100%" height={24} currValue={d.inputDevice} model={model(d.inputs)} disabled={setOff} onValueChanged={v => { if (v !== d.inputDevice) set('input', v); }} data-role="audio-input" />
        </Row>
        {hasConfig ? (
          <Row label="Sample rate" width={96} title="Audio input sample rate (PopupAudioConfiguration.qml)">
            <CustomComboBox width="100%" height={24} currValue={d.inputSampleRate} model={SAMPLE_RATES}
              onValueChanged={v => { if (Number(v) !== d.inputSampleRate) setConfig({ inputSampleRate: Number(v) }, 'Audio input sample rate'); }} data-role="audio-samplerate" />
          </Row>
        ) : null}
        {hasConfig ? (
          <Row label="Channels" width={96} title="Audio input channels">
            <CustomComboBox width="100%" height={24} currValue={d.inputChannels} model={CHANNELS}
              onValueChanged={v => { if (Number(v) !== d.inputChannels) setConfig({ inputChannels: Number(v) }, 'Audio input channels'); }} data-role="audio-channels" />
          </Row>
        ) : null}
        {!qlc.isUnsupported('io.audio.inputPreview.set') && hasConfig ? <InputLevel qlc={qlc} onStatus={onStatus} /> : null}
        <Row label="Output" width={96} title="Audio output for Audio functions on the QLC+ host">
          <CustomComboBox width="100%" height={24} currValue={d.outputDevice} model={model(d.outputs)} disabled={setOff} onValueChanged={v => { if (v !== d.outputDevice) set('output', v); }} data-role="audio-output" />
        </Row>
        {hasConfig ? (
          <Row label="Buffer size" width={96} title="Audio output buffer length">
            <CustomSpinBox value={buffer != null ? buffer : d.outputBufferMs} from={10} to={1000} stepSize={10} suffix=" ms" width={120} height={24}
              onValueModified={setBufferSoon} data-role="audio-buffer" />
          </Row>
        ) : null}
        <RobotoText label={(d.inputs.length - 1) + ' input / ' + (d.outputs.length - 1) + ' output devices on the QLC+ host.' + (hasConfig ? '' : ' Sample rate, channels and buffer size need a newer server.')}
          fontSize={12} labelColor={NOTE} wrapText height="auto" />
      </div>
    );
  }

  window.IOAudioDevices = { AudioDevices };
})();
