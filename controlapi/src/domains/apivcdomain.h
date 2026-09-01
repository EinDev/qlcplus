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

class ApiDispatcher;
class ApiServer;
class ApiSession;
class ApiVcHost;
class Doc;

/**
 * Implementation of the "vc.page.*"/"vc.widget.*" domain (docs/api-spec/fragments/virtualconsole.yaml),
 * scoped to: vc.page.{list,create,delete,rename,setPin,validatePin,select} and
 * vc.widget.{list,get,create,update,setConfig,delete,reparent,reposition}. Every other vc.* method in
 * the spec (usage, createMatrix, createFromFunctions, align, distribute, bulkStyle, inputSource.*,
 * keySequence.*, inputDetect.*, preset.*, vc.button.press, vc.slider.*, vc.xyPad.*, vc.frame.*,
 * vc.clock.*, ...) is deliberately NOT registered here - left for a future pass. An unregistered
 * method name is not a crash: ApiDispatcher::dispatch() already responds NOT_FOUND for any method
 * nobody registered.
 *
 * All request-shape validation (baseRevision/docRevision checks, "is this a known widget type",
 * "does this parent exist and accept children", cycle detection on reparent) lives here. The actual
 * page/widget object graph is owned by whatever ApiVcHost implementation is running the process -
 * qmlui's App in production, a headless FakeVcHost in controlapi/test/apivcdomain - obtained via
 * dynamic_cast on ApiServer's parent, exactly like ApiCoreDomain::projectHost() does for
 * ApiProjectHost. See apivchost.h for why this seam exists (controlapi must build without qmlui) and
 * exactly what each side of it assumes.
 */
class ApiVcDomain : public QObject
{
    Q_OBJECT

public:
    ApiVcDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerPageMethods(ApiDispatcher *d);
    void registerWidgetMethods(ApiDispatcher *d);

    ApiVcHost *vcHost() const;

    /** True if $ancestorCandidate is $id itself or anywhere in $id's ancestor chain (queried live via
     *  vcHost()->vcWidgetParentId()) - used to reject a reparent that would make a widget its own
     *  descendant. */
    bool isSelfOrAncestorOf(ApiVcHost *host, quint32 ancestorCandidate, quint32 id) const;

    static bool parseWidgetId(const QString &s, quint32 &outId);

    Doc *m_doc;
    ApiServer *m_server;
};

#endif
