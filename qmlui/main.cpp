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
#include "freezewatchdog.h"
#include "networkmanager.h"
#include "apiserver.h"
#include "qlcconfig.h"
#include "qlcfile.h"

QFile logFile;

// qInstallMessageHandler only accepts a plain (captureless) function pointer,
// so this has to be a file-scope global rather than captured. The handler
// itself just enqueues the message and returns - the actual (flushed,
// therefore blocking) file/stderr I/O happens on AsyncLogWriter's own
// background thread instead, so a busy debug session can't stall the
// calling (often UI/render) thread. Only constructed when -d is passed.
AsyncLogWriter *g_logWriter = nullptr;

void writeLogMessage(const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();

    if (logFile.isOpen())
    {
        logFile.write(localMsg);
        logFile.write((char *)"\n");
        logFile.flush();
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
    QApplication app(argc, argv);

    // Since Qt6, the default rendering backend is Rhi.
    // QLC+ doesn't support it yet so OpenGL have to be forced.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGLRhi);
    qputenv("QT3D_RENDERER", "opengl");

    QApplication::setOrganizationName("qlcplus");
    QApplication::setOrganizationDomain("qlcplus.org");
    QApplication::setApplicationName(APPNAME);
    QApplication::setApplicationVersion(QString(APPVERSION));

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
    bool enableApi = parser.isSet(apiOption) || parser.isSet(apiPortOption);
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
        logFile.setFileName(logFilename);
        if (!logFile.open(QIODevice::Append))
            qWarning("Warning: Unable to open log file.");
    }

    // logging option
    if (parser.isSet(debugOption))
    {
        g_logWriter = new AsyncLogWriter(writeLogMessage);
        qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &msg) {
                if (g_logWriter)
                    g_logWriter->enqueue(msg);
        });
    }

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

    if (enableApi && qlcplusApp.apiServer() != nullptr)
    {
        ApiServer *apiSrv = qlcplusApp.apiServer();
        quint16 port = apiPort > 0 ? quint16(apiPort) : quint16(API_SERVER_DEFAULT_PORT);
        if (apiSrv->listen(port) == false)
            qCritical().noquote() << "Could not start the WebSocket control API:" << apiSrv->errorString();
        else
            qInfo().noquote() << "WebSocket control API listening on port" << port;
    }

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
#endif

    int result = app.exec();
    delete g_logWriter; // destructor drains the queue and joins the thread
    return result;
}
