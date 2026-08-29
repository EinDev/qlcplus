/*
  Q Light Controller Plus
  simpledesk.cpp

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

#include <QQmlContext>
#include <QTextStream>

#include "simpledesk.h"
#include "keypadparser.h"
#include "genericfader.h"
#include "genericdmxsource.h"
#include "functionmanager.h"
#include "fadechannel.h"
#include "scenevalue.h"
#include "listmodel.h"
#include "tardis.h"
#include "app.h"
#include "doc.h"
#include "function.h"
#include "qlcchannel.h"
#include "virtualconsole.h"
#include "chaser.h"

#define UserRoleClassReference  (Qt::UserRole + 1)
#define UserRoleChannelIndex    (Qt::UserRole + 2)
#define UserRoleChannelValue    (Qt::UserRole + 3)
#define UserRoleChannelStatus   (Qt::UserRole + 4)
#define UserRoleChannelOverride (Qt::UserRole + 5)

#define MAX_KEYPAD_HISTORY      10
#define WEB_SD_CHANNELS_PER_PAGE 32

SimpleDesk::SimpleDesk(QQuickView *view, Doc *doc,
                       FunctionManager *funcMgr, QObject *parent)
    : PreviewContext(view, doc, "SDESK", parent)
    , m_functionManager(funcMgr)
    , m_dumpChannelMask(0)
{
    Q_ASSERT(m_doc != nullptr);

    setContextResource("qrc:/SimpleDesk.qml");
    setContextTitle(tr("Simple Desk"));

    view->rootContext()->setContextProperty("simpleDesk", this);

    m_doc->masterTimer()->registerDMXSource(this);

    m_channelList = new ListModel(this);
    QStringList listRoles;
    listRoles << "cRef" << "chIndex" << "chValue" << "chDisplay" << "isOverride";
    m_channelList->setRoleNames(listRoles);

    m_keyPadParser = new KeyPadParser();

    // initialize channel value data
    for (quint32 i = 0; i < m_doc->inputOutputMap()->universesCount(); i++)
        m_prevUniverseValues[i].fill(0, 512);

    updateChannelList();

    connect(m_doc, SIGNAL(loaded()), this, SLOT(updateChannelList()));
    connect(m_doc, SIGNAL(fixtureAdded(quint32)), this, SLOT(updateChannelList()));
    connect(m_doc, SIGNAL(fixtureRemoved(quint32)), this, SLOT(updateChannelList()));
    connect(m_doc, SIGNAL(fixtureChanged(quint32)), this, SLOT(updateChannelList()));
    connect(m_doc->inputOutputMap(), SIGNAL(universeAdded(quint32)),
            this, SIGNAL(universesListModelChanged()));
    connect(m_doc->inputOutputMap(), SIGNAL(universeRemoved(quint32)),
            this, SIGNAL(universesListModelChanged()));
    connect(m_doc->inputOutputMap(), SIGNAL(universeWritten(quint32,QByteArray)),
            this, SLOT(slotUniverseWritten(quint32,QByteArray)));
}

SimpleDesk::~SimpleDesk()
{
    m_doc->masterTimer()->unregisterDMXSource(this);
}

void SimpleDesk::setUniverseFilter(quint32 universeFilter)
{
    PreviewContext::setUniverseFilter(universeFilter);
    updateChannelList();
    emit fixtureListChanged();
}

QVariant SimpleDesk::channelList() const
{
    return QVariant::fromValue(m_channelList);
}

QVariantList SimpleDesk::fixtureList() const
{
    QVariantList list;
    QList<Fixture*> fixtureList = m_doc->fixtures();

    std::sort(fixtureList.begin(), fixtureList.end(),
              [](Fixture *left, Fixture *right)
    {
        return *left < *right;
    });

    for (Fixture *fxi : fixtureList)
    {
        if (fxi->universe() != m_universeFilter)
            continue;

        list.append(QVariant::fromValue(fxi));
    }

    return list;
}

void SimpleDesk::updateChannelList()
{
    quint32 start = (m_universeFilter * 512);
    quint32 prevID = Fixture::invalidId();
    int status = None;

    m_channelList->clear();

    if (m_prevUniverseValues.contains(m_universeFilter) == false)
        m_prevUniverseValues[m_universeFilter].fill(0, 512);

    QByteArray currUni = m_prevUniverseValues.value(m_universeFilter);

    for (int i = 0; i < currUni.length(); i++)
    {
        quint32 chIndex = 0;
        quint32 chValue = currUni.at(i);
        bool isOverriding = false;

        Fixture *fixture = m_doc->fixture(m_doc->fixtureForAddress(start + i));
        if (fixture != nullptr)
        {
            if (fixture->id() != prevID)
            {
                status = (status == Odd) ? Even : Odd;
                prevID = fixture->id();
            }
            chIndex = i - fixture->address();
            if (hasChannel(i))
            {
                chValue = value(i);
                isOverriding = true;
            }
            else
            {
                chValue = fixture->channelValueAt(chIndex);
            }
        }
        else
        {
            if (hasChannel(i))
            {
                isOverriding = true;
                chValue = value(i);
            }
        }

        QVariantMap chMap;
        chMap.insert("cRef", QVariant::fromValue(fixture));
        chMap.insert("chIndex", chIndex);
        chMap.insert("chValue", chValue);
        chMap.insert("chDisplay", fixture == nullptr ? None : status);
        chMap.insert("isOverride", isOverriding);

        m_channelList->addDataMap(chMap);
    }

    emit channelListChanged();
}

QVariant SimpleDesk::universesListModel() const
{
    QVariantList universesList;

    for (Universe *uni : m_doc->inputOutputMap()->universes())
    {
        QVariantMap uniMap;
        uniMap.insert("mLabel", uni->name());
        uniMap.insert("mValue", uni->id());
        universesList.append(uniMap);
    }

    return QVariant::fromValue(universesList);
}

/************************************************************************
 * Universe Values
 ************************************************************************/

void SimpleDesk::setValue(quint32 fixtureID, uint channel, uchar value)
{
    QMutexLocker locker(&m_mutex);
    quint32 start = (m_universeFilter * 512);
    QVariant currentVal, newVal;
    SceneValue currScv;
    Fixture *fixture = nullptr;

    qDebug() << "[Simple Desk] set value for fixture" << fixtureID << "channel" << channel << "value" << value;

    if (fixtureID != Fixture::invalidId())
    {
        fixture = m_doc->fixture(fixtureID);
        channel += fixture->address();
    }
    if (m_values.contains(start + channel))
    {
        //currScv.fxi = fixtureID;
        currScv.channel = channel;
        currScv.value = m_values[start + channel];
    }
    currentVal.setValue(currScv);

    m_values[start + channel] = value;

    QModelIndex mIndex = m_channelList->index(int(channel), 0, QModelIndex());
    m_channelList->setData(mIndex, value, UserRoleChannelValue);
    m_channelList->setData(mIndex, true, UserRoleChannelOverride);

    newVal.setValue(SceneValue(Fixture::invalidId(), channel, value));
    Tardis::instance()->enqueueAction(Tardis::SimpleDeskSetChannel, 0, currentVal, newVal);

    if (fixture != nullptr)
    {
        quint32 relCh = channel - fixture->address();
        setDumpValue(fixtureID, relCh, value);
    }

    setChanged(true);
}

uchar SimpleDesk::value(uint channel) const
{
    QMutexLocker locker(&m_mutex);
    quint32 start = (m_universeFilter * 512);
    if (m_values.contains(start + channel) == true)
        return m_values[start + channel];
    else
        return 0;
}

bool SimpleDesk::hasChannel(uint channel)
{
    QMutexLocker locker(&m_mutex);
    quint32 start = (m_universeFilter * 512);
    return m_values.contains(start + channel);
}

static QString fadeChannelFlagsToString(int flags)
{
    QStringList list;
    if (flags & FadeChannel::HTP) list << "HTP";
    if (flags & FadeChannel::LTP) list << "LTP";
    if (flags & FadeChannel::Fine) list << "Fine";
    if (flags & FadeChannel::Intensity) list << "Intensity";
    if (flags & FadeChannel::CanFade) list << "CanFade";
    if (flags & FadeChannel::Flashing) list << "Flashing";
    if (flags & FadeChannel::Relative) list << "Relative";
    if (flags & FadeChannel::Override) list << "Override";
    if (flags & FadeChannel::SetTarget) list << "SetTarget";
    if (flags & FadeChannel::AutoRemove) list << "AutoRemove";
    if (flags & FadeChannel::CrossFade) list << "CrossFade";
    if (flags & FadeChannel::ForceLTP) list << "ForceLTP";

    return list.isEmpty() ? QStringLiteral("none") : list.join("|");
}

// Describes a single FunctionParent as precisely as the engine's actually
// tracked data allows. $childFunctionID is the ID of the Function that this
// source started (i.e. the Function owning the fader being reported on) -
// used to try to resolve which Chaser/Sequence step is currently active.
static QString describeFunctionParent(const FunctionParent &source, Doc *doc,
                                       QQuickView *view, quint32 childFunctionID)
{
    switch (source.type())
    {
        case FunctionParent::Function:
        {
            Function *parent = doc->function(source.id());
            if (parent == nullptr)
                return QString("a Function (ID %1) that no longer exists").arg(source.id());

            QString stepInfo;
            if (parent->type() & (Function::ChaserType | Function::SequenceType))
            {
                Chaser *chaser = qobject_cast<Chaser *>(parent);
                if (chaser != nullptr)
                {
                    ChaserRunnerStep step = chaser->currentRunningStep();
                    if (step.m_function != nullptr && step.m_function->id() == childFunctionID)
                        stepInfo = QString(", step %1 (0-based)").arg(step.m_index);
                    else
                        stepInfo = ", step undetermined (not the Chaser's first currently-running step)";
                }
            }
            return QString("%1 \"%2\" (ID %3)%4")
                    .arg(parent->typeString()).arg(parent->name()).arg(parent->id()).arg(stepInfo);
        }

        case FunctionParent::AutoVCWidget:
        case FunctionParent::ManualVCWidget:
        {
            QString kind = source.type() == FunctionParent::ManualVCWidget ? "manual" : "automatic";
            VirtualConsole *vc = view != nullptr
                    ? qobject_cast<VirtualConsole *>(view->rootContext()->contextProperty("virtualConsole").value<QObject *>())
                    : nullptr;
            VCWidget *widget = vc != nullptr ? vc->widget(source.id()) : nullptr;
            if (widget != nullptr)
            {
                return QString("Virtual Console %1 widget \"%2\" (ID %3), page %4")
                        .arg(kind).arg(widget->caption()).arg(widget->id()).arg(widget->page());
            }
            return QString("a Virtual Console %1 widget (ID %2) that no longer exists").arg(kind).arg(source.id());
        }

        case FunctionParent::Master:
        {
            switch (source.id())
            {
                case FunctionParent::EngineSelfStop:
                    return QStringLiteral("the Function's own internal logic (natural completion, "
                                           "or an error condition such as a missing fixture group)");
                case FunctionParent::MasterTimerStopAll:
                    return QStringLiteral("MasterTimer's stop-all-functions request (e.g. Blackout/panic)");
                case FunctionParent::ProjectAutostart:
                    return QStringLiteral("the project's configured startup Function, auto-started on project load");
                case FunctionParent::FunctionManagerPreview:
                    return QStringLiteral("Function Manager's preview toggle/selection (no editor open)");
                case FunctionParent::FunctionManagerScenePreview:
                    return QStringLiteral("Function Manager's live Scene preview (Scene or Sequence editor open)");
                case FunctionParent::FunctionManagerDelete:
                    return QStringLiteral("Function Manager stopping the Function before deleting it");
                case FunctionParent::FunctionEditorPreview:
                    return QStringLiteral("a Function Editor's own preview toggle (Scene/EFX/Audio/Collection/"
                                           "Script/Video/Chaser/RGBMatrix editor)");
                case FunctionParent::ChaserEditorStepPreview:
                    return QStringLiteral("Chaser/Sequence Editor's step-scrubbing preview of the bound Scene");
                case FunctionParent::ShowManagerPlayback:
                    return QStringLiteral("Show Manager's play/pause/stop transport controls");
                case FunctionParent::TardisUndoRedo:
                    return QStringLiteral("Tardis replaying a Function start/stop action during undo/redo");
                case FunctionParent::WebAccess:
                    return QStringLiteral("a WebAccess/OSC \"setFunctionStatus\" API request");
                case FunctionParent::VideoWindowClosed:
                    return QStringLiteral("the user closing a Video function's preview window");
                case FunctionParent::ScriptStopFunction:
                    return QStringLiteral("a Script's \"stopFunction\" command (or its own exit cleanup)");
                case FunctionParent::GenericOverride:
                default:
                    return QString(
                        "a generic override source (FunctionParent::Master, ID %1) not yet given its own "
                        "identity - expected only from engine/UI test facilities or the legacy QLC+4 Qt "
                        "Widgets UI, neither of which this checkout builds").arg(source.id());
            }
        }

        default:
            return QString("an unrecognized source type %1 (ID %2)").arg(source.type()).arg(source.id());
    }
}

// Describes a GenericDMXSource::Feature tag (see genericdmxsource.h) as
// precisely as it identifies a UI feature - the GenericDMXSource equivalent
// of describeFunctionParent()'s FunctionParent::Master case above, but for
// a channel with no owning Function at all (fader->parentFunctionID() ==
// Function::invalidId()).
static QString describeGenericDMXSourceFeature(GenericDMXSource::Feature feature)
{
    switch (feature)
    {
        case GenericDMXSource::DragPositionPush:
            return QStringLiteral("dragging a DMX-position/rotation-driven fixture in the 2D/3D view");
        case GenericDMXSource::PersistedTransformRestore:
            return QStringLiteral("a saved position/rotation being restored onto live DMX channels on project load");
        case GenericDMXSource::PositionPickPoint:
            return QStringLiteral("the 3D view's \"click a point to aim\" Pan/Tilt tool");
        case GenericDMXSource::FixtureConsoleChannelSet:
            return QStringLiteral("a raw channel value set from the Fixture Console/DMX View/Scene fixture console");
        case GenericDMXSource::IntensityTool:
            return QStringLiteral("the left panel's Intensity slider");
        case GenericDMXSource::ColorTool:
            return QStringLiteral("the left panel's Color tool live preview");
        case GenericDMXSource::PositionCenterTool:
            return QStringLiteral("the Position tool's \"center\" button");
        case GenericDMXSource::PresetTool:
            return QStringLiteral("a capability/preset picker (Maintenance/Speed/Prism/Effect tool, "
                                   "Gobo/Color macro picker...)");
        case GenericDMXSource::FixtureHighlight:
            return QStringLiteral("the \"Highlight\" button flashing the selected fixtures");
        case GenericDMXSource::PositionTool:
            return QStringLiteral("the Position tool's Pan/Tilt drag");
        case GenericDMXSource::BeamTool:
            return QStringLiteral("the Beam/zoom tool");
        case GenericDMXSource::PalettePreview:
            return QStringLiteral("Palette Manager's preview of a palette");
        case GenericDMXSource::DumpUndoRedo:
            return QStringLiteral("Tardis replaying a dumped channel value during undo/redo");
        case GenericDMXSource::SceneEditorPreview:
            return QStringLiteral("a Scene Editor's own live preview of the Scene being edited");
        case GenericDMXSource::SceneEditorExternalControlHighlight:
            return QStringLiteral("a Scene Editor's external-control (Virtual Console fader/joystick) "
                                   "fixture/position highlight");
        case GenericDMXSource::Unspecified:
        default:
            return QString(
                "a generic/untagged GenericDMXSource caller (Feature %1) not yet given its own identity - "
                "expected only from engine test facilities").arg(int(feature));
    }
}

QString SimpleDesk::debugChannelInfo(int channel) const
{
    QMutexLocker locker(&m_mutex);
    quint32 start = (m_universeFilter * 512);
    quint32 address = start + quint32(channel);

    quint32 fxID = m_doc->fixtureForAddress(address);
    Fixture *fixture = m_doc->fixture(fxID);
    // NOTE: Fixture::address() returns only the low 9 bits (the address
    // within the fixture's own universe, 0-511), while m_doc->fixtureForAddress()
    // and this method's own "address" are keyed by the full universe-qualified
    // address (universe << 9 | address). Subtracting fixture->address() from
    // "address" here is only correct for universe 0; for any other universe it
    // undercounts the subtraction by (universe * 512), producing a bogus,
    // way-too-large relative channel. Use fixture->universeAddress(), which is
    // in the same domain as "address", instead.
    quint32 relChannel = fixture != nullptr ? address - fixture->universeAddress() : address;

    QString info;
    QTextStream out(&info);

    out << "===== Simple Desk channel debug =====\n";
    out << "Universe " << m_universeFilter << ", channel " << channel
        << " (absolute address " << address << ")\n";

    if (fixture != nullptr)
    {
        const QLCChannel *ch = fixture->channel(relChannel);
        out << "Fixture: " << fixture->name() << " (ID " << fixture->id()
            << "), relative channel " << relChannel;
        if (ch != nullptr)
            out << " \"" << ch->name() << "\" [" << QLCChannel::groupToString(ch->group()) << "]";
        out << "\n";
    }
    else
    {
        out << "No fixture at this address\n";
    }

    bool overridden = m_values.contains(address);
    out << "Simple Desk override: " << (overridden ? "YES" : "no");
    if (overridden)
        out << ", value = " << int(m_values.value(address));
    out << "\n";

    bool dumped = false;
    for (const SceneValue &scv : m_dumpValues)
    {
        if (scv.fxi == fxID && scv.channel == relChannel)
        {
            out << "Queued for Scene dump: value = " << int(scv.value) << "\n";
            dumped = true;
            break;
        }
    }
    if (dumped == false)
        out << "Queued for Scene dump: no\n";

    QList<Universe *> ua = m_doc->inputOutputMap()->claimUniverses();
    if (int(m_universeFilter) < ua.count())
    {
        Universe *uni = ua.at(int(m_universeFilter));
        out << "Universe pre-GM value: " << int(uni->preGMValue(channel)) << "\n";
        out << "Universe post-GM value (actual DMX output): " << int(uni->postGMValue(channel)) << "\n";

        quint32 hash = GenericFader::channelHash(fixture != nullptr ? fxID : Fixture::invalidId(), relChannel);
        QList<QSharedPointer<GenericFader> > faders = uni->faders();
        bool anyFader = false;

        out << "Active faders touching this channel:\n";
        for (QSharedPointer<GenericFader> fader : faders)
        {
            if (fader.isNull())
                continue;

            QHash<quint32, FadeChannel> channels = fader->channels();
            if (channels.contains(hash) == false)
                continue;

            anyFader = true;
            FadeChannel fc = channels.value(hash);
            QString funcName = "(none)";
            Function *f = nullptr;
            if (fader->parentFunctionID() != Function::invalidId())
            {
                f = m_doc->function(fader->parentFunctionID());
                if (f != nullptr)
                    funcName = QString("%1 (ID %2)").arg(f->name()).arg(f->id());
            }

            out << "  - fader \"" << fader->name() << "\", function " << funcName
                << ", priority " << fader->priority()
                << ", intensity " << fader->intensity();
            if (fader->isPaused())
                out << ", PAUSED";
            if (fader->isFadingOut())
                out << ", FADING OUT";
            out << "\n";
            out << "    start=" << int(fc.start()) << " current=" << int(fc.current())
                << " target=" << int(fc.target())
                << " fadeTime=" << fc.fadeTime() << "ms elapsed=" << fc.elapsed() << "ms"
                << " ready=" << (fc.isReady() ? "yes" : "no")
                << " flags=[" << fadeChannelFlagsToString(fc.flags()) << "]\n";

            // WHO started the Function driving this fader? GenericFader::parentFunctionID()
            // above is only WHICH Function owns the fader (e.g. a Scene's own ID) - the actual
            // start source is tracked separately on the Function itself, via Function::start()'s
            // FunctionParent argument (see Function::sources()).
            if (f != nullptr)
            {
                QList<FunctionParent> sources = f->sources();
                if (sources.isEmpty())
                {
                    out << "    Started by: (none currently recorded on the Function - "
                           "its stop is likely already pending, e.g. fading out)\n";
                }
                else
                {
                    for (const FunctionParent &source : sources)
                        out << "    Started by: " << describeFunctionParent(source, m_doc, m_view, f->id()) << "\n";
                }
            }
            else
            {
                // No owning Function at all - this fader was requested directly by
                // some other DMXSource. Simple Desk owns its own fader(s) (it is a
                // DMXSource in its own right, not a GenericDMXSource user - see
                // m_fadersMap), so check that first...
                bool isSimpleDeskFader = m_fadersMap.values().contains(fader);
                if (isSimpleDeskFader)
                {
                    out << "    Started by: Simple Desk's own manual channel override "
                           "(slider/keypad set on this channel)\n";
                }
                else
                {
                    // ...otherwise see if a live GenericDMXSource instance (ContextManager's
                    // 2D/3D/dump-value source, or a SceneEditor's preview/highlight source)
                    // tagged this exact fader/channel with a Feature (see genericdmxsource.h).
                    GenericDMXSource::Feature feature;
                    if (GenericDMXSource::findFeatureForFader(fader.data(), fxID, relChannel, feature))
                    {
                        out << "    Started by: " << describeGenericDMXSourceFeature(feature) << "\n";
                    }
                    else
                    {
                        // Neither Simple Desk nor any live GenericDMXSource claims this
                        // fader. The remaining known fader owners that never set a
                        // parent Function ID or go through GenericDMXSource are Virtual
                        // Console widgets in "level"/relative mode (VCSlider, VCXYPad,
                        // VCAudioTriggers) and CueStack/ScriptRunner - none of these are
                        // tagged by this debug tool today.
                        out << "    Started by: (unidentified - no live GenericDMXSource or "
                               "Simple Desk instance claims this channel; likely a Virtual "
                               "Console widget, CueStack, or Script fader, none of which "
                               "currently tag their start source)\n";
                    }
                }
            }
        }
        if (anyFader == false)
            out << "  (none)\n";
    }
    else
    {
        out << "Universe pre/post-GM value: universe not available\n";
    }
    m_doc->inputOutputMap()->releaseUniverses(false);

    qDebug().noquote() << info;

    return info;
}

void SimpleDesk::resetUniverse(int universe)
{
    // remove values previously set on universe
    QMutexLocker locker(&m_mutex);
    QHashIterator <uint,uchar> it(m_values);
    while (it.hasNext() == true)
    {
        it.next();
        uint absChannel = it.key();
        int uni = absChannel >> 9;
        if (uni == universe)
        {
            m_values.remove(absChannel);

            // remove the override flag from the displayed channel
            QModelIndex mIndex = m_channelList->index(int(absChannel & 0x01FF), 0, QModelIndex());
            //uchar chValue = uchar(m_prevUniverseValues[uni].at(absChannel & 0x01FF));
            m_channelList->setData(mIndex, 0, UserRoleChannelValue);
            m_channelList->setData(mIndex, false, UserRoleChannelOverride);
        }
    }

    // add command to queue. Will be taken care of at the next writeDMX call
    m_commandQueue.append(QPair<int,quint32>(ResetUniverse, universe));
    setChanged(true);
}

void SimpleDesk::resetChannel(uint channel)
{
    QMutexLocker locker(&m_mutex);
    quint32 start = (m_universeFilter * 512);
    QVariant currentVal;
    SceneValue currScv;

    if (m_values.contains(start + channel) == false)
        return;

    currScv.channel = channel;
    currScv.value = m_values[start + channel];
    currentVal.setValue(currScv);
    m_values.remove(start + channel);

    // add command to queue. Will be taken care of at the next writeDMX call
    m_commandQueue.append(QPair<int,quint32>(ResetChannel, start + channel));

    Tardis::instance()->enqueueAction(Tardis::SimpleDeskResetChannel, 0, currentVal, currentVal);

    setChanged(true);
}

int SimpleDesk::getSlidersNumber() const
{
    return WEB_SD_CHANNELS_PER_PAGE;
}

int SimpleDesk::getCurrentUniverseIndex() const
{
    const int idx = int(m_universeFilter);
    return idx < 0 ? 0 : idx;
}

int SimpleDesk::getCurrentPage() const
{
    return 1;
}

uchar SimpleDesk::getAbsoluteChannelValue(uint address) const
{
    QMutexLocker locker(&m_mutex);
    if (m_values.contains(address))
        return m_values.value(address);

    locker.unlock();

    if (address >= (m_doc->inputOutputMap()->universesCount() * 512))
        return 0;

    QList<Universe*> ua = m_doc->inputOutputMap()->claimUniverses();
    int uni = address >> 9;
    uint channel = address & 0x01FF;
    if (uni >= ua.count())
    {
        m_doc->inputOutputMap()->releaseUniverses(false);
        return 0;
    }
    uchar value = ua.at(uni)->preGMValue(channel);
    m_doc->inputOutputMap()->releaseUniverses(false);
    return value;
}

bool SimpleDesk::isChannelOverridden(uint address) const
{
    QMutexLocker locker(&m_mutex);
    return m_values.contains(address);
}

void SimpleDesk::setAbsoluteChannelValue(uint address, uchar value)
{
    if (address >= (m_doc->inputOutputMap()->universesCount() * 512))
        return;

    QMutexLocker locker(&m_mutex);
    m_values[address] = value;

    int uni = address >> 9;
    uint channel = address & 0x01FF;
    if (uni == int(m_universeFilter))
    {
        QModelIndex mIndex = m_channelList->index(int(channel), 0, QModelIndex());
        m_channelList->setData(mIndex, value, UserRoleChannelValue);
        m_channelList->setData(mIndex, true, UserRoleChannelOverride);
    }

    setChanged(true);
}

void SimpleDesk::resetAbsoluteChannel(uint address)
{
    QMutexLocker locker(&m_mutex);
    if (m_values.contains(address) == false)
        return;

    m_values.remove(address);

    int uni = address >> 9;
    uint channel = address & 0x01FF;
    if (uni == int(m_universeFilter))
    {
        QModelIndex mIndex = m_channelList->index(int(channel), 0, QModelIndex());
        m_channelList->setData(mIndex, 0, UserRoleChannelValue);
        m_channelList->setData(mIndex, false, UserRoleChannelOverride);
    }

    m_commandQueue.append(QPair<int,quint32>(ResetChannel, address));
    setChanged(true);
}

void SimpleDesk::slotUniverseWritten(quint32 idx, const QByteArray& ua)
{
    if (idx != m_universeFilter) // || isEnabled() == false)
        return;

    if (m_prevUniverseValues.contains(idx) == false)
        m_prevUniverseValues[idx].fill(0, 512);

    QByteArray currUni = m_prevUniverseValues.value(idx);

    for (int i = 0; i < ua.length(); i++)
    {
        if (ua.at(i) == currUni.at(i))
            continue;

        //qDebug() << "Channel " << i << "changed to " << QString::number(uchar(ua.at(i)));

        // update displayed channel only if it is not overridden
        if (hasChannel(i) == false)
        {
            QModelIndex mIndex = m_channelList->index(int(i), 0, QModelIndex());
            m_channelList->setData(mIndex, QVariant(uchar(ua.at(i))), UserRoleChannelValue);
        }
    }

    m_prevUniverseValues[idx].replace(0, ua.length(), ua);
}

/*********************************************************************
 * DMX channels dump
 *********************************************************************/

void SimpleDesk::setDumpValue(quint32 fxID, quint32 channel, uchar value)
{
    QVariant currentVal, newVal;
    SceneValue sValue(fxID, channel, value);
    int valIndex = m_dumpValues.indexOf(sValue);
    uchar currDmxValue = valIndex >= 0 ? m_dumpValues.at(valIndex).value : 0;
    currentVal.setValue(SceneValue(fxID, channel, currDmxValue));
    newVal.setValue(sValue);

    if (currentVal != newVal || value != currDmxValue)
    {
        if (valIndex >= 0)
        {
            m_dumpValues.replace(valIndex, sValue);
        }
        else
        {
            m_dumpValues.append(sValue);
            emit dumpValuesCountChanged();

            const QLCChannel *ch = m_doc->fixture(fxID)->channel(channel);
            if (ch != nullptr)
            {
                if (ch->group() == QLCChannel::Intensity)
                {
                    if (ch->colour() == QLCChannel::NoColour)
                        m_dumpChannelMask |= App::DimmerType;
                    else
                        m_dumpChannelMask |= App::ColorType;
                }
                else
                {
                    m_dumpChannelMask |= (1 << ch->group());
                }
                emit dumpChannelMaskChanged();
            }
        }

        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture)
            fixture->checkAlias(channel, value);
    }
}

void SimpleDesk::unsetDumpValue(quint32 fxID, quint32 channel)
{
    SceneValue sValue(fxID, channel, 0);
    int valIndex = m_dumpValues.indexOf(sValue);

    if (valIndex >= 0)
    {
        m_dumpValues.removeAt(valIndex);
        emit dumpValuesCountChanged();
    }
}

int SimpleDesk::dumpValuesCount() const
{
    int i = 0;
    QList<quint32> fixtureList;

    for (SceneValue scv : m_dumpValues)
        if (!fixtureList.contains(scv.fxi))
            fixtureList.append(scv.fxi);

    if (fixtureList.isEmpty())
        return m_dumpValues.count();

    for (SceneValue sv : m_dumpValues)
        if (fixtureList.contains(sv.fxi))
            i++;

    return i;
}

int SimpleDesk::dumpChannelMask() const
{
    return m_dumpChannelMask;
}

void SimpleDesk::dumpDmxChannels(QString name, quint32 mask, int sceneID, bool nonZeroOnly)
{
    QList<quint32> fixtureList;

    for (SceneValue &scv : m_dumpValues)
        if (!fixtureList.contains(scv.fxi))
            fixtureList.append(scv.fxi);

    m_functionManager->dumpDmxValues(m_dumpValues, fixtureList, mask, name,
                                     sceneID == -1 ? Function::invalidId() : sceneID, nonZeroOnly);
}

/************************************************************************
 * Keypad
 ************************************************************************/

bool SimpleDesk::sendKeypadCommand(QString command)
{
    if (command.isEmpty())
        return false;

    QByteArray uniData = m_prevUniverseValues.value(m_universeFilter);
    QList<SceneValue> scvList = m_keyPadParser->parseCommand(m_doc, command.toUpper(), uniData);

    for (SceneValue &scv : scvList)
    {
        quint32 fxID = m_doc->fixtureForAddress((m_universeFilter * 512) + scv.channel);
        Fixture *fixture = m_doc->fixture(fxID);
        if (fixture != nullptr)
            setValue(fxID, scv.channel - fixture->address(), scv.value);
        else
            setValue(fxID, scv.channel, scv.value);
        QModelIndex mIndex = m_channelList->index(int(scv.channel), 0, QModelIndex());
        m_channelList->setData(mIndex, QVariant(scv.value), UserRoleChannelValue);
    }

    m_keypadCommandHistory.prepend(command.toUpper());
    if (m_keypadCommandHistory.count() > MAX_KEYPAD_HISTORY)
        m_keypadCommandHistory.removeLast();

    emit commandHistoryChanged();

    return true;
}

QStringList SimpleDesk::commandHistory() const
{
    return m_keypadCommandHistory;
}

/************************************************************************
 * DMXSource
 ************************************************************************/

FadeChannel *SimpleDesk::getFader(const QList<Universe *> universes, quint32 universeID, quint32 fixtureID, quint32 channel)
{
    qDebug() << "[Simple Desk] get fader for universe" << universeID << "fixture" << fixtureID << "channel" << channel;

    // get the universe Fader first. If doesn't exist, create it
    QSharedPointer<GenericFader> fader = m_fadersMap.value(universeID, QSharedPointer<GenericFader>());
    if (fader.isNull())
    {
        fader = universes[universeID]->requestFader(Universe::SimpleDesk);
        m_fadersMap[universeID] = fader;
    }

    return fader->getChannelFader(m_doc, universes[universeID], fixtureID, channel);
}

void SimpleDesk::writeDMX(MasterTimer *timer, QList<Universe *> ua)
{
    Q_UNUSED(timer)

    QMutexLocker locker(&m_mutex);

    if (m_commandQueue.isEmpty() == false)
    {
        for (int i = 0; i < m_commandQueue.count(); i++)
        {
            QPair<int,quint32> command = m_commandQueue.at(i);
            if (command.first == ResetUniverse)
            {
                quint32 universe = command.second;
                if (universe >= (quint32)ua.count())
                    continue;

                ua[universe]->reset(0, 512);

                QSharedPointer<GenericFader> fader = m_fadersMap.value(universe, QSharedPointer<GenericFader>());
                if (!fader.isNull())
                {
                    // loop through all active fadechannels and restore default values
                    QHashIterator<quint32, FadeChannel> it(fader->channels());
                    while (it.hasNext() == true)
                    {
                        it.next();
                        FadeChannel fc = it.value();
                        Fixture *fixture = m_doc->fixture(fc.fixture());
                        quint32 chIndex = fc.channel() & 0x01FF;
                        if (fixture != NULL)
                        {
                            const QLCChannel *ch = fixture->channel(chIndex);
                            if (ch != NULL)
                            {
                                //qDebug() << "Restoring default value of fixture" << fixture->id()
                                //         << "channel" << chIndex << "value" << ch->defaultValue();
                                ua[universe]->setChannelDefaultValue(fixture->address() + chIndex, ch->defaultValue());
                            }
                        }
                        else
                        {
                            ua[universe]->reset(chIndex, 1);
                        }
                    }
                    ua[universe]->dismissFader(fader);
                    m_fadersMap.remove(universe);
                }

                // reset DMX dump as well
                m_dumpValues.clear();
                emit dumpValuesCountChanged();

                m_dumpChannelMask = 0;
                emit dumpChannelMaskChanged();
            }
            else if (command.first == ResetChannel)
            {
                quint32 address = command.second;
                quint32 universe = address >> 9;
                QSharedPointer<GenericFader> fader = m_fadersMap.value(universe, QSharedPointer<GenericFader>());
                if (!fader.isNull())
                {
                    // Build the same (fixture, channel) key used when this channel's
                    // FadeChannel was added in the hasChanged() block above, otherwise
                    // GenericFader::remove()'s hash lookup misses and the stale entry
                    // is left latching its last value into the universe forever.
                    quint32 fxID = m_doc->fixtureForAddress(address);
                    Fixture *fixture = m_doc->fixture(fxID);
                    quint32 chIndex = fixture != nullptr ? address - fixture->address() : address;

                    FadeChannel fc(m_doc, fxID, chIndex);
                    fader->remove(&fc);
                    ua[universe]->reset(address & 0x01FF, 1);
                    if (fixture != NULL)
                    {
                        const QLCChannel *ch = fixture->channel(chIndex);
                        if (ch != NULL)
                        {
                            qDebug() << "Restoring default value of fixture" << fixture->id()
                                     << "channel" << chIndex << "value" << ch->defaultValue();
                            ua[universe]->setChannelDefaultValue(address & 0x01FF, ch->defaultValue());
                        }
                    }

                    // remove the override flag from the displayed channel
                    QModelIndex mIndex = m_channelList->index(int(address & 0x01FF), 0, QModelIndex());
                    m_channelList->setData(mIndex, false, UserRoleChannelOverride);
                }
            }
        }
        m_commandQueue.clear();
    }

    if (hasChanged())
    {
        QHashIterator <uint,uchar> it(m_values);
        while (it.hasNext() == true)
        {
            it.next();
            int uni = it.key() >> 9;
            int address = it.key();
            uchar value = it.value();
            quint32 fxID = m_doc->fixtureForAddress(address);
            quint32 channel = address;

            if (fxID != Fixture::invalidId())
            {
                Fixture *fixture = m_doc->fixture(fxID);
                if (fixture != nullptr)
                    channel = address - fixture->address();
            }
            FadeChannel *fc = getFader(ua, uni, fxID, channel);
            fc->setCurrent(value);
            fc->setTarget(value);
            fc->addFlag(FadeChannel::Override);
        }
        setChanged(false);
    }
}
