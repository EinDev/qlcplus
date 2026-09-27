/*
  Q Light Controller Plus - Control API
  apivclayoutdomain.h

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

#ifndef APIVCLAYOUTDOMAIN_H
#define APIVCLAYOUTDOMAIN_H

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
 * The Virtual Console "layout and per-widget configuration" slice of the vc.* domain
 * (docs/api-spec/fragments/virtualconsole.yaml), kept in its own class next to ApiVcDomain so the
 * two can grow independently:
 *  - vc.frame.setPin / vc.frame.validatePin / vc.frame.cloneFirstPage
 *  - vc.slider.setLevelChannels (§4a) / vc.slider.flash (§4b)
 *  - vc.widget.align / vc.widget.distribute / vc.widget.bulkStyle (§4a, multi-widget, broadcast on
 *    vc.widget.bulkUpdated)
 *  - vc.widget.createFromFunctions / vc.widget.createMatrix (§4a bulk creators - per the spec these
 *    broadcast the new widgets on vc.widget.bulkUpdated, NOT vc.widget.created)
 *  - vc.widget.usage
 *
 * Same host seam as ApiVcDomain: the page/widget object graph is reached through ApiVcHost
 * (qmlui's App in production, FakeVcHost in controlapi/test/apivclayoutdomain), obtained via
 * dynamic_cast on ApiServer's parent. Request-shape validation (revision check, "does every id
 * exist", "same parent for an alignment", enum whitelists, Function existence / Chaser-only for the
 * bulk creators) lives here; align/distribute do their geometry math here too, over the host's
 * widget snapshots, and commit through the generic vcRepositionWidgets()/vcUpdateWidgetCommon() -
 * VirtualConsole's own setWidgetsAlignment()/setWidgetsDistribution() operate on the on-screen
 * selection, not on ids, so they are not usable from an API request.
 */
class ApiVcLayoutDomain : public QObject
{
    Q_OBJECT

public:
    ApiVcLayoutDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerFrameMethods(ApiDispatcher *d);
    void registerSliderMethods(ApiDispatcher *d);
    void registerLayoutMethods(ApiDispatcher *d);
    void registerCreationMethods(ApiDispatcher *d);

    ApiVcHost *vcHost() const;

    /** Sends ErrInternal and returns nullptr when no host is attached. */
    ApiVcHost *requireHost(ApiSession *session, const QString &id);

    /** §4a front half: compares params.baseRevision with Doc::docRevision(), answering CONFLICT
     *  (with the current revision in details) and returning false on a mismatch. */
    bool checkRevision(ApiSession *session, const QString &id, const QJsonObject &params);

    /** Resolves params.widgetId, answering NOT_FOUND / INVALID_PARAMS (wrong type) itself. */
    bool resolveWidget(ApiSession *session, const QString &id, const QJsonObject &params,
                       const QStringList &allowedTypes, ApiVcHost *host, quint32 *outId);

    /** Resolves params.widgetIds (minItems $minCount) to existing ids, answering INVALID_PARAMS /
     *  NOT_FOUND itself. Duplicates are collapsed. */
    bool resolveWidgetIds(ApiSession *session, const QString &id, const QJsonObject &params, int minCount,
                          ApiVcHost *host, QList<quint32> *outIds);

    /** Resolves params.page / params.parentId for the bulk creators exactly like vc.widget.create:
     *  page in range, parentId (if present) an existing Frame/SoloFrame. */
    bool resolveTarget(ApiSession *session, const QString &id, const QJsonObject &params, ApiVcHost *host,
                       int *outPage, quint32 *outParentId);

    /** {docRevision} ok response after Doc::setModified(). */
    void replyRevision(ApiSession *session, const QString &id);

    /** vc.widget.configChanged with the widget's full snapshot. */
    void broadcastConfigChanged(ApiVcHost *host, quint32 widgetId, ApiSession *session);

    /** vc.widget.bulkUpdated with every listed widget's full snapshot. */
    void broadcastBulkUpdated(ApiVcHost *host, const QList<quint32> &widgetIds, ApiSession *session);

    static bool parseWidgetId(const QString &s, quint32 &outId);
    static QJsonArray idsToJson(const QList<quint32> &ids);

    Doc *m_doc;
    ApiServer *m_server;
};

#endif
