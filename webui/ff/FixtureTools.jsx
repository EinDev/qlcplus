/**
 * FixtureTools.jsx — the QML "capability tools" (LeftPanel.qml: Intensity, Shutter, Position,
 * Color, Color Wheel, Gobos, Beam, Speed, Prism, Effect, Maintenance) for the selected fixtures.
 *
 * Target: live output (writes go through io.simpleDesk.setChannels at the fixture's flat
 * addresses — a manual override, released with "Release"), or the open Scene (values written with
 * functions.scene.setValue through the revision queue), the same switch ContextManager makes
 * between setDumpValue() and FunctionManager::setChannelValue() when a Scene editor is open.
 *
 * Channel maths (RGB → CMY/WAUV, 16-bit pan/tilt) live in ff-core.jsx. Capability presets appear
 * when fixtures.defs.getMode/getModel is available; otherwise every channel gets a plain slider.
 */
(function () {
  'use strict';
  const FF = window.FF;
  const { RobotoText, SectionBox, QLCPlusFader, CustomSlider, CustomSpinBox, GenericButton, IconButton, IconTextEntry } = window.PatchDesignSystem_5432c9;

  const BASIC_COLOURS = [
    ['red', 255, 0, 0], ['green', 0, 255, 0], ['blue', 0, 0, 255], ['cyan', 0, 255, 255], ['magenta', 255, 0, 255], ['yellow', 255, 255, 0],
    ['amber', 255, 127, 0], ['white', 255, 255, 255], ['lime', 127, 255, 0], ['indigo', 75, 0, 130], ['uv', 64, 0, 128]
  ];
  const PRESET_GROUPS = [
    ['shutter', 'shutter', 'Shutter'], ['colorwheel', 'colorwheel', 'Color Wheel'], ['gobo', 'gobo', 'Gobos'], ['beam', 'beam', 'Beam'],
    ['speed', 'speed', 'Speed'], ['prism', 'prism', 'Prism'], ['effect', 'effect', 'Effect'], ['maintenance', 'other', 'Maintenance'], ['other', 'other', 'Other']
  ];

  /** Per selected fixture: {detail, channels (classified + capabilities)} once loaded. */
  function useToolFixtures(qlc, fixtureIds) {
    const details = FF.useFixtureDetails(qlc, fixtureIds);
    const [modes, setModes] = React.useState({});
    const defKeys = Object.keys(details).map(id => { const d = details[id]; return d && d.manufacturer ? [d.manufacturer, d.model, d.mode].join('|') : ''; }).filter(Boolean);
    const defKeyStr = Array.from(new Set(defKeys)).sort().join('\n');
    React.useEffect(() => {
      if (!qlc.online) return undefined;
      let alive = true;
      Array.from(new Set(defKeys)).forEach(k => {
        if (modes[k] !== undefined) return;
        const [manufacturer, model, mode] = k.split('|');
        FF.modeChannels(qlc, { manufacturer, model, mode }).then(chs => { if (alive) setModes(m => Object.assign({}, m, { [k]: chs })); });
      });
      return () => { alive = false; };
    }, [qlc.online, defKeyStr]);
    return React.useMemo(() => fixtureIds.map(id => {
      const d = details[String(id)];
      if (!d) return null;
      const k = d.manufacturer ? [d.manufacturer, d.model, d.mode].join('|') : '';
      return { detail: d, channels: FF.mergeChannels(d.channelList, k ? modes[k] : null), capsKnown: k ? modes[k] !== undefined : true, hasCaps: !!(k && modes[k]) };
    }).filter(Boolean), [details, modes, fixtureIds.join(',')]);
  }

  function Section({ label, icon, children, open = true }) {
    const D = window.QLCData;
    const [exp, setExp] = React.useState(open);
    return (
      <SectionBox sectionLabel={<span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>{icon ? <img src={D.icon(icon)} alt="" style={{ width: 18, height: 18 }} /> : null}{label}</span>} isExpanded={exp} onToggle={() => setExp(!exp)}>
        <div style={{ padding: 8, display: 'flex', flexDirection: 'column', gap: 8 }}>{children}</div>
      </SectionBox>
    );
  }

  function Slider({ label, value, onChange, to = 255, width = 100, icon }) {
    const D = window.QLCData;
    return (
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26 }}>
        {icon ? <img src={D.icon(icon)} alt="" style={{ width: 18, height: 18, flex: 'none' }} /> : null}
        <RobotoText label={label} fontSize={13} height={26} style={{ width: 56, flex: "none" }} labelColor="var(--fg-light)" />
        <CustomSlider value={value} from={0} to={to} length={width} onMoved={onChange} />
        <CustomSpinBox value={value} from={0} to={to} width={to > 255 ? 70 : 56} showControls={false} onValueModified={onChange} />
      </div>
    );
  }

  /** Small XY pad: x = pan, y = tilt (top = 0), both in 16-bit. */
  function XYPad({ pan, tilt, onMove, size = 150 }) {
    const ref = React.useRef(null);
    const set = (e) => {
      const r = ref.current.getBoundingClientRect();
      const x = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)), y = Math.max(0, Math.min(1, (e.clientY - r.top) / r.height));
      onMove(Math.round(x * 65535), Math.round(y * 65535));
    };
    const start = (e) => {
      e.preventDefault(); set(e);
      const move = (ev) => set(ev);
      const up = () => { window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up); };
      window.addEventListener('mousemove', move); window.addEventListener('mouseup', up);
    };
    return (
      <div ref={ref} onMouseDown={start} title="Pan / Tilt"
        style={{ width: size, height: size, position: 'relative', background: 'var(--bg-stronger)', border: 'var(--border-control)', cursor: 'crosshair', boxSizing: 'border-box' }}>
        <div style={{ position: 'absolute', left: '50%', top: 0, bottom: 0, width: 1, background: 'var(--bg-control)' }} />
        <div style={{ position: 'absolute', top: '50%', left: 0, right: 0, height: 1, background: 'var(--bg-control)' }} />
        <div style={{ position: 'absolute', left: (pan / 65535) * 100 + '%', top: (tilt / 65535) * 100 + '%', width: 10, height: 10, marginLeft: -5, marginTop: -5, borderRadius: 5, background: 'var(--highlight)', border: '1px solid var(--fg-main)' }} />
      </div>
    );
  }

  /** Hex colour text field: buffered locally so it can be typed character by character (a
      controlled input bound straight to the current colour reset itself after every keystroke
      that was not yet a complete #rrggbb, so keyboard entry never got through). Applies as soon
      as the text is a valid colour, and snaps back to the current colour on blur/Escape. */
  function HexInput({ value, onCommit }) {
    const [text, setText] = React.useState(value);
    const [editing, setEditing] = React.useState(false);
    React.useEffect(() => { if (!editing) setText(value); }, [value, editing]);
    const change = (e) => { const t = e.target.value; setText(t); const c = FF.parseHex(t); if (c) onCommit(c); };
    return (
      <input value={text} onChange={change} onFocus={() => setEditing(true)} onBlur={() => { setEditing(false); setText(value); }}
        onKeyDown={e => { if (e.key === 'Escape' || e.key === 'Enter') e.currentTarget.blur(); }} spellCheck={false} title="Hex colour, e.g. #ff2000"
        style={{ width: 78, height: 24, boxSizing: 'border-box', background: 'var(--bg-stronger)', color: FF.parseHex(text) ? 'var(--fg-main)' : 'var(--override-red)', border: 'var(--border-control)', fontFamily: 'var(--font-mono)', fontSize: 13, padding: '0 4px' }} />
    );
  }

  function FixtureTools({ qlc, fixtureIds, fixtures, sceneId, sceneName }) {
    const D = window.QLCData;
    const items = useToolFixtures(qlc, fixtureIds);
    const [target, setTarget] = React.useState('live');
    React.useEffect(() => { setTarget(sceneId != null ? 'scene' : 'live'); }, [sceneId]);
    const toScene = target === 'scene' && sceneId != null;

    /* Local (optimistic) tool state — nothing reads DMX back per fixture here. */
    const [intensity, setIntensity] = React.useState(255);
    const [rgb, setRgb] = React.useState({ r: 255, g: 255, b: 255 });
    const [wauv, setWauv] = React.useState({ w: 0, a: 0, uv: 0 });
    const [pan, setPan] = React.useState(32768);
    const [tilt, setTilt] = React.useState(32768);
    const [presetVals, setPresetVals] = React.useState({});

    const channelsAll = React.useMemo(() => items.reduce((a, it) => a.concat(it.channels), []), [items]);
    const mask = React.useMemo(() => FF.toolMask(channelsAll), [channelsAll]);
    const hasWauv = channelsAll.some(c => c.role === 'white' || c.role === 'amber' || c.role === 'uv');
    const has16 = channelsAll.some(c => (c.role === 'pan' || c.role === 'tilt') && c.fine);
    /* Pan-only / tilt-only fixtures get a single-axis tool (SingleAxisTool.qml) in degrees. */
    const hasPan = channelsAll.some(c => c.role === 'pan'), hasTilt = channelsAll.some(c => c.role === 'tilt');
    const physOf = (k, dflt) => { const it = items.find(x => x.detail.physical && x.detail.physical[k]); return it ? it.detail.physical[k] : dflt; };
    const panMax = physOf('focusPanMax', 360), tiltMax = physOf('focusTiltMax', 270);

    /** Apply writes (per fixture: fn(channels) → [{channel,value}]) to the current target. */
    const apply = (key, fn) => { items.forEach(it => writeOne(it, fn(it.channels), key)); };
    /** Write [{channel,value}] for one fixture ({detail}) to the current target. */
    const writeOne = (it, writes, key) => {
      if (!writes.length) return;
      if (toScene) {
        writes.forEach(w => {
          FF.mutate(qlc, 'functions.scene.setValue', { functionId: String(sceneId), fixture: String(it.detail.id), channel: w.channel, value: w.value },
            { key: 'scene:' + sceneId + ':' + it.detail.id + ':' + w.channel }).catch(() => {});
          /* Own server echoes are filtered, so tell the open Scene editor directly. */
          FF.notifyLocal('scene.value', { sceneId: String(sceneId), fixture: String(it.detail.id), channel: w.channel, value: w.value });
        });
      } else {
        FF.writeLive(qlc, it.detail, writes, key + ':' + it.detail.id);
      }
    };
    const setInt = (v) => { setIntensity(v); apply('int', chs => FF.roleValues(chs, 'dimmer', v)); };
    const setColour = (c, w) => { setRgb(c); if (w) setWauv(w); apply('col', chs => FF.colourValues(chs, c, w || wauv)); };
    const setPos = (p, t) => { setPan(p); setTilt(t); apply('pos', chs => FF.positionValues(chs, 'pan', p).concat(FF.positionValues(chs, 'tilt', t))); };
    /* Preset channels are matched across fixtures by (role, ordinal within that role). */
    const setPreset = (role, ordinal, v) => {
      setPresetVals(s => Object.assign({}, s, { [role + ':' + ordinal]: v }));
      apply('pre', chs => { const list = chs.filter(c => c.role === role && !c.fine); const ch = list[ordinal]; return ch ? [{ channel: ch.index, value: v }] : []; });
    };
    const release = () => {
      const client = qlc.client();
      if (!client) return;
      items.forEach(it => { for (let i = 0; i < it.detail.channels; i++) client.resetChannel(FF.flatAddress(it.detail, i)); });
    };
    /* Highlight (LeftPanel.qml's "locate"): full intensity + white on the selected fixtures as a
       desk override; toggling off releases exactly those channels again. */
    const [highlight, setHighlight] = React.useState(false);
    const toggleHighlight = () => {
      const client = qlc.client();
      if (!client) return;
      if (!highlight) {
        items.forEach(it => {
          const writes = FF.roleValues(it.channels, 'dimmer', 255).concat(FF.colourValues(it.channels, { r: 255, g: 255, b: 255 }, { w: 255, a: 0, uv: 0 }));
          if (writes.length) client.setChannels(writes.map(w => ({ address: FF.flatAddress(it.detail, w.channel), value: w.value })));
        });
      } else {
        items.forEach(it => {
          const writes = FF.roleValues(it.channels, 'dimmer', 0).concat(FF.colourValues(it.channels, { r: 0, g: 0, b: 0 }, { w: 0, a: 0, uv: 0 }));
          writes.forEach(w => client.resetChannel(FF.flatAddress(it.detail, w.channel)));
        });
      }
      setHighlight(!highlight);
    };
    React.useEffect(() => { setHighlight(false); }, [fixtureIds.join(',')]);

    if (!fixtureIds.length) return (
      <div style={{ padding: 10, display: 'flex', flexDirection: 'column', gap: 6 }}>
        <RobotoText label="Fixture Tools" fontBold fontSize={14} />
        <FF.Note text="Select one or more fixtures in the tree (Ctrl-click, Shift-click or the multi-select toggle) to drive intensity, colour, position and presets." />
      </div>
    );

    /* Primary fixture (first selected) provides the preset channel list; others follow by role. */
    const primary = items[0];
    const presetRows = primary ? PRESET_GROUPS.map(([role, icon, label]) => {
      const list = primary.channels.filter(c => c.role === role && !c.fine);
      return list.length ? { role, icon, label, list } : null;
    }).filter(Boolean) : [];
    const capsPending = items.some(it => !it.capsKnown);
    const anyCaps = items.some(it => it.hasCaps);

    return (
      <div style={{ display: 'flex', flexDirection: 'column' }}>
        <div style={{ padding: '8px 10px 4px', display: 'flex', flexDirection: 'column', gap: 6 }}>
          <RobotoText label={'Fixture Tools · ' + fixtureIds.length + ' fixture' + (fixtureIds.length === 1 ? '' : 's')} fontBold fontSize={14} />
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <RobotoText label="Target" fontSize={13} labelColor="var(--fg-light)" style={{ width: 44 }} />
            <FF.Choice options={sceneId != null ? ['live', 'scene'] : ['live']} value={toScene ? 'scene' : 'live'} onChange={setTarget}
              labels={{ live: 'Live output', scene: 'Scene' + (sceneName ? ': ' + sceneName : '') }} />
          </div>
          <FF.Note text={toScene ? 'Values are written into the scene (functions.scene.setValue). Fixtures not yet in the scene are added by the first value.' : 'Values override the DMX output like Simple Desk channels. Release removes the overrides again.'} />
          {!toScene ? <div style={{ display: 'flex', gap: 6 }}>
            <GenericButton label="Release fixtures" width={130} height={24} onClick={release} />
            <span title="Highlight: full white on the selected fixtures (locate them on the rig); click again to release" data-ff-highlight={highlight ? 'on' : 'off'}>
              <GenericButton label={highlight ? 'Highlight off' : 'Highlight'} width={100} height={24} bgColor={highlight ? 'var(--highlight)' : undefined} onClick={toggleHighlight} />
            </span>
          </div> : null}
        </div>
        {items.length < fixtureIds.length ? <div style={{ padding: '0 10px' }}><RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" /></div> : null}

        {mask.intensity ? (
          <Section label="Intensity" icon="intensity">
            <div style={{ display: 'flex', gap: 12, alignItems: 'flex-start' }}>
              <QLCPlusFader value={intensity} onMoved={setInt} height={120} />
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                <CustomSpinBox value={intensity} from={0} to={255} width={70} onValueModified={setInt} />
                <RobotoText label={Math.round(intensity / 2.55) + ' %'} fontSize={13} labelColor="var(--fg-light)" height={20} />
                <GenericButton label="Full" width={70} height={24} onClick={() => setInt(255)} />
                <GenericButton label="Zero" width={70} height={24} onClick={() => setInt(0)} />
              </div>
            </div>
          </Section>
        ) : null}

        {mask.colour ? (
          <Section label="Color" icon="color">
            <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
              <input type="color" value={FF.hex(rgb)} onChange={e => { const c = FF.parseHex(e.target.value); if (c) setColour(c); }}
                title="Pick a colour" style={{ width: 44, height: 30, padding: 0, border: 'var(--border-control)', background: 'var(--bg-control)', cursor: 'pointer' }} />
              <HexInput value={FF.hex(rgb)} onCommit={setColour} />
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(6, 1fr)', gap: 4 }}>
              {BASIC_COLOURS.map(([name, r, g, b]) => (
                <img key={name} src={D.icon(name)} alt={name} title={name} onClick={() => setColour({ r, g, b })}
                  style={{ width: '100%', aspectRatio: 1, cursor: 'pointer', border: (rgb.r === r && rgb.g === g && rgb.b === b) ? '2px solid var(--highlight)' : '2px solid transparent', boxSizing: 'border-box' }} />
              ))}
              <img src={D.icon('blackout')} alt="black" title="Black" onClick={() => setColour({ r: 0, g: 0, b: 0 }, { w: 0, a: 0, uv: 0 })} style={{ width: '100%', aspectRatio: 1, cursor: 'pointer', border: '2px solid transparent', boxSizing: 'border-box' }} />
            </div>
            <Slider label="Red" icon="red" value={rgb.r} onChange={v => setColour({ r: v, g: rgb.g, b: rgb.b })} width={120} />
            <Slider label="Green" icon="green" value={rgb.g} onChange={v => setColour({ r: rgb.r, g: v, b: rgb.b })} width={120} />
            <Slider label="Blue" icon="blue" value={rgb.b} onChange={v => setColour({ r: rgb.r, g: rgb.g, b: v })} width={120} />
            {hasWauv ? <>
              <Slider label="White" icon="white" value={wauv.w} onChange={v => setColour(rgb, { w: v, a: wauv.a, uv: wauv.uv })} width={120} />
              <Slider label="Amber" icon="amber" value={wauv.a} onChange={v => setColour(rgb, { w: wauv.w, a: v, uv: wauv.uv })} width={120} />
              <Slider label="UV" icon="uv" value={wauv.uv} onChange={v => setColour(rgb, { w: wauv.w, a: wauv.a, uv: v })} width={120} />
            </> : null}
            {channelsAll.some(c => c.role === 'cyan' || c.role === 'magenta' || c.role === 'yellow') ? <FF.Note text="CMY fixtures receive the converted complement of the RGB colour." /> : null}
          </Section>
        ) : null}

        {mask.colour && FF.ColorFiltersPicker ? (
          <Section label="Color filters" icon="color" open={false}>
            <FF.ColorFiltersPicker qlc={qlc} onPick={(c, w) => setColour(c, w)} />
          </Section>
        ) : null}

        {mask.position && FF.SingleAxis && (!hasPan || !hasTilt) ? (
          <Section label="Position" icon="position">
            {hasPan ? <FF.SingleAxis axis="pan" value16={pan} maxDegrees={panMax} onChange={p => setPos(p, tilt)} />
              : <FF.SingleAxis axis="tilt" value16={tilt} maxDegrees={tiltMax} onChange={t => setPos(pan, t)} />}
          </Section>
        ) : mask.position ? (
          <Section label="Position" icon="position">
            <div style={{ display: 'flex', gap: 10, alignItems: 'flex-start' }}>
              <XYPad pan={pan} tilt={tilt} onMove={setPos} size={140} />
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                <GenericButton label="Center" width={80} height={24} onClick={() => setPos(32768, 32768)} />
                <RobotoText label={'Pan ' + (pan >> 8) + (has16 ? '.' + (pan & 255) : '')} fontSize={13} labelColor="var(--fg-light)" height={20} />
                <RobotoText label={'Tilt ' + (tilt >> 8) + (has16 ? '.' + (tilt & 255) : '')} fontSize={13} labelColor="var(--fg-light)" height={20} />
              </div>
            </div>
            <Slider label="Pan" icon="pan" value={has16 ? pan : (pan >> 8)} to={has16 ? 65535 : 255} onChange={v => setPos(has16 ? v : v * 257, tilt)} width={110} />
            <Slider label="Tilt" icon="tilt" value={has16 ? tilt : (tilt >> 8)} to={has16 ? 65535 : 255} onChange={v => setPos(pan, has16 ? v : v * 257)} width={110} />
          </Section>
        ) : null}

        {presetRows.map(row => (
          <Section key={row.role} label={row.label} icon={row.icon} open={row.role !== 'maintenance' && row.role !== 'other'}>
            {row.list.map((ch, ordinal) => {
              const key = row.role + ':' + ordinal;
              const v = presetVals[key] != null ? presetVals[key] : 0;
              return (
                <div key={ch.index} style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                  <Slider label={ch.name} value={v} onChange={val => setPreset(row.role, ordinal, val)} width={120} />
                  {ch.capabilities && ch.capabilities.length ? (
                    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 3, paddingLeft: 6 }}>
                      {ch.capabilities.map((cap, i) => {
                        const active = v >= cap.min && v <= cap.max;
                        return (
                          <button key={i} type="button" onClick={() => setPreset(row.role, ordinal, cap.min)} title={cap.name + ' (' + cap.min + '–' + cap.max + ')'}
                            style={{ height: 22, padding: '0 6px', border: 'var(--border-control)', background: active ? 'var(--highlight)' : 'var(--bg-control)', color: 'var(--fg-main)', fontFamily: 'var(--font-roboto)', fontSize: 12, cursor: 'pointer', display: 'inline-flex', alignItems: 'center', gap: 4, maxWidth: 130 }}>
                            {cap.color1 ? <span style={{ width: 10, height: 10, background: cap.color1, border: '1px solid var(--bg-strong)', flex: 'none' }} /> : null}
                            {cap.color2 ? <span style={{ width: 10, height: 10, background: cap.color2, border: '1px solid var(--bg-strong)', flex: 'none' }} /> : null}
                            <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{cap.name}</span>
                          </button>
                        );
                      })}
                    </div>
                  ) : null}
                </div>
              );
            })}
          </Section>
        ))}
        {presetRows.length && !anyCaps && !capsPending ? (
          <div style={{ padding: '4px 10px 10px' }}><FF.Note text="Capability presets (gobo names, colour wheel slots, strobe ranges) are not available: the server has no fixtures.defs.getMode/getModel yet, so these channels only get plain 0–255 sliders." /></div>
        ) : null}
        {FF.FixtureConsole ? (
          <Section label="Channels" icon="sliders" open={false}>
            {items.length ? <FF.FixtureConsole items={items} allFixtures={fixtures} writeFn={writeOne} /> : <RobotoText label="Loading…" fontSize={13} labelColor="var(--fg-medium)" />}
          </Section>
        ) : null}
        {items.length > 1 ? <div style={{ padding: '4px 10px 10px' }}><FF.Note text="With several fixtures selected, presets follow the first fixture's channel layout and are applied to every fixture that has a channel of the same kind." /></div> : null}
      </div>
    );
  }

  FF.FixtureTools = FixtureTools;
  FF.useToolFixtures = useToolFixtures;
  FF.ToolSection = Section;
  FF.ToolSlider = Slider;
})();
