/*
  Q Light Controller Plus - Control API
  apimediadomain.cpp

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

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QVector3D>
#include <QRect>
#include <limits>

#include "apimediadomain.h"
#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "scriptwrapper.h"
#include "audio.h"
#include "audioplugincache.h"
#include "audiorenderer.h"
#include "video.h"
#include "function.h"
#include "doc.h"

namespace {

QJsonObject docRevisionResult(Doc *doc)
{
    QJsonObject result;
    result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return result;
}

QJsonObject conflictDetails(Doc *doc)
{
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return details;
}

QString functionIdString(Function *function)
{
    return QString::number(function->id());
}

// "Uncaught exception at line 3. SyntaxError: ..." (ScriptRunner::collectScriptData)
const QRegularExpression &syntaxErrorPattern()
{
    static const QRegularExpression re(QStringLiteral("^Uncaught exception at line (\\d+)\\.\\s*(.*)$"),
                                       QRegularExpression::DotMatchesEverythingOption);
    return re;
}

/** functions.script.validate's result / FunctionsScriptDetail's error part */
void scriptErrorsToJson(Script *script, QJsonObject &obj)
{
    QJsonArray lines;
    QJsonArray errors;
#ifdef QMLUI
    // scriptv4: one human-readable message per error, line embedded in it
    const QStringList messages = script->syntaxErrorsLines();
    for (const QString &message : messages)
    {
        int line = 0;
        QString text = message;
        QRegularExpressionMatch m = syntaxErrorPattern().match(message);
        if (m.hasMatch())
        {
            line = m.captured(1).toInt();
            text = m.captured(2);
        }
        lines.append(line);
        QJsonObject err;
        err.insert(QStringLiteral("line"), line);
        err.insert(QStringLiteral("message"), text);
        errors.append(err);
    }
#else
    // legacy line-command Script: 1-based line numbers, no message text
    const QList<int> errLines = script->syntaxErrorsLines();
    for (int line : errLines)
    {
        lines.append(line);
        QJsonObject err;
        err.insert(QStringLiteral("line"), line);
        err.insert(QStringLiteral("message"), QStringLiteral("Unrecognized command"));
        errors.append(err);
    }
#endif
    obj.insert(QStringLiteral("syntaxErrorLines"), lines);
    obj.insert(QStringLiteral("syntaxErrors"), errors);
}

QJsonArray scriptFunctionRefsToJson(Script *script)
{
    QJsonArray refs;
    const QList<quint32> list = script->functionList(); // id, line, id, line, ...
    for (int i = 0; i + 1 < list.count(); i += 2)
    {
        QJsonObject ref;
        ref.insert(QStringLiteral("functionId"), QString::number(list.at(i)));
        ref.insert(QStringLiteral("line"), int(list.at(i + 1)));
        refs.append(ref);
    }
    return refs;
}

QJsonArray scriptFixtureRefsToJson(Script *script)
{
    QJsonArray refs;
    const QList<quint32> list = script->fixtureList();
#ifdef QMLUI
    // scriptv4 reports ids only
    for (quint32 fxId : list)
    {
        QJsonObject ref;
        ref.insert(QStringLiteral("fixtureId"), QString::number(fxId));
        refs.append(ref);
    }
#else
    for (int i = 0; i + 1 < list.count(); i += 2)
    {
        QJsonObject ref;
        ref.insert(QStringLiteral("fixtureId"), QString::number(list.at(i)));
        ref.insert(QStringLiteral("line"), int(list.at(i + 1)));
        refs.append(ref);
    }
#endif
    return refs;
}

QJsonObject snippet(const QString &label, const QString &insert, const QString &icon, int caretOffset = 2)
{
    QJsonObject s;
    s.insert(QStringLiteral("label"), label);
    s.insert(QStringLiteral("insert"), insert);
    s.insert(QStringLiteral("caretOffset"), caretOffset);
    s.insert(QStringLiteral("icon"), icon);
    return s;
}

QJsonObject configOf(Function *function)
{
    return ApiFunctionsDomain::typeDetail(function).value(QStringLiteral("config")).toObject();
}

QString outputModeToString(Video::OutputMode mode)
{
    switch (mode)
    {
    case Video::Fullscreen: return QStringLiteral("fullscreen");
    case Video::Spout:      return QStringLiteral("spout");
    default:                return QStringLiteral("windowed");
    }
}

} // namespace

ApiMediaDomain::ApiMediaDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    ApiFunctionsDomain::setTypeDetailProvider(Function::ScriptType, [doc](Function *function)
    {
        return scriptDetailToJson(doc, static_cast<Script *>(function));
    });

    registerScriptMethods();
    registerAudioMethods();
    registerVideoMethods();
}

QJsonObject ApiMediaDomain::scriptDetailToJson(Doc *doc, Script *script)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("functionId"), functionIdString(script));
    obj.insert(QStringLiteral("source"), script->data());
    scriptErrorsToJson(script, obj);
    obj.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    return obj;
}

Function *ApiMediaDomain::requireFunctionOfType(ApiSession *session, const QString &id, const QJsonObject &params, int type) const
{
    QJsonValue value = params.value(QStringLiteral("functionId"));
    if (value.isUndefined())
        value = params.value(QStringLiteral("id"));

    bool ok = false;
    quint32 functionId = 0;
    if (value.isDouble())
    {
        double d = value.toDouble();
        ok = d >= 0 && d <= double(std::numeric_limits<quint32>::max());
        functionId = ok ? quint32(d) : 0;
    }
    else
    {
        functionId = value.toString().toUInt(&ok);
    }

    Function *function = ok ? m_doc->function(functionId) : nullptr;
    if (function == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such function")));
        return nullptr;
    }
    if (function->type() != type)
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                        QStringLiteral("Function %1 is a %2, not a %3")
                                                            .arg(functionId)
                                                            .arg(Function::typeToString(function->type()),
                                                                 Function::typeToString(Function::Type(type)))));
        return nullptr;
    }
    return function;
}

bool ApiMediaDomain::checkBaseRevision(ApiSession *session, const QString &id, const QJsonObject &params) const
{
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision != m_doc->docRevision())
    {
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale"), conflictDetails(m_doc)));
        return false;
    }
    return true;
}

/*****************************************************************************
 * Script
 *****************************************************************************/

void ApiMediaDomain::registerScriptMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // Static keyword catalog + the editor's insert-at-cursor menu
    // (qmlui/qml/fixturesfunctions/ScriptEditor.qml, addMethodMenu).
    dispatcher->registerMethod(QStringLiteral("functions.script.listCommands"), [](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray commands;
#ifdef QMLUI
        commands << Script::startFunctionCmd << Script::stopFunctionCmd
                 << QStringLiteral("Engine.isFunctionRunning") << QStringLiteral("Engine.setFunctionAttribute")
                 << Script::setFixtureCmd << QStringLiteral("Engine.getChannelValue")
                 << Script::waitCmd << Script::waitFunctionStartCmd << Script::waitFunctionStopCmd
                 << Script::blackoutCmd << Script::stopOnExitCmd << Script::systemCmd
                 << QStringLiteral("Engine.setBPM") << QStringLiteral("Engine.random");
        QJsonArray snippets;
        snippets << snippet(QStringLiteral("Start function"), QStringLiteral("Engine.startFunction();"), QStringLiteral("play"))
                 << snippet(QStringLiteral("Stop function"), QStringLiteral("Engine.stopFunction();"), QStringLiteral("stop"))
                 << snippet(QStringLiteral("Set fixture channel"), QStringLiteral("Engine.setFixture();"), QStringLiteral("sliders"))
                 << snippet(QStringLiteral("Wait time"), QStringLiteral("Engine.waitTime();"), QStringLiteral("hourglass"))
                 << snippet(QStringLiteral("Random number"), QStringLiteral("Engine.random();"), QStringLiteral("dice"))
                 << snippet(QStringLiteral("Blackout"), QStringLiteral("Engine.setBlackout();"), QStringLiteral("moon"))
                 << snippet(QStringLiteral("System command"), QStringLiteral("Engine.systemCommand();"), QStringLiteral("terminal"));
#else
        commands << Script::stopOnExitCmd << Script::startFunctionCmd << Script::stopFunctionCmd
                 << Script::blackoutCmd << Script::waitCmd << Script::waitKeyCmd
                 << Script::waitFunctionStartCmd << Script::waitFunctionStopCmd << Script::setFixtureCmd
                 << Script::systemCmd << Script::labelCmd << Script::jumpCmd;
        QJsonArray snippets;
        snippets << snippet(QStringLiteral("Start function"), Script::startFunctionCmd + QStringLiteral(":"), QStringLiteral("play"), 0)
                 << snippet(QStringLiteral("Stop function"), Script::stopFunctionCmd + QStringLiteral(":"), QStringLiteral("stop"), 0)
                 << snippet(QStringLiteral("Set fixture channel"), Script::setFixtureCmd + QStringLiteral(":"), QStringLiteral("sliders"), 0)
                 << snippet(QStringLiteral("Wait time"), Script::waitCmd + QStringLiteral(":"), QStringLiteral("hourglass"), 0)
                 << snippet(QStringLiteral("Blackout"), Script::blackoutCmd + QStringLiteral(":"), QStringLiteral("moon"), 0)
                 << snippet(QStringLiteral("System command"), Script::systemCmd + QStringLiteral(":"), QStringLiteral("terminal"), 0);
#endif
        QJsonObject result;
        result.insert(QStringLiteral("commands"), commands);
        result.insert(QStringLiteral("snippets"), snippets);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.script.validate"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::ScriptType);
        if (function == nullptr)
            return;
        Script *script = static_cast<Script *>(function);

        QJsonObject result;
        scriptErrorsToJson(script, result);
        result.insert(QStringLiteral("functionRefs"), scriptFunctionRefsToJson(script));
        result.insert(QStringLiteral("fixtureRefs"), scriptFixtureRefsToJson(script));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.script.setSource"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::ScriptType);
        if (function == nullptr)
            return;
        if (params.value(QStringLiteral("source")).isString() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("source must be a string")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Script *script = static_cast<Script *>(function);
        QString source = params.value(QStringLiteral("source")).toString();
#ifdef QMLUI
        // scriptv4's setData() calls Doc::setModified() itself, but only
        // when the text actually changed - an identical body is a no-op.
        script->setData(source);
#else
        script->setData(source);
        doc->setModified();
#endif

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("source"), script->data());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.script.sourceChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.script.appendLine"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::ScriptType);
        if (function == nullptr)
            return;
        if (params.value(QStringLiteral("line")).isString() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("line must be a string")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Script *script = static_cast<Script *>(function);
        // appendData() does not bump the revision on its own (on scriptv4 it
        // also runs the legacy-syntax converter on the line)
        script->appendData(params.value(QStringLiteral("line")).toString());
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("source"), script->data());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.script.sourceChanged"), data, session->clientId(), false);
    });
}

/*****************************************************************************
 * Audio
 *****************************************************************************/

void ApiMediaDomain::registerAudioMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.audio.listCapabilities"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray extensions;
        for (const QString &ext : doc->audioPluginCache()->getSupportedFormats())
            extensions.append(ext);

        // Output devices, the way AudioEditor.qml's combo lists them:
        // "Default device" (empty id) first, then every AUDIO_CAP_OUTPUT
        // device by its private name.
        QJsonArray devices;
        QJsonObject def;
        def.insert(QStringLiteral("id"), QString());
        def.insert(QStringLiteral("name"), QStringLiteral("Default device"));
        devices.append(def);
        const QList<AudioDeviceInfo> devList = doc->audioPluginCache()->audioDevicesList();
        for (const AudioDeviceInfo &info : devList)
        {
            if ((info.capabilities & AUDIO_CAP_OUTPUT) == 0)
                continue;
            QJsonObject dev;
            dev.insert(QStringLiteral("id"), info.privateName);
            dev.insert(QStringLiteral("name"), info.deviceName);
            devices.append(dev);
        }

        QJsonObject result;
        result.insert(QStringLiteral("extensions"), extensions);
        result.insert(QStringLiteral("devices"), devices);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.audio.setSource"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::AudioType);
        if (function == nullptr)
            return;
        QString source = params.value(QStringLiteral("sourceFileName")).toString();
        if (source.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("sourceFileName is required")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        QString error;
        if (ApiFunctionsDomain::applyMediaSource(doc, function, source, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        Audio *audio = static_cast<Audio *>(function);
        QJsonObject detail = ApiFunctionsDomain::typeDetail(function);
        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("sourceFileName"), detail.value(QStringLiteral("source")));
        data.insert(QStringLiteral("detectedDurationMs"), double(audio->totalDuration()));
        data.insert(QStringLiteral("name"), function->name());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.audio.sourceChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.audio.setVolume"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::AudioType);
        if (function == nullptr)
            return;
        QJsonValue v = params.value(QStringLiteral("volume"));
        if (v.isDouble() == false || v.toDouble() < 0.0 || v.toDouble() > 1.0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("volume must be a number in 0..1")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Audio *audio = static_cast<Audio *>(function);
        audio->setVolume(v.toDouble()); // no changed() from the engine setter
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("volume"), audio->volume());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.audio.volumeChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.audio.setDuration"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::AudioType);
        if (function == nullptr)
            return;
        QJsonValue v = params.value(QStringLiteral("duration"));
        if (v.isDouble() == false || v.toDouble() < 0.0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("duration must be a non-negative integer (ms)")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Audio *audio = static_cast<Audio *>(function);
        quint32 ms = quint32(v.toDouble());
        // Both: the persisted Speed/Duration and the playback length the
        // renderer stops at (Audio::setSourceFileName sets the pair too)
        audio->setDuration(ms);
        audio->setTotalDuration(ms);
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("duration"), double(audio->totalDuration()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.audio.durationChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.audio.setDevice"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::AudioType);
        if (function == nullptr)
            return;
        QJsonValue v = params.value(QStringLiteral("audioDevice"));
        if (v.isString() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("audioDevice must be a string (empty = default output)")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Audio *audio = static_cast<Audio *>(function);
        audio->setAudioDevice(v.toString()); // no changed() from the engine setter
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("audioDevice"), audio->audioDevice());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.audio.deviceChanged"), data, session->clientId(), false);
    });
}

/*****************************************************************************
 * Video
 *****************************************************************************/

void ApiMediaDomain::registerVideoMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.video.listCapabilities"), [](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonArray videoExt;
        for (const QString &ext : Video::getVideoCapabilities())
            videoExt.append(ext);
        QJsonArray pictureExt;
        for (const QString &ext : Video::getPictureCapabilities())
            pictureExt.append(ext);

        // Displays of the engine host - only meaningful with a
        // QGuiApplication (the test suites run a bare QCoreApplication and
        // get an empty list, like a headless engine would).
        QJsonArray screens;
        if (qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr)
        {
            int i = 0;
            const QList<QScreen *> list = QGuiApplication::screens();
            for (QScreen *screen : list)
            {
                QJsonObject s;
                s.insert(QStringLiteral("index"), i++);
                s.insert(QStringLiteral("name"), screen->name());
                QJsonObject g;
                g.insert(QStringLiteral("x"), screen->geometry().x());
                g.insert(QStringLiteral("y"), screen->geometry().y());
                g.insert(QStringLiteral("width"), screen->geometry().width());
                g.insert(QStringLiteral("height"), screen->geometry().height());
                s.insert(QStringLiteral("geometry"), g);
                screens.append(s);
            }
        }

        QJsonObject result;
        result.insert(QStringLiteral("videoExtensions"), videoExt);
        result.insert(QStringLiteral("pictureExtensions"), pictureExt);
        result.insert(QStringLiteral("screens"), screens);
        // qmlui's Spout output (qmlui/CMakeLists.txt) exists on every
        // Windows qmlui build and nowhere else.
#ifdef Q_OS_WIN
        result.insert(QStringLiteral("spoutAvailable"), true);
#else
        result.insert(QStringLiteral("spoutAvailable"), false);
#endif
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    dispatcher->registerMethod(QStringLiteral("functions.video.setSource"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::VideoType);
        if (function == nullptr)
            return;
        QString source = params.value(QStringLiteral("sourceUrl")).toString();
        if (source.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("sourceUrl is required")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        QString error;
        if (ApiFunctionsDomain::applyMediaSource(doc, function, source, &error) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        Video *video = static_cast<Video *>(function);
        QJsonObject detail = ApiFunctionsDomain::typeDetail(function);
        QJsonObject cfg = detail.value(QStringLiteral("config")).toObject();
        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("sourceUrl"), detail.value(QStringLiteral("source")));
        data.insert(QStringLiteral("isPicture"), video->isPicture());
        if (cfg.contains(QStringLiteral("resolution")))
            data.insert(QStringLiteral("detectedResolution"), cfg.value(QStringLiteral("resolution")));
        data.insert(QStringLiteral("detectedDurationMs"), double(video->totalDuration()));
        if (video->videoCodec().isEmpty() == false)
            data.insert(QStringLiteral("videoCodec"), video->videoCodec());
        if (video->audioCodec().isEmpty() == false)
            data.insert(QStringLiteral("audioCodec"), video->audioCodec());
        data.insert(QStringLiteral("name"), function->name());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.video.sourceChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.video.setGeometry"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::VideoType);
        if (function == nullptr)
            return;
        QJsonValue g = params.value(QStringLiteral("customGeometry"));
        if (g.isUndefined() == false && g.isNull() == false && g.isObject() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("customGeometry must be an object, null, or omitted")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Video *video = static_cast<Video *>(function);
        if (g.isUndefined() == false)
        {
            QRect rect;
            if (g.isObject())
            {
                QJsonObject o = g.toObject();
                rect = QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(),
                             o.value(QStringLiteral("width")).toInt(), o.value(QStringLiteral("height")).toInt());
            }
            video->setCustomGeometry(rect); // no changed() from the engine setter
            doc->setModified();
        }

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("customGeometry"), configOf(function).value(QStringLiteral("customGeometry")));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.video.geometryChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.video.setRotation"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::VideoType);
        if (function == nullptr)
            return;
        QJsonValue r = params.value(QStringLiteral("rotation"));
        if (r.isObject() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("rotation {x, y, z} is required")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Video *video = static_cast<Video *>(function);
        QJsonObject o = r.toObject();
        QVector3D current = video->rotation();
        QVector3D rotation(float(o.value(QStringLiteral("x")).toDouble(current.x())),
                           float(o.value(QStringLiteral("y")).toDouble(current.y())),
                           float(o.value(QStringLiteral("z")).toDouble(current.z())));
        video->setRotation(rotation); // no changed() from the engine setter
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("rotation"), configOf(function).value(QStringLiteral("rotation")));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.video.rotationChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.video.setLayer"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::VideoType);
        if (function == nullptr)
            return;
        QJsonValue z = params.value(QStringLiteral("zIndex"));
        if (z.isDouble() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("zIndex must be an integer")));
            return;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Video *video = static_cast<Video *>(function);
        video->setZIndex(z.toInt()); // no changed() from the engine setter
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("zIndex"), video->zIndex());
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.video.layerChanged"), data, session->clientId(), false);
    });

    dispatcher->registerMethod(QStringLiteral("functions.video.setScreenTarget"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = requireFunctionOfType(session, id, params, Function::VideoType);
        if (function == nullptr)
            return;
        QJsonValue screen = params.value(QStringLiteral("screen"));
        QJsonValue fullscreen = params.value(QStringLiteral("fullscreen"));
        QJsonValue modeValue = params.value(QStringLiteral("outputMode"));
        if (screen.isDouble() == false || screen.toInt() < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("screen must be a non-negative integer")));
            return;
        }
        if (fullscreen.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("fullscreen must be a boolean")));
            return;
        }
        Video::OutputMode mode = Video::Windowed;
        bool haveMode = false;
        if (modeValue.isUndefined() == false)
        {
            QString m = modeValue.toString();
            if (m == QStringLiteral("windowed"))
                mode = Video::Windowed;
            else if (m == QStringLiteral("fullscreen"))
                mode = Video::Fullscreen;
            else if (m == QStringLiteral("spout"))
                mode = Video::Spout;
            else
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("outputMode must be windowed, fullscreen or spout")));
                return;
            }
#ifndef Q_OS_WIN
            if (mode == Video::Spout)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported, QStringLiteral("Spout output is not available on this build")));
                return;
            }
#endif
            haveMode = true;
        }
        if (checkBaseRevision(session, id, params) == false)
            return;

        Video *video = static_cast<Video *>(function);
        video->setScreen(screen.toInt()); // emits changed() -> revision bump
        if (haveMode)
            video->setOutputMode(mode);
        else if (fullscreen.toBool() != video->fullscreen())
            // only when it differs: a Spout video must not be dropped back
            // to windowed by a client that only knows the two-state flag
            video->setFullscreen(fullscreen.toBool());

        session->send(ApiEnvelope::buildOkResponse(id, docRevisionResult(doc)));

        QJsonObject data;
        data.insert(QStringLiteral("functionId"), functionIdString(function));
        data.insert(QStringLiteral("screen"), video->screen());
        data.insert(QStringLiteral("fullscreen"), video->fullscreen());
        data.insert(QStringLiteral("outputMode"), outputModeToString(video->outputMode()));
        data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        m_server->broadcast(QStringLiteral("functions.video.screenTargetChanged"), data, session->clientId(), false);
    });
}
