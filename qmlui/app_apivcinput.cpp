/*
  Q Light Controller Plus
  app_apivcinput.cpp

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

/*
 * App's ApiVcHost implementation of the external-controls slice (ApiVcInputDomain, see
 * controlapi/src/apivchost.h for each method's contract): the input sources, key sequences and
 * external control table of a VC widget. Mirrors what ExternalControls.qml / ExternalControlDelegate.qml
 * / KeyboardSequenceDelegate.qml / PopupManualInputSource.qml / PopupCustomFeedback.qml call on
 * VirtualConsole and VCWidget, one to one:
 *  - manual source: VirtualConsole::createAndAddInputSource() (source + VCPage::mapInputSource on
 *    every page), the control combo: VCWidget::updateInputSourceControlID(), the custom feedback
 *    popup: updateInputSourceFeedbackValues() / updateInputSourceExtraParams(), remove:
 *    VirtualConsole::deleteInputSource().
 *  - keys: the learn's tail (VCWidget::updateKeySequence + VCPage::mapKeySequence), remove:
 *    VirtualConsole::deleteKeySequence().
 * Doc::setModified() is the domain's job (the VCWidget setters already flag the doc, the domain
 * bumps the revision once per request).
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeySequence>
#include <QSharedPointer>

#include "app.h"
#include "inputoutputmap.h"
#include "inputpatch.h"
#include "qlcinputchannel.h"
#include "qlcinputprofile.h"
#include "qlcinputsource.h"
#include "virtualconsole/virtualconsole.h"
#include "virtualconsole/vcpage.h"
#include "virtualconsole/vcwidget.h"

namespace {

/** The 1-based MIDI feedback table index PopupCustomFeedback.qml shows, or -1 when the source has
 *  no integer extra param for $type (OSC profiles store a path string there; a fresh source none). */
int feedbackChannelIndex(const QSharedPointer<QLCInputSource> &source, QLCInputFeedback::FeedbackType type)
{
    QVariant extra = source->feedbackExtraParams(type);
    if (extra.isValid() == false || extra.userType() != QMetaType::Int || extra.toInt() < 0)
        return -1;
    return extra.toInt() + 1;
}

} // namespace

QJsonArray App::vcWidgetExternalControls(quint32 id) const
{
    QJsonArray arr;
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return arr;

    for (quint8 controlId : w->externalControlIds())
    {
        QJsonObject c;
        c.insert(QStringLiteral("controlId"), int(controlId));
        c.insert(QStringLiteral("name"), w->externalControlName(controlId));
        c.insert(QStringLiteral("allowKeyboard"), w->externalControlAllowsKeyboard(controlId));
        arr.append(c);
    }
    return arr;
}

QJsonArray App::vcWidgetInputSources(quint32 id) const
{
    QJsonArray arr;
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return arr;

    InputOutputMap *ioMap = m_doc->inputOutputMap();
    for (const QSharedPointer<QLCInputSource> &source : w->inputSources())
    {
        if (source.isNull())
            continue;

        QJsonObject s;
        s.insert(QStringLiteral("controlId"), int(source->id()));
        // double, not int: an abandoned QML auto-detection leaves invalidUniverse/invalidChannel
        // (0xFFFFFFFF) behind, and a composited channel uses the full unsigned range.
        s.insert(QStringLiteral("universe"), double(source->universe()));
        s.insert(QStringLiteral("channel"), double(source->channel()));
        s.insert(QStringLiteral("lowerValue"), int(source->feedbackValue(QLCInputFeedback::LowerValue)));
        s.insert(QStringLiteral("upperValue"), int(source->feedbackValue(QLCInputFeedback::UpperValue)));
        s.insert(QStringLiteral("monitorValue"), int(source->feedbackValue(QLCInputFeedback::MonitorValue)));

        int idx = feedbackChannelIndex(source, QLCInputFeedback::LowerValue);
        if (idx >= 0) s.insert(QStringLiteral("lowerChannel"), idx);
        idx = feedbackChannelIndex(source, QLCInputFeedback::UpperValue);
        if (idx >= 0) s.insert(QStringLiteral("upperChannel"), idx);
        idx = feedbackChannelIndex(source, QLCInputFeedback::MonitorValue);
        if (idx >= 0) s.insert(QStringLiteral("monitorChannel"), idx);

        // Additive display fields, the same VCWidget::inputSourcesList() computes for the QML delegate.
        QString uniName, chName;
        bool valid = source->isValid() && ioMap->inputSourceNames(source, uniName, chName);
        if (valid == false)
        {
            uniName = source->isValid() ? QStringLiteral("Universe %1").arg(source->universe() + 1) : tr("None");
            chName = source->isValid() ? QStringLiteral("Channel %1").arg((source->channel() & 0xFFFF) + 1) : tr("None");
        }
        bool supportsCustomFeedback = false;
        InputPatch *ip = source->isValid() ? ioMap->inputPatch(source->universe()) : nullptr;
        if (ip != nullptr && ip->profile() != nullptr)
        {
            QLCInputChannel *ich = ip->profile()->channel(source->channel() & 0xFFFF);
            if (ich != nullptr && ich->type() == QLCInputChannel::Button)
                supportsCustomFeedback = true;
        }
        s.insert(QStringLiteral("universeName"), uniName);
        s.insert(QStringLiteral("channelName"), chName);
        s.insert(QStringLiteral("supportsCustomFeedback"), supportsCustomFeedback);
        s.insert(QStringLiteral("invalid"), source->isValid() == false);
        arr.append(s);
    }
    return arr;
}

QJsonArray App::vcWidgetKeySequences(quint32 id) const
{
    QJsonArray arr;
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
        return arr;

    QMap<QKeySequence, quint32> map = w->keySequenceMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
    {
        if (it.key().isEmpty())
            continue; // a QML key learn still waiting for its key
        QJsonObject k;
        k.insert(QStringLiteral("keySequence"), it.key().toString(QKeySequence::PortableText));
        k.insert(QStringLiteral("controlId"), int(it.value()));
        arr.append(k);
    }
    return arr;
}

bool App::vcWidgetInputSourceSet(quint32 id, quint32 controlId, quint32 universe, quint32 channel,
                                 const QJsonObject &feedback, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    // A source inside a multipage frame only fires on its own page: the engine's own learn folds the
    // widget's page into the channel (VCWidget::updateInputSource -> setPage), the manual QML popup
    // does not. Do it here unless the caller already sent a composited channel.
    if ((channel >> 16) == 0 && w->page() > 0)
        channel |= quint32(w->page()) << 16;

    // (universe, channel) identifies a source within a widget - see VCWidget::inputSource(uni, ch).
    QSharedPointer<QLCInputSource> existing;
    for (const QSharedPointer<QLCInputSource> &source : w->inputSources())
    {
        if (source.isNull() == false && source->universe() == universe && source->channel() == channel)
        {
            existing = source;
            break;
        }
    }

    if (existing.isNull())
    {
        // VirtualConsole::createAndAddInputSource(), with the requested control instead of its
        // "blind guess" of id 0.
        QSharedPointer<QLCInputSource> source(new QLCInputSource());
        source->setID(controlId);
        source->setUniverse(universe);
        source->setChannel(channel);
        w->addInputSource(source); // applies the input profile's defaults (feedback, working mode)
        for (int i = 0; i < m_virtualConsole->pagesCount(); i++)
            m_virtualConsole->page(i)->mapInputSource(source, w, true);
        existing = source;
    }
    else if (existing->id() != controlId)
    {
        // The pages map keys on (universe, channel) and reads the id from the source object at
        // dispatch time, so re-targeting needs no remap.
        w->updateInputSourceControlID(universe, channel, controlId);
        w->setDocModified();
    }

    // Explicit request values override whatever the profile defaults set.
    bool touched = false;
    uchar lower = existing->feedbackValue(QLCInputFeedback::LowerValue);
    uchar upper = existing->feedbackValue(QLCInputFeedback::UpperValue);
    uchar monitor = existing->feedbackValue(QLCInputFeedback::MonitorValue);
    if (feedback.contains(QStringLiteral("lowerValue"))) { lower = uchar(feedback.value(QStringLiteral("lowerValue")).toInt()); touched = true; }
    if (feedback.contains(QStringLiteral("upperValue"))) { upper = uchar(feedback.value(QStringLiteral("upperValue")).toInt()); touched = true; }
    if (feedback.contains(QStringLiteral("monitorValue"))) { monitor = uchar(feedback.value(QStringLiteral("monitorValue")).toInt()); touched = true; }
    if (touched)
        w->updateInputSourceFeedbackValues(universe, channel, lower, upper, monitor);

    // MIDI feedback routing: wire 1-based table index, 0 = "from plugin settings" (stored -1), exactly
    // what PopupCustomFeedback.qml's combos hand to updateInputSourceExtraParams(). Only integer
    // params are written, and only for the keys sent - an OSC profile keeps its path string.
    static const struct { const char *key; QLCInputFeedback::FeedbackType type; } routing[] = {
        { "lowerChannel", QLCInputFeedback::LowerValue },
        { "upperChannel", QLCInputFeedback::UpperValue },
        { "monitorChannel", QLCInputFeedback::MonitorValue }
    };
    bool routed = false;
    for (const auto &r : routing)
    {
        QString key = QString::fromLatin1(r.key);
        if (feedback.contains(key) == false)
            continue;
        existing->setFeedbackExtraParams(r.type, feedback.value(key).toInt() - 1);
        routed = true;
    }
    if (routed)
    {
        w->setDocModified();
        w->updateFeedback();
    }

    return true;
}

bool App::vcWidgetInputSourceRemove(quint32 id, quint32 controlId, quint32 universe, quint32 channel, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    if (w->inputSource(controlId, universe, channel).isNull())
    {
        if (error) *error = QStringLiteral("No such input source on this widget");
        return false;
    }

    // VirtualConsole::deleteInputSource(): unmap on every page, then drop it from the widget.
    m_virtualConsole->deleteInputSource(w, controlId, universe, channel);
    return true;
}

bool App::vcWidgetKeySequenceSet(quint32 id, quint32 controlId, const QString &keySequence, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    QKeySequence seq = QKeySequence::fromString(keySequence, QKeySequence::PortableText);
    if (seq.isEmpty())
    {
        if (error) *error = QStringLiteral("Invalid key sequence");
        return false;
    }

    // Already bound on this widget? Unmap the old (sequence, control) pair from every page first:
    // VCPage::mapKeySequence() only dedupes identical pairs, so a changed control id would leave the
    // stale pair behind and the widget would receive the key twice.
    QMap<QKeySequence, quint32> map = w->keySequenceMap();
    if (map.contains(seq))
    {
        quint32 oldId = map.value(seq);
        if (oldId == controlId)
            return true; // nothing to do
        for (int i = 0; i < m_virtualConsole->pagesCount(); i++)
            m_virtualConsole->page(i)->unMapKeySequence(seq, oldId, w, true);
    }

    w->addKeySequence(seq, controlId);
    for (int i = 0; i < m_virtualConsole->pagesCount(); i++)
        m_virtualConsole->page(i)->mapKeySequence(seq, controlId, w, true);
    return true;
}

bool App::vcWidgetKeySequenceRemove(quint32 id, const QString &keySequence, QString *error)
{
    VCWidget *w = vcFindWidget(id);
    if (w == nullptr)
    {
        if (error) *error = QStringLiteral("No such widget");
        return false;
    }

    QKeySequence seq = QKeySequence::fromString(keySequence, QKeySequence::PortableText);
    QMap<QKeySequence, quint32> map = w->keySequenceMap();
    if (seq.isEmpty() || map.contains(seq) == false)
    {
        if (error) *error = QStringLiteral("No such key sequence on this widget");
        return false;
    }

    m_virtualConsole->deleteKeySequence(w, map.value(seq), seq.toString(QKeySequence::PortableText));
    return true;
}
