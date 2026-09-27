/*
  Q Light Controller Plus - Control API
  apiioconfigdomain.cpp

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

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaMethod>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <memory>

#include "apiioconfigdomain.h"
#include "apiiodomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "inputoutputmap.h"
#include "ioplugincache.h"
#include "qlcioplugin.h"
#include "qlcinputprofile.h"
#include "qlcinputchannel.h"
#include "grandmaster.h"
#include "outputpatch.h"
#include "inputpatch.h"
#include "universe.h"
#include "audioplugincache.h"
#include "audiocapture.h"
#include "audiorenderer.h"
#include "qlcfile.h"
#include "doc.h"

#define AUDIO_DEFAULT_DEVICE QStringLiteral("__qlcplusdefault__") // qmlui/inputoutputmanager.cpp's spelling

namespace {

/*********************************************************************
 * Shared request helpers - same contracts as apiiodomain.cpp's
 * anonymous-namespace helpers (kept file-local there; duplicated rather
 * than exported so the two files stay independently readable).
 *********************************************************************/

// §4a check: enforced only when the client sends baseRevision (see
// apiiodomain.cpp and io-notes.md's "deviation" note).
bool baseRevisionAccepted(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    if (params.contains(QStringLiteral("baseRevision")) == false)
        return true;
    quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
    if (baseRevision == doc->docRevision())
        return true;
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
    session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                    QStringLiteral("baseRevision is stale"), details));
    return false;
}

Universe *findUniverseParam(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    int universeId = params.value(QStringLiteral("universeId")).toInt(-1);
    Universe *universe = universeId >= 0 ? doc->inputOutputMap()->universe(quint32(universeId)) : nullptr;
    if (universe == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such universe")));
    return universe;
}

QLCIOPlugin *findPluginParam(Doc *doc, ApiSession *session, const QString &id, const QJsonObject &params)
{
    QString pluginName = params.value(QStringLiteral("pluginName")).toString();
    if (pluginName.isEmpty())
        pluginName = params.value(QStringLiteral("plugin")).toString();
    QLCIOPlugin *plugin = doc->ioPluginCache()->plugin(pluginName);
    if (plugin == nullptr)
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("No such plugin")));
    return plugin;
}

enum PatchDirection { PatchInput, PatchOutput, PatchFeedback };

bool parsePatchDirection(const QJsonObject &params, PatchDirection &direction)
{
    QString str = params.value(QStringLiteral("direction")).toString();
    if (str.isEmpty())
        str = params.value(QStringLiteral("patchType")).toString();
    if (str == QStringLiteral("input"))         direction = PatchInput;
    else if (str == QStringLiteral("output"))   direction = PatchOutput;
    else if (str == QStringLiteral("feedback")) direction = PatchFeedback;
    else return false;
    return true;
}

QJsonArray pluginLinesToJson(const QStringList &names, const QStringList &uids)
{
    QJsonArray arr;
    for (int i = 0; i < names.count(); i++)
    {
        QJsonObject line;
        line.insert(QStringLiteral("index"), i);
        line.insert(QStringLiteral("line"), i);
        line.insert(QStringLiteral("name"), names.at(i));
        line.insert(QStringLiteral("uid"), i < uids.count() ? uids.at(i) : QString());
        arr.append(line);
    }
    return arr;
}

QJsonObject pluginParametersToJson(const QMap<QString, QVariant> &parameters)
{
    QJsonObject obj;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it)
        obj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    return obj;
}

// JSON numbers arrive as doubles; plugins read their parameters with
// toInt()/toString() and persist them to the .qxw as text, so an integral
// number is handed over as an int to keep "6454" from becoming "6454.0".
QVariant parameterValueToVariant(const QJsonValue &value)
{
    if (value.isDouble())
    {
        double d = value.toDouble();
        if (d == double(qint64(d)))
            return QVariant(qint64(d));
    }
    return value.toVariant();
}

QString grandMasterChannelModeToJson(GrandMaster::ChannelMode mode)
{
    return mode == GrandMaster::AllChannels ? QStringLiteral("AllChannels") : QStringLiteral("Intensity");
}

QString grandMasterValueModeToJson(GrandMaster::ValueMode mode)
{
    return mode == GrandMaster::Reduce ? QStringLiteral("Reduce") : QStringLiteral("Limit");
}

QJsonObject grandMasterStateToJson(InputOutputMap *ioMap)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("value"), int(ioMap->grandMasterValue()));
    obj.insert(QStringLiteral("channelMode"), grandMasterChannelModeToJson(ioMap->grandMasterChannelMode()));
    obj.insert(QStringLiteral("valueMode"), grandMasterValueModeToJson(ioMap->grandMasterValueMode()));
    return obj;
}

/*********************************************************************
 * Input profiles <-> io.yaml IoInputProfile / IoInputChannel
 *********************************************************************/

// Spec spellings (IoInputChannel.type); QLCInputChannel::typeToString()
// gives the .qxi persistence form ("Next Page", "None"), accepted on input
// too so a client can echo either.
QString channelTypeToJson(QLCInputChannel::Type type)
{
    switch (type)
    {
    case QLCInputChannel::Slider:   return QStringLiteral("Slider");
    case QLCInputChannel::Knob:     return QStringLiteral("Knob");
    case QLCInputChannel::Encoder:  return QStringLiteral("Encoder");
    case QLCInputChannel::Button:   return QStringLiteral("Button");
    case QLCInputChannel::NextPage: return QStringLiteral("NextPage");
    case QLCInputChannel::PrevPage: return QStringLiteral("PrevPage");
    case QLCInputChannel::PageSet:  return QStringLiteral("PageSet");
    default:                        return QStringLiteral("NoType");
    }
}

bool channelTypeFromJson(const QString &str, QLCInputChannel::Type &type)
{
    if (str == QStringLiteral("Slider"))        type = QLCInputChannel::Slider;
    else if (str == QStringLiteral("Knob"))     type = QLCInputChannel::Knob;
    else if (str == QStringLiteral("Encoder"))  type = QLCInputChannel::Encoder;
    else if (str == QStringLiteral("Button"))   type = QLCInputChannel::Button;
    else if (str == QStringLiteral("NextPage")) type = QLCInputChannel::NextPage;
    else if (str == QStringLiteral("PrevPage")) type = QLCInputChannel::PrevPage;
    else if (str == QStringLiteral("PageSet"))  type = QLCInputChannel::PageSet;
    else if (str == QStringLiteral("NoType"))   type = QLCInputChannel::NoType;
    else
    {
        // .qxi spelling ("Next Page", ...) - stringToType() falls back to
        // NoType for anything unknown, so only accept a real match.
        QLCInputChannel::Type parsed = QLCInputChannel::stringToType(str);
        if (parsed == QLCInputChannel::NoType && str != QLCInputChannel::typeToString(QLCInputChannel::NoType))
            return false;
        type = parsed;
    }
    return true;
}

QJsonObject inputChannelToJson(quint32 number, const QLCInputChannel *ch)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("number"), int(number));
    obj.insert(QStringLiteral("name"), ch->name());
    obj.insert(QStringLiteral("type"), channelTypeToJson(ch->type()));
    obj.insert(QStringLiteral("movementType"), ch->movementType() == QLCInputChannel::Relative
               ? QStringLiteral("Relative") : QStringLiteral("Absolute"));
    obj.insert(QStringLiteral("movementSensitivity"), ch->movementSensitivity());
    obj.insert(QStringLiteral("sendExtraPress"), ch->sendExtraPress());
    obj.insert(QStringLiteral("lowerValue"), int(ch->lowerValue()));
    obj.insert(QStringLiteral("upperValue"), int(ch->upperValue()));
    // MIDI channel (QLCInputChannel::lowerChannel, the .qxi MidiChannel
    // attribute). io.yaml's upperChannel is declared by the engine header but
    // never defined, persisted or read anywhere - deliberately not exposed.
    obj.insert(QStringLiteral("lowerChannel"), ch->lowerChannel());
    return obj;
}

bool isUserProfilePath(const QString &path)
{
    // A profile that was never loaded from disk (created over the API and
    // not yet saved, or added by a test) counts as the user's own.
    if (path.isEmpty())
        return true;
    QString userDir = QDir::cleanPath(InputOutputMap::userProfileDirectory().absolutePath());
    return QDir::cleanPath(QFileInfo(path).absolutePath()).startsWith(userDir, Qt::CaseInsensitive);
}

// IoInputProfile plus three read-only conveniences: name (the
// "manufacturer model" key every other io.inputProfile.* method takes),
// path and isUser (ProfilesList.qml only offers delete for isUser).
QJsonObject inputProfileToJson(QLCInputProfile *profile)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), profile->name());
    obj.insert(QStringLiteral("manufacturer"), profile->manufacturer());
    obj.insert(QStringLiteral("model"), profile->model());
    obj.insert(QStringLiteral("type"), QLCInputProfile::typeToString(profile->type()));
    obj.insert(QStringLiteral("midiSendNoteOff"), profile->midiSendNoteOff());
    obj.insert(QStringLiteral("path"), profile->path());
    obj.insert(QStringLiteral("isUser"), isUserProfilePath(profile->path()));

    QJsonArray channels;
    QMap<quint32, QLCInputChannel *> map = profile->channels();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        channels.append(inputChannelToJson(it.key(), it.value()));
    obj.insert(QStringLiteral("channels"), channels);

    QJsonArray colors;
    QMap<uchar, QPair<QString, QColor>> colorTable = profile->colorTable();
    for (auto it = colorTable.constBegin(); it != colorTable.constEnd(); ++it)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("value"), int(it.key()));
        entry.insert(QStringLiteral("label"), it.value().first);
        entry.insert(QStringLiteral("color"), it.value().second.name());
        colors.append(entry);
    }
    obj.insert(QStringLiteral("colorTable"), colors);

    QJsonArray midiChannels;
    QMap<uchar, QString> midiTable = profile->midiChannelTable();
    for (auto it = midiTable.constBegin(); it != midiTable.constEnd(); ++it)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("channel"), int(it.key()));
        entry.insert(QStringLiteral("label"), it.value());
        midiChannels.append(entry);
    }
    obj.insert(QStringLiteral("midiChannelTable"), midiChannels);
    return obj;
}

// Whole-document parse of an IoInputProfile. Returns nullptr and fills
// error on the first problem; nothing is touched in the engine.
QLCInputProfile *inputProfileFromJson(const QJsonObject &obj, QString &error)
{
    QString manufacturer = obj.value(QStringLiteral("manufacturer")).toString().trimmed();
    QString model = obj.value(QStringLiteral("model")).toString().trimmed();
    if (manufacturer.isEmpty() || model.isEmpty())
    {
        error = QStringLiteral("profile.manufacturer and profile.model must not be empty");
        return nullptr;
    }

    QString typeStr = obj.value(QStringLiteral("type")).toString();
    QLCInputProfile::Type type = QLCInputProfile::stringToType(typeStr);
    if (QLCInputProfile::typeToString(type) != typeStr)
    {
        error = QStringLiteral("profile.type must be one of MIDI, OS2L, OSC, HID, DMX, Enttec");
        return nullptr;
    }

    std::unique_ptr<QLCInputProfile> profile(new QLCInputProfile());
    profile->setManufacturer(manufacturer);
    profile->setModel(model);
    profile->setType(type);
    profile->setMidiSendNoteOff(obj.value(QStringLiteral("midiSendNoteOff")).toBool(true));

    QJsonValue channelsValue = obj.value(QStringLiteral("channels"));
    if (channelsValue.isUndefined() == false && channelsValue.isArray() == false)
    {
        error = QStringLiteral("profile.channels must be an array");
        return nullptr;
    }
    for (const QJsonValue &v : channelsValue.toArray())
    {
        QJsonObject cj = v.toObject();
        int number = cj.value(QStringLiteral("number")).toInt(-1);
        if (number < 0)
        {
            error = QStringLiteral("each channel needs a non-negative number");
            return nullptr;
        }
        if (profile->channel(quint32(number)) != nullptr)
        {
            error = QStringLiteral("channel number %1 appears twice").arg(number);
            return nullptr;
        }
        QString name = cj.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            error = QStringLiteral("channel %1 needs a name").arg(number);
            return nullptr;
        }
        QLCInputChannel::Type chType = QLCInputChannel::Button;
        QString chTypeStr = cj.value(QStringLiteral("type")).toString(QStringLiteral("Button"));
        if (channelTypeFromJson(chTypeStr, chType) == false)
        {
            error = QStringLiteral("channel %1: unknown type \"%2\"").arg(number).arg(chTypeStr);
            return nullptr;
        }

        QLCInputChannel *ch = new QLCInputChannel();
        ch->setName(name);
        ch->setType(chType);
        ch->setMovementType(cj.value(QStringLiteral("movementType")).toString() == QStringLiteral("Relative")
                            ? QLCInputChannel::Relative : QLCInputChannel::Absolute);
        ch->setMovementSensitivity(cj.value(QStringLiteral("movementSensitivity")).toInt(20));
        ch->setSendExtraPress(cj.value(QStringLiteral("sendExtraPress")).toBool(false));
        ch->setRange(uchar(qBound(0, cj.value(QStringLiteral("lowerValue")).toInt(0), 255)),
                     uchar(qBound(0, cj.value(QStringLiteral("upperValue")).toInt(255), 255)));
        ch->setLowerChannel(cj.value(QStringLiteral("lowerChannel")).toInt(-1));
        profile->insertChannel(quint32(number), ch);
    }

    for (const QJsonValue &v : obj.value(QStringLiteral("colorTable")).toArray())
    {
        QJsonObject entry = v.toObject();
        int value = entry.value(QStringLiteral("value")).toInt(-1);
        QColor color(entry.value(QStringLiteral("color")).toString());
        if (value < 0 || value > 255 || color.isValid() == false)
        {
            error = QStringLiteral("colorTable entries need value 0-255 and a #rrggbb color");
            return nullptr;
        }
        profile->addColor(uchar(value), entry.value(QStringLiteral("label")).toString(), color);
    }

    for (const QJsonValue &v : obj.value(QStringLiteral("midiChannelTable")).toArray())
    {
        QJsonObject entry = v.toObject();
        int channel = entry.value(QStringLiteral("channel")).toInt(-1);
        if (channel < 0 || channel > 15)
        {
            error = QStringLiteral("midiChannelTable entries need channel 0-15");
            return nullptr;
        }
        profile->addMidiChannel(uchar(channel), entry.value(QStringLiteral("label")).toString());
    }

    return profile.release();
}

// "<Manufacturer>-<Model>.qxi" like InputOutputManager::saveInputProfile(),
// minus anything a client could use to escape the profile directory.
QString profileFileName(const QLCInputProfile *profile)
{
    QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9 _.()+-]"));
    QString manufacturer = profile->manufacturer(), model = profile->model();
    manufacturer.replace(unsafe, QStringLiteral("_"));
    model.replace(unsafe, QStringLiteral("_"));
    return QStringLiteral("%1-%2%3").arg(manufacturer, model, KExtInputProfile);
}

QJsonObject audioDeviceToJson(const QString &name, const QString &privateName)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("privateName"), privateName);
    return obj;
}

/** PopupAudioConfiguration.qml's values, from the QSettings keys InputOutputManager uses. */
QJsonObject audioConfigToJson()
{
    QSettings settings;
    QJsonObject obj;
    obj.insert(QStringLiteral("inputSampleRate"), settings.value(QLatin1String(SETTINGS_AUDIO_INPUT_SRATE), AUDIO_DEFAULT_SAMPLE_RATE).toInt());
    obj.insert(QStringLiteral("inputChannels"), settings.value(QLatin1String(SETTINGS_AUDIO_INPUT_CHANNELS), AUDIO_DEFAULT_CHANNELS).toInt());
    obj.insert(QStringLiteral("outputBufferMs"), settings.value(QLatin1String(SETTINGS_AUDIO_OUTPUT_BUFFER), DEFAULT_AUDIO_OUTPUT_BUFFER_MS).toInt());
    return obj;
}

} // namespace

ApiIoConfigDomain::ApiIoConfigDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
    , m_ioDomain(ioDomain)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);
    Q_ASSERT(m_ioDomain != nullptr);

    registerMethods();

    // String-based connect across the qlcplusengine.dll boundary - see
    // ApiIoDomain's constructor for why the pointer form silently fails.
    connect(m_doc->inputOutputMap(), SIGNAL(pluginConfigurationChanged(QString,bool)),
            this, SLOT(slotPluginConfigurationChanged(QString,bool)));
}

ApiIoConfigDomain::~ApiIoConfigDomain()
{
    stopLearning();
    m_previewSessions.clear();
    detachAudioPreview();
}

void ApiIoConfigDomain::registerMethods()
{
    registerPluginMethods();
    registerPatchMethods();
    registerProfileMethods();
    registerLiveMethods();
    registerAudioMethods();
}

/*********************************************************************
 * io.plugin.*
 *********************************************************************/

void ApiIoConfigDomain::registerPluginMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // io.plugin.getLines {pluginName} -> {inputs: [IoPluginLine], outputs: [IoPluginLine]}
    dispatcher->registerMethod(QStringLiteral("io.plugin.getLines"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QLCIOPlugin *plugin = findPluginParam(doc, session, id, params);
        if (plugin == nullptr)
            return;
        QJsonObject result;
        result.insert(QStringLiteral("pluginName"), plugin->name());
        result.insert(QStringLiteral("inputs"), pluginLinesToJson(plugin->inputs(), plugin->inputsUID()));
        result.insert(QStringLiteral("outputs"), pluginLinesToJson(plugin->outputs(), plugin->outputsUID()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.plugin.rescan {pluginName} -> {} - QLCIOPlugin has no rescan entry
    // point; hotplug plugins expose one as a Q_INVOKABLE (DMXUSB::rescanWidgets)
    // found here by name, so an older plugin DLL simply reports UNSUPPORTED.
    // Line changes then arrive through the plugin's own configurationChanged()
    // -> InputOutputMap::pluginConfigurationChanged -> io.plugin.linesChanged.
    dispatcher->registerMethod(QStringLiteral("io.plugin.rescan"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QLCIOPlugin *plugin = findPluginParam(doc, session, id, params);
        if (plugin == nullptr)
            return;

        const QMetaObject *meta = plugin->metaObject();
        int index = meta->indexOfMethod("rescanWidgets()");
        if (index < 0)
            index = meta->indexOfMethod("rescan()");
        if (index < 0)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                QStringLiteral("Plugin \"%1\" cannot re-enumerate its lines on request").arg(plugin->name())));
            return;
        }

        QMetaMethod method = meta->method(index);
        bool invoked = false;
        if (method.returnType() == QMetaType::Bool)
        {
            bool returned = false;
            invoked = method.invoke(plugin, Qt::DirectConnection, Q_RETURN_ARG(bool, returned));
        }
        else
        {
            invoked = method.invoke(plugin, Qt::DirectConnection);
        }
        if (invoked == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                QStringLiteral("The plugin's rescan method could not be invoked")));
            return;
        }
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // io.plugin.configure {pluginName} -> {openedOnHost: true}
    // Thin passthrough to InputOutputMap::configurePlugin(): the plugin's
    // native dialog opens on the machine running QLC+ (a modal one keeps
    // this response waiting until it is closed). A host without a GUI, or a
    // plugin without a dialog, gets an explicit UNSUPPORTED instead - the
    // remote-friendly path is io.patch.setParameters.
    dispatcher->registerMethod(QStringLiteral("io.plugin.configure"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QLCIOPlugin *plugin = findPluginParam(doc, session, id, params);
        if (plugin == nullptr)
            return;
        if (plugin->canConfigure() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                QStringLiteral("Plugin \"%1\" has no configuration dialog; set its line parameters with io.patch.setParameters").arg(plugin->name())));
            return;
        }
        QCoreApplication *app = QCoreApplication::instance();
        if (app == nullptr || app->inherits("QGuiApplication") == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrUnsupported,
                QStringLiteral("Plugin \"%1\" is configured through a native dialog, and this QLC+ host has no desktop to show it on; use io.patch.setParameters").arg(plugin->name())));
            return;
        }
        // A modal dialog runs a nested event loop: this client may disconnect
        // meanwhile, and ApiServer's deleteLater() of its session then runs
        // inside that loop - don't answer through a dangling pointer.
        QPointer<ApiSession> guard(session);
        doc->inputOutputMap()->configurePlugin(plugin->name());
        if (guard.isNull())
            return;
        QJsonObject result;
        result.insert(QStringLiteral("openedOnHost"), true);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}

void ApiIoConfigDomain::slotPluginConfigurationChanged(const QString &pluginName, bool success)
{
    Q_UNUSED(success)
    QLCIOPlugin *plugin = m_doc->ioPluginCache()->plugin(pluginName);
    if (plugin == nullptr)
        return;
    QJsonObject data;
    data.insert(QStringLiteral("pluginName"), pluginName);
    data.insert(QStringLiteral("inputs"), pluginLinesToJson(plugin->inputs(), plugin->inputsUID()));
    data.insert(QStringLiteral("outputs"), pluginLinesToJson(plugin->outputs(), plugin->outputsUID()));
    // Engine-driven (hotplug or a rescan/configure by any client): no
    // request to attribute it to, so a null origin. Structural-ish and
    // rare: not subscribe-gated.
    m_server->broadcast(QStringLiteral("io.plugin.linesChanged"), data, QString(), false);
}

/*********************************************************************
 * io.patch.setParameters / io.patch.output.setState
 *********************************************************************/

void ApiIoConfigDomain::registerPatchMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // io.patch.setParameters {universeId, patchType|direction, index?, parameters, baseRevision?}
    //   -> {docRevision, parameters}
    // Generic key/value plugin parameters for one patched line
    // (QLCIOPlugin::setParameter / unSetParameter for null values) - ArtNet's
    // outputIP/outputUni/transmitMode, OSC's ports, ... They are saved into
    // the .qxw with the patch (OutputPatch/InputPatch::saveXML), hence §4a.
    dispatcher->registerMethod(QStringLiteral("io.patch.setParameters"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (baseRevisionAccepted(doc, session, id, params) == false)
            return;
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;
        PatchDirection direction;
        if (parsePatchDirection(params, direction) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("patchType must be one of input, output, feedback")));
            return;
        }
        QJsonValue parametersValue = params.value(QStringLiteral("parameters"));
        if (parametersValue.isObject() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("parameters must be an object of key -> string|number|boolean|null")));
            return;
        }
        QJsonObject parameters = parametersValue.toObject();

        InputPatch *inputPatch = nullptr;
        OutputPatch *outputPatch = nullptr;
        int index = params.value(QStringLiteral("index")).toInt(0);
        switch (direction)
        {
        case PatchInput:
            inputPatch = universe->inputPatch();
            break;
        case PatchOutput:
            if (index >= 0 && index < universe->outputPatchesCount())
                outputPatch = universe->outputPatch(index);
            break;
        case PatchFeedback:
            outputPatch = universe->feedbackPatch();
            break;
        }
        if (inputPatch == nullptr && outputPatch == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                QStringLiteral("Universe has no such patch")));
            return;
        }
        QLCIOPlugin *plugin = inputPatch != nullptr ? inputPatch->plugin() : outputPatch->plugin();
        if (plugin == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                QStringLiteral("The patch has no plugin bound")));
            return;
        }

        quint32 line = inputPatch != nullptr ? inputPatch->input() : outputPatch->output();
        QLCIOPlugin::Capability capability = inputPatch != nullptr ? QLCIOPlugin::Input : QLCIOPlugin::Output;
        for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it)
        {
            if (it.value().isNull())
            {
                plugin->unSetParameter(universe->id(), line, capability, it.key());
                continue;
            }
            if (it.value().isObject() || it.value().isArray())
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                    QStringLiteral("parameter \"%1\" must be a string, number, boolean or null").arg(it.key())));
                return;
            }
            QVariant value = parameterValueToVariant(it.value());
            if (inputPatch != nullptr)
                inputPatch->setPluginParameter(it.key(), value);
            else
                outputPatch->setPluginParameter(it.key(), value);
        }

        doc->setModified();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        result.insert(QStringLiteral("parameters"), pluginParametersToJson(
            inputPatch != nullptr ? inputPatch->getPluginParameters() : outputPatch->getPluginParameters()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
        m_ioDomain->broadcastUniverseUpdated(universe, session->clientId());
    });

    // io.patch.output.setState {universeId, index, paused?, blackout?} -> {}
    // Live only (OutputPatch::paused/blackout are never saved) - no
    // baseRevision, no Doc::setModified().
    dispatcher->registerMethod(QStringLiteral("io.patch.output.setState"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;
        int index = params.value(QStringLiteral("index")).toInt(0);
        OutputPatch *patch = (index >= 0 && index < universe->outputPatchesCount()) ? universe->outputPatch(index) : nullptr;
        if (patch == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                QStringLiteral("No output patch at index %1").arg(index)));
            return;
        }
        QJsonValue paused = params.value(QStringLiteral("paused"));
        QJsonValue blackout = params.value(QStringLiteral("blackout"));
        if (paused.isBool() == false && blackout.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("At least one of paused (boolean) or blackout (boolean) is required")));
            return;
        }
        if (paused.isBool())
            patch->setPaused(paused.toBool());
        if (blackout.isBool())
            patch->setBlackout(blackout.toBool());

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("universeId"), int(universe->id()));
        data.insert(QStringLiteral("index"), index);
        data.insert(QStringLiteral("paused"), patch->paused());
        data.insert(QStringLiteral("blackout"), patch->blackout());
        m_server->broadcast(QStringLiteral("io.patch.output.stateChanged"), data, session->clientId(), false);
    });
}

/*********************************************************************
 * io.inputProfile.get / save / delete / learn.*
 *********************************************************************/

void ApiIoConfigDomain::registerProfileMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // §4c: baseRevision means profilesRevision here. Same "enforced when
    // sent" rule as the docRevision-gated methods.
    auto profilesRevisionAccepted = [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (params.contains(QStringLiteral("baseRevision")) == false)
            return true;
        if (quint32(params.value(QStringLiteral("baseRevision")).toInt()) == m_ioDomain->profilesRevision())
            return true;
        QJsonObject details;
        details.insert(QStringLiteral("profilesRevision"), int(m_ioDomain->profilesRevision()));
        session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                        QStringLiteral("baseRevision is stale (profilesRevision)"), details));
        return false;
    };

    // io.inputProfile.get {name} -> {profile: IoInputProfile, profilesRevision}
    dispatcher->registerMethod(QStringLiteral("io.inputProfile.get"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QLCInputProfile *profile = doc->inputOutputMap()->profile(params.value(QStringLiteral("name")).toString());
        if (profile == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such input profile")));
            return;
        }
        QJsonObject result;
        result.insert(QStringLiteral("profile"), inputProfileToJson(profile));
        result.insert(QStringLiteral("profilesRevision"), int(m_ioDomain->profilesRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.inputProfile.save {profile, baseRevision?} -> {profilesRevision, name, path}
    // Whole-document upsert keyed on (manufacturer, model): the .qxi is
    // written to the user profile directory (InputOutputMap::
    // userProfileDirectory(), overridable via QLCPLUS_USER_INPUTPROFILE_DIR)
    // and the in-memory profile is updated IN PLACE (QLCInputProfile::
    // operator=) so every InputPatch holding its pointer keeps working -
    // unlike InputOutputManager::saveInputProfile(), which leaves the loaded
    // copy stale until restart.
    dispatcher->registerMethod(QStringLiteral("io.inputProfile.save"), [doc, this, profilesRevisionAccepted](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (profilesRevisionAccepted(session, id, params) == false)
            return;
        QJsonValue profileValue = params.value(QStringLiteral("profile"));
        if (profileValue.isObject() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("profile (IoInputProfile object) is required")));
            return;
        }
        QString error;
        QLCInputProfile *edited = inputProfileFromJson(profileValue.toObject(), error);
        if (edited == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, error));
            return;
        }

        QDir userDir = InputOutputMap::userProfileDirectory();
        QString absPath = userDir.absolutePath() + QLatin1Char('/') + profileFileName(edited);
        if (edited->saveXML(absPath) == false)
        {
            delete edited;
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                QStringLiteral("Could not write %1").arg(absPath)));
            return;
        }

        InputOutputMap *ioMap = doc->inputOutputMap();
        QLCInputProfile *profile = ioMap->profile(edited->name());
        if (profile != nullptr)
        {
            *profile = *edited; // deep copy incl. the new path; patch pointers stay valid
            delete edited;
        }
        else
        {
            ioMap->addProfile(edited);
            profile = edited;
        }
        m_ioDomain->bumpProfilesRevision();

        QJsonObject result;
        result.insert(QStringLiteral("profilesRevision"), int(m_ioDomain->profilesRevision()));
        result.insert(QStringLiteral("name"), profile->name());
        result.insert(QStringLiteral("path"), profile->path());
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("profile"), inputProfileToJson(profile));
        data.insert(QStringLiteral("profilesRevision"), int(m_ioDomain->profilesRevision()));
        m_server->broadcast(QStringLiteral("io.inputProfile.changed"), data, session->clientId(), false);
    });

    // io.inputProfile.delete {name, baseRevision?} -> {profilesRevision}
    // Only user profiles (ProfilesList.qml offers delete for isUser only);
    // a universe still using the profile has it cleared first, since
    // InputOutputMap::removeProfile() deletes the object the InputPatch
    // points at (InputOutputManager::removeInputProfile() leaves that
    // dangling).
    dispatcher->registerMethod(QStringLiteral("io.inputProfile.delete"), [doc, this, profilesRevisionAccepted](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        if (profilesRevisionAccepted(session, id, params) == false)
            return;
        InputOutputMap *ioMap = doc->inputOutputMap();
        QString name = params.value(QStringLiteral("name")).toString();
        QLCInputProfile *profile = ioMap->profile(name);
        if (profile == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such input profile")));
            return;
        }
        if (isUserProfilePath(profile->path()) == false)
        {
            QJsonObject details;
            details.insert(QStringLiteral("path"), profile->path());
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                QStringLiteral("\"%1\" is a bundled system profile and cannot be deleted").arg(name), details));
            return;
        }
        if (profile->path().isEmpty() == false && QFile::exists(profile->path()) && QFile::remove(profile->path()) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal,
                QStringLiteral("Could not remove %1").arg(profile->path())));
            return;
        }

        QList<Universe *> affected;
        for (Universe *universe : ioMap->universes())
        {
            if (universe->inputPatch() != nullptr && universe->inputPatch()->profile() == profile)
            {
                ioMap->setInputProfile(universe->id(), QString());
                affected.append(universe);
            }
        }
        ioMap->removeProfile(name);
        m_ioDomain->bumpProfilesRevision();
        if (affected.isEmpty() == false)
            doc->setModified(); // the patch's profile name is part of the .qxw

        QJsonObject result;
        result.insert(QStringLiteral("profilesRevision"), int(m_ioDomain->profilesRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        QJsonObject data;
        data.insert(QStringLiteral("name"), name);
        data.insert(QStringLiteral("profilesRevision"), int(m_ioDomain->profilesRevision()));
        m_server->broadcast(QStringLiteral("io.inputProfile.deleted"), data, session->clientId(), false);
        for (Universe *universe : std::as_const(affected))
            m_ioDomain->broadcastUniverseUpdated(universe, session->clientId());
    });

    // io.inputProfile.learn.start {universeId, profileName?} -> {}
    // One learn session per server, owned by the requesting client: every
    // raw input signal on that universe is relayed to THAT client only as
    // io.inputProfile.learn.signal (io-notes.md's scoping fix). Input keeps
    // flowing to the Virtual Console meanwhile, exactly like qmlui's
    // InputProfileEditor::toggleDetection() (it only listens, never eats).
    dispatcher->registerMethod(QStringLiteral("io.inputProfile.learn.start"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;
        if (universe->inputPatch() == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                QStringLiteral("Universe %1 has no input patch to learn from").arg(universe->id())));
            return;
        }
        QString profileName = params.value(QStringLiteral("profileName")).toString();
        if (profileName.isEmpty() == false && doc->inputOutputMap()->profile(profileName) == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such input profile")));
            return;
        }
        if (m_learnSession.isNull() == false && m_learnSession != session)
        {
            QJsonObject details;
            details.insert(QStringLiteral("clientId"), m_learnSession->clientId());
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                QStringLiteral("Another client is already learning; ask it to stop first"), details));
            return;
        }

        stopLearning();
        m_learnSession = session;
        m_learnUniverse = universe->id();
        m_learnProfileName = profileName;
        connect(doc->inputOutputMap(), SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
                this, SLOT(slotInputValueChanged(quint32,quint32,uchar,QString)));
        connect(session, &ApiSession::disconnected, this, &ApiIoConfigDomain::slotLearnSessionDisconnected);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // io.inputProfile.learn.stop {} -> {} (idempotent; only the learning
    // client can stop its own session, anyone else gets INVALID_STATE)
    dispatcher->registerMethod(QStringLiteral("io.inputProfile.learn.stop"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        if (m_learnSession.isNull() == false && m_learnSession != session)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState,
                QStringLiteral("The learn session belongs to another client")));
            return;
        }
        stopLearning();
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}

void ApiIoConfigDomain::stopLearning()
{
    if (m_learnSession.isNull() == false)
        disconnect(m_learnSession, &ApiSession::disconnected, this, &ApiIoConfigDomain::slotLearnSessionDisconnected);
    disconnect(m_doc->inputOutputMap(), SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
               this, SLOT(slotInputValueChanged(quint32,quint32,uchar,QString)));
    m_learnSession.clear();
    m_learnProfileName.clear();
}

void ApiIoConfigDomain::attachAudioPreview()
{
    if (m_previewCapture.isNull() == false)
        return;
    m_previewCapture = m_doc->audioInputCapture();
    if (m_previewCapture.isNull())
        return;
    // AudioCapture lives in the engine's audio library: string-based connect (see CLAUDE.md), and
    // dataProcessed comes from the capture thread, so this is a queued connection.
    connect(m_previewCapture.data(), SIGNAL(dataProcessed(double*,int,double,quint32)),
            this, SLOT(slotAudioPreviewData(double*,int,double,quint32)));
    m_previewCapture->registerBandsNumber(FREQ_SUBBANDS_DEFAULT_NUMBER);
    m_previewThrottle.invalidate();
}

void ApiIoConfigDomain::detachAudioPreview()
{
    if (m_previewCapture.isNull())
        return;
    m_previewCapture->unregisterBandsNumber(FREQ_SUBBANDS_DEFAULT_NUMBER);
    disconnect(m_previewCapture.data(), SIGNAL(dataProcessed(double*,int,double,quint32)),
               this, SLOT(slotAudioPreviewData(double*,int,double,quint32)));
    m_previewCapture.clear();
}

void ApiIoConfigDomain::restartAudioPreview()
{
    const bool running = m_previewCapture.isNull() == false;
    detachAudioPreview();
    m_doc->destroyAudioCapture();
    if (running)
        attachAudioPreview();
}

void ApiIoConfigDomain::slotAudioPreviewData(double *spectrumBands, int size, double maxMagnitude, quint32 power)
{
    Q_UNUSED(spectrumBands)
    Q_UNUSED(size)
    Q_UNUSED(maxMagnitude)
    if (m_previewThrottle.isValid() && m_previewThrottle.elapsed() < 50)
        return;
    m_previewThrottle.restart();

    QJsonObject data;
    data.insert(QStringLiteral("level"), int(qMin(power, quint32(0x7FFF))));
    for (const QPointer<ApiSession> &s : m_previewSessions)
    {
        if (s.isNull() == false)
            s->send(ApiEnvelope::buildEvent(QStringLiteral("io.audio.inputLevel"), data, QString()));
    }
}

void ApiIoConfigDomain::slotPreviewSessionDisconnected(ApiSession *session)
{
    m_previewSessions.removeAll(session);
    m_previewSessions.removeAll(QPointer<ApiSession>());
    if (m_previewSessions.isEmpty())
        detachAudioPreview();
}

void ApiIoConfigDomain::slotLearnSessionDisconnected(ApiSession *session)
{
    if (m_learnSession == session)
        stopLearning();
}

void ApiIoConfigDomain::slotInputValueChanged(quint32 universe, quint32 channel, uchar value, const QString &key)
{
    if (m_learnSession.isNull() || universe != m_learnUniverse)
        return;

    QLCInputProfile *profile = nullptr;
    if (m_learnProfileName.isEmpty() == false)
        profile = m_doc->inputOutputMap()->profile(m_learnProfileName);
    else if (Universe *uni = m_doc->inputOutputMap()->universe(universe))
        profile = uni->inputPatch() != nullptr ? uni->inputPatch()->profile() : nullptr;

    QJsonObject data;
    data.insert(QStringLiteral("universeId"), int(universe));
    // The profile's own channel key (IoInputChannel.number, 0-based), not
    // the +1 display number InputProfileEditor::inputSignalReceived emits.
    data.insert(QStringLiteral("channelNumber"), int(channel));
    data.insert(QStringLiteral("value"), int(value));
    data.insert(QStringLiteral("key"), key);
    data.insert(QStringLiteral("alreadyMapped"), profile != nullptr && profile->channel(channel) != nullptr);
    // To the learning client only - never broadcast.
    m_learnSession->send(ApiEnvelope::buildEvent(QStringLiteral("io.inputProfile.learn.signal"), data, m_learnSession->clientId()));
}

/*********************************************************************
 * io.grandMaster.setMode / io.universe.setMonitor
 *********************************************************************/

void ApiIoConfigDomain::registerLiveMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // io.grandMaster.setMode {channelMode?, valueMode?} -> {}
    // Live (§4b, no baseRevision) like setValue, but the two modes ARE
    // saved with the workspace (InputOutputMap::saveXML's GrandMaster
    // element), so the doc is flagged modified. Neither engine setter emits
    // a usable signal, hence the broadcast from here.
    dispatcher->registerMethod(QStringLiteral("io.grandMaster.setMode"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString channelMode = params.value(QStringLiteral("channelMode")).toString();
        QString valueMode = params.value(QStringLiteral("valueMode")).toString();
        if (channelMode.isEmpty() && valueMode.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("At least one of channelMode (Intensity|AllChannels) or valueMode (Limit|Reduce) is required")));
            return;
        }
        if (channelMode.isEmpty() == false && channelMode != QStringLiteral("Intensity") && channelMode != QStringLiteral("AllChannels"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("channelMode must be Intensity or AllChannels")));
            return;
        }
        if (valueMode.isEmpty() == false && valueMode != QStringLiteral("Limit") && valueMode != QStringLiteral("Reduce"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("valueMode must be Limit or Reduce")));
            return;
        }

        InputOutputMap *ioMap = doc->inputOutputMap();
        if (channelMode.isEmpty() == false)
            ioMap->setGrandMasterChannelMode(channelMode == QStringLiteral("AllChannels") ? GrandMaster::AllChannels : GrandMaster::Intensity);
        if (valueMode.isEmpty() == false)
            ioMap->setGrandMasterValueMode(valueMode == QStringLiteral("Reduce") ? GrandMaster::Reduce : GrandMaster::Limit);
        doc->setModified();

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
        m_server->broadcast(QStringLiteral("io.grandMaster.changed"), grandMasterStateToJson(ioMap), session->clientId(), false);
    });

    // io.universe.setMonitor {universeId, monitor} -> {}
    // Live only: Universe::setMonitor is never written to the .qxw.
    dispatcher->registerMethod(QStringLiteral("io.universe.setMonitor"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Universe *universe = findUniverseParam(doc, session, id, params);
        if (universe == nullptr)
            return;
        QJsonValue monitor = params.value(QStringLiteral("monitor"));
        if (monitor.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("monitor (boolean) is required")));
            return;
        }
        doc->inputOutputMap()->setUniverseMonitor(int(universe->id()), monitor.toBool());
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("universeId"), int(universe->id()));
        data.insert(QStringLiteral("monitor"), universe->monitor());
        m_server->broadcast(QStringLiteral("io.universe.monitorChanged"), data, session->clientId(), false);
    });
}

/*********************************************************************
 * io.audio.listDevices / io.audio.setDevice
 *********************************************************************/

void ApiIoConfigDomain::registerAudioMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    // io.audio.listDevices {} -> {inputs: [{name, privateName}], outputs: [...],
    //                             inputDevice, outputDevice}
    // The QLC+ host's own sound devices (AudioPluginCache::audioDevicesList),
    // with the "__qlcplusdefault__" pseudo-device first like AudioCardsList.qml.
    // inputDevice/outputDevice are the selected privateNames (QSettings
    // audio/input, audio/output - the keys qmlui's InputOutputManager and
    // the engine's AudioCapture/AudioRenderer read).
    dispatcher->registerMethod(QStringLiteral("io.audio.listDevices"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QSettings settings;
        QString inputDevice = settings.value(SETTINGS_AUDIO_INPUT_DEVICE).toString();
        QString outputDevice = settings.value(SETTINGS_AUDIO_OUTPUT_DEVICE).toString();

        QJsonArray inputs, outputs;
        inputs.append(audioDeviceToJson(QStringLiteral("Default device"), AUDIO_DEFAULT_DEVICE));
        outputs.append(audioDeviceToJson(QStringLiteral("Default device"), AUDIO_DEFAULT_DEVICE));
        for (const AudioDeviceInfo &info : doc->audioPluginCache()->audioDevicesList())
        {
            if (info.capabilities & AUDIO_CAP_INPUT)
                inputs.append(audioDeviceToJson(info.deviceName, info.privateName));
            if (info.capabilities & AUDIO_CAP_OUTPUT)
                outputs.append(audioDeviceToJson(info.deviceName, info.privateName));
        }

        QJsonObject result;
        result.insert(QStringLiteral("inputs"), inputs);
        result.insert(QStringLiteral("outputs"), outputs);
        result.insert(QStringLiteral("inputDevice"), inputDevice.isEmpty() ? AUDIO_DEFAULT_DEVICE : inputDevice);
        result.insert(QStringLiteral("outputDevice"), outputDevice.isEmpty() ? AUDIO_DEFAULT_DEVICE : outputDevice);
        const QJsonObject config = audioConfigToJson();
        for (auto it = config.begin(); it != config.end(); ++it)
            result.insert(it.key(), it.value());
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.audio.setConfig {inputSampleRate?, inputChannels?, outputBufferMs?} -> {}
    // PopupAudioConfiguration.qml's fields, written exactly like InputOutputManager's setters:
    // host-wide QSettings (the default value removes the key), and a changed input format tears
    // the running audio capture down so the next user re-opens it with the new format.
    dispatcher->registerMethod(QStringLiteral("io.audio.setConfig"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        static const QList<int> sampleRates = { 8000, 11025, 22050, 32000, 44100, 48000 };
        const bool hasRate = params.contains(QStringLiteral("inputSampleRate"));
        const bool hasChannels = params.contains(QStringLiteral("inputChannels"));
        const bool hasBuffer = params.contains(QStringLiteral("outputBufferMs"));
        const int rate = params.value(QStringLiteral("inputSampleRate")).toInt(-1);
        const int channels = params.value(QStringLiteral("inputChannels")).toInt(-1);
        const int buffer = params.value(QStringLiteral("outputBufferMs")).toInt(-1);
        QString problem;
        if (hasRate == false && hasChannels == false && hasBuffer == false)
            problem = QStringLiteral("Nothing to set: pass inputSampleRate, inputChannels and/or outputBufferMs");
        else if (hasRate && sampleRates.contains(rate) == false)
            problem = QStringLiteral("inputSampleRate must be one of 8000, 11025, 22050, 32000, 44100, 48000");
        else if (hasChannels && channels != 1 && channels != 2)
            problem = QStringLiteral("inputChannels must be 1 (mono) or 2 (stereo)");
        else if (hasBuffer && (buffer < 10 || buffer > 1000))
            problem = QStringLiteral("outputBufferMs must be 10..1000");
        if (problem.isEmpty() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, problem));
            return;
        }

        QSettings settings;
        auto write = [&settings](const char *key, int value, int defaultValue)
        {
            if (settings.value(QLatin1String(key), defaultValue).toInt() == value)
                return false;
            if (value == defaultValue)
                settings.remove(QLatin1String(key));
            else
                settings.setValue(QLatin1String(key), value);
            return true;
        };
        bool inputChanged = false, changed = false;
        if (hasRate && write(SETTINGS_AUDIO_INPUT_SRATE, rate, AUDIO_DEFAULT_SAMPLE_RATE))
            inputChanged = true;
        if (hasChannels && write(SETTINGS_AUDIO_INPUT_CHANNELS, channels, AUDIO_DEFAULT_CHANNELS))
            inputChanged = true;
        if (hasBuffer && write(SETTINGS_AUDIO_OUTPUT_BUFFER, buffer, DEFAULT_AUDIO_OUTPUT_BUFFER_MS))
            changed = true;
        settings.sync();
        if (inputChanged)
            restartAudioPreview(); // Doc::destroyAudioCapture(), re-opening a running preview

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
        if (inputChanged || changed)
            m_server->broadcast(QStringLiteral("io.audio.configChanged"), audioConfigToJson(), session->clientId(), false);
    });

    // io.audio.inputPreview.set {enabled} -> {}
    // PopupAudioConfiguration.qml's "Signal level" check (InputOutputManager::enableAudioInputPreview):
    // while at least one client has it on, the host's audio input runs and every previewing client
    // gets io.audio.inputLevel (~20/s). Per client; a disconnect ends that client's preview.
    dispatcher->registerMethod(QStringLiteral("io.audio.inputPreview.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QJsonValue enabled = params.value(QStringLiteral("enabled"));
        if (enabled.isBool() == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("enabled (boolean) is required")));
            return;
        }
        m_previewSessions.removeAll(QPointer<ApiSession>());
        const bool had = m_previewSessions.contains(session);
        if (enabled.toBool() && had == false)
        {
            m_previewSessions.append(session);
            connect(session, &ApiSession::disconnected, this, &ApiIoConfigDomain::slotPreviewSessionDisconnected, Qt::UniqueConnection);
        }
        else if (enabled.toBool() == false && had)
        {
            m_previewSessions.removeAll(session);
        }
        if (m_previewSessions.isEmpty())
            detachAudioPreview();
        else
            attachAudioPreview();

        QJsonObject result;
        result.insert(QStringLiteral("capturing"), m_previewCapture.isNull() == false);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // io.audio.setDevice {direction: input|output, privateName} -> {}
    // Same effect as InputOutputManager::setAudioInput/setAudioOutput: a
    // host-wide QSettings write (not part of the project), the running
    // audio capture is torn down so the next user re-opens the new input.
    dispatcher->registerMethod(QStringLiteral("io.audio.setDevice"), [doc, this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString direction = params.value(QStringLiteral("direction")).toString();
        if (direction != QStringLiteral("input") && direction != QStringLiteral("output"))
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                QStringLiteral("direction must be input or output")));
            return;
        }
        QString privateName = params.value(QStringLiteral("privateName")).toString();
        if (privateName.isEmpty())
            privateName = AUDIO_DEFAULT_DEVICE;
        bool isInput = direction == QStringLiteral("input");
        if (privateName != AUDIO_DEFAULT_DEVICE)
        {
            bool known = false;
            for (const AudioDeviceInfo &info : doc->audioPluginCache()->audioDevicesList())
                if (info.privateName == privateName && (info.capabilities & (isInput ? AUDIO_CAP_INPUT : AUDIO_CAP_OUTPUT)))
                    known = true;
            if (known == false)
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                    QStringLiteral("No %1 device named \"%2\" on this host").arg(direction, privateName)));
                return;
            }
        }

        QSettings settings;
        const char *key = isInput ? SETTINGS_AUDIO_INPUT_DEVICE : SETTINGS_AUDIO_OUTPUT_DEVICE;
        if (privateName == AUDIO_DEFAULT_DEVICE)
            settings.remove(QLatin1String(key));
        else
            settings.setValue(QLatin1String(key), privateName);
        if (isInput)
            restartAudioPreview(); // Doc::destroyAudioCapture(), re-opening a running preview

        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));

        QJsonObject data;
        data.insert(QStringLiteral("direction"), direction);
        data.insert(QStringLiteral("privateName"), privateName);
        m_server->broadcast(QStringLiteral("io.audio.deviceChanged"), data, session->clientId(), false);
    });
}
