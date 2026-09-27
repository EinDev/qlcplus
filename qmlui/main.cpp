/*
  Q Light Controller Plus
  main.cpp

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

#include <QSettings>
#include <QApplication>
#include <QSurfaceFormat>
#include <QCommandLineParser>
#include <QQmlApplicationEngine>
#include <QTimer>

#include "app.h"
#include "asynclogwriter.h"
#include "crashhandler.h"
#include "freezewatchdog.h"
#include "networkmanager.h"
#include "apiserver.h"
#include "webserver.h"
#include "qlcconfig.h"
#include "qlcfile.h"
#include "slowclickapplication.h"

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
#include <QColor>
#include <QImage>
#include <memory>
#include "spoutsender.h"
#endif

// Heap-allocated and deliberately never deleted, for the same reason as
// g_logWriter below: the message handler can still write to it after main()
// has returned (static destructors, plugin DLL unload), when a file-scope
// QFile would already have been destroyed. Every write is flushed, so
// leaking it loses nothing; the OS closes the handle at process exit.
QFile *g_logFile = nullptr;

// qInstallMessageHandler only accepts a plain (captureless) function pointer,
// so this has to be a file-scope global rather than captured. The handler
// itself just enqueues the message and returns - the actual (flushed,
// therefore blocking) file/stderr I/O happens on AsyncLogWriter's own
// background thread instead, so a busy debug session can't stall the
// calling (often UI/render) thread. Only constructed when -d is passed.
//
// Never deleted: the handler stays installed until the process exits, and
// destructors that run after main()'s event loop (App -> Doc ->
// IOPluginCache -> plugins, QApplication, static objects) and threads that
// are still winding down all log through it. Deleting the writer while the
// handler still points at it was a use-after-free on exit. Instead it is
// shut down (queue drained, worker joined) once App is gone, after which it
// writes every further message synchronously - see LogWriterShutdown in
// main().
AsyncLogWriter *g_logWriter = nullptr;

void writeLogMessage(const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();

    if (g_logFile != nullptr && g_logFile->isOpen())
    {
        g_logFile->write(localMsg);
        g_logFile->write((char *)"\n");
        g_logFile->flush();
    }

    fprintf(stderr, "%s\n", localMsg.constData());
    fflush(stderr);
}

/**
 * Prints the application version
 */
void printVersion()
{
    QTextStream cout(stdout, QIODevice::WriteOnly);

    cout << Qt::endl;
    cout << APPNAME << " " << "version " << APPVERSION << Qt::endl;
    cout << "This program is licensed under the terms of the ";
    cout << "Apache 2.0 license." << Qt::endl;
    cout << "Copyright (c) Heikki Junnila (hjunnila@users.sf.net)" << Qt::endl;
    cout << "Copyright (c) Massimo Callegari (massimocallegari@yahoo.it)" << Qt::endl;
    cout << Qt::endl;
}

int main(int argc, char *argv[])
{
    SlowClickApplication app(argc, argv);

    // Since Qt6, the default rendering backend is Rhi.
    // QLC+ doesn't support it yet so OpenGL have to be forced.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    qputenv("QT3D_RENDERER", "opengl");

    QApplication::setOrganizationName("qlcplus");
    QApplication::setOrganizationDomain("qlcplus.org");
    QApplication::setApplicationName(APPNAME);
    QApplication::setApplicationVersion(QString(APPVERSION));

    /* QLCPLUS_SETTINGS_DIR: keep every QSettings() of this process in an .ini file under that
     * folder instead of the per-user registry / plist / config file that every other QLC+ on the
     * machine shares. dev-webui-sandbox.ps1 sets it so a throwaway test instance can change
     * application settings (audio devices, UI settings, ...) without touching the real ones. */
    const QByteArray settingsDir = qgetenv("QLCPLUS_SETTINGS_DIR");
    if (settingsDir.isEmpty() == false)
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromLocal8Bit(settingsDir));
    }

    printVersion();

    QCommandLineParser parser;
    parser.setApplicationDescription("Q Light Controller Plus");

    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption openFileOption(QStringList() << "o" << "open",
                                      "Specify a file to open.");
    parser.addOption(openFileOption);

    parser.addPositionalArgument("file", "File to open.", "[file]");

    QCommandLineOption openLastOption(QStringList() << "9" << "openlast",
                                      "Open the file from last session.");
    parser.addOption(openLastOption);

    QCommandLineOption fullscreenOption(QStringList() << "f" << "fullscreen",
                                        "Start the application in fullscreen mode");
    parser.addOption(fullscreenOption);

    QCommandLineOption kioskOption(QStringList() << "k" << "kiosk",
                                      "Enable kiosk mode (only Virtual Console)");
    parser.addOption(kioskOption);

    QCommandLineOption localeOption(QStringList() << "l" << "locale",
                                      "Specify a language to use.",
                                      "locale", "");
    parser.addOption(localeOption);

    QCommandLineOption debugOption(QStringList() << "d" << "debug",
                                   "Enable debug messages.");
    parser.addOption(debugOption);

    QCommandLineOption logOption(QStringList() << "g" << "log",
                                   "Log debug messages to a file.");
    parser.addOption(logOption);

    QCommandLineOption threedSupportOption(QStringList() << "3" << "no3d",
                                      "Disable the 3D preview.");
    parser.addOption(threedSupportOption);

    QCommandLineOption noWmOption(QStringList() << "m" << "nowm",
                                  "The OS doesn't provide a window manager");
    parser.addOption(noWmOption);

    QCommandLineOption webAccessOption(QStringList() << "w" << "web",
                                      "Enable remote web access");
    parser.addOption(webAccessOption);

    QCommandLineOption webPortOption(QStringList() << "wp" << "web-port",
                                      "Set the port to use for web access",
                                      "port", "");
    parser.addOption(webPortOption);

    QCommandLineOption webAuthOption(QStringList() << "wa" << "web-auth",
                                      "Enable remote web access with users authentication");
    parser.addOption(webAuthOption);

    QCommandLineOption webAuthFileOption(QStringList() << "a" << "web-auth-file",
                                      "Specify a file where to store web access basic authentication credentials",
                                      "file", "");
    parser.addOption(webAuthFileOption);

    QCommandLineOption remoteOption(QStringList() << "s" << "server",
                                      "Enable the native network server");
    parser.addOption(remoteOption);

    QCommandLineOption allowAllNativeOption(QStringList() << "sa" << "server-allow-all",
        "Automatically grant full access to every native TCP client (unsafe on untrusted networks)");
    parser.addOption(allowAllNativeOption);

    QCommandLineOption apiOption(QStringList() << "api",
                                  "Enable the WebSocket control API (docs/api-spec/)");
    parser.addOption(apiOption);

    QCommandLineOption apiPortOption(QStringList() << "api-port",
                                      "Set the port to use for the WebSocket control API",
                                      "port", "");
    parser.addOption(apiPortOption);

    // Browser-based web UI (docs/webui.md). Named "webui", not "web": -w/--web
    // and -wp/--web-port above already belong to the legacy webaccess remote.
    QCommandLineOption webUiOption(QStringList() << "webui",
                                   "Serve the browser-based web UI over HTTP (docs/webui.md). Implies --api.");
    parser.addOption(webUiOption);

    QCommandLineOption webUiPortOption(QStringList() << "webui-port",
                                       "Set the port for the web UI's HTTP server (default 9011). Implies --webui.",
                                       "port", "");
    parser.addOption(webUiPortOption);

    QCommandLineOption webUiRootOption(QStringList() << "webui-root",
                                       "Serve the web UI from this directory instead of the installed one "
                                       "(development: point it at the repository's webui/). Implies --webui.",
                                       "dir", "");
    parser.addOption(webUiRootOption);

    parser.process(app);

    bool enableWebAccess = parser.isSet(webAccessOption)
        || parser.isSet(webPortOption)
        || parser.isSet(webAuthOption)
        || parser.isSet(webAuthFileOption);
    bool enableWebAuth = parser.isSet(webAuthOption);
    int webAccessPort = parser.value(webPortOption).toInt();
    QString webAccessPasswordFile = parser.value(webAuthFileOption);
    bool allowAllNative = parser.isSet(allowAllNativeOption);
    bool enableNativeServer = parser.isSet(remoteOption) || allowAllNative;
    bool enableWebUi = parser.isSet(webUiOption)
        || parser.isSet(webUiPortOption)
        || parser.isSet(webUiRootOption);
    int webUiPort = parser.value(webUiPortOption).toInt();
    QString webUiRoot = parser.value(webUiRootOption);
    // The web UI is a client of the WebSocket control API - useless without it
    bool enableApi = parser.isSet(apiOption) || parser.isSet(apiPortOption) || enableWebUi;
    int apiPort = parser.value(apiPortOption).toInt();

#if !defined Q_OS_ANDROID
    // 3D enablement
    if (!parser.isSet(threedSupportOption))
    {
        QSurfaceFormat format;
        format.setMajorVersion(3);
        format.setMinorVersion(3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        QSurfaceFormat::setDefaultFormat(format);
    }
#endif

    if (parser.isSet(noWmOption))
        QLCFile::setHasWindowManager(false);

    if (parser.isSet(logOption))
    {
        QString logFilename = QDir::homePath() + QDir::separator() + "QLC+.log";
        g_logFile = new QFile(logFilename);
        if (!g_logFile->open(QIODevice::Append))
            qWarning("Warning: Unable to open log file.");
    }

    // logging option
    if (parser.isSet(debugOption))
    {
        g_logWriter = new AsyncLogWriter(writeLogMessage);
        qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext &, const QString &msg) {
                if (g_logWriter == nullptr)
                    return;
                // Qt aborts the process right after a fatal message (and the
                // crash handler's dialog blocks before that), so don't leave
                // it - or anything queued before it - sitting in the async
                // queue: drain, then write it synchronously.
                if (type == QtFatalMsg)
                    g_logWriter->shutdown();
                g_logWriter->enqueue(msg);
        });
    }

    // Crash reporter (see qmlui/crashhandler.h). Installed after the -d log
    // handler above on purpose: it wraps whatever message handler is current
    // and chains every message to it, only adding the report on QtFatalMsg.
    CrashHandler::install();

    // Declared before App (and FreezeWatchdog) on purpose: locals are
    // destroyed in reverse order, so this runs right after ~App() - after
    // Doc, the I/O plugins and their threads have been torn down and have
    // logged their goodbyes into the async queue. It drains that queue and
    // stops the worker; from then on the writer logs synchronously, which
    // covers ~QApplication and anything later. The writer itself is never
    // deleted (see g_logWriter).
    struct LogWriterShutdown
    {
        ~LogWriterShutdown()
        {
            if (g_logWriter != nullptr)
                g_logWriter->shutdown();
        }
    } logWriterShutdown;

    // language settings
    QString locale = parser.value(localeOption);

    App qlcplusApp;
    if (locale.isEmpty())
    {
        QSettings settings;
        QVariant language = settings.value(SETTINGS_LANGUAGE);
        if (language.isValid())
            locale = language.toString();
    }
    qlcplusApp.setLanguage(locale);

    if (parser.isSet(threedSupportOption))
        qlcplusApp.set3dSupported(false);

    // kiosk mode
    if (parser.isSet(kioskOption))
        qlcplusApp.enableKioskMode();

    qlcplusApp.startup();

    // open file
    QString filename;
    QStringList posArgs = parser.positionalArguments();
    if (!posArgs.isEmpty())
        filename = posArgs.first();

    if (filename.isEmpty() == false)
    {
        if (filename.endsWith(KExtFixture))
            qlcplusApp.loadFixture(filename);
        else
            qlcplusApp.loadWorkspace(filename);
    }

    // open last file
    if (parser.isSet(openLastOption))
        qlcplusApp.loadLastWorkspace();

    if ((enableWebAccess || enableNativeServer) && qlcplusApp.networkManager() != nullptr)
    {
        NetworkManager *netMgr = qlcplusApp.networkManager();
        netMgr->setAllowAllNative(allowAllNative);
        if (allowAllNative)
        {
            qCritical().noquote()
                << "WARNING: --server-allow-all grants full QLC+ control to every native client, including LAN clients. Keep TCP port 9998 firewalled or use only a trusted network.";
        }
        int forcedTypes = NetworkManager::NoServer;

        if (enableWebAccess)
        {
            netMgr->setWebServerConfiguration(webAccessPort, enableWebAuth, webAccessPasswordFile);
            forcedTypes |= NetworkManager::WebServer;
        }

        if (enableNativeServer)
            forcedTypes |= NetworkManager::NativeServer;

        netMgr->setForcedServerTypes(forcedTypes);
        netMgr->startServer();
    }

    // Both started through App so the QML side (App::webUiUrl, the actions
    // menu's "Open web UI" entry) is notified, and so that entry can start
    // them the same way at runtime when none of these flags was given
    if (enableApi)
        qlcplusApp.startApiServer(apiPort > 0 ? quint16(apiPort) : quint16(API_SERVER_DEFAULT_PORT));

    if (enableWebUi)
        qlcplusApp.startWebUiServer(webUiPort > 0 ? quint16(webUiPort) : quint16(WEB_SERVER_DEFAULT_PORT),
                                    webUiRoot);

    // fullscreen mode
    if (parser.isSet(fullscreenOption))
        qlcplusApp.toggleFullscreen();

    // Freeze/hang watchdog (docs/agent-reports/2026-08-29-crash-freeze-diagnostics-options.md,
    // option F3). Started right here, immediately before the event loop
    // starts running, so that whatever synchronous startup/project-loading
    // work happened above (which can legitimately take a while for a big
    // .qxw) is never counted against the freeze threshold - only stalls of
    // the *running* event loop are.
    FreezeWatchdog freezeWatchdog;
    freezeWatchdog.start();

#ifdef Q_OS_WIN
    // Dev-only: deliberately hang the main thread to verify the watchdog
    // above actually fires end-to-end. Gated behind an env var so it can
    // never trigger outside a manual test; never wired to any UI/flag.
    if (qEnvironmentVariableIsSet("QLCPLUS_DEBUG_FREEZE"))
    {
        int secs = qEnvironmentVariableIntValue("QLCPLUS_DEBUG_FREEZE");
        if (secs <= 0)
            secs = 20;
        QTimer::singleShot(3000, &app, [secs]() { FreezeWatchdog::debugBlockMainThread(secs); });
    }

    // Dev-only: deliberately crash (QLCPLUS_DEBUG_CRASH=fatal|segv|abort) to
    // verify the crash reporter end-to-end. Same gating rationale as above.
    if (qEnvironmentVariableIsSet("QLCPLUS_DEBUG_CRASH"))
    {
        const QString mode = qEnvironmentVariable("QLCPLUS_DEBUG_CRASH");
        QTimer::singleShot(3000, &app, [mode]() { CrashHandler::debugTriggerCrash(mode); });
    }

#if defined(QLC_SPOUT)
    // Dev-only: prove the vendored Spout SDK end-to-end (docs/agent-reports/
    // 2026-09-15-spout-video-output-design.md, milestone 1). 3 s after
    // startup, register a Spout sender "QLC+ test" at 1280x720, publish a
    // transparent frame, then alternate every 2 s between a solid opaque
    // red frame and a transparent one until the app quits. With OBS's Spout2
    // source set to "Premultiplied Alpha" this shows nothing, then red,
    // then nothing again. Same gating rationale as the two hooks above;
    // never wired to any UI/flag.
    if (qEnvironmentVariableIsSet("QLCPLUS_DEBUG_SPOUT"))
    {
        QTimer::singleShot(3000, &app, [&app]() {
            auto sender = std::make_shared<SpoutSender>();
            const QSize size(1280, 720);

            if (sender->create("QLC+ test", size.width(), size.height()) == false)
            {
                qWarning() << "[Spout] test hook: failed to create sender";
                return;
            }

            qDebug().noquote() << "[Spout] created sender" << sender->name()
                               << "at" << sender->size().width() << "x" << sender->size().height()
                               << "- sent initial transparent frame";
            qDebug().noquote() << "[Spout] active senders:" << sender->activeSenders().join(", ");

            QImage red(size, QImage::Format_ARGB32_Premultiplied);
            red.fill(QColor(255, 0, 0, 255));

            QTimer *timer = new QTimer(&app);
            timer->setInterval(2000);
            QObject::connect(timer, &QTimer::timeout, &app, [sender, red, showRed = true]() mutable {
                if (showRed)
                {
                    sender->sendImage(red);
                    qDebug().noquote() << "[Spout] sent solid red frame (alpha 255) on" << sender->name();
                }
                else
                {
                    sender->sendTransparent();
                    qDebug().noquote() << "[Spout] sent transparent frame on" << sender->name();
                }
                showRed = !showRed;
            });
            timer->start();

            QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [sender, timer]() {
                timer->stop();
                sender->release();
                qDebug().noquote() << "[Spout] test hook: sender released";
            });
        });
    }
#endif
#endif

    // No log writer cleanup here: App is still alive at this point and its
    // teardown logs (see LogWriterShutdown above)
    return app.exec();
}
