/*
  Q Light Controller Plus - Control API
  apivclivedomain.h

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

#ifndef APIVCLIVEDOMAIN_H
#define APIVCLIVEDOMAIN_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "apivchost.h"

class ApiDispatcher;
class ApiServer;
class ApiSession;
class Doc;

/**
 * The XY Pad / Clock / Animation / Audio Triggers slice of the vc.* domain
 * (docs/api-spec/fragments/virtualconsole.yaml), a third class next to ApiVcDomain and
 * ApiVcLayoutDomain sharing the same ApiVcHost seam:
 *  - vc.xyPad.fixture.add / fixture.remove / setHeadsRange (§4a, broadcast on
 *    vc.xyPad.fixturesChanged), vc.xyPad.preset.move / preset.rename (§4a, vc.xyPad.presetsChanged),
 *    vc.xyPad.setFloorPosition (§4b, vc.xyPad.floorPositionChanged)
 *  - vc.clock.schedule.add / update / remove (§4a, vc.clock.schedulesChanged), vc.clock.playPause /
 *    reset (§4b, vc.clock.timeChanged)
 *  - vc.animation.preset.move (§4a, vc.animation.presetsChanged), vc.animation.setFaderLevel /
 *    setPresetKnobValue (§4b, vc.animation.faderLevelChanged / activePresetChanged)
 *  - vc.audioTriggers.setBarConfig (§4a, vc.audioTriggers.barsChanged), vc.audioTriggers.
 *    setCaptureEnabled (§4b, vc.audioTriggers.captureEnabledChanged)
 * The preset add / remove / apply of pads and animations go through ApiVcDomain's generic
 * vc.widget.preset.* (the host dispatches on the widget type); the typeConfig of the four types is
 * served by vc.widget.get / setConfig (qmlui/app_apivcconfig_live.cpp).
 *
 * Live events: this class is the host's ApiVcLiveListenerExt (registered in the constructor,
 * detached in the destructor) and broadcasts every callback - with the requesting client as
 * originClientId while one of its own live requests is being served (m_liveOriginClientId), null
 * otherwise. vc.clock.timeChanged and vc.audioTriggers.levelsChanged are subscribe-gated (a running
 * stopwatch ticks at 10 Hz, the audio capture faster still); the rest goes to every session like
 * ApiVcDomain's live events.
 *
 * Every structural mutation also re-broadcasts the widget on vc.widget.configChanged: the affected
 * lists (fixtures, presets, schedules, bars) ride along read-only in typeConfig, and a client that
 * only follows the generic widget events must not keep a stale copy.
 */
class ApiVcLiveDomain : public QObject, public ApiVcLiveListenerExt
{
    Q_OBJECT

public:
    ApiVcLiveDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiVcLiveDomain() override;

    /** @reimp ApiVcLiveListenerExt */
    void vcXyPadFloorPositionChanged(quint32 widgetId, double x, double y, double z) override;
    void vcXyPadActivePresetChanged(quint32 widgetId, int presetId) override;
    void vcClockTimeChanged(quint32 widgetId, int currentTime, bool running) override;
    void vcAnimationFaderLevelChanged(quint32 widgetId, int level) override;
    void vcAnimationActivePresetChanged(quint32 widgetId, int presetId, int knobPresetId, int knobValue) override;
    void vcAnimationStyleChanged(quint32 widgetId, int algorithmIndex, const QStringList &colors) override;
    void vcAudioTriggersCaptureEnabledChanged(quint32 widgetId, bool enabled) override;
    void vcAudioTriggersLevelsChanged(quint32 widgetId, const QList<int> &levels) override;
    void vcSliderMonitorChanged(quint32 widgetId, int monitorValue, bool isOverriding) override;
    void vcXyPadFixturePositionsChanged(quint32 widgetId, const QList<QPointF> &positions) override;

private:
    /** vc.slider.resetOverride (the monitor readback of Level sliders rides along here). */
    void registerSliderMethods(ApiDispatcher *d);
    void registerXyPadMethods(ApiDispatcher *d);
    void registerClockMethods(ApiDispatcher *d);
    void registerAnimationMethods(ApiDispatcher *d);
    void registerAudioTriggersMethods(ApiDispatcher *d);

    ApiVcHost *vcHost() const;

    /** Sends ErrInternal and returns nullptr when no host is attached. */
    ApiVcHost *requireHost(ApiSession *session, const QString &id);

    /** §4a front half: CONFLICT (with the current revision in details) on a stale baseRevision. */
    bool checkRevision(ApiSession *session, const QString &id, const QJsonObject &params);

    /** Resolves params.widgetId to an existing widget of one of $allowedTypes, answering NOT_FOUND /
     *  INVALID_PARAMS itself; with $live also refuses a disabled widget (INVALID_STATE), like
     *  ApiVcDomain::resolveLiveWidget(). */
    bool resolveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                       const QStringList &allowedTypes, bool live, ApiVcHost *host, quint32 *outId);

    /** {docRevision} ok response after Doc::setModified(). */
    void replyRevision(ApiSession *session, const QString &id);

    /** vc.widget.configChanged with the widget's full snapshot plus the slice's own list event
     *  ($topic with $listKey = the matching typeConfig array), both carrying docRevision. */
    void broadcastStructural(ApiVcHost *host, quint32 widgetId, ApiSession *session,
                             const QString &topic, const QString &listKey);

    /** Live (§4b) event with widgetId and m_liveOriginClientId folded in. */
    void broadcastLive(const QString &topic, quint32 widgetId, QJsonObject data, bool gated = false);

    static bool parseWidgetId(const QString &s, quint32 &outId);

    Doc *m_doc;
    ApiServer *m_server;

    /** The client whose live request is being served right now (empty otherwise) - see the class
     *  comment. */
    QString m_liveOriginClientId;
};

#endif
