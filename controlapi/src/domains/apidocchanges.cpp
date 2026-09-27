/*
  Q Light Controller Plus - Control API
  apidocchanges.cpp

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

#include <QJsonObject>
#include <algorithm>

#include "apidocchanges.h"
#include "apifixturesdomain.h"
#include "apifixturegroupdomain.h"
#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apivchost.h"

#include "fixturegroup.h"
#include "qlcpalette.h"
#include "function.h"
#include "fixture.h"
#include "doc.h"

ApiDocChanges::ApiDocChanges(Doc *doc, ApiServer *server)
    : m_doc(doc)
    , m_server(server)
    , m_pageCount(0)
{
    for (Fixture *fxi : m_doc->fixtures())
        m_fixtures.insert(fxi->id());
    for (FixtureGroup *grp : m_doc->fixtureGroups())
        m_groups.insert(grp->id());
    for (Function *f : m_doc->functions())
        m_functions.insert(f->id());
    for (QLCPalette *p : m_doc->palettes())
        m_palettes.insert(p->id());

    ApiVcHost *vc = dynamic_cast<ApiVcHost *>(m_server->parent());
    if (vc != nullptr)
    {
        m_pageCount = vc->vcPageCount();
        for (quint32 wid : vc->vcWidgetIds())
            m_widgets.insert(wid);
    }
}

void ApiDocChanges::broadcastCreated(const QString &originClientId)
{
    const int revision = int(m_doc->docRevision());

    // fixtures.patched: one event for all new fixtures, like a multi-fixture patch
    QList<quint32> fixtureIds;
    for (Fixture *fxi : m_doc->fixtures())
        if (m_fixtures.contains(fxi->id()) == false)
            fixtureIds.append(fxi->id());
    std::sort(fixtureIds.begin(), fixtureIds.end());
    if (fixtureIds.isEmpty() == false)
    {
        QJsonArray fixturesJson;
        for (quint32 id : fixtureIds)
        {
            createdFixtureIds.append(QString::number(id));
            fixturesJson.append(ApiFixturesDomain::summary(m_doc->fixture(id)));
        }
        QJsonObject data;
        data.insert(QStringLiteral("docRevision"), revision);
        data.insert(QStringLiteral("fixtures"), fixturesJson);
        m_server->broadcast(QStringLiteral("fixtures.patched"), data, originClientId, false);
    }

    QList<quint32> groupIds;
    for (FixtureGroup *grp : m_doc->fixtureGroups())
        if (m_groups.contains(grp->id()) == false)
            groupIds.append(grp->id());
    std::sort(groupIds.begin(), groupIds.end());
    for (quint32 id : groupIds)
    {
        createdGroupIds.append(QString::number(id));
        QJsonObject data;
        data.insert(QStringLiteral("group"), ApiFixtureGroupDomain::toJson(m_doc->fixtureGroup(id)));
        data.insert(QStringLiteral("docRevision"), revision);
        m_server->broadcast(QStringLiteral("fixtures.group.created"), data, originClientId, false);
    }

    for (QLCPalette *p : m_doc->palettes())
        if (m_palettes.contains(p->id()) == false)
            createdPaletteIds.append(QString::number(p->id()));

    QList<quint32> functionIds;
    for (Function *f : m_doc->functions())
        if (m_functions.contains(f->id()) == false)
            functionIds.append(f->id());
    std::sort(functionIds.begin(), functionIds.end());
    for (quint32 id : functionIds)
    {
        createdFunctionIds.append(QString::number(id));
        QJsonObject data;
        data.insert(QStringLiteral("function"), ApiFunctionsDomain::summary(m_doc->function(id)));
        data.insert(QStringLiteral("docRevision"), revision);
        m_server->broadcast(QStringLiteral("functions.created"), data, originClientId, false);
    }

    ApiVcHost *vc = dynamic_cast<ApiVcHost *>(m_server->parent());
    if (vc == nullptr)
        return;

    // Pages are only ever appended by the generators (StageWizard::pickTargetPage())
    for (int i = m_pageCount; i < vc->vcPageCount(); i++)
    {
        createdPageIndexes.append(i);
        QJsonObject data;
        data.insert(QStringLiteral("page"), vc->vcPageSnapshot(i));
        data.insert(QStringLiteral("docRevision"), revision);
        m_server->broadcast(QStringLiteral("vc.page.created"), data, originClientId, false);
    }

    QList<quint32> widgetIds;
    for (quint32 wid : vc->vcWidgetIds())
        if (m_widgets.contains(wid) == false)
            widgetIds.append(wid);
    std::sort(widgetIds.begin(), widgetIds.end());
    for (quint32 wid : widgetIds)
    {
        createdWidgetIds.append(QString::number(wid));
        QJsonObject data;
        data.insert(QStringLiteral("widget"), vc->vcWidgetSnapshot(wid));
        data.insert(QStringLiteral("docRevision"), revision);
        m_server->broadcast(QStringLiteral("vc.widget.created"), data, originClientId, false);
    }
}
