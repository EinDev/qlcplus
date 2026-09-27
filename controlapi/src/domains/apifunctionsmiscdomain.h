/*
  Q Light Controller Plus - Control API
  apifunctionsmiscdomain.h

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

#ifndef APIFUNCTIONSMISCDOMAIN_H
#define APIFUNCTIONSMISCDOMAIN_H

#include <QObject>

class ApiServer;
class ApiVcHost;
class Doc;

/**
 * The function-side leftovers of the Fixtures & Functions screen
 * (docs/api-spec/fragments/functions-core.yaml + core.yaml):
 *
 * - functions.chaser.setSpeedModes (§4a): Common / Per Step / Default for
 *   fade in, fade out and duration; broadcasts functions.chaser.changed.
 * - functions.chaser.setAction (§4b): next / previous / go to step, stop
 *   step, pause - Chaser::setAction(), which queues a startup action when
 *   the Chaser is not running (the next start honours it).
 * - functions.sequence.setBoundScene (§4a): re-binds a Sequence and every
 *   step to another Scene; broadcasts functions.sequence.changed.
 * - functions.sequence.applyDumpValues (§4a): Sequence::applyDumpValues(),
 *   with an additive `captureLive` flag that snapshots the live pre-Grand-
 *   Master output of the bound Scene's channels on the server (the same
 *   source FunctionManager::dumpDmxValues() reads); broadcasts
 *   functions.sequence.stepsChanged with a full /steps replace.
 * - functions.adjustAttribute (§4b ack): a function's base attribute
 *   (Intensity etc.), clamped to the attribute's own range; broadcasts
 *   functions.attributeChanged so every open header follows.
 * - functions.tap (§4b): Function::tap().
 * - functions.clone (§4a): Function::createCopy() per id, " (Copy)"
 *   suffix, a Sequence gets its bound Scene cloned and re-bound too
 *   (FunctionManager::cloneFunctions()); broadcasts functions.created.
 * - functions.usage: Doc::getUsage() (functions that reference the id and
 *   at which step / position) plus the Virtual Console widgets from
 *   ApiVcHost::vcWidgetsUsingFunction() when a host is present.
 * - core.project.setStartupFunction (§4a): Doc::setStartupFunction(),
 *   broadcasts core.project.startupFunctionChanged. core.project.get
 *   carries startupFunctionId.
 */
class ApiFunctionsMiscDomain : public QObject
{
    Q_OBJECT

public:
    ApiFunctionsMiscDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();
    ApiVcHost *vcHost() const;

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
