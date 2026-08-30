/*
  Q Light Controller Plus
  app.cpp

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

#include <QQuickItemGrabResult>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QCoreApplication>
#include <QtCore/qbuffer.h>
#include <QFontDatabase>
#include <QOpenGLContext>
#include <QPrintDialog>
#include <QApplication>
#include <QLibraryInfo>
#include <QTranslator>
#include <QQmlContext>
#include <QQuickItem>
#include <QSettings>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPrinter>
#include <QPainter>
#include <QScreen>
#include <QFileInfo>
#include <QDir>
#include <unistd.h>

#include "app.h"
#include "uimanager.h"
#include "simpledesk.h"
#include "showmanager.h"
#include "fixtureeditor.h"
#include "modelselector.h"
#include "folderbrowser.h"
#include "videoprovider.h"
#include "importmanager.h"
#include "contextmanager.h"
#include "shortcutmanager.h"
#include "virtualconsole.h"
#include "fixturebrowser.h"
#include "fixturemanager.h"
#include "treeflatmodel.h"
#include "fixtureremapmanager.h"
#include "palettemanager.h"
#include "functionmanager.h"
#include "fixturegroupeditor.h"
#include "inputoutputmanager.h"
#include "freezewatchdog.h"

#include "tardis.h"
#include "networkmanager.h"
#include "stagewizard.h"
#include "apiserver.h"

#include "qlcfixturedefcache.h"
#include "audioplugincache.h"
#include "rgbscriptscache.h"
#include "qlcconfig.h"
#include "qlcfile.h"

#define SETTINGS_GEOMETRY      QStringLiteral("workspace/windowrect")
#define SETTINGS_WORKINGPATH   QStringLiteral("workspace/workingpath")
#define SETTINGS_RECENTFILE    QStringLiteral("workspace/recent")
#define KXMLQLCWorkspaceWindow QStringLiteral("CurrentWindow")

#define MAX_RECENT_FILES    10

/** Resolution multiplier applied when grabbing an item for printing.
 *  3x brings a screen resolution item close to a 300DPI page */
#define PRINT_OVERSAMPLING  3.0

App::App()
    : QQuickView()
    , m_forceQuit(false)
    , m_accessMask(defaultMask())
    , m_is3dSupported(true)
    , m_translator(nullptr)
    , m_translator_base(nullptr)
    , m_fixtureBrowser(nullptr)
    , m_fixtureManager(nullptr)
    , m_contextManager(nullptr)
    , m_ioManager(nullptr)
    , m_showManager(nullptr)
    , m_simpleDesk(nullptr)
    , m_shortcutManager(nullptr)
    , m_videoProvider(nullptr)
    , m_networkManager(nullptr)
    , m_apiServer(nullptr)
    , m_uiManager(nullptr)
    , m_stageWizard(nullptr)
    , m_doc(nullptr)
    , m_docLoaded(false)
    , m_printItem(nullptr)
    , m_fileName(QString())
    , m_importManager(nullptr)
    , m_fixtureRemapManager(nullptr)
    , m_fixtureEditor(nullptr)
{
    QSettings settings;

    setResizeMode(QQuickView::SizeRootObjectToView);

    updateRecentFilesList();

    QVariant dir = settings.value(SETTINGS_WORKINGPATH);
    if (dir.isValid())
        m_workingPath = dir.toString();

    connect(this, &App::screenChanged, this, &App::slotScreenChanged);
    connect(this, SIGNAL(closing(QQuickCloseEvent*)), this, SLOT(slotClosing()));
    connect(this, &App::sceneGraphInitialized, this, &App::slotSceneGraphInitialized);
    qApp->installEventFilter(this);
}

App::~App()
{
    QSettings settings;

    stopAllFunctions();

    // exit fullscreen before saving the geometry, otherwise the full screen
    // rect would be stored and the window would be hard to resize at next startup
    if (windowState() & Qt::WindowFullScreen)
        showNormal();

#if defined(Q_OS_ANDROID)
    settings.setValue(SETTINGS_GEOMETRY, QVariant());
#else
    if (m_doc->isKiosk() == false && QLCFile::hasWindowManager())
        settings.setValue(SETTINGS_GEOMETRY, geometry());
    else
        settings.setValue(SETTINGS_GEOMETRY, QVariant());
#endif

    /* remove autosave file if present */
    QFile asFile(autoSaveFileName());
    if (asFile.exists())
        asFile.remove();
}

QString App::appName() const
{
    return QString(APPNAME);
}

QString App::appVersion() const
{
    return QString(APPVERSION);
}

void App::startup()
{
    qmlRegisterUncreatableType<App>("org.qlcplus.classes", 1, 0, "App", "Can't create an App!");
    qmlRegisterUncreatableType<Fixture>("org.qlcplus.classes", 1, 0, "Fixture", "Can't create a Fixture!");
    qmlRegisterUncreatableType<Function>("org.qlcplus.classes", 1, 0, "QLCFunction", "Can't create a Function!");
    qmlRegisterType<ModelSelector>("org.qlcplus.classes", 1, 0, "ModelSelector");
    qmlRegisterType<TreeFlatModel>("org.qlcplus.classes", 1, 0, "TreeFlatModel");
    qmlRegisterType<FolderBrowser>("org.qlcplus.classes", 1, 0, "FolderBrowser");

    setTitle(APPNAME);
    setIcon(QIcon(":/qlcplus.svg"));

    if (QFontDatabase::addApplicationFont(":/RobotoCondensed-Regular.ttf") < 0)
        qWarning() << "Roboto condensed cannot be loaded!";

    if (QFontDatabase::addApplicationFont(":/RobotoMono-Regular.ttf") < 0)
        qWarning() << "Roboto mono cannot be loaded!";

    if (QFontDatabase::addApplicationFont(":/FontAwesome7-Free-Solid-900.otf") < 0)
        qWarning() << "FontAwesome cannot be loaded!";

    rootContext()->setContextProperty("qlcplus", this);

    initDoc();

    m_uiManager = new UiManager(this, m_doc);
    rootContext()->setContextProperty("uiManager", m_uiManager);
    m_ioManager = new InputOutputManager(this, m_doc);
    m_fixtureBrowser = new FixtureBrowser(this, m_doc);
    m_fixtureManager = new FixtureManager(this, m_doc);
    m_fixtureGroupEditor = new FixtureGroupEditor(this, m_doc, m_fixtureManager);
    m_fixtureRemapManager = new FixtureRemapManager(this, m_doc);
    m_functionManager = new FunctionManager(this, m_doc);
    m_simpleDesk = new SimpleDesk(this, m_doc, m_functionManager);
    m_contextManager = new ContextManager(this, m_doc, m_fixtureManager, m_functionManager);
    m_paletteManager = new PaletteManager(this, m_doc, m_contextManager);

    m_virtualConsole = new VirtualConsole(this, m_doc, m_contextManager);
    m_showManager = new ShowManager(this, m_doc);
    connect(m_showManager, &ShowManager::itemClicked, m_contextManager, &ContextManager::setLastClickedType);

    m_networkManager = new NetworkManager(this, m_doc, m_virtualConsole, m_simpleDesk);
    rootContext()->setContextProperty("networkManager", m_networkManager);

    connect(m_networkManager, &NetworkManager::clientAccessRequest, 
            this, &App::slotClientAccessRequest);
    connect(m_networkManager, &NetworkManager::clientAccessRequestCancelled,
            this, &App::slotClientAccessRequestCancelled);
    connect(m_networkManager, &NetworkManager::clientAutoAuthorized, this,
            [this](const QString &sessionId)
    {
        m_networkManager->sendWorkspaceToClient(sessionId, fileName());
    });
    connect(m_networkManager, &NetworkManager::accessMaskChanged, this, &App::setAccessMask);
    connect(m_networkManager, &NetworkManager::requestProjectLoad, this, &App::slotLoadDocFromMemory);
    connect(m_networkManager, &NetworkManager::requestProjectClear, this, &App::slotClearDocFromNetwork);
    connect(m_networkManager, &NetworkManager::clientProjectRequest, this, &App::slotClientProjectRequest);
    connect(m_networkManager, &NetworkManager::storeAutostartProject,
            this, &App::slotSaveAutostart);

    // WebSocket control API (docs/api-spec/) - constructed unconditionally like
    // m_networkManager above, but doesn't start listening until App::startApiServer()
    // is called (from main.cpp, gated behind --api/--api-port, mirroring how
    // WebAccessQml is only lazily started via NetworkManager::startServer()).
    m_apiServer = new ApiServer(this, m_doc);

    m_tardis = new Tardis(this, m_doc, m_networkManager, m_fixtureManager, m_functionManager,
                          m_contextManager, m_simpleDesk, m_showManager, m_virtualConsole);
    rootContext()->setContextProperty("tardis", m_tardis);

    m_shortcutManager = new ShortcutManager(this);
    rootContext()->setContextProperty("shortcutManager", m_shortcutManager);
    registerBuiltinShortcuts();

    m_stageWizard = new StageWizard(m_doc, m_fixtureManager, m_functionManager,
                                    m_virtualConsole, m_contextManager, this);
    rootContext()->setContextProperty("stageWizard", m_stageWizard);

    m_contextManager->registerContext(m_virtualConsole);
    m_contextManager->registerContext(m_simpleDesk);
    m_contextManager->registerContext(m_showManager);
    m_contextManager->registerContext(m_ioManager);

    // register an uncreatable type just to use the enums in QML
    qmlRegisterUncreatableType<ContextManager>("org.qlcplus.classes", 1, 0, "ContextManager", "Can't create a ContextManager!");
    qmlRegisterUncreatableType<ShowManager>("org.qlcplus.classes", 1, 0, "ShowManager", "Can't create a ShowManager!");
    qmlRegisterUncreatableType<NetworkManager>("org.qlcplus.classes", 1, 0, "NetworkManager", "Can't create a NetworkManager!");
    qmlRegisterUncreatableType<SimpleDesk>("org.qlcplus.classes", 1, 0, "SimpleDesk", "Can't create a SimpleDesk!");

    // Start up in non-modified state
    m_doc->resetModified();

    QSettings settings;
    QRect rect(0, 0, 800, 600);
    bool restoreWindowGeometry = false;
    QVariant var = settings.value(SETTINGS_GEOMETRY);
#if defined(Q_OS_ANDROID)
    QScreen *currScreen = screen();
    rect = currScreen->geometry();
    setGeometry(rect);
    show();
#else
    if (var.isValid())
    {
        //qDebug() << "Restoring window position" << var.toRect();
        rect = var.toRect();

        // Make sure the saved geometry is still valid against the current display
        // configuration. If the window was closed on a secondary monitor that is no
        // longer connected, the stored rect would place it off-screen, making it
        // impossible to move back. Consider it valid only if a meaningful portion of
        // the window (enough to grab the title bar) overlaps a connected screen.
        bool geometryValid = false;
        for (QScreen *displayScreen : QGuiApplication::screens())
        {
            QRect overlap = displayScreen->availableGeometry().intersected(rect);
            if (overlap.width() >= 100 && overlap.height() >= 30)
            {
                geometryValid = true;
                break;
            }
        }

        if (geometryValid == false)
        {
            qDebug() << "Saved geometry" << rect << "is off-screen. Restoring on the current display";
            QRect available = screen()->availableGeometry();
            rect.setSize(rect.size().boundedTo(available.size()));
            rect.moveCenter(available.center());
        }

        restoreWindowGeometry = true;
        setGeometry(rect);
        show();
    }
    else
    {
        QScreen *currScreen = screen();
        rect.moveTopLeft(currScreen->geometry().topLeft());
        setGeometry(rect);
        showMaximized();
    }
#endif

    slotScreenChanged(screen());
    m_uiManager->initialize();
    m_showManager->initialize();

    // and here we go!
    setSource(QUrl("qrc:/MainView.qml"));

    if (restoreWindowGeometry)
        setGeometry(rect);
}

void App::registerBuiltinShortcuts()
{
    ContextManager *cm = m_contextManager;
    InputOutputManager *ioMgr = m_ioManager;
    VirtualConsole *vc = m_virtualConsole;

    m_shortcutManager->registerAction("fixture.selectAll", QKeySequence(Qt::CTRL | Qt::Key_A),
                                       ShortcutManager::FixturesAndFunctions,
                                       tr("Select/Deselect all fixtures"),
                                       [cm]() { cm->toggleFixturesSelection(); });

    m_shortcutManager->registerAction("fixture.nextGroup", QKeySequence(Qt::CTRL | Qt::Key_Tab),
                                       ShortcutManager::FixturesAndFunctions,
                                       tr("Select next fixture group"),
                                       [cm]() { cm->selectNextFixtureGroup(); });

    m_shortcutManager->registerAction("fixture.positionPicking", QKeySequence(Qt::CTRL | Qt::Key_P),
                                       ShortcutManager::FixturesAndFunctions,
                                       tr("Enable 3D position picking"),
                                       [cm]() { cm->setPositionPicking(true); });

    m_shortcutManager->registerAction("fixture.resetDumpValues", QKeySequence(Qt::CTRL | Qt::Key_R),
                                       ShortcutManager::FixturesAndFunctions,
                                       tr("Reset DMX dump values"),
                                       [cm]() { cm->resetDumpValues(); });

    m_shortcutManager->registerAction("fixture.deleteSelection", QKeySequence(Qt::Key_Delete),
                                       ShortcutManager::FixturesAndFunctions,
                                       tr("Delete the currently selected item"),
                                       [cm]() { cm->deleteSelectedItems(); });

    // deleteSelectedItems() also handles Show items/Tracks (App::ShowDragItem/
    // TrackDragItem), selectable only from the Show Manager tab - register it
    // there too so Delete still works there, not just in FixturesAndFunctions.
    m_shortcutManager->registerAction("show.deleteSelection", QKeySequence(Qt::Key_Delete),
                                       ShortcutManager::ShowManager,
                                       tr("Delete the currently selected show item or track"),
                                       [cm]() { cm->deleteSelectedItems(); });

    m_shortcutManager->registerAction("app.save", QKeySequence(Qt::CTRL | Qt::Key_S),
                                       ShortcutManager::Global,
                                       tr("Save the current project"),
                                       [this]() { QMetaObject::invokeMethod(rootObject(), "saveProject"); });

    m_shortcutManager->registerAction("app.undo", QKeySequence(Qt::CTRL | Qt::Key_Z),
                                       ShortcutManager::Global,
                                       tr("Undo the last action"),
                                       []() { Tardis::instance()->undoAction(); });

    m_shortcutManager->registerAction("app.redo", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
                                       ShortcutManager::Global,
                                       tr("Redo the last undone action"),
                                       []() { Tardis::instance()->redoAction(); });

    // Ctrl+Y is a common alternate binding for redo - same callback, second id
    m_shortcutManager->registerAction("app.redoAlt", QKeySequence(Qt::CTRL | Qt::Key_Y),
                                       ShortcutManager::Global,
                                       tr("Redo the last undone action (alternate binding)"),
                                       []() { Tardis::instance()->redoAction(); });

    m_shortcutManager->registerAction("vc.editMode", QKeySequence(Qt::CTRL | Qt::Key_E),
                                       ShortcutManager::VirtualConsole,
                                       tr("Toggle Virtual Console edit mode"),
                                       [vc]() { vc->setEditMode(!vc->editMode()); });

    m_shortcutManager->registerAction("app.newProject", QKeySequence(Qt::CTRL | Qt::Key_N),
                                       ShortcutManager::Global,
                                       tr("Create a new project"),
                                       [this]() { newWorkspace(); });

    m_shortcutManager->registerAction("app.openProject", QKeySequence(Qt::CTRL | Qt::Key_O),
                                       ShortcutManager::Global,
                                       tr("Open a project"),
                                       [this]() { QMetaObject::invokeMethod(rootObject(), "openProject"); });

    m_shortcutManager->registerAction("app.saveProjectAs", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
                                       ShortcutManager::Global,
                                       tr("Save the project with a new name"),
                                       [this]() { QMetaObject::invokeMethod(rootObject(), "saveProjectAs"); });

    m_shortcutManager->registerAction("app.toggleFullscreen", QKeySequence(Qt::Key_F11),
                                       ShortcutManager::Global,
                                       tr("Toggle fullscreen mode"),
                                       [this]() { toggleFullscreen(); });

    m_shortcutManager->registerAction("io.blackoutToggle", QKeySequence(Qt::CTRL | Qt::Key_B),
                                       ShortcutManager::Global,
                                       tr("Toggle blackout"),
                                       [ioMgr]() { ioMgr->setBlackout(!ioMgr->blackout()); });

    m_shortcutManager->registerAction("app.panic", QKeySequence(Qt::CTRL | Qt::Key_Period),
                                       ShortcutManager::Global,
                                       tr("Stop all running functions"),
                                       [this]() { stopAllFunctions(); });

    m_shortcutManager->registerAction("app.dmxDump", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
                                       ShortcutManager::Global,
                                       tr("Dump DMX values to a Scene"),
                                       [this]() { QMetaObject::invokeMethod(rootObject(), "triggerDmxDump"); });

    m_shortcutManager->registerAction("context.switchFixturesAndFunctions", QKeySequence(Qt::CTRL | Qt::Key_1),
                                       ShortcutManager::Global,
                                       tr("Switch to the Fixtures & Functions tab"),
                                       [cm]() { cm->switchToContext("FIXANDFUNC"); });

    m_shortcutManager->registerAction("context.switchVirtualConsole", QKeySequence(Qt::CTRL | Qt::Key_2),
                                       ShortcutManager::Global,
                                       tr("Switch to the Virtual Console tab"),
                                       [cm]() { cm->switchToContext("VC"); });

    m_shortcutManager->registerAction("context.switchSimpleDesk", QKeySequence(Qt::CTRL | Qt::Key_3),
                                       ShortcutManager::Global,
                                       tr("Switch to the Simple Desk tab"),
                                       [cm]() { cm->switchToContext("SDESK"); });

    m_shortcutManager->registerAction("context.switchShowManager", QKeySequence(Qt::CTRL | Qt::Key_4),
                                       ShortcutManager::Global,
                                       tr("Switch to the Show Manager tab"),
                                       [cm]() { cm->switchToContext("SHOWMGR"); });

    m_shortcutManager->registerAction("context.switchIOManager", QKeySequence(Qt::CTRL | Qt::Key_5),
                                       ShortcutManager::Global,
                                       tr("Switch to the Input/Output Manager tab"),
                                       [cm]() { cm->switchToContext("IOMGR"); });
}

void App::toggleFullscreen()
{
    static int wstate = windowState();

    if (windowState() & Qt::WindowFullScreen)
    {
        if (wstate & Qt::WindowMaximized)
            showMaximized();
        else
            showNormal();
        wstate = windowState();
    }
    else
    {
        wstate = windowState();
        showFullScreen();
    }
}

void App::setLanguage(QString locale)
{
    if (m_translator != nullptr)
    {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
    }
    if (m_translator_base != nullptr)
    {
        QCoreApplication::removeTranslator(m_translator_base);
        delete m_translator_base;
    }

    QString translationPath = QLCFile::systemDirectory(TRANSLATIONDIR).absolutePath();

    if (locale.isEmpty() == true)
        locale = QLocale::system().name();

    m_translator = new QTranslator(QCoreApplication::instance());
    if (m_translator->load("qlcplus_" + locale, translationPath) == true)
        QCoreApplication::installTranslator(m_translator);

    m_translator_base = new QTranslator(QCoreApplication::instance());
#if defined(Q_OS_MACOS) || defined(APPIMAGE)
    if (m_translator_base->load("qtbase_" + locale, translationPath))
#else
    if (m_translator_base->load("qt_" + locale, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
#endif
        QCoreApplication::installTranslator(m_translator_base);

    QSettings settings;
    settings.setValue(SETTINGS_LANGUAGE, locale);

    engine()->retranslate();
}

QString App::goboSystemPath() const
{
    return QLCFile::systemDirectory(GOBODIR).absolutePath();
}

qreal App::pixelDensity() const
{
    return m_pixelDensity;
}

int App::accessMask() const
{
    return m_accessMask;
}

bool App::is3DSupported() const
{
    return m_is3dSupported;
}

void App::set3dSupported(bool enable)
{
    m_is3dSupported = enable;
}

void App::aboutQt()
{
    qApp->aboutQt();
}

void App::exit(bool force)
{
    m_forceQuit = force;
    stopAllFunctions();
    QApplication::quit();
}

void App::setAccessMask(int mask)
{
    if (mask == m_accessMask)
        return;

    m_accessMask = mask;
    emit accessMaskChanged(mask);
}

int App::defaultMask() const
{
    return AC_FixtureEditing | AC_FunctionEditing | AC_InputOutput |
            AC_ShowManager | AC_SimpleDesk | AC_VCControl | AC_VCEditing;
}

namespace {

/** Keys a focused text field legitimately wants for itself: any unmodified
 *  (or shifted, for uppercase/shifted-symbol) letter or digit, Delete, and
 *  the standard Ctrl+A/C/V/X editing shortcuts. Everything else (F-keys,
 *  Ctrl+S, Ctrl+Z, Ctrl+1..5, ...) still goes through the shortcut path
 *  even while a text field has focus. */
bool isTextEditingKey(const QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete)
        return true;

    Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;

    if (mods == Qt::ControlModifier)
    {
        switch (e->key())
        {
            case Qt::Key_A:
            case Qt::Key_C:
            case Qt::Key_V:
            case Qt::Key_X:
                return true;
            default:
                return false;
        }
    }

    if (mods == Qt::NoModifier || mods == Qt::ShiftModifier)
    {
        QString text = e->text();
        if (text.size() == 1 && text.at(0).isLetterOrNumber())
            return true;
    }

    return false;
}

} // namespace

bool App::isTextInputFocused() const
{
    QQuickItem *item = activeFocusItem();

    while (item != nullptr)
    {
        if (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
            return true;

        item = item->parentItem();
    }

    return false;
}

void App::keyPressEvent(QKeyEvent *e)
{
    if (isTextInputFocused() && isTextEditingKey(e))
    {
        QQuickView::keyPressEvent(e);
        return;
    }

    if (m_shortcutManager && m_shortcutManager->handleKeyEvent(e))
    {
        QQuickView::keyPressEvent(e);
        return;
    }

    // While a ShortcutsEditor.qml rebinding capture is in progress, the key
    // is meant for that capture alone - let it fall through to QML (below)
    // but don't also hand it to ContextManager, which could otherwise start
    // a VC widget/function or move a fixture selection off a key the user is
    // only trying to record as a new binding
    if (m_contextManager && (m_shortcutManager == nullptr || m_shortcutManager->isCapturing() == false))
        m_contextManager->handleKeyPress(e);

    QQuickView::keyPressEvent(e);
}

void App::keyReleaseEvent(QKeyEvent *e)
{
    if (m_contextManager && (m_shortcutManager == nullptr || m_shortcutManager->isCapturing() == false))
        m_contextManager->handleKeyRelease(e);

    QQuickView::keyReleaseEvent(e);
}

void App::mousePressEvent(QMouseEvent *e)
{
    if (m_contextManager)
        m_contextManager->setLastClickedType(App::NoDragItem);

    QQuickView::mousePressEvent(e);
}

bool App::event(QEvent *event)
{
    if (event->type() == QEvent::Close)
    {
        if (m_doc->isModified() && m_forceQuit == false)
        {
            QMetaObject::invokeMethod(rootObject(), "saveBeforeExit");
            event->ignore();
            return false;
        }
    }
    return QQuickView::event(event);
}

bool App::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::Quit)
    {
        if (m_doc && m_doc->isModified() && rootObject() && m_forceQuit == false)
        {
            QMetaObject::invokeMethod(rootObject(), "saveBeforeExit");
            event->ignore();
            return true;
        }
    }

    return QQuickView::eventFilter(obj, event);
}

void App::slotSceneGraphInitialized()
{
    // TODO: Qt6
}

void App::slotScreenChanged(QScreen *screen)
{
    bool isLandscape = (screen->orientation() == Qt::LandscapeOrientation ||
                     screen->orientation() == Qt::InvertedLandscapeOrientation) ? true : false;
    qreal sSize = isLandscape ? screen->size().height() : screen->size().width();
    m_pixelDensity = qMax(screen->physicalDotsPerInch() *  0.039370, sSize / 220.0);
    qDebug() << "Screen changed to" << screen->name() << ", pixel density:" << m_pixelDensity
             << ", geometry:" << screen->size() << ", physical size:" << screen->physicalSize() << isLandscape;
    rootContext()->setContextProperty("screenPixelDensity", m_pixelDensity);
}

void App::slotClosing()
{
    stopAllFunctions();

    if (m_contextManager)
    {
        delete m_contextManager;
        m_contextManager = nullptr;
    }

    QCoreApplication::quit();
    //QTimer::singleShot(2000, []() { QCoreApplication::exit(0); });
}

void App::slotClientAccessRequest(QString sessionId, QString name,
                                  QString peerAddress, quint16 peerPort)
{
    QMetaObject::invokeMethod(rootObject(), "openAccessRequest",
                              Q_ARG(QVariant, sessionId), Q_ARG(QVariant, name),
                              Q_ARG(QVariant, peerAddress), Q_ARG(QVariant, peerPort));
}

void App::slotClientAccessRequestCancelled(QString sessionId)
{
    QMetaObject::invokeMethod(rootObject(), "closeAccessRequest",
                              Q_ARG(QVariant, sessionId));
}

void App::slotClientProjectRequest(QString sessionId)
{
    /* The workspace is served from a file. If the current project has never
     * been saved, or has pending changes, dump it to a temporary file first,
     * otherwise the client would get a stale (or missing) project */
    QString fileName = m_fileName;

    if (fileName.isEmpty() || m_doc->isModified())
    {
        fileName = QString("%1/%2").arg(QDir::tempPath()).arg("qlcplus_netproject.qxw");
        if (saveXML(fileName, true) != QFile::NoError)
        {
            qWarning() << Q_FUNC_INFO << "Unable to serve the project to" << sessionId;
            return;
        }
    }

    qDebug() << Q_FUNC_INFO << "Serving" << fileName << "to session" << sessionId;

    if (m_networkManager->sendWorkspaceToClient(sessionId, fileName) == false)
        qWarning() << Q_FUNC_INFO << "Failed to send the workspace to session" << sessionId;
}

void App::slotAccessMaskChanged(int mask)
{
    setAccessMask(mask);
}

/*********************************************************************
 * Doc
 *********************************************************************/
Doc *App::doc()
{
    return m_doc;
}

VirtualConsole *App::virtualConsole() const
{
    return m_virtualConsole;
}

SimpleDesk *App::simpleDesk() const
{
    return m_simpleDesk;
}

NetworkManager *App::networkManager() const
{
    return m_networkManager;
}

ApiServer *App::apiServer() const
{
    return m_apiServer;
}

bool App::docLoaded()
{
    return m_docLoaded;
}

void App::setDocLoaded(bool loaded)
{
    if (m_docLoaded == loaded)
        return;

    m_docLoaded = loaded;
    emit docLoadedChanged();
}

bool App::docModified() const
{
    return m_doc->isModified();
}

void App::slotDocAutosave()
{
    saveXML(autoSaveFileName(), true);
}

void App::initDoc()
{
    Q_ASSERT(m_doc == nullptr);
    m_doc = new Doc(this);

    connect(m_doc, SIGNAL(modified(bool)), this, SIGNAL(docModifiedChanged()));
    connect(m_doc, SIGNAL(needAutosave()), this, SLOT(slotDocAutosave()));
    connect(m_doc->masterTimer(), SIGNAL(functionListChanged()),
            this, SIGNAL(runningFunctionsCountChanged()));

    /* Load user fixtures first so that they override system fixtures */
    m_doc->fixtureDefCache()->load(QLCFixtureDefCache::userDefinitionDirectory());
    m_doc->fixtureDefCache()->loadMap(QLCFixtureDefCache::systemDefinitionDirectory());

    /* Load channel modifiers templates */
    m_doc->modifiersCache()->load(QLCModifiersCache::systemTemplateDirectory(), true);
    m_doc->modifiersCache()->load(QLCModifiersCache::userTemplateDirectory());

    /* Load RGB scripts */
    m_doc->rgbScriptsCache()->load(RGBScriptsCache::systemScriptsDirectory());
    m_doc->rgbScriptsCache()->load(RGBScriptsCache::userScriptsDirectory());

    /* Load plugins */
#if defined Q_OS_ANDROID
    QString pluginsPath = QCoreApplication::applicationDirPath();
    m_doc->ioPluginCache()->load(QDir(pluginsPath));
#else
    m_doc->ioPluginCache()->load(IOPluginCache::systemPluginDirectory());
#endif

    /* Load audio decoder plugins
     * This doesn't use a AudioPluginCache::systemPluginDirectory() cause
     * otherwise the qlcconfig.h creation should have been moved into the
     * audio folder, which doesn't make much sense */
    m_doc->audioPluginCache()->load(QLCFile::systemDirectory(AUDIOPLUGINDIR, KExtPlugin));
    m_videoProvider = new VideoProvider(this, m_doc);

    Q_ASSERT(m_doc->inputOutputMap() != nullptr);

    /* Load input plugins & profiles */
    m_doc->inputOutputMap()->loadProfiles(InputOutputMap::userProfileDirectory());
    m_doc->inputOutputMap()->loadProfiles(InputOutputMap::systemProfileDirectory());
    m_doc->inputOutputMap()->loadDefaults();

    m_doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    m_doc->inputOutputMap()->startUniverses();
    m_doc->masterTimer()->start();
}

void App::clearDocument()
{
    if (m_videoProvider)
    {
        delete m_videoProvider;
        m_videoProvider = nullptr;
    }

    m_contextManager->resetFixtureSelection();
    //m_simpleDesk->resetContents(); // TODO
    m_showManager->resetContents();
    m_virtualConsole->resetContents();

    // Drop the preview items *before* the Doc is emptied: they hold references
    // to fixtures that clearContents() is about to delete
    m_contextManager->resetViewItems();

    m_doc->masterTimer()->stop();
    m_doc->clearContents();

    m_tardis->resetHistory();
    m_doc->inputOutputMap()->resetUniverses();
    setFileName(QString());
    m_doc->resetModified();
    m_doc->inputOutputMap()->startUniverses();
    m_doc->masterTimer()->start();
}

int App::runningFunctionsCount() const
{
    return m_doc->masterTimer()->runningFunctions();
}

void App::stopAllFunctions()
{
    // first, gracefully stop via Function Manager (if that's the case)
    m_functionManager->setPreviewEnabled(false);

    // close any fullscreen video windows before stopping functions
    if (m_videoProvider)
        m_videoProvider->shutdown();

    // then, brutally kill the rest (could be started from VC, etc)
    m_doc->masterTimer()->stopAllFunctions();
}

void App::enableKioskMode()
{
    // enable Virtual console only
    setAccessMask(AC_VCControl);
}

void App::createKioskCloseButton(const QRect &rect)
{
    Q_UNUSED(rect)
    // TODO
}

/*********************************************************************
 * Printer
 *********************************************************************/

void App::printItem(QQuickItem *item)
{
    if (item == nullptr)
        return;

    m_printItem = item;

    // Grab the item at a multiple of its on-screen size, otherwise the capture
    // carries only screen resolution pixels (~96DPI) and looks blurry once
    // blown up to a 300DPI page. The factor is clamped so the offscreen
    // surface never exceeds what the GPU can allocate (GL_MAX_TEXTURE_SIZE is
    // commonly 16384), which would silently return an empty grab.
    const qreal maxDimension = 16384.0;
    qreal factor = PRINT_OVERSAMPLING;

    if (item->width() > 0)
        factor = qMin(factor, maxDimension / item->width());
    if (item->height() > 0)
        factor = qMin(factor, maxDimension / item->height());
    factor = qMax(factor, 1.0);

    QSize targetSize(qRound(item->width() * factor), qRound(item->height() * factor));

    m_printerImage = item->grabToImage(targetSize);
    if (m_printerImage.isNull())
    {
        qWarning() << "Failed to grab item for printing";
        m_printItem = nullptr;
        return;
    }

    connect(m_printerImage.data(), &QQuickItemGrabResult::ready, this, &App::slotItemReadyForPrinting);
}

void App::slotItemReadyForPrinting()
{
    QPrinter printer;
    QPrintDialog *dlg = new QPrintDialog(&printer);
    if (dlg->exec() == QDialog::Accepted)
    {
        // the page rectangle must be expressed in device pixels, since it is
        // used together with image pixels below. paintRect() would return
        // points instead, which are a much coarser unit
        QRect pageRect = printer.pageLayout().paintRectPixels(printer.resolution());
        QImage img = m_printerImage->image();

        qDebug() << "Page size:" << pageRect << ", image size:" << img.size();

        if (img.isNull() == false && pageRect.isEmpty() == false)
        {
            QPainter painter(&printer);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            // scale factor to map image pixels to page pixels. Shrink an image
            // wider than the page, but never enlarge a narrower one
            qreal scale = qMin(qreal(1.0), pageRect.width() / qreal(img.width()));

            // number of image rows that fit on a single page
            int sliceHeight = qMax(1, int(pageRect.height() / scale));

            // handle multi-page printing. Offsets are in image coordinates and
            // the painter does the scaling, so the full grabbed resolution is
            // handed to the print device rather than a pre-downscaled copy
            for (int yOffset = 0; yOffset < img.height(); yOffset += sliceHeight)
            {
                int height = qMin(sliceHeight, img.height() - yOffset);
                QRectF srcRect(0, yOffset, img.width(), height);
                QRectF dstRect(0, 0, img.width() * scale, height * scale);

                painter.drawImage(dstRect, img, srcRect);

                if (yOffset + sliceHeight < img.height())
                    printer.newPage();
            }

            painter.end();
        }
    }

    m_printerImage.clear();
    if (m_printItem != nullptr)
        m_printItem->setProperty("isPrinting", false);
    m_printItem = nullptr;
}

/*********************************************************************
 * Load & Save
 *********************************************************************/

void App::setFileName(const QString &fileName)
{
    m_fileName = fileName;
    FreezeWatchdog::setCurrentProjectPath(fileName);
}

QString App::fileName() const
{
    return m_fileName;
}

QString App::autoSaveFileName() const
{
    QString fName = m_fileName;

    if (fName.isEmpty())
        fName = "NewProject.autosave.qxw";
    else
    {
        fName.remove(".qxw");
        fName.append(".autosave.qxw");
    }

    return fName;
}

void App::updateRecentFilesList(QString filename)
{
    QSettings settings;
    if (filename.isEmpty() == false)
    {
        m_recentFiles.removeAll(filename); // in case the string is already present, remove it...
        m_recentFiles.prepend(filename); // and add it to the top
        for (int i = 0; i < m_recentFiles.count(); i++)
        {
            settings.setValue(QString("%1%2").arg(SETTINGS_RECENTFILE).arg(i), m_recentFiles.at(i));
            emit recentFilesChanged();
        }
    }
    else
    {
        for (int i = 0; i < MAX_RECENT_FILES; i++)
        {
            QVariant recent = settings.value(QString("%1%2").arg(SETTINGS_RECENTFILE).arg(i));
            if (recent.isValid())
                m_recentFiles.append(recent.toString());
        }
    }
}

QStringList App::recentFiles() const
{
    return m_recentFiles;
}

void App::loadLastWorkspace()
{
    if (m_recentFiles.isEmpty())
        return;

    loadWorkspace(m_recentFiles.first());
}

QString App::workingPath() const
{
    return m_workingPath;
}

void App::setWorkingPath(QString workingPath)
{
    QString strippedPath = workingPath.replace("file://", "");

    if (m_workingPath == strippedPath)
        return;

    m_workingPath = strippedPath;

    QSettings settings;
    settings.setValue(SETTINGS_WORKINGPATH, m_workingPath);

    emit workingPathChanged(strippedPath);
}

bool App::newWorkspace()
{
    /* Warn the connected clients before dropping everything */
    m_networkManager->notifyProjectChanging();

    clearDocument();
    m_fixtureManager->slotDocLoaded();
    m_functionManager->slotDocLoaded();
    m_contextManager->resetContexts();

    /* Let the connected clients pick up the empty workspace */
    m_networkManager->notifyProjectLoaded();

    return true;
}

bool App::loadWorkspace(const QString &fileName)
{
    /* Warn the connected clients before dropping everything */
    m_networkManager->notifyProjectChanging();

    m_contextManager->resetContexts();

    /* Clear existing document data */
    clearDocument();
    setDocLoaded(false);

    QString localFilename =  fileName;
    if (localFilename.startsWith("file:"))
        localFilename = QUrl(fileName).toLocalFile();

    if (loadXML(localFilename) == QFile::NoError)
    {
        setTitle(QString("%1 - %2").arg(APPNAME).arg(localFilename));
        setFileName(localFilename);
        updateRecentFilesList(localFilename);
        setDocLoaded(true);
        m_doc->resetModified();
        m_videoProvider = new VideoProvider(this, m_doc);
        m_contextManager->resetContexts();

        // autostart Function if set
        if (m_doc->startupFunction() != Function::invalidId())
        {
            Function *func = m_doc->function(m_doc->startupFunction());
            if (func != nullptr)
            {
                qDebug() << Q_FUNC_INFO << "Starting startup function. (" << m_doc->startupFunction() << ")";
                func->start(m_doc->masterTimer(), FunctionParent::master(FunctionParent::ProjectAutostart));
            }
            else
            {
                qWarning() << Q_FUNC_INFO << "Startup function does not exist, erasing. (" << m_doc->startupFunction() << ")";
                m_doc->setStartupFunction(Function::invalidId());
            }
        }

        m_doc->inputOutputMap()->startUniverses();

        // Re-push any DMX-driven fixture's persisted position/rotation onto
        // its live channels (including real connected hardware) now that
        // both the Doc's fixtures and its MonitorProperties are fully loaded
        // and universe output is running - see restorePersistedDmxTransforms()'s
        // own doc comment. This calls pushPositionDelta()/pushRotationDelta(),
        // which mark the project modified; undo that immediately after so a
        // project that is opened and not otherwise touched still shows as
        // unmodified.
        m_contextManager->restorePersistedDmxTransforms();
        m_doc->resetModified();

        /* The workspace is complete: let the connected clients request it */
        m_networkManager->notifyProjectLoaded();

        return true;
    }
    return false;
}

void App::slotLoadDocFromMemory(QByteArray &xmlData)
{
    if (xmlData.isEmpty())
        return;

    m_contextManager->resetContexts();

    /* Clear existing document data */
    clearDocument();
    setDocLoaded(false);

    QBuffer databuf;
    databuf.setData(xmlData);
    databuf.open(QIODevice::ReadOnly | QIODevice::Text);

    //qDebug() << "Buffer data:" << databuf.data();
    QXmlStreamReader doc(&databuf);

    if (doc.hasError())
    {
        qWarning() << Q_FUNC_INFO << "Unable to read from XML in memory";
        return;
    }

    while (!doc.atEnd())
    {
        if (doc.readNext() == QXmlStreamReader::DTD)
            break;
    }
    if (doc.hasError())
    {
        qDebug() << "XML has errors:" << doc.errorString();
        return;
    }

    if (doc.dtdName() == KXMLQLCWorkspace)
    {
        /* Do not force the Virtual Console: honour the context saved in the
         * received project. Clients restricted to VC control only are still
         * switched to it by loadXML itself */
        loadXML(doc, false, true);
        setDocLoaded(true);
        m_doc->resetModified();
        m_doc->inputOutputMap()->startUniverses();
        m_contextManager->resetContexts();

        // See loadWorkspace()'s equivalent call for why this runs here and
        // why resetModified() follows it.
        m_contextManager->restorePersistedDmxTransforms();
        m_doc->resetModified();
    }
    else
        qDebug() << "XML doesn't have a Workspace tag";
}

void App::slotClearDocFromNetwork()
{
    qDebug() << Q_FUNC_INFO << "Clearing workspace on server request";

    /* Same teardown performed before loading a project: drop the view items
     * and the Virtual Console contents while the Doc is still populated,
     * so nothing keeps a reference to what is about to be deleted */
    m_contextManager->resetContexts();
    clearDocument();
    setDocLoaded(false);
}

void App::slotSaveAutostart(QString fileName)
{
    m_doc->setWorkspacePath(QFileInfo(fileName).absolutePath());
    QFile::FileError error = saveXML(fileName);
    if (error != QFile::NoError)
        qWarning() << Q_FUNC_INFO << "Unable to save autostart project" << fileName << error;
}

bool App::saveWorkspace(const QString &fileName)
{
    QString localFilename = fileName;
    QString asfName = autoSaveFileName();

    if (localFilename.startsWith("file:"))
        localFilename = QUrl(fileName).toLocalFile();

    /* Always use the workspace suffix */
    if (localFilename.right(4) != KExtWorkspace)
        localFilename += KExtWorkspace;

    /* Set the workspace path before saving the new XML. In this way local files
       can be loaded even if the workspace file will be moved */
    m_doc->setWorkspacePath(QFileInfo(localFilename).absolutePath());

    if (saveXML(localFilename) == QFile::NoError)
    {
        /* remove autosave file if present */
        QFile asFile(asfName);
        if (asFile.exists())
            asFile.remove();

        setTitle(QString("%1 - %2").arg(APPNAME).arg(localFilename));
        updateRecentFilesList(localFilename);
        return true;
    }

    return false;
}

QFileDevice::FileError App::loadXML(const QString &fileName)
{
    QFile::FileError retval = QFile::NoError;

    if (fileName.isEmpty() == true)
        return QFile::OpenError;

    QXmlStreamReader *doc = QLCFile::getXMLReader(fileName);
    if (doc == nullptr || doc->device() == nullptr || doc->hasError())
    {
        qWarning() << Q_FUNC_INFO << "Unable to read from" << fileName;
        return QFile::ReadError;
    }

    while (!doc->atEnd())
    {
        if (doc->readNext() == QXmlStreamReader::DTD)
            break;
    }
    if (doc->hasError())
    {
        QLCFile::releaseXMLReader(doc);
        return QFile::ResourceError;
    }

    /* Set the workspace path before loading the new XML. In this way local files
       can be loaded even if the workspace file has been moved */
    m_doc->setWorkspacePath(QFileInfo(fileName).absolutePath());

    if (doc->dtdName() == KXMLQLCWorkspace)
    {
        if (loadXML(*doc) == false)
        {
            retval = QFile::ReadError;
        }
        else
        {
            setFileName(fileName);
            m_doc->resetModified();
            retval = QFile::NoError;
        }
    }
    else
    {
        retval = QFile::ReadError;
        qWarning() << Q_FUNC_INFO << fileName << "is not a workspace file";
    }

    QLCFile::releaseXMLReader(doc);

    return retval;
}

bool App::loadXML(QXmlStreamReader &doc, bool goToConsole, bool fromMemory)
{
    if (doc.readNextStartElement() == false)
        return false;

    if (doc.name() != KXMLQLCWorkspace)
    {
        qWarning() << Q_FUNC_INFO << "Workspace node not found";
        return false;
    }

    QString contextName = doc.attributes().value(KXMLQLCWorkspaceWindow).toString();

    while (doc.readNextStartElement())
    {
        if (doc.name() == KXMLQLCEngine)
        {
            m_doc->loadXML(doc);
        }
        else if (doc.name() == KXMLQLCVirtualConsole)
        {
            m_virtualConsole->loadXML(doc);
        }
#if 0
        else if (doc.name() == KXMLQLCSimpleDesk)
        {
            SimpleDesk::instance()->loadXML(doc);
        }
#endif
        else if (doc.name() == KXMLQLCCreator)
        {
            /* Ignore creator information */
            doc.skipCurrentElement();
        }
        else
        {
            qWarning() << Q_FUNC_INFO << "Unknown Workspace tag:" << doc.name().toString();
            doc.skipCurrentElement();
        }
    }

    if (goToConsole == true || accessMask() == AC_VCControl)
        // Force the active window to be Virtual Console
        m_contextManager->switchToContext("VirtualConsole");
    else
        // Set the active window to what was saved in the workspace file
        m_contextManager->switchToContext(contextName);

    // Perform post-load operations
    m_virtualConsole->postLoad();

    if (m_doc->errorLog().isEmpty() == false &&
        fromMemory == false)
    {
        // TODO: emit a signal to inform the QML UI to display an error message
        /*
        QMessageBox msg(QMessageBox::Warning, tr("Warning"),
                        tr("Some errors occurred while loading the project:") + "\n\n" + m_doc->errorLog(),
                        QMessageBox::Ok);
        msg.exec();
        */
    }

    return true;
}

QFile::FileError App::saveXML(const QString& fileName, bool autosave)
{
#if defined(Q_OS_ANDROID)
    const QString outputFileName(fileName);
#else
    QString tempFileName(fileName);
    tempFileName += ".temp";
    const QString outputFileName(tempFileName);
#endif

    QFile file(outputFileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) == false)
        return file.error();

    QXmlStreamWriter doc(&file);
    doc.setAutoFormatting(true);
    doc.setAutoFormattingIndent(1);

    doc.writeStartDocument();
    doc.writeDTD(QString("<!DOCTYPE %1>").arg(KXMLQLCWorkspace));

    doc.writeStartElement(KXMLQLCWorkspace);
    doc.writeAttribute("xmlns", QString("%1%2").arg(KXMLQLCplusNamespace).arg(KXMLQLCWorkspace));

    /* Currently active context */
    doc.writeAttribute(KXMLQLCWorkspaceWindow, m_contextManager->currentContext());

    /* Creator information */
    doc.writeStartElement(KXMLQLCCreator);
    doc.writeTextElement(KXMLQLCCreatorName, APPNAME);
    doc.writeTextElement(KXMLQLCCreatorVersion, APPVERSION);
    doc.writeTextElement(KXMLQLCCreatorAuthor, QLCFile::currentUserName());
    doc.writeEndElement();

    /* Write engine components to the XML document */
    m_doc->saveXML(&doc);

    /* Write virtual console to the XML document */
    m_virtualConsole->saveXML(&doc);

    /* Write Simple Desk to the XML document */
    //SimpleDesk::instance()->saveXML(&doc);

    doc.writeEndElement(); // close KXMLQLCWorkspace

    /* End the document and close all the open elements */
    doc.writeEndDocument();

    if (doc.hasError())
    {
        qWarning() << Q_FUNC_INFO << "Error writing XML to" << outputFileName;
        file.close();
#if !defined(Q_OS_ANDROID)
        file.remove();
#endif
        return QFile::WriteError;
    }

    file.close();
    if (file.error() != QFile::NoError)
        return file.error();

#if !defined(Q_OS_ANDROID)
    // Save to actual requested file name
    QFile currFile(fileName);
    if (currFile.exists() && !currFile.remove())
    {
        qWarning() << "Could not erase" << fileName;
        return currFile.error();
    }
    if (!file.rename(fileName))
    {
        qWarning() << "Could not rename" << tempFileName << "to" << fileName;
        return file.error();
    }
#endif

    if (!autosave)
    {
        /* Set the file name for the current Doc instance and
           set it also in an unmodified state. */
        setFileName(fileName);
        m_doc->resetModified();
    }

    return QFile::NoError;
}

/*********************************************************************
 * Import project
 *********************************************************************/

bool App::loadImportWorkspace(const QString &fileName)
{
    if (m_importManager != nullptr)
        delete m_importManager;

    m_importManager = new ImportManager(this, m_doc);
    return m_importManager->loadWorkspace(fileName);
}

void App::cancelImport()
{
    if (m_importManager != nullptr)
        delete m_importManager;

    m_importManager = nullptr;
}

void App::importFromWorkspace()
{
    if (m_importManager == nullptr)
        return;

    m_importManager->apply();
    m_paletteManager->updatePaletteList();

    delete m_importManager;
    m_importManager = nullptr;
}

/*********************************************************************
 * Fixture editor
 *********************************************************************/

void App::createFixture()
{
    if (m_fixtureEditor == nullptr)
    {
        m_fixtureEditor = new FixtureEditor(this, m_doc);
        QMetaObject::invokeMethod(rootObject(), "switchToContext",
                                  Q_ARG(QVariant, "FXEDITOR"),
                                  Q_ARG(QVariant, "qrc:/FixtureEditor.qml"));
    }

    m_fixtureEditor->createDefinition();
}

void App::loadFixture(QString fileName)
{
    if (m_fixtureEditor == nullptr)
    {
        m_fixtureEditor = new FixtureEditor(this, m_doc);
        QMetaObject::invokeMethod(rootObject(), "switchToContext",
                                  Q_ARG(QVariant, "FXEDITOR"),
                                  Q_ARG(QVariant, "qrc:/FixtureEditor.qml"));
    }
    m_fixtureEditor->loadDefinition(fileName);
}

void App::editFixture(QString manufacturer, QString model)
{
    bool switchToEditor = false;

    if (m_fixtureEditor == nullptr)
    {
        m_fixtureEditor = new FixtureEditor(this, m_doc);
        switchToEditor = true;
    }

    if (m_fixtureEditor->editDefinition(manufacturer, model) == false)
    {
        delete m_fixtureEditor;
        m_fixtureEditor = nullptr;
        return;
    }

    if (switchToEditor)
    {
        QMetaObject::invokeMethod(rootObject(), "switchToContext",
                                  Q_ARG(QVariant, "FXEDITOR"),
                                  Q_ARG(QVariant, "qrc:/FixtureEditor.qml"));
    }
}

void App::closeFixtureEditor()
{
    if (m_fixtureEditor)
    {
        delete m_fixtureEditor;
        m_fixtureEditor = nullptr;
    }

    // reload the QLC+ main view
    //setSource(QUrl("qrc:/MainView.qml"));
    QMetaObject::invokeMethod(rootObject(), "switchToContext",
                              Q_ARG(QVariant, "FIXANDFUNC"),
                              Q_ARG(QVariant, "qrc:/FixturesAndFunctions.qml"));
}
