/**
 * VideoEditor.jsx — editor for Video functions, modelled on
 * qmlui/qml/fixturesfunctions/VideoEditor.qml: file name / URL (functions.video.setSource via the
 * server-side file browser or a typed URL), Reload (functions.media.reload), duration / resolution /
 * codecs from the detail, playback mode (functions.update runOrder), output screen and output mode
 * windowed / fullscreen / Spout (functions.video.setScreenTarget, screens and spoutAvailable from
 * functions.video.listCapabilities), geometry Original / Custom with X Y W H
 * (functions.video.setGeometry), rotation X Y Z (functions.video.setRotation), layer
 * (functions.video.setLayer). Volume, mute and the Spout sender size have no API setter yet and are
 * shown read-only with a note.
 *
 * Registered as window.QLCEditors.Video.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, IconButton, GenericButton, CustomSpinBox, CustomComboBox, CustomCheckBox, CustomPopupDialog } = window.PatchDesignSystem_5432c9;

  const LABEL_W = 110;
  const inputStyle = { height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: 'var(--fg-main)', border: 'var(--border-control)', fontFamily: 'var(--font-roboto)', fontSize: 13, padding: '0 6px' };

  function VideoEditor({ qlc, detail, reload, setDetail }) {
    const fid = String(detail.id);
    const td = detail.typeDetail || {};
    const cfg = td.config || {};
    const geom = cfg.customGeometry || null;
    const rot = cfg.rotation || { x: 0, y: 0, z: 0 };
    const [caps, setCaps] = React.useState(null);
    const [browser, setBrowser] = React.useState(false);
    const [urlDialog, setUrlDialog] = React.useState(false);
    const [url, setUrl] = React.useState('http://');
    /* Custom geometry being edited before the first size is known (QML fills in the resolution). */
    const [customOn, setCustomOn] = React.useState(!!geom);
    React.useEffect(() => { setCustomOn(!!geom); }, [!!geom, fid]);
    React.useEffect(() => {
      if (!qlc.online) return;
      qlc.call('functions.video.listCapabilities', {}).then(setCaps).catch(() => setCaps({ videoExtensions: [], pictureExtensions: [], screens: [], spoutAvailable: false }));
    }, [qlc.online]);
    FF.useForeignEvents(qlc, ['functions.video.sourceChanged', 'functions.video.geometryChanged', 'functions.video.rotationChanged', 'functions.video.layerChanged', 'functions.video.screenTargetChanged', 'functions.media.reloaded'],
      (topic, d) => { if (d && String(d.functionId) === fid) reload(); }, [fid]);

    const patchCfg = (p) => setDetail(d => Object.assign({}, d, { typeDetail: Object.assign({}, d.typeDetail, { config: Object.assign({}, (d.typeDetail || {}).config, p) }) }));
    const patchFn = (p) => {
      setDetail(d => Object.assign({}, d, p));
      FF.mutate(qlc, 'functions.update', Object.assign({ functionId: fid }, p), { key: 'update:' + fid + ':' + Object.keys(p).join(',') }).catch(() => reload());
    };
    const setSource = (path) => FF.mutate(qlc, 'functions.video.setSource', { functionId: fid, sourceUrl: path }).then(reload).catch(() => reload());
    const reloadMedia = () => FF.mutate(qlc, 'functions.media.reload', { functionId: fid }).then(reload).catch(() => reload());
    const setGeometry = (g) => { patchCfg({ customGeometry: g }); FF.mutate(qlc, 'functions.video.setGeometry', { functionId: fid, customGeometry: g }, { key: 'video:geom:' + fid }).catch(() => reload()); };
    const setRotation = (r) => { patchCfg({ rotation: r }); FF.mutate(qlc, 'functions.video.setRotation', { functionId: fid, rotation: r }, { key: 'video:rot:' + fid }).catch(() => reload()); };
    const setLayer = (z) => { patchCfg({ zIndex: z }); FF.mutate(qlc, 'functions.video.setLayer', { functionId: fid, zIndex: z }, { key: 'video:layer:' + fid }).catch(() => reload()); };
    const setTarget = (screen, outputMode) => {
      const fullscreen = outputMode === 'fullscreen';
      patchCfg({ screen, outputMode, fullscreen });
      FF.mutate(qlc, 'functions.video.setScreenTarget', { functionId: fid, screen, fullscreen, outputMode }, { key: 'video:target:' + fid }).catch(() => reload());
    };
    const geomField = (key, value) => {
      const g = Object.assign({ x: 0, y: 0, width: 0, height: 0 }, geom || {}, { [key]: value });
      setGeometry(g);
    };
    const res = cfg.resolution;
    const screens = (caps && caps.screens) || [];
    const screenModel = screens.map(s => ({ mLabel: 'Screen ' + s.index + ' - (' + s.name + ')' + (s.geometry ? ' ' + s.geometry.width + 'x' + s.geometry.height : ''), mValue: s.index }));
    if (cfg.screen != null && !screens.some(s => s.index === cfg.screen)) screenModel.push({ mLabel: 'Screen ' + cfg.screen, mValue: cfg.screen });
    const isSpout = cfg.outputMode === 'spout';
    const modes = ['windowed', 'fullscreen'].concat(caps && caps.spoutAvailable ? ['spout'] : (isSpout ? ['spout'] : []));
    const filters = [
      window.ServerFileBrowser.filter('Video files', (caps && caps.videoExtensions && caps.videoExtensions.length) ? caps.videoExtensions : ['*.mp4', '*.mkv', '*.avi', '*.mov', '*.webm']),
      window.ServerFileBrowser.filter('Picture files', (caps && caps.pictureExtensions && caps.pictureExtensions.length) ? caps.pictureExtensions : ['*.png', '*.jpg', '*.jpeg', '*.bmp', '*.gif']),
      window.ServerFileBrowser.filter('All files', [])
    ];
    const spin = (value, onChange, opts) => <CustomSpinBox value={value} from={opts.from} to={opts.to} suffix={opts.suffix || ''} width={opts.width || 80} height={24} disabled={opts.disabled} onValueModified={onChange} />;
    const MediaSourceRow = FF.MediaSourceRow;

    return (
      <div className="qlc-video-editor" style={{ flex: 1, minHeight: 0, overflow: 'auto', padding: 12, display: 'flex', flexDirection: 'column', gap: 6, maxWidth: 760 }}>
        <MediaSourceRow td={td} onReload={reloadMedia} onReplace={() => setBrowser(true)}
          extra={<IconButton faSource={''} size={24} tooltip="Set a URL" onClick={() => { setUrl(td.managed || !td.source ? 'http://' : td.source); setUrlDialog(true); }} />} />
        <FF.Row label="Duration" width={LABEL_W}>{cfg.detectedDurationMs ? FF.ms(cfg.detectedDurationMs) : (cfg.isPicture ? 'picture' : '—')}</FF.Row>
        <FF.Row label="Resolution" width={LABEL_W}>{res ? res.width + 'x' + res.height : '—'}</FF.Row>
        <FF.Row label="Video codec" width={LABEL_W}>{cfg.videoCodec || '—'}</FF.Row>
        <FF.Row label="Audio codec" width={LABEL_W}>{cfg.audioCodec || '—'}</FF.Row>
        <FF.Row label="Playback mode" width={LABEL_W}>
          <FF.Choice options={['SingleShot', 'Loop']} labels={{ SingleShot: 'Single shot', Loop: 'Looped' }} value={detail.runOrder === 'Loop' ? 'Loop' : 'SingleShot'} onChange={v => patchFn({ runOrder: v })} />
        </FF.Row>
        <FF.Row label="Volume" width={LABEL_W}>
          <CustomSpinBox value={Math.round(cfg.volume != null ? cfg.volume : 100)} from={0} to={100} suffix="%" width={90} height={24} disabled onValueModified={() => {}} />
          <CustomCheckBox checked={!!cfg.muted} size={22} disabled tooltip="Volume / mute have no API method yet — read-only" onToggled={() => {}} />
          <RobotoText label="Mute" fontSize={14} labelColor="var(--fg-medium)" />
        </FF.Row>
        <FF.Row label="Output screen" width={LABEL_W}>
          <CustomComboBox width={320} height={24} model={screenModel.length ? screenModel : [{ mLabel: 'Screen 0', mValue: 0 }]} currValue={cfg.screen || 0}
            onValueChanged={(v) => setTarget(v, cfg.outputMode || 'windowed')} disabled={isSpout || !caps} />
        </FF.Row>
        <FF.Row label="Output mode" width={LABEL_W}>
          <FF.Choice options={modes} labels={{ windowed: 'Windowed', fullscreen: 'Fullscreen', spout: 'Spout' }} value={cfg.outputMode || (cfg.fullscreen ? 'fullscreen' : 'windowed')} onChange={v => setTarget(cfg.screen || 0, v)} />
        </FF.Row>
        {isSpout ? <>
          <FF.Row label="Sender size" width={LABEL_W}>
            <RobotoText label={cfg.spoutSize && (cfg.spoutSize.width || cfg.spoutSize.height) ? cfg.spoutSize.width + ' x ' + cfg.spoutSize.height : 'native resolution'} fontSize={14} />
            <RobotoText label="(read-only: no API setter yet)" fontSize={12} labelColor="var(--fg-medium)" />
          </FF.Row>
        </> : null}
        <FF.Row label="Geometry" width={LABEL_W}>
          <FF.Choice options={['original', 'custom']} labels={{ original: 'Original', custom: 'Custom' }} value={customOn ? 'custom' : 'original'} disabled={isSpout}
            onChange={v => { if (v === 'original') { setCustomOn(false); if (geom) setGeometry(null); } else { setCustomOn(true); if (!geom) setGeometry({ x: 0, y: 0, width: res ? res.width : 1280, height: res ? res.height : 720 }); } }} />
        </FF.Row>
        {customOn ? <>
          <FF.Row label="Position" width={LABEL_W}>
            <RobotoText label="X" fontSize={13} />{spin((geom && geom.x) || 0, v => geomField('x', v), { from: 0, to: 99999, disabled: isSpout })}
            <RobotoText label="Y" fontSize={13} />{spin((geom && geom.y) || 0, v => geomField('y', v), { from: 0, to: 99999, disabled: isSpout })}
          </FF.Row>
          <FF.Row label="Size" width={LABEL_W}>
            <RobotoText label="W" fontSize={13} />{spin((geom && geom.width) || 0, v => geomField('width', v), { from: 0, to: 99999, disabled: isSpout })}
            <RobotoText label="H" fontSize={13} />{spin((geom && geom.height) || 0, v => geomField('height', v), { from: 0, to: 99999, disabled: isSpout })}
          </FF.Row>
        </> : null}
        <FF.Row label="Rotation" width={LABEL_W}>
          <RobotoText label="X" fontSize={13} />{spin(Math.round(rot.x || 0), v => setRotation(Object.assign({}, rot, { x: v })), { from: -360, to: 360, suffix: '°', disabled: isSpout })}
          <RobotoText label="Y" fontSize={13} />{spin(Math.round(rot.y || 0), v => setRotation(Object.assign({}, rot, { y: v })), { from: -360, to: 360, suffix: '°', disabled: isSpout })}
          <RobotoText label="Z" fontSize={13} />{spin(Math.round(rot.z || 0), v => setRotation(Object.assign({}, rot, { z: v })), { from: -360, to: 360, suffix: '°', disabled: isSpout })}
        </FF.Row>
        <FF.Row label="Layer" width={LABEL_W}>
          {spin(cfg.zIndex != null ? cfg.zIndex : 1, setLayer, { from: 1, to: 100, disabled: isSpout })}
        </FF.Row>
        <FF.Note text={'Replace… copies the picked file (on the QLC+ machine) into the project\'s media store, a URL is kept as-is. Screen, geometry, rotation and layer do not apply in Spout mode, like in the desktop editor. Duration, resolution and codecs are probed by the desktop app when it plays the file' + (td.origin ? '. Origin: ' + td.origin : '') + '.'} style={{ marginTop: 6 }} />
        <window.ServerFileBrowser open={browser} qlc={qlc} title="Replace video file" filters={filters}
          initialPath={td.origin ? td.origin.replace(/[\\/][^\\/]*$/, '') : undefined}
          onPick={setSource} onClose={() => setBrowser(false)} />
        <CustomPopupDialog open={urlDialog} title="Enter a URL" width={480} standardButtons={['Cancel', 'Ok']} disabledButtons={/^[a-z][a-z0-9+.-]*:\/\/./i.test(url) ? [] : ['Ok']}
          onClicked={(b) => { if (b === 'Ok') setSource(url.trim()); setUrlDialog(false); }} onClose={() => setUrlDialog(false)}>
          <input className="qlc-video-url" autoFocus value={url} onChange={e => setUrl(e.target.value)} onKeyDown={e => { if (e.key === 'Enter' && /^[a-z][a-z0-9+.-]*:\/\/./i.test(url)) { setSource(url.trim()); setUrlDialog(false); } }} style={Object.assign({ width: '100%' }, inputStyle)} />
        </CustomPopupDialog>
      </div>
    );
  }

  window.QLCEditors = Object.assign(window.QLCEditors || {}, { Video: VideoEditor });
  FF.VideoEditor = VideoEditor;
})();
