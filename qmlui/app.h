/*
  Q Light Controller Plus
  app.h

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef APP_H
#define APP_H

#include <QQmlEngine>
#include <QQuickView>
#include <QQuickItem>
#include <QObject>
#include "doc.h"
#include "apiprojecthost.h"
#include "apivchost.h"
// Full definition, not a forward declaration: moc registers the VCWidget* parameter of the
// slotVcWidgetRegistered() slot, which needs a complete type (same trap as apidispatcher.h).
#include "virtualconsole/vcwidget.h"

class MainView2D;
class ShowManager;
class SimpleDesk;
class UiManager;
class ShortcutManager;
class FixtureBrowser;
class FixtureManager;
class PaletteManager;
class ContextManager;
class VirtualConsole;
class FunctionManager;
class QXmlStreamReader;
class FixtureGroupEditor;
class FixtureRemapManager;
class InputOutputManager;
class ImportManager;
class NetworkManager;
class ApiServer;
class WebServer;
class VideoProvider;
class FixtureEditor;
class StageWizard;
class Tardis;
class QMouseEvent;

#define KXMLQLCWorkspace QStringLiteral("Workspace")

class App final : public QQuickView, public ApiProjectHost, public ApiVcHost
{
    Q_OBJECT
    Q_DISABLE_COPY(App)
    Q_PROPERTY(bool docLoaded READ docLoaded NOTIFY docLoadedChanged)
    Q_PROPERTY(bool docModified READ docModified NOTIFY docModifiedChanged)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(QString workingPath READ workingPath WRITE setWorkingPath NOTIFY workingPathChanged)
    Q_PROPERTY(int accessMask READ accessMask WRITE setAccessMask NOTIFY accessMaskChanged)
    Q_PROPERTY(int runningFunctionsCount READ runningFunctionsCount NOTIFY runningFunctionsCountChanged)
    Q_PROPERTY(QString mediaImportStatus READ mediaImportStatus NOTIFY mediaImportStatusChanged)

    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(bool is3DSupported READ is3DSupported CONSTANT)
    Q_PROPERTY(qreal screenDiagonal READ screenDiagonal NOTIFY screenDiagonalChanged)
    Q_PROPERTY(bool smallScreen READ smallScreen NOTIFY screenDiagonalChanged)

public:
    App();
    ~App();

    QString appName() const;
    QString appVersion() const;

    enum MouseEvents
    {
        Pressed = 0,
        Released,
        Clicked,
        DoubleClicked,
        DragStarted,
        DragFinished,
        Checked
    };
    Q_ENUM(MouseEvents)

    enum FileDialogOpModes
    {
        OpenMode = 0,
        SaveMode,
        SaveAsMode,
        ImportMode
    };
    Q_ENUM(FileDialogOpModes)

    enum DragItemType
    {
        NoDragItem,
        GenericDragItem,
        FolderDragItem,
        FunctionDragItem,
        UniverseDragItem,
        FixtureGroupDragItem,
        FixtureDragItem,
        ChannelDragItem,
        PaletteDragItem,
        HeadDragItem,
        ShowDragItem,
        TrackDragItem,
        WidgetDragItem
    };
    Q_ENUM(DragItemType)

    enum ChannelType
    {
        DimmerType      = (1 << QLCChannel::Intensity),
        ColorMacroType  = (1 << QLCChannel::Colour), // Color wheels, color macros
        GoboType        = (1 << QLCChannel::Gobo),
        SpeedType       = (1 << QLCChannel::Speed),
        PanType         = (1 << QLCChannel::Pan),
        TiltType        = (1 << QLCChannel::Tilt),
        ShutterType     = (1 << QLCChannel::Shutter),
        PrismType       = (1 << QLCChannel::Prism),
        BeamType        = (1 << QLCChannel::Beam),
        EffectType      = (1 << QLCChannel::Effect),
        MaintenanceType = (1 << QLCChannel::Maintenance),
        ColorType       = (1 << (QLCChannel::Maintenance + 1)) // RGB/CMY/WAUV
    };
    Q_ENUM(ChannelType)

    enum ChannelColors
    {
        Red     = (1 << 0),
        Green   = (1 << 1),
        Blue    = (1 << 2),
        Cyan    = (1 << 3),
        Magenta = (1 << 4),
        Yellow  = (1 << 5),
        White   = (1 << 6),
        Amber   = (1 << 7),
        UV      = (1 << 8),
        Lime    = (1 << 9),
        Indigo  = (1 << 10),
    };
    Q_ENUM(ChannelColors)

    enum AccessControl
    {
        AC_FixtureEditing  = (1 << 0),
        AC_FunctionEditing = (1 << 1),
        AC_VCControl       = (1 << 2),
        AC_VCEditing       = (1 << 3),
        AC_SimpleDesk      = (1 << 4),
        AC_ShowManager     = (1 << 5),
        AC_InputOutput     = (1 << 6)
    };
    Q_ENUM(AccessControl)

    /** Method to turn the key and start the engine */
    void startup();

    /** Toggle between windowed and fullscreeen mode */
    Q_INVOKABLE void toggleFullscreen();

    Q_INVOKABLE void setLanguage(QString locale);

    Q_INVOKABLE QString goboSystemPath() const;

    void enableKioskMode();
    void createKioskCloseButton(const QRect& rect);

    /** Return the number of pixels in 1mm */
    qreal pixelDensity() const;

    /** Return the physical diagonal size of the current screen, in inches */
    qreal screenDiagonal() const;

    /** Return true if the current screen is a small one (7 inches or below),
     *  where the UI needs to compact itself to save space */
    bool smallScreen() const;

    /** Get/Set the UI access mask */
    int defaultMask() const;
    void setAccessMask(int mask);
    int accessMask() const;

    /** Get/Set the 3D support status */
    bool is3DSupported() const;
    void set3dSupported(bool enable);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

    Q_INVOKABLE void aboutQt();

    Q_INVOKABLE void exit(bool force = false);

protected:
    void keyPressEvent(QKeyEvent * e) override;
    void keyReleaseEvent(QKeyEvent * e) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool event(QEvent *event) override;

private:
    /** Returns true if activeFocusItem(), or one of its QQuickItem
     *  ancestors, is a text-editing item (QQuickTextInput/QQuickTextEdit) */
    bool isTextInputFocused() const;

    /** Returns true if activeFocusItem(), or one of its QQuickItem ancestors,
     *  is parented under the window's QQuickOverlay - i.e. a Popup (such as
     *  PopupInvertGroupSelection or PopupRenameItems) currently owns focus.
     *  Used to keep fixture.deselectAll (Escape) from also clearing the
     *  fixture selection while Escape is instead meant to cancel/close an
     *  open popup - e.g. cancelling PopupInvertGroupSelection is documented
     *  to leave the selection untouched. */
    bool isPopupFocused() const;

    /** Register every built-in ShortcutManager action and bind it to its
     *  default key sequence. Called once from startup() after m_shortcutManager
     *  and every manager it binds to (m_contextManager, m_ioManager,
     *  m_virtualConsole, m_tardis) have been constructed */
    void registerBuiltinShortcuts();

protected slots:
    void slotSceneGraphInitialized();
    void slotScreenChanged(QScreen *screen);
    void slotClosing();
    void slotClientAccessRequest(QString sessionId, QString name,
                                 QString peerAddress, quint16 peerPort);
    void slotClientAccessRequestCancelled(QString sessionId);

    /** Serve the current workspace to a client that requested it */
    void slotClientProjectRequest(QString sessionId);

    void slotAccessMaskChanged(int mask);
    void slotDocAutosave();

signals:
    void accessMaskChanged(int mask);
    void screenDiagonalChanged();

private:
    /** Flag to quit the application forcefully */
    bool m_forceQuit = false;

    /** The number of pixels in one millimeter */
    qreal m_pixelDensity;

    /** The physical diagonal size of the current screen, in inches */
    qreal m_screenDiagonal = 0;

    /** Bitmask to enable/disable UI functionalities */
    int m_accessMask;

    /** 3D support flag */
    bool m_is3dSupported;

    QTranslator *m_translator;
    QTranslator *m_translator_base;

    FixtureBrowser *m_fixtureBrowser;
    FixtureManager *m_fixtureManager;
    FixtureGroupEditor *m_fixtureGroupEditor;
    PaletteManager *m_paletteManager;
    ContextManager *m_contextManager;
    FunctionManager *m_functionManager;
    InputOutputManager *m_ioManager;
    VirtualConsole *m_virtualConsole;
    ShowManager *m_showManager;
    SimpleDesk *m_simpleDesk;
    ShortcutManager *m_shortcutManager;
    VideoProvider *m_videoProvider;
    NetworkManager *m_networkManager;
    ApiServer *m_apiServer;
    WebServer *m_webServer;
    UiManager *m_uiManager;
    StageWizard *m_stageWizard;
    Tardis *m_tardis;

    /*********************************************************************
     * Doc
     *********************************************************************/
public:
    /** Return a reference to the Doc instance */
    Doc *doc();

    /** Return the QML Virtual Console instance */
    VirtualConsole *virtualConsole() const;

    /** Return the QML Simple Desk instance */
    SimpleDesk *simpleDesk() const;

    /** Return the network manager instance */
    NetworkManager *networkManager() const;

    /** Return the WebSocket control API server instance (docs/api-spec/) */
    ApiServer *apiServer() const;

    /** Return the HTTP server that serves the browser-based web UI
     *  (docs/webui.md); started by main.cpp behind --webui */
    WebServer *webServer() const;

    /** Return if the current Doc instance has been loaded */
    bool docLoaded();

    /** Set the status of the doc loading state */
    void setDocLoaded(bool loaded);

    /** Return the Doc instance modified flag */
    bool docModified() const;

    /** Reset the currently loaded Doc instance */
    void clearDocument();

    /** Return the number of currently running Functions */
    int runningFunctionsCount() const;

    /** Stop all the currently running Functions */
    Q_INVOKABLE void stopAllFunctions();

    /*********************************************************************
     * Media assets (Doc::assets())
     *********************************************************************/

    /** One-line progress text of the background media copy in flight
     *  ("Copying big.mp4 into the project... 42%"), empty when idle */
    QString mediaImportStatus() const;

    /** Number of Audio/Video sources living outside the project's store */
    Q_INVOKABLE int externalMediaCount() const;

    /** Copy every external Audio/Video source into the project's media
     *  store and repoint the functions. Returns
     *  { copied, queued, failed, error } - queued files finish in the
     *  background (see mediaImportStatus) */
    Q_INVOKABLE QVariantMap collectMedia();

    /** Files in the project's media store no function uses anymore */
    Q_INVOKABLE QStringList unusedMedia() const;

    /** Delete the given store files (only ones unusedMedia() would list);
     *  returns false if any entry was refused or could not be deleted */
    Q_INVOKABLE bool removeUnusedMedia(const QStringList &files);

    /** Number of managed Audio/Video copies whose origin file changed on disk */
    Q_INVOKABLE int changedMediaCount();

    /** Re-import every managed copy whose origin changed on disk. Returns
     *  { reloaded, queued, unchanged, missing, busy, failed, error } -
     *  queued files finish in the background (see mediaImportStatus).
     *  Never runs on its own: only from the actions menu / banner */
    Q_INVOKABLE QVariantMap reloadChangedMedia();

protected slots:
    void slotMediaImportStarted(QString source, qint64 bytes);
    void slotMediaImportProgress(QString source, qint64 done, qint64 total);
    void slotMediaImportFinished(QString source, QString target, QString error);
    void slotMediaPendingImportsChanged();

    /** A reload re-pointed a function: a Video's new duration is only known
     *  after a probe, so run one here (no editor may be open) and log the
     *  old/new duration the way the engine does for Audio */
    void slotMediaOriginReloaded(quint32 functionId, QString oldPath, QString newPath, quint32 oldDuration);

private:
    void initDoc();
    void setMediaImportStatus(const QString &status);

signals:
    void docLoadedChanged();
    void docModifiedChanged();
    void runningFunctionsCountChanged();
    void mediaImportStatusChanged();

private:
    Doc *m_doc;
    bool m_docLoaded;
    QString m_mediaImportStatus;

    /*********************************************************************
     * Printer
     *********************************************************************/
public:
    /** Send $item content to a printer */
    Q_INVOKABLE void printItem(QQuickItem *item);

protected slots:
    void slotItemReadyForPrinting();

private:
    QQuickItem *m_printItem;
    QSharedPointer<QQuickItemGrabResult> m_printerImage;

    /*********************************************************************
     * Load & Save
     *********************************************************************/
public:
    /** Get/Set the name of the current workspace file */
    Q_INVOKABLE QString fileName() const override;
    void setFileName(const QString& fileName) override;

    /**
     * Get the autosave version of the name
     * of the current workspace file
     */
    QString autoSaveFileName() const;

    /** Return the list of the recently opened files */
    QStringList recentFiles() const override;

    /** Open the file from last session */
    void loadLastWorkspace();

    /** Get/Set the path currently used by QLC+ to access projects and resources */
    QString workingPath() const override;
    void setWorkingPath(QString workingPath) override;

    /** ApiProjectHost undo/redo hooks, delegating to m_tardis (which is
     *  constructed AFTER m_apiServer in App::App() - hence the null checks
     *  in app.cpp). historyChanged() below is the relay of
     *  Tardis::historyChanged() that ApiCoreDomain connects to by name. */
    bool canUndo() const override;
    bool canRedo() const override;
    QString undoText() const override;
    QString redoText() const override;
    bool undo() override;
    bool redo() override;

    /** Reset everything and start a new workspace */
    Q_INVOKABLE bool newWorkspace() override;

    /** Load the workspace with the given $fileName */
    Q_INVOKABLE bool loadWorkspace(const QString& fileName) override;

    /** Save the current workspace with the given $fileName */
    Q_INVOKABLE bool saveWorkspace(const QString& fileName) override;

    /**
     * Load workspace contents from a XML file with the given name.
     *
     * @param fileName The name of the file to load from.
     * @return QFile::NoError if successful.
     */
    QFile::FileError loadXML(const QString& fileName);

    /**
     * Load workspace contents from the given XML document.
     *
     * @param doc The XML document to load from.
     */
    bool loadXML(QXmlStreamReader &doc, bool goToConsole = false, bool fromMemory = false);

    /**
     * Save workspace contents to a file with the given name. Changes the
     * current workspace file name to the given fileName.
     *
     * @param fileName The name of the file to save to.
     * @return QFile::NoError if successful.
     */
    QFile::FileError saveXML(const QString& fileName, bool autosave = false);

private:
    /**
     * Update the list of the recently open files.
     * If filename is specified, it will be removed from the list
     * if present and added to the beginning of the list
     */
    void updateRecentFilesList(QString filename = QString());

signals:
    void recentFilesChanged();
    void workingPathChanged(QString workingPath);
    /** Relay of Tardis::historyChanged() - see canUndo() above */
    void historyChanged();

public slots:
    void slotLoadDocFromMemory(QByteArray &xmlData) override;

    /** Clear the whole workspace on request of the connected server, which
     *  is about to replace its own project */
    void slotClearDocFromNetwork();

    void slotSaveAutostart(QString fileName);

private:
    QString m_fileName;
    QStringList m_recentFiles;
    QString m_workingPath;

    /*********************************************************************
     * Import project
     *********************************************************************/
public:
    /** Start the import process for the workspace with the given $fileName */
    Q_INVOKABLE bool loadImportWorkspace(const QString& fileName);

    /** Cancel an ongoing import process started with loadImportWorkspace */
    Q_INVOKABLE void cancelImport();

    /** Perform the actual import of the selected items */
    Q_INVOKABLE void importFromWorkspace();

private:
    ImportManager *m_importManager;
    FixtureRemapManager *m_fixtureRemapManager;

    /*********************************************************************
     * Fixture editor
     *********************************************************************/
public:
    /** Request to create a new fixture definition. If the Fixture Editor
     *  doesn't exist, it will be created */
    Q_INVOKABLE void createFixture();
    Q_INVOKABLE void loadFixture(QString fileName);
    Q_INVOKABLE void editFixture(QString manufacturer, QString model);
    Q_INVOKABLE void closeFixtureEditor();

private:
    FixtureEditor *m_fixtureEditor;

    /*********************************************************************
     * ApiVcHost implementation (controlapi/src/apivchost.h)
     *
     * Drives the real, live m_virtualConsole object graph on behalf of
     * ApiVcDomain (controlapi/src/domains/apivcdomain.cpp), obtained there via
     * dynamic_cast<ApiVcHost*>(m_server->parent()) - App is ApiServer's Qt
     * parent (see initDoc()/m_apiServer construction in app.cpp), exactly
     * like it already is for ApiProjectHost. Implemented in
     * app_apivchost.cpp, not app.cpp, to keep this substantial slice of
     * VCWidget/VCPage/VCFrame-facing code out of app.cpp's own already large
     * body.
     *********************************************************************/
public:
    int vcPageCount() const override;
    QJsonObject vcPageSnapshot(int index) const override;
    int vcSelectedPage() const override;
    void vcSetSelectedPage(int index) override;
    void vcAddPage(int index) override;
    bool vcDeletePage(int index, QJsonArray &deletedWidgetIds) override;
    void vcRenamePage(int index, const QString &name) override;
    bool vcSetPagePin(int index, const QString &currentPin, const QString &newPin) override;
    bool vcValidatePagePin(int index, const QString &pin) const override;

    bool vcWidgetExists(quint32 id) const override;
    QString vcWidgetType(quint32 id) const override;
    int vcWidgetPage(quint32 id) const override;
    quint32 vcWidgetParentId(quint32 id) const override;
    bool vcIsContainerWidget(quint32 id) const override;
    QList<quint32> vcWidgetIds() const override;
    QJsonObject vcWidgetSnapshot(quint32 id) const override;

    quint32 vcCreateWidget(const QString &widgetType, int page, quint32 parentId,
                            const QJsonObject &geometry, const QJsonObject &style,
                            const QJsonObject &typeConfig, QString *error) override;
    void vcDeleteWidgets(const QList<quint32> &ids, QJsonArray &deletedIds) override;
    bool vcUpdateWidgetCommon(quint32 id, const QJsonObject &fields, QString *error) override;
    void vcMoveTopLevelWidgetToPage(quint32 id, int newPage) override;
    bool vcSetWidgetConfig(quint32 id, const QJsonObject &configPatch, QString *error) override;
    bool vcReparentWidget(quint32 id, quint32 newParentId, QPointF newTopLeft, QString *error) override;
    void vcRepositionWidgets(const QList<QPair<quint32, QJsonObject> > &updates) override;

    // Live interaction (vc.button.press, vc.slider.setValue, vc.cueList.*, vc.xyPad.setPosition,
    // vc.speedDial.*, vc.frame.gotoPage/get) - see apivchost.h for each method's contract.
    void vcSetLiveListener(ApiVcLiveListener *listener) override;
    bool vcButtonPress(quint32 id, bool pressed, QString *error) override;
    bool vcSliderSetValue(quint32 id, int value, QString *error) override;
    bool vcCueListAction(quint32 id, CueListAction action, QString *error) override;
    bool vcCueListSetPlaybackIndex(quint32 id, int index, QString *error) override;
    QJsonObject vcCueListSnapshot(quint32 id) const override;
    bool vcXyPadSetPosition(quint32 id, double x, double y, QString *error) override;
    bool vcSpeedDialSetValue(quint32 id, int ms, QString *error) override;
    bool vcSpeedDialTap(quint32 id, QString *error) override;
    bool vcFrameGotoPage(quint32 id, int page, QString *error) override;
    QJsonObject vcFrameSnapshot(quint32 id) const override;

    // Cue list side fader, speed dial extras and widget presets (vc.cueList.setSideFaderLevel,
    // vc.speedDial.setFactor/apply/resetTap, vc.widget.preset.*, vc.speedDial.preset.update) -
    // implemented in app_apivchost_cue.cpp; the per-widget-type preset shaping lives in
    // app_apivcconfig_{cue,live}.cpp (ApiVcConfig::*Preset* functions).
    bool vcCueListSetSideFaderLevel(quint32 id, int level, QString *error) override;
    bool vcSpeedDialSetFactor(quint32 id, const QString &factor, QString *error) override;
    bool vcSpeedDialApply(quint32 id, QString *error) override;
    bool vcSpeedDialResetTap(quint32 id, QString *error) override;
    bool vcWidgetSupportsPresets(quint32 id) const override;
    QJsonArray vcWidgetPresets(quint32 id) const override;
    int vcWidgetPresetAdd(quint32 id, const QJsonObject &preset, QString *error) override;
    bool vcWidgetPresetRemove(quint32 id, int presetId, QString *error) override;
    bool vcWidgetPresetApply(quint32 id, int presetId, QString *error) override;
    bool vcSpeedDialPresetUpdate(quint32 id, int presetId, const QJsonObject &patch, QString *error) override;

    // Layout / configuration slice (vc.frame.setPin/validatePin/cloneFirstPage, vc.slider.
    // setLevelChannels/flash, vc.widget.createFromFunctions/createMatrix/usage) - see apivchost.h.
    bool vcFrameSetPin(quint32 id, const QString &currentPin, const QString &newPin) override;
    bool vcFrameValidatePin(quint32 id, const QString &pin) const override;
    bool vcFrameCloneFirstPage(quint32 id, QJsonArray &createdIds, QString *error) override;
    bool vcSliderSetLevelChannels(quint32 id, const QList<QPair<quint32, quint32> > &channels, QString *error) override;
    bool vcSliderFlash(quint32 id, bool on, QString *error) override;
    QList<quint32> vcCreateWidgetsFromFunctions(int page, quint32 parentId, const QList<quint32> &functionIds,
                                                QPointF position, const QString &widgetHint, QString *error) override;
    QList<quint32> vcCreateWidgetMatrix(int page, quint32 parentId, const QString &matrixType, QPointF position,
                                        int columns, int rows, int widgetWidth, int widgetHeight,
                                        bool soloFrame, QString *error) override;
    QList<quint32> vcWidgetsUsingFunction(quint32 functionId) const override;

protected slots:
    /** VirtualConsole::widgetRegistered() - hooks the per-type live-state signals of every widget
     *  that enters the VC (created, loaded, pasted) to relays that forward the new state to
     *  m_vcLiveListener (if any). The relays are context-bound functors holding a QPointer to the
     *  widget, so they always run on the GUI thread and survive the widget being deleted while a
     *  queued delivery is still pending - see the implementation for why sender() is not used. */
    void slotVcWidgetRegistered(VCWidget *widget);

private:
    /** Resolve a VC widget id to its live VCWidget instance via m_virtualConsole->widget(id), or
     *  nullptr if not found - used by every ApiVcHost widget method above. */
    VCWidget *vcFindWidget(quint32 id) const;

    /** The control API's live-event receiver (ApiVcDomain), or nullptr while none is attached. Not
     *  owned. */
    ApiVcLiveListener *m_vcLiveListener = nullptr;
};
#endif // APP_H
