/**
 * AudioEditor.jsx — editor for Audio functions, modelled on
 * qmlui/qml/fixturesfunctions/AudioEditor.qml: file name (managed copy / external reference, the
 * "changed on disk" flag), Reload (functions.media.reload), Replace file through the server-side
 * file browser (functions.audio.setSource — the file is copied into the project's media store),
 * duration (functions.audio.setDuration), channels / sample rate / bitrate / BPM from the detail,
 * playback mode (functions.update runOrder), output device (functions.audio.setDevice, devices from
 * functions.audio.listCapabilities), volume (functions.audio.setVolume, 0-100 here, 0-1 on the wire),
 * fade in / out (functions.update), mute (functions.audio.setMuted) and Detect BPM
 * (functions.audio.detectBpm, the result follows as functions.audio.bpmChanged).
 *
 * Registered as window.QLCEditors.Audio.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox } = window.PatchDesignSystem_5432c9;

  const LABEL_W = 110;

  /** Shared by the Audio and Video editors: the source row with managed / origin state. */
  function MediaSourceRow({ td, onReload, onReplace, extra }) {
    const managed = !!td.managed;
    const src = td.source || '';
    const display = managed ? src.split('/').pop() : src;
    const changed = !!td.originChanged;
    const tip = !managed ? 'Reload the file from disk (re-reads duration / media info)'
      : !td.origin ? 'No origin recorded for this copy'
      : !td.originAvailable ? 'Origin file not found: ' + td.origin
      : 'Re-import from ' + td.origin;
    return (
      <FF.Row label="File name" width={LABEL_W}>
        <div style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'center', gap: 4 }}>
          <RobotoText label={(managed ? 'Managed: ' : '') + (display || '(none)') + (changed ? ' — changed on disk' : '') + (td.importPending ? ' — copying…' : '')}
            fontSize={13} labelColor={changed ? 'var(--selection, #f0a030)' : 'var(--fg-light)'} wrapText height="auto" title={src} style={{ flex: 1, minWidth: 0, wordBreak: 'break-all' }} />
          <IconButton faSource={FF.GLYPH.retweet} size={24} tooltip={tip} disabled={!td.originAvailable} onClick={onReload} />
          <GenericButton label="Replace…" width={80} height={24} onClick={onReplace} />
          {extra || null}
        </div>
      </FF.Row>
    );
  }

  function AudioEditor({ qlc, detail, reload, setDetail }) {
    const fid = String(detail.id);
    const td = detail.typeDetail || {};
    const cfg = td.config || {};
    const [caps, setCaps] = React.useState(null);
    const [browser, setBrowser] = React.useState(false);
    const [volume, setVolume] = React.useState(Math.round((cfg.volume != null ? cfg.volume : 1) * 100));
    React.useEffect(() => { setVolume(Math.round((cfg.volume != null ? cfg.volume : 1) * 100)); }, [cfg.volume, fid]);
    React.useEffect(() => {
      if (!qlc.online) return;
      qlc.call('functions.audio.listCapabilities', {}).then(setCaps).catch(() => setCaps({ extensions: [], devices: [] }));
    }, [qlc.online]);
    FF.useForeignEvents(qlc, ['functions.audio.sourceChanged', 'functions.audio.volumeChanged', 'functions.audio.durationChanged', 'functions.audio.deviceChanged', 'functions.media.reloaded'],
      (topic, d) => { if (d && String(d.functionId) === fid) reload(); }, [fid]);

    const patchCfg = (p) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { config: Object.assign({}, (d.typeDetail || {}).config, p) }) }));
    const patchFn = (p) => {
      setDetail(d => Object.assign({}, d, p));
      FF.mutate(qlc, 'functions.update', Object.assign({ functionId: fid }, p), { key: 'update:' + fid + ':' + Object.keys(p).join(',') }).catch(() => reload());
    };
    const setVol = (v) => {
      setVolume(v); patchCfg({ volume: v / 100 });
      FF.mutate(qlc, 'functions.audio.setVolume', { functionId: fid, volume: v / 100 }, { key: 'audio:vol:' + fid }).catch(() => reload());
    };
    const setDuration = (ms) => { patchCfg({ duration: ms }); FF.mutate(qlc, 'functions.audio.setDuration', { functionId: fid, duration: ms }).catch(() => reload()); };
    const setDevice = (id) => { patchCfg({ audioDevice: id }); FF.mutate(qlc, 'functions.audio.setDevice', { functionId: fid, audioDevice: id }).catch(() => reload()); };
    const setSource = (path) => FF.mutate(qlc, 'functions.audio.setSource', { functionId: fid, sourceFileName: path }).then(reload).catch(() => reload());
    const reloadMedia = () => FF.mutate(qlc, 'functions.media.reload', { functionId: fid }).then(reload).catch(() => reload());
    const setMuted = (m) => { const v = typeof m === 'boolean' ? m : !cfg.muted; patchCfg({ muted: v }); FF.mutate(qlc, 'functions.audio.setMuted', { functionId: fid, muted: v }).catch(() => reload()); };
    const detectBpm = () => qlc.call('functions.audio.detectBpm', { functionId: fid }).then(r => { if (r && r.bpm) patchCfg({ bpm: r.bpm }); }).catch(e => FF.reportError(e, 'functions.audio.detectBpm'));
    /* the analysis result arrives later, for this client too */
    React.useEffect(() => qlc.subscribeTo('functions.audio.bpmChanged', d => { if (d && String(d.functionId) === fid && d.bpm) patchCfg({ bpm: d.bpm }); }), [fid, qlc.online]);
    FF.useForeignEvents(qlc, ['functions.audio.mutedChanged'], (topic, d) => { if (d && String(d.functionId) === fid) patchCfg({ muted: !!d.muted }); }, [fid]);
    const time = (label, field) => (
      <FF.Row label={label} width={LABEL_W}>
        <FF.InlineNumber value={detail[field] || 0} format={FF.ms} parse={FF.parseMs} onCommit={v => patchFn({ [field]: v })} width={90} title="Click to edit: 500, 1.5s, 2m" />
        <IconButton faSource="fa_xmark" size={22} tooltip="Set 0" onClick={() => patchFn({ [field]: 0 })} />
      </FF.Row>
    );
    const bpm = cfg.bpm || {};
    const bpmLabel = bpm.state === 'analyzing' ? 'Detecting…' : bpm.state === 'done' ? Number(bpm.value || 0).toFixed(1) + (bpm.confidence != null ? ' (confidence ' + Math.round(bpm.confidence * 100) + '%)' : '') : bpm.state === 'failed' ? 'Detection failed' : 'Not analyzed';
    const devices = (caps && caps.devices) || [{ id: '', name: 'Default device' }];
    const devModel = devices.map(d => ({ mLabel: d.name, mValue: d.id }));
    if (cfg.audioDevice && !devices.some(d => d.id === cfg.audioDevice)) devModel.push({ mLabel: cfg.audioDevice + ' (not present)', mValue: cfg.audioDevice });
    const filters = [
      /* the decoder plugins report the patterns; an instance without them (no Plugins dir) reports none */
      window.ServerFileBrowser.filter('Audio files', (caps && caps.extensions && caps.extensions.length) ? caps.extensions : ['*.mp3', '*.wav', '*.ogg', '*.flac', '*.aiff', '*.m4a']),
      window.ServerFileBrowser.filter('All files', [])
    ];

    return (
      <div className="qlc-audio-editor" style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'flex', flexDirection: 'column', gap: 6, maxWidth: 720 }}>
        <MediaSourceRow td={td} onReload={reloadMedia} onReplace={() => setBrowser(true)} />
        <FF.Row label="Duration" width={LABEL_W}>
          <FF.InlineNumber value={cfg.duration || 0} format={FF.ms} parse={FF.parseMs} onCommit={setDuration} width={90} title="Playback length; click to override (500, 1.5s, 2m)" />
          <RobotoText label="playback length, independent of the file's own" fontSize={12} labelColor="var(--fg-medium)" />
        </FF.Row>
        <FF.Row label="Channels" width={LABEL_W}>{cfg.channels ? String(cfg.channels) : '—'}</FF.Row>
        <FF.Row label="Sample rate" width={LABEL_W}>{cfg.sampleRate ? cfg.sampleRate + ' Hz' : '—'}</FF.Row>
        <FF.Row label="Bitrate" width={LABEL_W}>{cfg.bitrate ? cfg.bitrate + ' kb/s' : '—'}</FF.Row>
        <FF.Row label="BPM" width={LABEL_W}>
          <RobotoText label={bpmLabel} fontSize={14} data-e2e="audio-bpm" />
          <IconButton faSource={FF.GLYPH.rotateLeft} size={22} tooltip="Detect BPM (runs the analysis again; needs the audio decoder plugins on the QLC+ machine)" disabled={bpm.state === 'analyzing'} onClick={detectBpm} />
        </FF.Row>
        <FF.Row label="Playback mode" width={LABEL_W}>
          <FF.Choice options={['SingleShot', 'Loop']} labels={{ SingleShot: 'Single shot', Loop: 'Looped' }} value={detail.runOrder === 'Loop' ? 'Loop' : 'SingleShot'} onChange={v => patchFn({ runOrder: v })} />
        </FF.Row>
        <FF.Row label="Output device" width={LABEL_W}>
          <CustomComboBox width={320} height={24} model={devModel} currValue={cfg.audioDevice || ''} onValueChanged={setDevice} disabled={!caps} />
        </FF.Row>
        <FF.Row label="Volume" width={LABEL_W}>
          <CustomSpinBox value={volume} from={0} to={100} suffix="%" width={90} height={24} onValueModified={setVol} />
          <CustomCheckBox checked={!!cfg.muted} size={22} tooltip="Mute this audio function" onToggled={setMuted} />
          <RobotoText label="Mute" fontSize={14} />
        </FF.Row>
        {time('Fade in', 'fadeInSpeed')}
        {time('Fade out', 'fadeOutSpeed')}
        <FF.Note text={'Replace… copies the picked file (on the QLC+ machine) into the project\'s media store and repoints this function only; Reload re-imports a managed copy from its origin' + (td.origin ? ' (' + td.origin + ')' : '') + '.'} style={{ marginTop: 6 }} />
        <window.ServerFileBrowser open={browser} qlc={qlc} title="Replace audio file" filters={filters}
          initialPath={td.origin ? td.origin.replace(/[\\/][^\\/]*$/, '') : undefined}
          onPick={setSource} onClose={() => setBrowser(false)} />
      </div>
    );
  }

  window.QLCEditors = Object.assign(window.QLCEditors || {}, { Audio: AudioEditor });
  Object.assign(FF, { AudioEditor, MediaSourceRow });
})();
