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

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QVector>

class ApiDispatcher;
class ApiServer;
class ApiSession;
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
 * *** IMPORTANT ARCHITECTURAL LIMITATION - READ BEFORE EXTENDING THIS CLASS ***
 *
 * Unlike every other domain (ApiIoDomain, ApiCoreDomain), this one does NOT construct or mutate the
 * real qmlui VCWidget/VCButton/VCFrame/.../VirtualConsole object graph. It maintains its own
 * self-contained, headless model of pages and widgets (m_pages/m_widgets below), independent of
 * whatever the running app's actual Virtual Console currently shows. This was a deliberate choice,
 * not an oversight - both roads the task considered turned out to be closed:
 *
 * 1. Constructing real VCWidget subclasses here is a build-graph dead end, not just an inconvenience.
 *    qmlui/virtualconsole/vcwidget.h unconditionally #includes <QQuickView>/<QQuickItem>, and
 *    VirtualConsole's own constructor requires a live QQuickView* plus a ContextManager* (which per
 *    this project's CLAUDE.md drags in MainView2D/MainView3D and Qt3D) - there is no headless way to
 *    even name these types, let alone construct the container. Worse, qmlui/CMakeLists.txt compiles
 *    virtualconsole/(star).cpp directly into add_executable(qlcplus5 ...), not into a separate library -
 *    so adding those same .cpp files to controlapi's CMakeLists (a static lib also linked into
 *    qlcplus5) would compile every VCWidget/VirtualConsole symbol twice into the same final binary,
 *    an ODR violation at link time. Fixing that would mean carving qmlui's virtualconsole/ sources
 *    into their own library first - a real restructuring, out of scope for one domain and not
 *    something to force through unilaterally while four sibling domains assume controlapi stays
 *    dependency-free of qmlui (see controlapi/src/CMakeLists.txt's own comment to that effect).
 *
 * 2. The task's fallback ("manipulate the same show-file model the widgets serialize to/from") also
 *    doesn't exist as a shared artifact to manipulate: grep confirms engine/src/doc.h has no
 *    Virtual Console concept at all - Doc never stores VC state. The <VirtualConsole> XML section is
 *    read/written exclusively by qmlui/app.cpp calling VirtualConsole::loadXML()/saveXML() on the
 *    App-owned VirtualConsole instance. There is no Doc-level or on-disk "model" independent of that
 *    live qmlui object tree to read or write from a qmlui-free static library.
 *
 * So what follows is the closest honest implementation available: the full request/response/
 * broadcast/docRevision contract from the spec, backed by a private in-memory model that mimics
 * VCWidget/VCPage's field semantics (geometry, zIndex, style, page, parent, type-specific config)
 * closely enough to unit-test and to serve a real client faithfully - BUT it is entirely disconnected
 * from the app's real Virtual Console:
 *
 *   - A widget created via vc.widget.create does NOT appear in the running qmlui Virtual Console UI.
 *   - core.project.save/saveAs (ApiCoreDomain) does NOT persist anything created here - App serializes
 *     its own separate, real VirtualConsole instance, never this class's model.
 *   - Conversely, VC state loaded from an opened .qxw is invisible to this domain.
 *
 * docRevision bumps are still real and shared: this class calls Doc::setModified() (exactly what
 * VCWidget::setDocModified() itself would call) so baseRevision/docRevision optimistic-concurrency
 * checks behave identically to every other structural domain, even though the resource being
 * versioned lives only in this class today.
 *
 * The real fix, for a future pass: an ApiVcHost interface (in controlapi/src, analogous to
 * apiprojecthost.h's ApiProjectHost) implemented by qmlui's App, expressed entirely in primitive/JSON
 * terms so this module still never needs to name a qmlui/Quick type - letting this domain drive the
 * *actual* live VirtualConsole through a narrow seam instead of a parallel, disconnected copy.
 */
class ApiVcDomain : public QObject
{
    Q_OBJECT

public:
    ApiVcDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    struct VcPageState
    {
        QString name;
        QString pin; // empty = no PIN set
    };

    struct VcWidgetState
    {
        quint32 id = 0;
        QString widgetType;
        int page = 0; // invariant: always equal to the top-level ancestor's page - vc.widget.update
                      // rejects a direct "page" change on any widget with a parent; use
                      // vc.widget.reparent to move a nested widget instead.
        quint32 parentId = InvalidWidgetId; // InvalidWidgetId = "placed on the page root"
        QRectF geometry;
        int zIndex = 0;
        bool allowResize = true;
        bool isDisabled = false;
        bool isVisible = true;
        QString caption;
        QString backgroundColor;  // empty = default (null on the wire)
        QString backgroundImage;  // empty = none (null on the wire)
        QString foregroundColor;  // empty = default (null on the wire)
        QJsonObject font;
        QJsonObject typeConfig;
    };

    static const quint32 InvalidWidgetId;
    static const QStringList ContainerWidgetTypes; // Frame, SoloFrame - the only valid vc.widget.create/reparent parent types

    void registerPageMethods(ApiDispatcher *d);
    void registerWidgetMethods(ApiDispatcher *d);

    // --- model <-> JSON ---
    QJsonObject pageToJson(int index) const;
    QJsonObject styleToJson(const VcWidgetState &w) const;
    QJsonObject widgetSummaryToJson(const VcWidgetState &w) const;
    QJsonObject widgetDetailToJson(const VcWidgetState &w) const;
    void applyStyleFromJson(VcWidgetState &w, const QJsonObject &style) const;
    QRectF geometryFromJson(const QJsonObject &geom) const;
    QJsonObject geometryToJson(const QRectF &geom) const;

    // --- model helpers ---
    bool parseWidgetId(const QString &s, quint32 &outId) const;
    VcWidgetState *findWidget(const QString &widgetIdStr);
    bool isContainerWidget(quint32 id) const;
    /** Every descendant of $id (children, grandchildren, ...), NOT including $id itself. */
    QList<quint32> collectDescendants(quint32 id) const;
    /** True if $ancestorCandidate is $id itself or anywhere in $id's ancestor chain - used to
     *  reject a reparent that would make a widget its own descendant. */
    bool isSelfOrAncestorOf(quint32 ancestorCandidate, quint32 id) const;
    void setWidgetPageRecursive(quint32 id, int newPage);

    Doc *m_doc;
    ApiServer *m_server;

    QVector<VcPageState> m_pages;
    int m_selectedPage;

    QHash<quint32, VcWidgetState> m_widgets;
    quint32 m_nextWidgetId;
};

#endif
