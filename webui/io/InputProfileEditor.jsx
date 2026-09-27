/**
 * InputProfileEditor.jsx — the input profile editor of the Input/Output screen, mirroring
 * qmlui/qml/inputoutput/InputProfileEditor.qml (manufacturer, model, type, MIDI note-off setting,
 * channel table, colour table, MIDI channel table, auto-detection) and
 * qmlui/qml/popup/PopupInputChannelEditor.qml (number, name, type, MIDI channel/message/parameter,
 * behaviour, sensitivity, custom feedback values). Whole-document save/delete through
 * io.inputProfile.save / io.inputProfile.delete (§4c profilesRevision), channel detection through
 * io.inputProfile.learn.start/stop + the requester-scoped io.inputProfile.learn.signal event.
 *
 * Profile channel numbers are the engine's 0-based keys (IoInputChannel.number); the UI shows +1
 * like the desktop. MIDI channel numbers encode (midiChannel * 4096 + messageOffset + parameter),
 * see plugins/midi/src/common/midiprotocol.h.
 *
 * window.IOInputProfileEditor = { InputProfileEditorDialog }
 */
(function () {
  'use strict';
  const { RobotoText, IconButton, GenericButton, CustomCheckBox, CustomComboBox, CustomSpinBox, CustomPopupDialog, MenuBarEntry, IconTextEntry } = window.PatchDesignSystem_5432c9;
  const { Row, TextField, isUnknownMethod, errorText, NOTE, withConflictRetry } = window.IOShared;

  const PROFILE_TYPES = ['MIDI', 'OS2L', 'OSC', 'HID', 'DMX', 'Enttec'];
  const CHANNEL_TYPES = [
    { v: 'Slider', l: 'Slider', icon: 'slider' }, { v: 'Knob', l: 'Knob', icon: 'knob' }, { v: 'Encoder', l: 'Encoder', icon: 'knob' },
    { v: 'Button', l: 'Button', icon: 'button' }, { v: 'NextPage', l: 'Next Page', icon: 'forward' }, { v: 'PrevPage', l: 'Previous Page', icon: 'back' },
    { v: 'PageSet', l: 'Page Set', icon: 'other' }, { v: 'NoType', l: 'None', icon: 'other' }
  ];
  const typeInfo = (v) => CHANNEL_TYPES.find(t => t.v === v) || CHANNEL_TYPES[CHANNEL_TYPES.length - 1];

  /* MIDI channel number layout (midiprotocol.h CHANNEL_OFFSET_*, midiChannelOffset in PopupInputChannelEditor.qml). */
  const MIDI_CH_OFFSET = 4096;
  const MIDI_MESSAGES = [
    { v: 'cc', l: 'Control Change', off: 0, param: true }, { v: 'note', l: 'Note On/Off', off: 128, param: true },
    { v: 'noteat', l: 'Note Aftertouch', off: 256, param: true }, { v: 'pc', l: 'Program Change', off: 384, param: true },
    { v: 'chat', l: 'Channel Aftertouch', off: 512, param: false }, { v: 'pitch', l: 'Pitch Wheel', off: 513, param: false },
    { v: 'mbcplay', l: 'Beat Clock: Start/Stop/Continue', off: 529, param: false }, { v: 'mbcbeat', l: 'Beat Clock: Beat', off: 530, param: false },
    { v: 'mbcstop', l: 'Beat Clock: Stop', off: 531, param: false }
  ];
  const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  function midiDecode(number) {
    const channel = Math.floor(number / MIDI_CH_OFFSET), rest = number % MIDI_CH_OFFSET;
    let msg = MIDI_MESSAGES[0];
    for (const m of MIDI_MESSAGES) if (rest >= m.off) msg = m;
    const param = msg.param ? rest - msg.off : 0;
    return { channel, message: msg.v, param, note: msg.v === 'note' ? NOTES[param % 12] + (Math.floor(param / 12) - 1) : '--' };
  }
  function midiEncode(channel, message, param) {
    const msg = MIDI_MESSAGES.find(m => m.v === message) || MIDI_MESSAGES[0];
    return channel * MIDI_CH_OFFSET + msg.off + (msg.param ? Math.max(0, Math.min(127, param)) : 0);
  }
  function channelLabel(profileType, number) {
    if (profileType !== 'MIDI') return String(number + 1);
    const d = midiDecode(number);
    const msg = MIDI_MESSAGES.find(m => m.v === d.message);
    return 'Ch ' + (d.channel + 1) + ' · ' + (msg ? msg.l : '?') + (msg && msg.param ? ' ' + d.param : '') + (d.message === 'note' ? ' (' + d.note + ')' : '');
  }

  const emptyProfile = () => ({ manufacturer: '', model: '', type: 'MIDI', midiSendNoteOff: true, channels: [], colorTable: [], midiChannelTable: [] });
  const defaultChannel = (number) => ({ number, name: '', type: 'Button', movementType: 'Absolute', movementSensitivity: 20, sendExtraPress: false, lowerValue: 0, upperValue: 255, lowerChannel: -1 });
  const sortChannels = (list) => list.slice().sort((a, b) => a.number - b.number);
  const head = { display: 'flex', alignItems: 'center', height: 26, background: 'var(--section-header)', borderBottom: '2px solid var(--section-header-div)', padding: '0 6px', flex: 'none' };
  const cell = (w) => ({ width: w, minWidth: w, flex: w ? 'none' : 1, padding: '0 4px', boxSizing: 'border-box' });

  /* ---- channel editor (PopupInputChannelEditor.qml) -------------------------------------------- */
  function ChannelEditorDialog({ open, profileType, channel, takenNumbers, onSave, onClose }) {
    const D = window.QLCData;
    const [ch, setCh] = React.useState(channel || defaultChannel(0));
    const [error, setError] = React.useState('');
    React.useEffect(() => { if (open) { setCh(channel || defaultChannel(0)); setError(''); } }, [open]);
    if (!open) return null;
    const isMidi = profileType === 'MIDI';
    const midi = midiDecode(ch.number);
    const set = (patch) => setCh(c => Object.assign({}, c, patch));
    const setMidi = (patch) => { const m = Object.assign({}, midi, patch); set({ number: midiEncode(m.channel, m.message, m.param) }); };
    const msgInfo = MIDI_MESSAGES.find(m => m.v === midi.message) || MIDI_MESSAGES[0];
    const save = () => {
      if (!ch.name.trim()) { setError('The channel needs a name'); return; }
      if (takenNumbers.indexOf(ch.number) !== -1 && (!channel || channel.number !== ch.number)) { setError('Channel ' + (ch.number + 1) + ' is already in the profile'); return; }
      onSave(Object.assign({}, ch, { name: ch.name.trim() }));
    };
    const sliderLike = ch.type === 'Slider' || ch.type === 'Knob';
    return (
      <CustomPopupDialog open={open} title={channel ? 'Edit channel' : 'Add channel'} width={520} standardButtons={['Cancel', 'Ok']} onClicked={(b) => { if (b === 'Ok') save(); else onClose(); }} onClose={onClose}>
        <div data-role="channel-editor" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
          <Row label="Number" width={90} title="Channel number within the profile (1-based here, 0-based in the file)">
            <CustomSpinBox value={ch.number + 1} from={1} to={65536 * 4} width={110} height={24} onValueModified={v => set({ number: v - 1 })} data-role="channel-number" />
          </Row>
          <Row label="Name" width={90}><TextField value={ch.name} width="100%" placeholder="e.g. Fader 1" onLive={t => set({ name: t })} onCommit={t => set({ name: t })} data-role="channel-name" /></Row>
          <Row label="Type" width={90}>
            <CustomComboBox width={200} height={24} currValue={ch.type} model={CHANNEL_TYPES.map(t => ({ mLabel: t.l, mValue: t.v, mIcon: D.icon(t.icon) }))} onValueChanged={v => { if (v !== ch.type) set({ type: v, movementSensitivity: v === 'Encoder' ? 1 : 20 }); }} data-role="channel-type" />
          </Row>
          {isMidi ? (
            <div style={{ border: '1px solid var(--bg-light)', borderRadius: 4, padding: 6, display: 'flex', flexDirection: 'column', gap: 4 }}>
              <RobotoText label="MIDI" fontSize={12} fontBold height={20} />
              <Row label="Channel" width={90}><CustomSpinBox value={midi.channel + 1} from={1} to={16} width={80} height={24} onValueModified={v => setMidi({ channel: v - 1 })} data-role="midi-channel" /></Row>
              <Row label="Message" width={90}>
                <CustomComboBox width={260} height={24} currValue={midi.message} model={MIDI_MESSAGES.map(m => ({ mLabel: m.l, mValue: m.v }))} onValueChanged={v => setMidi({ message: v, param: 0 })} data-role="midi-message" />
              </Row>
              <Row label="Parameter" width={90}>
                <CustomSpinBox value={midi.param} from={0} to={127} width={80} height={24} disabled={!msgInfo.param} onValueModified={v => setMidi({ param: v })} data-role="midi-param" />
                <RobotoText label={midi.message === 'note' ? 'Note ' + midi.note : ''} fontSize={12} labelColor={NOTE} height={24} />
              </Row>
            </div>
          ) : null}
          {ch.type === 'Button' ? (
            <div style={{ border: '1px solid var(--bg-light)', borderRadius: 4, padding: 6, display: 'flex', flexDirection: 'column', gap: 4 }}>
              <RobotoText label="Button behaviour" fontSize={12} fontBold height={20} />
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                <CustomCheckBox checked={!!ch.sendExtraPress} size={22} onToggled={v => set({ sendExtraPress: v })} data-role="extra-press" />
                <RobotoText label="Generate an extra Press/Release when toggled" fontSize={14} height={24} />
              </div>
              <RobotoText label="Custom feedback" fontSize={12} fontBold height={20} />
              <Row label="Lower value" width={90}><CustomSpinBox value={ch.lowerValue} from={0} to={255} width={80} height={24} onValueModified={v => set({ lowerValue: v })} data-role="lower-value" /></Row>
              <Row label="Upper value" width={90}><CustomSpinBox value={ch.upperValue} from={0} to={255} width={80} height={24} onValueModified={v => set({ upperValue: v })} data-role="upper-value" /></Row>
              {isMidi ? <Row label="Feedback ch." width={90} title="MIDI channel the feedback is sent on (0 = same as the input)">
                <CustomSpinBox value={ch.lowerChannel + 1} from={0} to={16} width={80} height={24} onValueModified={v => set({ lowerChannel: v - 1 })} data-role="feedback-channel" />
                <RobotoText label={ch.lowerChannel < 0 ? 'same as input' : 'channel ' + (ch.lowerChannel + 1)} fontSize={12} labelColor={NOTE} height={24} />
              </Row> : null}
            </div>
          ) : null}
          {sliderLike || ch.type === 'Encoder' ? (
            <div style={{ border: '1px solid var(--bg-light)', borderRadius: 4, padding: 6, display: 'flex', flexDirection: 'column', gap: 4 }}>
              <RobotoText label={ch.type + ' behaviour'} fontSize={12} fontBold height={20} />
              {sliderLike ? <Row label="Movement" width={90}>
                <CustomComboBox width={140} height={24} currValue={ch.movementType} model={[{ mLabel: 'Absolute', mValue: 'Absolute' }, { mLabel: 'Relative', mValue: 'Relative' }]} onValueChanged={v => set({ movementType: v })} data-role="movement" />
              </Row> : null}
              {/* InputProfileEditor.qml updateOptions(): Slider / Knob 10..100, Encoder 1..20 */}
              <Row label="Sensitivity" width={90}>
                <CustomSpinBox value={ch.movementSensitivity} from={sliderLike ? 10 : 1} to={sliderLike ? 100 : 20} width={80} height={24} onValueModified={v => set({ movementSensitivity: v })} data-role="sensitivity" />
              </Row>
            </div>
          ) : null}
          {error ? <RobotoText label={error} fontSize={12} labelColor="var(--override-red)" height={22} data-role="channel-error" /> : null}
        </div>
      </CustomPopupDialog>
    );
  }

  /* ---- the editor ------------------------------------------------------------------------------ */
  function InputProfileEditorDialog({ open, qlc, profileName, universes, onClose, onStatus }) {
    const D = window.QLCData;
    const [profile, setProfile] = React.useState(emptyProfile);
    const [loaded, setLoaded] = React.useState(null);        // the server copy (name/path/isUser), null for a new profile
    const [revision, setRevision] = React.useState(0);
    const [dirty, setDirty] = React.useState(false);
    const [tab, setTab] = React.useState('channels');
    const [sel, setSel] = React.useState(null);              // selected channel number
    const [selColor, setSelColor] = React.useState(null);
    const [selMidi, setSelMidi] = React.useState(null);
    const [chEditor, setChEditor] = React.useState(null);    // {channel|null}
    const [confirmDelete, setConfirmDelete] = React.useState(false);
    const [status, setStatus] = React.useState({ text: '', error: false });
    const [detect, setDetect] = React.useState({ on: false, universeId: null });
    const [signaled, setSignaled] = React.useState(null);
    const [newColor, setNewColor] = React.useState({ value: 0, label: '', color: '#ff0000' });
    const [newMidi, setNewMidi] = React.useState({ channel: 1, label: '' });
    const history = React.useRef({});
    const detectRef = React.useRef(detect);
    detectRef.current = detect;

    /* load */
    React.useEffect(() => {
      if (!open) return;
      setDirty(false); setStatus({ text: '', error: false }); setTab('channels'); setSel(null); setSignaled(null); history.current = {};
      if (!profileName) { setProfile(emptyProfile()); setLoaded(null); qlc.call('io.inputProfile.list').then(r => setRevision(r.profilesRevision || 0)).catch(() => {}); return; }
      qlc.call('io.inputProfile.get', { name: profileName }).then(r => {
        const p = r.profile || {};
        setProfile({ manufacturer: p.manufacturer || '', model: p.model || '', type: p.type || 'MIDI', midiSendNoteOff: p.midiSendNoteOff !== false,
          channels: sortChannels(p.channels || []), colorTable: (p.colorTable || []).slice(), midiChannelTable: (p.midiChannelTable || []).slice() });
        setLoaded(p); setRevision(r.profilesRevision || 0);
      }).catch(e => setStatus({ text: 'Cannot load the profile: ' + errorText(e), error: true }));
    }, [open, profileName]);

    /* detection: learn.signal is only delivered to this client while our session is active */
    React.useEffect(() => {
      if (!open) return;
      const off = qlc.subscribeTo('io.inputProfile.learn.signal', (data) => {
        if (!detectRef.current.on || !data) return;
        const number = data.channelNumber;
        const values = history.current[number] = (history.current[number] || []).concat([data.value]).slice(-10);
        setProfile(p => {
          const existing = p.channels.find(c => c.number === number);
          if (!existing) {
            const name = data.key || ('Button ' + (number + 1));
            return Object.assign({}, p, { channels: sortChannels(p.channels.concat([Object.assign(defaultChannel(number), { name, type: 'Button' })])) });
          }
          /* InputProfileEditor::shouldPromoteToSlider: two consecutive values less than a full swing apart → a fader */
          if (existing.type === 'Button' && values.length >= 2 && Math.abs(values[values.length - 1] - values[values.length - 2]) < 255) {
            const renamed = /^Button \d+$/.test(existing.name) ? (data.key || 'Slider ' + (number + 1)) : existing.name;
            return Object.assign({}, p, { channels: p.channels.map(c => c.number === number ? Object.assign({}, c, { type: 'Slider', name: renamed }) : c) });
          }
          return p;
        });
        setDirty(true);
        setSel(number);
        setSignaled(number);
        setTimeout(() => setSignaled(s => s === number ? null : s), 1200);
      });
      return off;
    }, [open]);
    const stopDetect = React.useCallback(() => {
      if (detectRef.current.on) qlc.call('io.inputProfile.learn.stop', {}).catch(() => {});
      setDetect(d => Object.assign({}, d, { on: false }));
    }, []);
    React.useEffect(() => () => { if (detectRef.current.on) qlc.call('io.inputProfile.learn.stop', {}).catch(() => {}); }, []);
    React.useEffect(() => { if (!open) stopDetect(); }, [open]);

    if (!open) return null;

    const edit = (patch) => { setProfile(p => Object.assign({}, p, patch)); setDirty(true); };
    const inputUniverses = (universes || []).filter(u => u.inputPatch);
    const detectUniverse = detect.universeId != null ? detect.universeId : (inputUniverses[0] ? inputUniverses[0].id : null);
    const toggleDetect = () => {
      if (detect.on) { stopDetect(); return; }
      if (detectUniverse == null) { setStatus({ text: 'Patch an input line to a universe first, then detect from it', error: true }); return; }
      qlc.call('io.inputProfile.learn.start', Object.assign({ universeId: detectUniverse }, loaded ? { profileName: loaded.name } : {}))
        .then(() => { history.current = {}; setDetect({ on: true, universeId: detectUniverse }); setStatus({ text: 'Detecting: move a control on the patched input; new channels are added as buttons and promoted to sliders when they sweep', error: false }); })
        .catch(e => setStatus({ text: isUnknownMethod(e) ? 'Not available: this server has no io.inputProfile.learn.start' : 'Cannot start detection: ' + errorText(e), error: true }));
    };

    const saveChannel = (ch) => {
      const original = chEditor && chEditor.channel;
      edit({ channels: sortChannels(profile.channels.filter(c => !(original && c.number === original.number) && c.number !== ch.number).concat([ch])) });
      setSel(ch.number); setChEditor(null);
    };
    const removeChannel = () => { if (sel == null) return; edit({ channels: profile.channels.filter(c => c.number !== sel) }); setSel(null); };

    const save = () => {
      const manufacturer = profile.manufacturer.trim(), model = profile.model.trim();
      if (!manufacturer || !model) { setStatus({ text: 'Manufacturer and model are required', error: true }); return; }
      const payload = { manufacturer, model, type: profile.type, midiSendNoteOff: !!profile.midiSendNoteOff, channels: profile.channels, colorTable: profile.colorTable, midiChannelTable: profile.midiChannelTable };
      withConflictRetry((details) => qlc.call('io.inputProfile.save', { profile: payload, baseRevision: details && details.profilesRevision != null ? details.profilesRevision : revision }))
        .then(r => {
          setRevision(r.profilesRevision); setDirty(false);
          setLoaded(Object.assign({}, loaded || {}, { name: r.name || (manufacturer + ' ' + model), path: r.path, isUser: true }));
          setStatus({ text: 'Saved ' + (r.name || manufacturer + ' ' + model) + (r.path ? ' to ' + r.path : ''), error: false });
          if (onStatus) onStatus('Input profile "' + (r.name || manufacturer + ' ' + model) + '" saved', false);
        })
        .catch(e => setStatus({ text: isUnknownMethod(e) ? 'Not available: this server has no io.inputProfile.save' : 'Save failed: ' + errorText(e), error: true }));
    };
    const remove = () => {
      setConfirmDelete(false);
      if (!loaded) return;
      withConflictRetry((details) => qlc.call('io.inputProfile.delete', { name: loaded.name, baseRevision: details && details.profilesRevision != null ? details.profilesRevision : revision }))
        .then(() => { if (onStatus) onStatus('Input profile "' + loaded.name + '" deleted', false); onClose(); })
        .catch(e => setStatus({ text: 'Delete failed: ' + errorText(e), error: true }));
    };
    const close = () => { stopDetect(); onClose(); };

    const takenNumbers = profile.channels.map(c => c.number);
    const selected = profile.channels.find(c => c.number === sel) || null;
    const isMidi = profile.type === 'MIDI';
    const tabs = [['channels', 'Input Mapping'], ['colors', 'Colors'], ['midi', 'MIDI Channels']];
    const title = (loaded ? 'Edit input profile — ' + loaded.name : 'New input profile') + (dirty ? ' *' : '');

    return (
      <CustomPopupDialog open={open} title={title} width={780} standardButtons={[]} onClose={close}>
        <div data-role="profile-editor" style={{ display: 'flex', flexDirection: 'column', gap: 8, maxHeight: '78vh' }}>
          <div style={{ display: 'flex', gap: 16 }}>
            <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 4 }}>
              <Row label="Manufacturer" width={100}><TextField value={profile.manufacturer} width="100%" placeholder="e.g. Novation" onLive={t => edit({ manufacturer: t })} onCommit={t => edit({ manufacturer: t })} data-role="profile-manufacturer" /></Row>
              <Row label="Model" width={100}><TextField value={profile.model} width="100%" placeholder="e.g. Launch Control" onLive={t => edit({ model: t })} onCommit={t => edit({ model: t })} data-role="profile-model" /></Row>
              <Row label="Type" width={100}>
                <CustomComboBox width={160} height={24} currValue={profile.type} model={PROFILE_TYPES.map(t => ({ mLabel: t, mValue: t }))} onValueChanged={v => edit({ type: v })} data-role="profile-type" />
              </Row>
            </div>
            <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 4 }}>
              {isMidi ? (
                <div style={{ display: 'flex', alignItems: 'flex-start', gap: 8 }}>
                  <CustomCheckBox checked={!!profile.midiSendNoteOff} size={22} onToggled={v => edit({ midiSendNoteOff: v })} data-role="midi-note-off" />
                  <RobotoText label="When MIDI notes are used, send a Note Off when value is 0" fontSize={12} wrapText height="auto" />
                </div>
              ) : null}
              <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <GenericButton label={detect.on ? 'Stop detection' : 'Detect channels'} width={130} height={26} bgColor={detect.on ? 'var(--selection)' : undefined} disabled={qlc.isUnsupported('io.inputProfile.learn.start')} onClick={toggleDetect} data-role="detect-toggle" />
                <RobotoText label="from" fontSize={12} height={26} />
                <CustomComboBox width={150} height={24} currValue={detectUniverse == null ? -1 : detectUniverse} disabled={detect.on}
                  model={inputUniverses.length ? inputUniverses.map(u => ({ mLabel: u.name + ' (' + u.inputPatch.pluginName + ')', mValue: u.id })) : [{ mLabel: 'no input patched', mValue: -1 }]}
                  onValueChanged={v => setDetect(d => Object.assign({}, d, { universeId: v }))} data-role="detect-universe" />
              </div>
              <RobotoText label={loaded ? (loaded.isUser ? 'User profile' + (loaded.path ? ': ' + loaded.path : '') : 'Bundled profile: saving writes a user copy that overrides it') : 'Saved as <Manufacturer>-<Model>.qxi in the QLC+ host\'s user profile folder'} fontSize={12} labelColor={NOTE} wrapText height="auto" />
            </div>
          </div>

          <div style={{ display: 'flex', gap: 2, borderBottom: '2px solid var(--bg-light)' }}>
            {tabs.map(([id, label]) => <MenuBarEntry key={id} entryText={label} checked={tab === id} height={28} checkedColor="var(--toolbar-selection-sub)" onClick={() => setTab(id)} data-tab={id} />)}
          </div>

          <div style={{ flex: 1, minHeight: 220, maxHeight: 340, overflow: 'auto', border: '1px solid var(--bg-light)', background: 'var(--bg-stronger)' }}>
            {tab === 'channels' ? (
              <div>
                <div style={head}><RobotoText label="Channel" fontSize={14} fontBold style={cell(220)} /><RobotoText label="Name" fontSize={14} fontBold style={cell(0)} /><RobotoText label="Type" fontSize={14} fontBold style={cell(150)} /></div>
                {profile.channels.map(c => {
                  const t = typeInfo(c.type);
                  return (
                    <div key={c.number} data-channel={c.number} onClick={() => setSel(c.number)} onDoubleClick={() => setChEditor({ channel: c })}
                      style={{ display: 'flex', alignItems: 'center', height: 28, cursor: 'pointer', background: sel === c.number ? 'var(--highlight)' : signaled === c.number ? 'var(--selection)' : 'transparent', borderBottom: '1px solid var(--bg-light)' }}>
                      <RobotoText label={channelLabel(profile.type, c.number)} fontSize={13} height={28} style={cell(220)} title={'Channel number ' + (c.number + 1)} />
                      <RobotoText label={c.name} fontSize={14} height={28} style={cell(0)} />
                      <IconTextEntry iSrc={D.icon(t.icon)} tLabel={t.l} tFontSize={13} height={28} style={cell(150)} />
                    </div>
                  );
                })}
                {!profile.channels.length ? <RobotoText label="No channels yet — add one, or start detection and move the controls of the connected device." fontSize={12} labelColor={NOTE} wrapText height="auto" leftMargin={6} /> : null}
              </div>
            ) : null}
            {tab === 'colors' ? (
              <div>
                <div style={head}><RobotoText label="Value" fontSize={14} fontBold style={cell(80)} /><RobotoText label="Label" fontSize={14} fontBold style={cell(0)} /><RobotoText label="Color" fontSize={14} fontBold style={cell(120)} /></div>
                {profile.colorTable.slice().sort((a, b) => a.value - b.value).map(c => (
                  <div key={c.value} data-color={c.value} onClick={() => setSelColor(c.value)} style={{ display: 'flex', alignItems: 'center', height: 28, cursor: 'pointer', background: selColor === c.value ? 'var(--highlight)' : 'transparent', borderBottom: '1px solid var(--bg-light)' }}>
                    <RobotoText label={String(c.value)} fontSize={14} height={28} style={cell(80)} />
                    <RobotoText label={c.label} fontSize={14} height={28} style={cell(0)} />
                    <div style={Object.assign({}, cell(120), { display: 'flex', alignItems: 'center', gap: 6 })}><span style={{ width: 40, height: 18, background: c.color, border: '1px solid var(--fg-light)', borderRadius: 3 }} /><RobotoText label={c.color} fontSize={12} height={28} /></div>
                  </div>
                ))}
                {!profile.colorTable.length ? <RobotoText label="No colour table: the device's LED feedback colours are not described. Add value/label/colour rows for controllers with RGB pads." fontSize={12} labelColor={NOTE} wrapText height="auto" leftMargin={6} /> : null}
              </div>
            ) : null}
            {tab === 'midi' ? (
              <div>
                <div style={head}><RobotoText label="Channel" fontSize={14} fontBold style={cell(100)} /><RobotoText label="Name" fontSize={14} fontBold style={cell(0)} /></div>
                {profile.midiChannelTable.slice().sort((a, b) => a.channel - b.channel).map(m => (
                  <div key={m.channel} data-midi-channel={m.channel} onClick={() => setSelMidi(m.channel)} style={{ display: 'flex', alignItems: 'center', height: 28, cursor: 'pointer', background: selMidi === m.channel ? 'var(--highlight)' : 'transparent', borderBottom: '1px solid var(--bg-light)' }}>
                    <RobotoText label={String(m.channel + 1)} fontSize={14} height={28} style={cell(100)} />
                    <RobotoText label={m.label} fontSize={14} height={28} style={cell(0)} />
                  </div>
                ))}
                {!profile.midiChannelTable.length ? <RobotoText label="No MIDI channel labels. Name the MIDI channels the device uses (e.g. 1 = Faders, 2 = Pads)." fontSize={12} labelColor={NOTE} wrapText height="auto" leftMargin={6} /> : null}
              </div>
            ) : null}
          </div>

          {tab === 'channels' ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <GenericButton label="Add channel" width={110} height={26} onClick={() => setChEditor({ channel: null })} data-role="channel-add" />
              <GenericButton label="Edit" width={70} height={26} disabled={!selected} onClick={() => selected && setChEditor({ channel: selected })} data-role="channel-edit" />
              <GenericButton label="Remove" width={80} height={26} disabled={!selected} onClick={removeChannel} data-role="channel-remove" />
              <RobotoText label={profile.channels.length + ' channels'} fontSize={12} labelColor={NOTE} height={26} />
            </div>
          ) : null}
          {tab === 'colors' ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <RobotoText label="Value" fontSize={12} height={26} />
              <CustomSpinBox value={newColor.value} from={0} to={255} width={70} height={24} onValueModified={v => setNewColor(c => Object.assign({}, c, { value: v }))} data-role="color-value" />
              <RobotoText label="Label" fontSize={12} height={26} />
              <TextField value={newColor.label} width={160} placeholder="e.g. Red" onLive={t => setNewColor(c => Object.assign({}, c, { label: t }))} onCommit={t => setNewColor(c => Object.assign({}, c, { label: t }))} data-role="color-label" />
              <input type="color" value={newColor.color} onChange={e => setNewColor(c => Object.assign({}, c, { color: e.target.value }))} data-role="color-pick" style={{ width: 36, height: 24, padding: 0, border: '1px solid var(--spin-border)', background: 'var(--bg-control)' }} />
              <GenericButton label="Add / update" width={110} height={26} onClick={() => { edit({ colorTable: profile.colorTable.filter(c => c.value !== newColor.value).concat([{ value: newColor.value, label: newColor.label, color: newColor.color }]) }); setSelColor(newColor.value); }} data-role="color-add" />
              <GenericButton label="Remove" width={80} height={26} disabled={selColor == null} onClick={() => { edit({ colorTable: profile.colorTable.filter(c => c.value !== selColor) }); setSelColor(null); }} data-role="color-remove" />
            </div>
          ) : null}
          {tab === 'midi' ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
              <RobotoText label="Channel" fontSize={12} height={26} />
              <CustomSpinBox value={newMidi.channel} from={1} to={16} width={70} height={24} onValueModified={v => setNewMidi(m => Object.assign({}, m, { channel: v }))} data-role="midi-table-channel" />
              <RobotoText label="Name" fontSize={12} height={26} />
              <TextField value={newMidi.label} width={200} placeholder="e.g. Faders" onLive={t => setNewMidi(m => Object.assign({}, m, { label: t }))} onCommit={t => setNewMidi(m => Object.assign({}, m, { label: t }))} data-role="midi-table-label" />
              <GenericButton label="Add / update" width={110} height={26} onClick={() => { const ch = newMidi.channel - 1; edit({ midiChannelTable: profile.midiChannelTable.filter(m => m.channel !== ch).concat([{ channel: ch, label: newMidi.label }]) }); setSelMidi(ch); }} data-role="midi-table-add" />
              <GenericButton label="Remove" width={80} height={26} disabled={selMidi == null} onClick={() => { edit({ midiChannelTable: profile.midiChannelTable.filter(m => m.channel !== selMidi) }); setSelMidi(null); }} data-role="midi-table-remove" />
            </div>
          ) : null}

          {status.text ? <RobotoText label={status.text} fontSize={12} labelColor={status.error ? 'var(--override-red)' : 'var(--check-lime)'} wrapText height="auto" data-role="profile-status" /> : null}

          <div style={{ display: 'flex', alignItems: 'center', gap: 8, borderTop: '1px solid var(--bg-light)', paddingTop: 8 }}>
            {loaded && loaded.isUser !== false ? <GenericButton label="Delete profile" width={120} height={26} disabled={qlc.isUnsupported('io.inputProfile.delete')} onClick={() => setConfirmDelete(true)} data-role="profile-delete" /> : null}
            <div style={{ flex: 1 }} />
            <GenericButton label={dirty ? 'Discard' : 'Close'} width={90} height={26} onClick={close} data-role="profile-close" />
            <GenericButton label="Save" width={90} height={26} disabled={!dirty && !!loaded || qlc.isUnsupported('io.inputProfile.save')} onClick={save} data-role="profile-save" />
          </div>
        </div>

        <ChannelEditorDialog open={!!chEditor} profileType={profile.type} channel={chEditor ? chEditor.channel : null} takenNumbers={takenNumbers} onSave={saveChannel} onClose={() => setChEditor(null)} />
        <CustomPopupDialog open={confirmDelete} title="Delete input profile" width={380}
          message={loaded ? 'Delete "' + loaded.name + '"' + (loaded.path ? ' (' + loaded.path + ')' : '') + '? Universes using it lose their profile.' : ''}
          standardButtons={['Cancel', 'Delete']} onClicked={(b) => { if (b === 'Delete') remove(); else setConfirmDelete(false); }} onClose={() => setConfirmDelete(false)} />
      </CustomPopupDialog>
    );
  }

  window.IOInputProfileEditor = { InputProfileEditorDialog, midiDecode, midiEncode, channelLabel };
})();
