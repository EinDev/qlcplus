/**
 * Virtual Console — widget background image, stacking order and the page's size (design mode).
 *
 *  - VCBackgroundImageRow: VCWidgetProperties.qml's "Background image" row. The image is a file on
 *    the QLC+ host: picked with window.ServerFileBrowser (core.fs.list), stored as
 *    style.backgroundImage (the server refuses UNC / network paths), rendered from
 *    vc.widget.getBackgroundImage's data URL (the browser cannot open a host path).
 *  - vcBackgroundImageCss(w): the CSS `background` layer VCWidgetBody composes over the widget's
 *    colour, like VCWidgetItem.qml's Image (PreserveAspectFit, centred, above the fill, below the
 *    content). Fetched once per path and cached for the page's lifetime.
 *  - VCZIndexRow: the "Z-Index" spin box plus raise / lower / to front / to back. Stacking is among
 *    siblings (widgets with the same parent), which is what VirtualConsole.jsx sorts by.
 *  - VCPagePropertiesPanel: VCPageProperties.qml's Width / Height for the shown page
 *    (vc.page.setSize), shown in the properties panel while no widget is selected.
 *
 * Globals: VCBackgroundImageRow, VCZIndexRow, VCPagePropertiesPanel, vcBackgroundImageCss, useVCBackgroundImage.
 */
(function () {
  'use strict';
  const { RobotoText, IconButton, GenericButton, CustomSpinBox } = window.PatchDesignSystem_5432c9;

  /* Font Awesome 7 Solid "image" - not in the bundle's FA map, FaIcon renders the raw glyph */
  const GLYPH_IMAGE = String.fromCharCode(0xf03e);

  const IMAGE_FILTERS = () => [
    window.ServerFileBrowser.filter('Image files', ['*.png', '*.bmp', '*.jpg', '*.jpeg', '*.gif', '*.svg', '*.webp']),
    window.ServerFileBrowser.filter('All files', [])
  ];

  /* path -> { status: 'loading'|'ok'|'error', dataUrl, reason, waiters: Set<fn> } */
  const imageCache = new Map();

  function requestImage(qlc, widgetId, path) {
    let entry = imageCache.get(path);
    if (entry && entry.status !== 'error') return entry;
    entry = { status: 'loading', dataUrl: null, reason: '', waiters: (entry && entry.waiters) || new Set() };
    imageCache.set(path, entry);
    qlc.call('vc.widget.getBackgroundImage', { widgetId: String(widgetId) }).then(r => {
      /* the widget may have moved on to another image meanwhile: file the answer under its own path */
      const answered = (r && r.path) || path;
      const target = imageCache.get(answered) || entry;
      target.status = r && r.dataUrl ? 'ok' : 'error';
      target.dataUrl = (r && r.dataUrl) || null;
      target.reason = (r && r.reason) || '';
      imageCache.set(answered, target);
      if (answered !== path) { entry.status = 'error'; entry.reason = 'changed'; }
      [target, entry].forEach(x => x.waiters.forEach(fn => fn()));
    }).catch(e => {
      entry.status = 'error';
      entry.reason = (e && e.message) || 'failed';
      entry.waiters.forEach(fn => fn());
    });
    return entry;
  }

  /** {dataUrl, status, reason} of the widget's background image, re-rendering when it arrives. */
  function useVCBackgroundImage(w) {
    const vc = useVC();
    const path = w && w.style && w.style.backgroundImage;
    const [, bump] = React.useReducer(x => x + 1, 0);
    const qlc = vc && vc.qlc;
    React.useEffect(() => {
      if (!path || !qlc || !qlc.online) return undefined;
      const entry = requestImage(qlc, w.id, path);
      entry.waiters.add(bump);
      return () => { entry.waiters.delete(bump); };
    }, [path, qlc && qlc.online, w && w.id]);
    if (!path) return { status: 'none', dataUrl: null, reason: '' };
    const entry = imageCache.get(path);
    return entry ? { status: entry.status, dataUrl: entry.dataUrl, reason: entry.reason } : { status: 'loading', dataUrl: null, reason: '' };
  }

  /** `background` value: the image (contain, centred) over the widget's colour. */
  function vcBackgroundImageCss(dataUrl, color, fallback) {
    return 'url("' + dataUrl + '") center / contain no-repeat, ' + (color || fallback || 'var(--bg-strong)');
  }

  const baseName = (p) => String(p || '').split(/[\\/]/).pop();

  function VCBackgroundImageRow({ widgets, setStyle, PropRow }) {
    const vc = useVC();
    const [browser, setBrowser] = React.useState(false);
    const w = widgets[0];
    const path = w && w.style ? w.style.backgroundImage : null;
    const mixed = widgets.some(x => ((x.style && x.style.backgroundImage) || '') !== (path || ''));
    const img = useVCBackgroundImage(widgets.length === 1 ? w : null);
    const note = widgets.length === 1 && path
      ? (img.status === 'ok' ? '' : img.status === 'loading' ? 'loading…' : 'not shown: ' + (img.reason || 'unavailable'))
      : '';
    return (
      <PropRow label="Background image">
        <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column' }} title={path || ''} data-vc-bgimage-path={path || ''} data-vc-bgimage-status={img.status}>
          <RobotoText label={mixed ? '(different images)' : path ? baseName(path) : 'None'} fontSize="var(--text-size-menubar)" height="auto"
            labelColor={path ? 'var(--fg-main)' : 'var(--fg-medium)'} style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }} />
          {note ? <RobotoText label={note} fontSize="var(--text-size-menubar)" height="auto" labelColor="var(--fg-medium)" /> : null}
        </div>
        <IconButton faSource={GLYPH_IMAGE} faColor="lightyellow" size={24} tooltip="Set a custom background (an image file on the QLC+ host)"
          disabled={!vc.qlc.online} onClick={() => setBrowser(true)} data-vc-bgimage-pick="" />
        <IconButton faSource="fa_xmark" size={24} tooltip="Remove the background image" disabled={!path && !mixed}
          onClick={() => setStyle({ backgroundImage: null })} data-vc-bgimage-clear="" />
        {browser && window.ServerFileBrowser ? (
          <window.ServerFileBrowser open={browser} qlc={vc.qlc} title="Select an image" filters={IMAGE_FILTERS()}
            onClose={() => setBrowser(false)} onPick={(p) => { setBrowser(false); setStyle({ backgroundImage: p }); }} />
        ) : null}
      </PropRow>
    );
  }

  /** Siblings of $w (same parent, same page) as the screen's widget list knows them. */
  function siblingsOf(vc, w) {
    return (vc.widgets || []).filter(x => x.id !== w.id && String(x.parentId || '') === String(w.parentId || ''));
  }

  function VCZIndexRow({ w, PropRow }) {
    const vc = useVC();
    const z = Number(w.zIndex) || 0;
    const sib = siblingsOf(vc, w).map(x => Number(x.zIndex) || 0);
    const maxZ = sib.length ? Math.max.apply(null, sib) : 0;
    const minZ = sib.length ? Math.min.apply(null, sib) : 0;
    const set = (v) => { v = Math.max(0, Math.min(1000, Math.round(v))); if (v !== z) vc.editApi.updateWidget(w.id, { zIndex: v }); };
    const SpinField = window.VCSpinField;
    const onTop = !sib.length || z > maxZ, atBottom = !sib.length || (z < minZ) || (z === 0 && minZ > 0);
    return (
      <PropRow label="Z-Index">
        <span data-vc-zindex={z} style={{ display: 'inline-flex' }}><SpinField value={z} from={0} to={1000} width={64} height={24} onCommit={set} /></span>
        <IconButton faSource="fa_chevron_up" size={24} tooltip="Raise (Z-Index + 1)" disabled={z >= 1000} onClick={() => set(z + 1)} data-vc-z="raise" />
        <IconButton faSource="fa_chevron_down" size={24} tooltip="Lower (Z-Index - 1)" disabled={z <= 0} onClick={() => set(z - 1)} data-vc-z="lower" />
        <GenericButton label="Front" width={46} height={24} fontSize="var(--text-size-menubar)" disabled={onTop} tooltip="Bring above every sibling"
          onClick={() => set(maxZ + 1)} data-vc-z="front" />
        <GenericButton label="Back" width={46} height={24} fontSize="var(--text-size-menubar)" disabled={atBottom} tooltip="Send below every sibling"
          onClick={() => set(Math.max(0, minZ - 1))} data-vc-z="back" />
      </PropRow>
    );
  }

  /** VCPageProperties.qml: the shown page's Width / Height (vc.page.setSize). */
  function VCPagePropertiesPanel() {
    const vc = useVC();
    const page = (vc.pages || []).find(p => p.index === vc.page);
    const PropRow = window.VCPropRow, SpinField = window.VCSpinField;
    if (!page) return <RobotoText label="Select a widget first" fontSize="var(--text-size-small)" labelColor="var(--fg-medium)" height="var(--icon-size-default)" textHAlign="center" style={{ width: '100%' }} />;
    const has = page.width != null && page.height != null;
    const setSize = (width, height) => vcStructural(vc.qlc, 'vc.page.setSize', { index: page.index, width, height })
      .catch(e => vc.notice('vc.page.setSize: ' + ((e && e.message) || 'failed')));
    return (
      <div data-vc-page-props="" style={{ display: 'flex', flexDirection: 'column' }}>
        <RobotoText label={'Page "' + (page.name || 'Page ' + (page.index + 1)) + '"'} fontSize="var(--text-size-small)" labelColor="var(--fg-light)" height="var(--list-item-height)" leftMargin={6} />
        {has ? (
          <>
            <PropRow label="Width"><span data-vc-page-width={page.width} style={{ display: 'inline-flex' }}><SpinField value={page.width} from={1} to={100000} width={100} height={24} suffix="px" onCommit={(v) => setSize(v, page.height)} /></span></PropRow>
            <PropRow label="Height"><span data-vc-page-height={page.height} style={{ display: 'inline-flex' }}><SpinField value={page.height} from={1} to={100000} width={100} height={24} suffix="px" onCommit={(v) => setSize(page.width, v)} /></span></PropRow>
          </>
        ) : <RobotoText label="This server does not report page sizes" fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" height="auto" style={{ padding: 6 }} />}
        <RobotoText label="Select a widget to edit its properties" fontSize="var(--text-size-menubar)" labelColor="var(--fg-medium)" height="auto" style={{ padding: 6 }} wrapText />
      </div>
    );
  }

  Object.assign(window, { VCBackgroundImageRow, VCZIndexRow, VCPagePropertiesPanel, vcBackgroundImageCss, useVCBackgroundImage });
})();
