/*
  Q Light Controller Plus - Control API
  apimediadomain.h

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

#ifndef APIMEDIADOMAIN_H
#define APIMEDIADOMAIN_H

#include <QObject>
#include <QJsonObject>

class ApiServer;
class ApiSession;
class Doc;
class Function;
class Script;

/**
 * docs/api-spec/fragments/functions-advanced.yaml, the three "media-like"
 * function types whose editors are plain property forms rather than
 * timelines: Script (functions.script.setSource/appendLine/listCommands/
 * validate), Audio (functions.audio.setSource/setVolume/setDuration/
 * setDevice/listCapabilities) and Video (functions.video.setSource/
 * setGeometry/setRotation/setLayer/setScreenTarget/listCapabilities).
 *
 * All setters are §4a structural edits (baseRevision-gated, bump
 * docRevision, broadcast the matching functions.<type>.<prop>Changed event,
 * ungated). The listCommands/listCapabilities/validate methods are plain
 * queries.
 *
 * Script on a qmlui build is the JavaScript scriptv4 (see
 * engine/src/scriptwrapper.h) - the keyword catalog, the syntax checker
 * (a throwaway QJSEngine evaluation, every Engine.* call is a no-op while
 * the runner is not started) and the line-number semantics documented in
 * functions-advanced-notes.md all follow from that.
 *
 * Audio/Video source changes reuse ApiFunctionsDomain::applyMediaSource so
 * a picked host file is copied into the project's media store exactly like
 * functions.create/update {source} and the QML editors' Replace button;
 * functions.get's Audio/Video typeDetail (incl. the `config` object) is
 * built by ApiFunctionsDomain, this domain only registers the Script one.
 */
class ApiMediaDomain : public QObject
{
    Q_OBJECT

public:
    ApiMediaDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

    /** FunctionsScriptDetail: {functionId, source, syntaxErrorLines,
     *  syntaxErrors, docRevision} - registered as the ScriptType typeDetail
     *  provider and also the payload shape functions.script.sourceChanged
     *  is derived from. */
    static QJsonObject scriptDetailToJson(Doc *doc, Script *script);

private:
    void registerScriptMethods();
    void registerAudioMethods();
    void registerVideoMethods();

    /** Resolve params.functionId to a Function of the given type, sending
     *  NOT_FOUND / INVALID_PARAMS and returning nullptr otherwise. */
    Function *requireFunctionOfType(ApiSession *session, const QString &id, const QJsonObject &params, int type) const;

    /** baseRevision gate shared by every structural method here: sends
     *  CONFLICT and returns false when it does not match the current
     *  docRevision. */
    bool checkBaseRevision(ApiSession *session, const QString &id, const QJsonObject &params) const;

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
