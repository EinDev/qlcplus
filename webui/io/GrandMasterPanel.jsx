/**
 * GrandMasterPanel.jsx — Grand Master level + modes for the Input/Output screen's right panel:
 * io.grandMaster.get / setValue / setMode, following io.grandMaster.changed. Channel mode
 * (Intensity / All channels) and value mode (Reduce / Limit) are what the Qt UI edits in
 * VCSliderProperties.qml's Grand Master section.
 *
 * window.IOGrandMasterPanel = { GrandMasterPanel }
 */
(function () {
  'use strict';
  const { RobotoText, CustomComboBox, CustomSlider } = window.PatchDesignSystem_5432c9;
  const { Row, errorText, NOTE, isUnknownMethod } = window.IOShared;

  function GrandMasterPanel({ qlc, onStatus }) {
    const [gm, setGm] = React.useState(null);
    React.useEffect(() => {
      if (!qlc.online) { setGm(null); return; }
      let alive = true;
      qlc.call('io.grandMaster.get').then(r => { if (alive) setGm(r); }).catch(() => {});
      const off = qlc.subscribeTo('io.grandMaster.changed', (r) => { if (alive) setGm(r); });
      return () => { alive = false; off(); };
    }, [qlc.online]);
    if (!qlc.online) return <RobotoText label="Grand Master: connect to a server first" fontSize={12} labelColor={NOTE} height={24} />;
    if (!gm) return <RobotoText label="Loading…" fontSize={12} labelColor={NOTE} height={24} />;

    const move = (v) => { const value = Math.round(v); setGm(g => Object.assign({}, g, { value })); const c = qlc.client(); if (c) c.setGrandMaster(value); };
    const setMode = (patch) => {
      setGm(g => Object.assign({}, g, patch));
      qlc.call('io.grandMaster.setMode', patch)
        .then(() => onStatus && onStatus('Grand Master ' + Object.keys(patch)[0] + ' set to ' + patch[Object.keys(patch)[0]], false),
          e => onStatus && onStatus((isUnknownMethod(e) ? 'Not available: this server has no io.grandMaster.setMode' : 'Grand Master mode failed: ' + errorText(e)), true));
    };
    const modeOff = qlc.isUnsupported('io.grandMaster.setMode');
    return (
      <div data-role="grand-master" style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        <Row label="Level" width={80}>
          <CustomSlider value={gm.value} from={0} to={255} length={130} onMoved={move} data-role="gm-slider" />
          <RobotoText label={gm.value + ' · ' + Math.round(gm.value / 255 * 100) + '%'} fontSize={12} height={24} style={{ width: 70 }} data-role="gm-value" />
        </Row>
        <Row label="Channels" width={80} title="Intensity: the Grand Master scales only intensity channels. All channels: every channel.">
          <CustomComboBox width="100%" height={24} currValue={gm.channelMode} disabled={modeOff} data-role="gm-channel-mode"
            model={[{ mLabel: 'Intensity', mValue: 'Intensity' }, { mLabel: 'All channels', mValue: 'AllChannels' }]} onValueChanged={v => { if (v !== gm.channelMode) setMode({ channelMode: v }); }} />
        </Row>
        <Row label="Values" width={80} title="Reduce: scale values by the Grand Master fraction. Limit: clamp values to the Grand Master level.">
          <CustomComboBox width="100%" height={24} currValue={gm.valueMode} disabled={modeOff} data-role="gm-value-mode"
            model={[{ mLabel: 'Reduce', mValue: 'Reduce' }, { mLabel: 'Limit', mValue: 'Limit' }]} onValueChanged={v => { if (v !== gm.valueMode) setMode({ valueMode: v }); }} />
        </Row>
        {modeOff ? <RobotoText label="Mode changes are not available: this server has no io.grandMaster.setMode." fontSize={12} labelColor={NOTE} wrapText height="auto" /> : null}
      </div>
    );
  }

  window.IOGrandMasterPanel = { GrandMasterPanel };
})();
