/*
  Q Light Controller Plus - Control API
  apifunctionsdomain.h

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

#ifndef APIFUNCTIONSDOMAIN_H
#define APIFUNCTIONSDOMAIN_H

#include <QObject>
#include <QJsonObject>
#include <functional>

class ApiServer;
class Doc;
class Function;

/**
 * docs/api-spec/fragments/functions-core.yaml: functions.start/stop/setPause
 * (§4b live/runtime - no baseRevision, no broadcast event, matching a real
 * console: pressing a VC button doesn't itself notify other consoles beyond
 * whatever live state they already observe, e.g. DMX output), plus the
 * generic structural (§4a) CRUD shared by all 10 Function types
 * (functions.list/get/create/delete/rename/move/update), Scene-specific
 * value/membership editing (functions.scene.setValues/setValue/unsetValue/
 * setMembers), and the Chaser/Sequence step CRUD they share
 * (functions.steps.addStep/replaceStep/removeStep/moveStep).
 *
 * functions.get's typeDetail is fully implemented for Scene/Chaser/Sequence
 * only; the other 7 types (EFX/Collection/Script/RGBMatrix/Show/Audio/Video)
 * get a minimal {functionId} placeholder for now - their full detail shapes
 * (FunctionsEfxDetail etc.) are functions-advanced.yaml territory, a
 * deliberately separate future slice, not an oversight.
 *
 * Live run-state (§4b): every summary/detail carries running/paused, and
 * functions.status.changed {id, functionId, running, paused, elapsed} is
 * broadcast (ungated) whenever MasterTimer starts or stops ANY function -
 * regardless of who started it (this API, a VC widget, the QML UI) - and
 * whenever Function::setPause() flips a running function's pause flag.
 * functions.stopAll mirrors the QML toolbar's "stop all functions" action
 * (App::stopAllFunctions() when hosted by qmlui, so it also drops the
 * Function Manager preview and closes fullscreen video; bare
 * MasterTimer::stopAllFunctions() otherwise).
 */
class ApiFunctionsDomain : public QObject
{
    Q_OBJECT

public:
    ApiFunctionsDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

    /** functions.get's typeDetail builder for one Function::Type. The
     *  type-specific domains (EFX, Collection, RGBMatrix, Script, Show, ...)
     *  register theirs from their own constructor instead of editing this
     *  file's typeDetailToJson(); Scene/Chaser/Sequence/Audio/Video are
     *  built in and cannot be overridden. */
    typedef std::function<QJsonObject(Function *)> TypeDetailProvider;
    static void setTypeDetailProvider(int functionType, TypeDetailProvider provider);

    /** typeDetail for any function: the built-in builders, else the
     *  registered provider for its type, else the {functionId} placeholder.
     *  Exposed so other domains broadcast exactly the shape functions.get
     *  returns. */
    static QJsonObject typeDetail(Function *function);

    /** Apply a new media source to an Audio or Video the way
     *  functions.create/update {source} do: a host file is copied into the
     *  project's media store first (MediaAssets::importOrKeep), a scheme://
     *  URL is passed through for Video. The full engine setters rename the
     *  function after the file. Returns false with @error set (the caller
     *  reports INVALID_PARAMS) when the file does not exist, the type takes
     *  no source, or an Audio is given a URL. Shared with ApiMediaDomain's
     *  functions.audio.setSource / functions.video.setSource so every path
     *  into the store is the same one. */
    static bool applyMediaSource(Doc *doc, Function *function, const QString &source, QString *error);

private:
    void registerMethods();

    /** Hook function's pauseChanged signal (string-based connect, engine
     *  DLL) so functions.status.changed also covers pause/resume, which
     *  MasterTimer has no signal for. Called for every Function present at
     *  construction and for each one Doc adds afterwards. */
    void watchFunction(Function *function);

    /** Build + broadcast functions.status.changed for id. running/paused
     *  are passed in rather than re-read because the Function may already
     *  be gone (deleted between MasterTimer's queued emit and delivery
     *  here) - elapsed is only included when it still exists. */
    void broadcastStatus(quint32 id, bool running, bool paused);

private slots:
    /** MasterTimer::functionStarted/functionStopped relays - emitted on the
     *  timer thread, delivered here queued (see apiserver.h). */
    void slotFunctionStarted(quint32 id);
    void slotFunctionStopped(quint32 id);

    /** Function::pauseChanged relay - see watchFunction() */
    void slotFunctionPauseChanged(quint32 id, bool paused);

    /** Doc::functionAdded relay - see watchFunction() */
    void slotFunctionAdded(quint32 id);

    /** MediaAssets re-pointed an Audio/Video at a fresh copy of its origin
     *  (functions.media.reload, the editors' Reload button, the bulk
     *  "Reload changed media" action, or a background copy that landed):
     *  broadcast functions.media.reloaded with the new typeDetail */
    void slotMediaOriginReloaded(quint32 functionId, QString oldPath, QString newPath, quint32 oldDuration);

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
