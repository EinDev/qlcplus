/*
  Q Light Controller Plus - Control API
  apimonitordomain.h

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

#ifndef APIMONITORDOMAIN_H
#define APIMONITORDOMAIN_H

#include <QJsonObject>
#include <QObject>

class ApiServer;
class ApiIoDomain;
class Doc;
class MonitorProperties;

/**
 * Implementation of the "fixtures.monitor.*" methods (docs/api-spec/
 * fragments/fixtures.yaml, section 5): the 2D/3D preview's stage settings
 * and per-fixture placement stored in MonitorProperties (engine/src/
 * monitorproperties.h, the .qxw <Monitor> element), the arrange / align /
 * distribute / rotate / centre tools and the "Pick a 3D point" aiming tool.
 *
 * The geometry is engine/src/monitorlayout.h - the very same functions
 * qmlui's ContextManager runs behind PopupArrangeFixtures.qml /
 * SettingsView2D.qml - so a browser client and the desktop UI lay fixtures
 * out identically. Everything here is §4a document state (baseRevision in,
 * docRevision out): MonitorProperties' setters never bump the revision
 * themselves, so every mutating handler calls Doc::setModified() exactly
 * once after applying its whole batch. MonitorProperties has no change
 * signals, so fixtures.monitor.changed is only broadcast from this
 * domain's own handlers (a placement edited in the Qt UI reaches clients
 * through the generic core.history.changed / docRevision path).
 *
 * fixtures.monitor.aimAt is live (§4b): the computed Pan/Tilt values go
 * through ApiIoDomain's Simple Desk override store (ApiIoDomain::
 * overrideChannels), the same path io.simpleDesk.setChannels uses, so
 * "Release" in a client clears them like any other override.
 */
class ApiMonitorDomain : public QObject
{
    Q_OBJECT

public:
    ApiMonitorDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, QObject *parent = nullptr);

    /** FixturesMonitorStage JSON for the current MonitorProperties. */
    static QJsonObject stageToJson(Doc *doc);

    /** FixturesMonitorItem JSON for one (fixtureId, head, linked) item.
     *  placed=false yields the synthetic centre-of-stage entry for a
     *  fixture without a monitor entry. */
    static QJsonObject itemToJson(Doc *doc, quint32 fixtureId, quint16 headIndex, quint16 linkedIndex, bool placed);

private:
    void registerMethods();

private:
    Doc *m_doc;
    ApiServer *m_server;
    ApiIoDomain *m_ioDomain;
};

#endif
