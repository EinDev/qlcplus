/*
  Q Light Controller Plus - Control API
  apivcdomain.h

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

#ifndef APIVCDOMAIN_H
#define APIVCDOMAIN_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "apivchost.h"

class ApiDispatcher;
class ApiServer;
class ApiSession;
class Doc;

/**
 * Implementation of the "vc.*" domain (docs/api-spec/fragments/virtualconsole.yaml), scoped to:
 *  - structural (§4a): vc.page.{list,create,delete,rename,setPin,validatePin,select} and
 *    vc.widget.{list,get,create,update,setConfig,delete,reparent,reposition}
 *  - live interaction (§4b): vc.button.press, vc.slider.setValue, vc.cueList.{play,stop,next,
 *    previous,setPlaybackIndex,get}, vc.xyPad.setPosition, vc.speedDial.{setValue,tap},
 *    vc.frame.{gotoPage,get}, plus the matching vc.button.stateChanged / vc.slider.valueChanged /
 *    vc.cueList.playbackChanged / vc.xyPad.positionChanged / vc.speedDial.valueChanged /
 *    vc.frame.pageChanged events (broadcast to every session, not subscribe-gated).
 *  - cue list / speed dial extras: vc.cueList.setSideFaderLevel (+ vc.cueList.sideFaderChanged),
 *    vc.speedDial.{setFactor,apply,resetTap} (+ vc.speedDial.factorChanged / tapChanged), and the
 *    generic widget presets vc.widget.preset.{add,apply,remove} + vc.speedDial.preset.update
 *    (+ vc.speedDial/xyPad/animation.presetsChanged) - see the section at the end of this class.
 * Every other vc.* method in the spec (usage, createMatrix, createFromFunctions, align, distribute,
 * bulkStyle, inputSource.*, keySequence.*, inputDetect.*, vc.slider.flash, vc.xyPad.floor/
 * fixture/preset.move/rename, vc.frame.setPin/cloneFirstPage, vc.clock.*, vc.animation.*, vc.audioTriggers.*,
 * ...) is deliberately NOT registered here - left for a future pass. An unregistered method name is
 * not a crash: ApiDispatcher::dispatch() already responds NOT_FOUND for any method nobody registered.
 *
 * All request-shape validation (baseRevision/docRevision checks, "is this a known widget type",
 * "does this parent exist and accept children", cycle detection on reparent, "is this widget really
 * a Button") lives here. The actual page/widget object graph is owned by whatever ApiVcHost
 * implementation is running the process - qmlui's App in production, a headless FakeVcHost in
 * controlapi/test/apivcdomain - obtained via dynamic_cast on ApiServer's parent, exactly like
 * ApiCoreDomain::projectHost() does for ApiProjectHost. See apivchost.h for why this seam exists
 * (controlapi must build without qmlui) and exactly what each side of it assumes.
 *
 * Live events: this class is also the host's ApiVcLiveListener (registered in the constructor,
 * detached in the destructor). The host calls back for every live-state change regardless of cause;
 * m_liveOriginClientId is set for the duration of a live request's host call so a change caused by
 * that request is broadcast with the requester's clientId as originClientId, while a change caused by
 * anything else (QML UI, external input, a Function stopping) goes out with a null origin. That works
 * because everything here runs synchronously on the host's GUI thread (see apiserver.h).
 */
class ApiVcDomain : public QObject, public ApiVcLiveListener
{
    Q_OBJECT

public:
    ApiVcDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiVcDomain() override;

    /** @reimp ApiVcLiveListener */
    void vcButtonStateChanged(quint32 widgetId, const QString &state) override;
    void vcSliderValueChanged(quint32 widgetId, int value) override;
    void vcCueListPlaybackChanged(quint32 widgetId, int playbackIndex, bool running, bool paused) override;
    void vcXyPadPositionChanged(quint32 widgetId, double x, double y) override;
    void vcSpeedDialValueChanged(quint32 widgetId, int ms) override;
    void vcFramePageChanged(quint32 widgetId, int page) override;

private:
    void registerPageMethods(ApiDispatcher *d);
    void registerWidgetMethods(ApiDispatcher *d);
    void registerLiveMethods(ApiDispatcher *d);

    ApiVcHost *vcHost() const;

    /** Shared front half of every live method: resolves params.widgetId, answers NOT_FOUND (unknown
     *  id), INVALID_PARAMS (widget exists but its wire type is not in $allowedTypes) or INVALID_STATE
     *  (widget is disabled - the on-screen widget refuses input then too) itself and returns false;
     *  on true, $outHost and $outId are set and nothing has been sent yet. */
    bool resolveLiveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                           const QStringList &allowedTypes, ApiVcHost **outHost, quint32 *outId);

    /** Broadcasts a live event ($data plus widgetId) with m_liveOriginClientId as its origin. */
    void broadcastLive(const QString &topic, quint32 widgetId, QJsonObject data);

    /** True if $ancestorCandidate is $id itself or anywhere in $id's ancestor chain (queried live via
     *  vcHost()->vcWidgetParentId()) - used to reject a reparent that would make a widget its own
     *  descendant. */
    bool isSelfOrAncestorOf(ApiVcHost *host, quint32 ancestorCandidate, quint32 id) const;

    static bool parseWidgetId(const QString &s, quint32 &outId);

    Doc *m_doc;
    ApiServer *m_server;

    /** Non-null only while a live method's host call is on the stack - see class comment. */
    QString m_liveOriginClientId;

    /*********************************************************************
     * Cue List side fader, Speed Dial extras and widget presets
     * (vc.cueList.setSideFaderLevel, vc.speedDial.setFactor/apply/resetTap, vc.speedDial.preset.update,
     * vc.widget.preset.add/apply/remove and the sideFaderChanged/factorChanged/tapChanged/
     * *.presetsChanged events) - implemented at the end of apivcdomain.cpp.
     *********************************************************************/
public:
    /** @reimp ApiVcLiveListener */
    void vcCueListSideFaderChanged(quint32 widgetId, int level, int nextStepIndex, bool primaryTop) override;
    void vcSpeedDialFactorChanged(quint32 widgetId, const QString &factor) override;
    void vcSpeedDialTapChanged(quint32 widgetId, int tapTimeValue, int currentTimeMs) override;

private:
    void registerCueSpeedDialMethods(ApiDispatcher *d);
    void registerPresetMethods(ApiDispatcher *d);

    /** Shared front half of the structural preset methods: baseRevision check, widgetId resolution and
     *  the "this widget type has presets" check (INVALID_PARAMS otherwise). On true nothing has been
     *  sent yet and $outHost/$outId are set. */
    bool resolvePresetWidget(ApiSession *session, const QString &id, const QJsonObject &params, bool checkRevision,
                             ApiVcHost **outHost, quint32 *outId);

    /** Broadcasts vc.<type>.presetsChanged (speedDial / xyPad / animation, by the widget's wire type)
     *  with the widget's full preset list and the current docRevision. */
    void broadcastPresetsChanged(ApiVcHost *host, quint32 widgetId, const QString &originClientId);
};

#endif
