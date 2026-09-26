window.QLCData = (function () {
  const I = 'assets/icons/';
  /* Icon paths live in JS strings, which a static bundler cannot discover. Every icon is
     therefore declared as an <meta name="ext-resource-dependency"> in index.html; at runtime
     in a bundled page window.__resources[name] holds a blob URL for the inlined bytes. */
  const icon = (name) => (window.__resources && window.__resources[name]) || (I + name + '.svg');
  const fixtures = [
    { id: 'g-front', name: 'Front Truss', icon: icon('group'), children: [
      { id: 'f1', name: 'Robe Pointe 1', icon: icon('movinghead'), address: '1.001', channels: 24, mode: 'Mode 1 (24ch)' },
      { id: 'f2', name: 'Robe Pointe 2', icon: icon('movinghead'), address: '1.025', channels: 24, mode: 'Mode 1 (24ch)' },
      { id: 'f3', name: 'Robe Pointe 3', icon: icon('movinghead'), address: '1.049', channels: 24, mode: 'Mode 1 (24ch)' }
    ] },
    { id: 'g-back', name: 'Back Truss', icon: icon('group'), children: [
      { id: 'f4', name: 'Mac Aura 1', icon: icon('movinghead'), address: '1.073', channels: 15, mode: 'Basic (15ch)' },
      { id: 'f5', name: 'Mac Aura 2', icon: icon('movinghead'), address: '1.088', channels: 15, mode: 'Basic (15ch)' }
    ] },
    { id: 'g-cyc', name: 'Cyc Wash', icon: icon('group'), children: [
      { id: 'f6', name: 'LED Bar 1', icon: icon('fixture'), address: '2.001', channels: 8, mode: 'RGBW (8ch)' },
      { id: 'f7', name: 'LED Bar 2', icon: icon('fixture'), address: '2.009', channels: 8, mode: 'RGBW (8ch)' },
      { id: 'f8', name: 'LED Bar 3', icon: icon('fixture'), address: '2.017', channels: 8, mode: 'RGBW (8ch)' }
    ] }
  ];
  const functions = [
    { id: 'fn-scenes', name: 'Scenes', icon: icon('folder'), children: [
      { id: 'sc1', name: 'Warm Front', icon: icon('scene'), type: 'Scene' },
      { id: 'sc2', name: 'Cold Back', icon: icon('scene'), type: 'Scene' },
      { id: 'sc3', name: 'Blackout', icon: icon('scene'), type: 'Scene' }
    ] },
    { id: 'fn-chasers', name: 'Chasers', icon: icon('folder'), children: [
      { id: 'ch1', name: 'Chase 1', icon: icon('chaser'), type: 'Chaser' },
      { id: 'ch2', name: 'Strobe Hits', icon: icon('chaser'), type: 'Chaser' }
    ] },
    { id: 'fn-fx', name: 'Effects', icon: icon('folder'), children: [
      { id: 'rm1', name: 'Rainbow', icon: icon('rgbmatrix'), type: 'RGB Matrix' },
      { id: 'ef1', name: 'Circle EFX', icon: icon('efx'), type: 'EFX' }
    ] }
  ];
  const channels = [
    ['Dimmer', 'dimmer', 255], ['Red', 'red', 128], ['Green', 'green', 0], ['Blue', 'blue', 64],
    ['White', 'white', 0], ['Pan', 'pan', 200], ['Tilt', 'tilt', 90], ['Gobo', 'gobo', 0],
    ['Colour', 'colorwheel', 32], ['Shutter', 'shutter', 255], ['Prism', 'prism', 0], ['Zoom', 'beam', 110],
    ['Focus', 'beam', 40], ['Speed', 'speed', 0], ['Strobe', 'strobe', 0], ['Control', 'other', 0]
  ].map((c, i) => ({
    address: i + 1, value: c[2], channelName: c[0], channelIcon: I + c[1] + '.svg',
    display: i < 8 ? 'odd' : 'even'
  }));
  const vcWidgets = [
    { id: 'w1', kind: 'slider', label: 'Master', value: 255 },
    { id: 'w2', kind: 'slider', label: 'Front', value: 190 },
    { id: 'w3', kind: 'slider', label: 'Back', value: 120 },
    { id: 'w4', kind: 'slider', label: 'Cyc', value: 210 },
    { id: 'w5', kind: 'button', label: 'Warm Front', on: true },
    { id: 'w6', kind: 'button', label: 'Cold Back', on: false },
    { id: 'w7', kind: 'button', label: 'Chase 1', on: false },
    { id: 'w8', kind: 'button', label: 'Blackout', on: false }
  ];
  const universes = [
    { id: 1, name: 'Universe 1', input: 'None', output: 'ArtNet 2.0.0.1', feedback: 'None', passthrough: false },
    { id: 2, name: 'Universe 2', input: 'None', output: 'E1.31 239.255.0.2', feedback: 'None', passthrough: false },
    { id: 3, name: 'Universe 3', input: 'MIDI Controller', output: 'DMX USB Pro', feedback: 'MIDI Controller', passthrough: false },
    { id: 4, name: 'Universe 4', input: 'None', output: 'None', feedback: 'None', passthrough: true }
  ];
  const shortcutGroups = [
    { title: 'Contexts', binds: [['Ctrl 1', 'Fixtures & Functions'], ['Ctrl 2', 'Virtual Console'], ['Ctrl 3', 'Simple Desk'], ['Ctrl 4', 'Show Manager'], ['Ctrl 5', 'Input / Output']] },
    { title: 'Workspace', binds: [['Ctrl N', 'New workspace'], ['Ctrl O', 'Open'], ['Ctrl S', 'Save'], ['Ctrl Z', 'Undo'], ['Ctrl Y', 'Redo']] },
    { title: 'Output', binds: [['Ctrl B', 'Blackout'], ['Ctrl .', 'Stop all functions'], ['Space', 'Tap tempo'], ['Ctrl L', 'Lock editing']] }
  ];
  return { I, icon, fixtures, functions, channels, vcWidgets, universes, shortcutGroups };
})();
