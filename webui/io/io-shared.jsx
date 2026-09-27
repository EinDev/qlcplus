/**
 * io-shared.jsx — small helpers shared by the Input/Output screen's sub-components
 * (io/PatchProperties.jsx, io/InputProfileEditor.jsx, io/AudioDevices.jsx, io/GrandMasterPanel.jsx).
 * Exposed as window.IOShared; load before those files and before InputOutput.jsx.
 */
(function () {
  'use strict';
  const { RobotoText, CustomTextInput } = window.PatchDesignSystem_5432c9;

  /** The server has no such method (qlcplus-api.js remembers these as `unsupported`). */
  const isUnknownMethod = (e) => !!e && e.code === 'NOT_FOUND' && /Unknown method/.test(e.message || '');
  const errorText = (e) => (e && (e.message || e.code)) || 'request failed';
  const NOTE = 'var(--fg-medium)';

  /** "Label | control" row, the layout every properties panel in the Qt UI uses. */
  function Row({ label, width = 110, title, children, style, wrap }) {
    return (
      <div title={title} style={Object.assign({ display: 'flex', alignItems: wrap ? 'flex-start' : 'center', gap: 8, minHeight: 26 }, style)}>
        <RobotoText label={label} fontSize={14} height={26} style={{ width, minWidth: width, flex: 'none' }} />
        <div style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'center', gap: 6, flexWrap: wrap ? 'wrap' : 'nowrap' }}>{children}</div>
      </div>
    );
  }

  const fieldBox = { height: 24, display: 'flex', alignItems: 'center', background: 'var(--bg-control)', border: '1px solid var(--spin-border)', borderRadius: 'var(--radius-spin)', padding: '0 5px', boxSizing: 'border-box' };

  /** An always-editing text field in the spin-box frame; commits on Enter / blur through onCommit. */
  function TextField({ value, onCommit, onLive, placeholder, width = '100%', autoFocus, disabled, ...rest }) {
    return (
      <span style={Object.assign({}, fieldBox, { width, opacity: disabled ? .5 : 1 })}>
        <CustomTextInput text={value == null ? '' : String(value)} editing={!disabled} autoFocus={autoFocus} placeholder={placeholder} width="100%" height={22}
          onTextConfirmed={(t) => onCommit && onCommit(t)} onInput={(e) => onLive && onLive(e.target.value)} {...rest} />
      </span>
    );
  }

  /** One io.* mutation with the standard CONFLICT retry (the error carries the fresh revision). */
  function withConflictRetry(send) {
    return send().catch(e => {
      if (e && e.code === 'CONFLICT') return send(e.details || {});
      throw e;
    });
  }

  window.IOShared = { isUnknownMethod, errorText, NOTE, Row, TextField, fieldBox, withConflictRetry };
})();
