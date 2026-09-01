/*
  Q Light Controller Plus - Control API
  apivchost.h

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

#ifndef APIVCHOST_H
#define APIVCHOST_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QtGlobal>
#include <limits>

/**
 * Virtual Console operations (docs/api-spec/fragments/virtualconsole.yaml's vc.page.* / vc.widget.*)
 * that ApiVcDomain needs. qmlui's App is the real implementation, driving the live VCPage/VCWidget/
 * VirtualConsole object graph - but controlapi must build and run without qmlui (see apiserver.h),
 * so ApiVcDomain depends on this plain interface instead, obtained via dynamic_cast on ApiServer's
 * parent (see ApiVcDomain::vcHost()), exactly like ApiCoreDomain does for ApiProjectHost.
 *
 * Request-shape validation (revision checks, "does this field make sense", type-string whitelisting,
 * cycle detection on reparent) stays in ApiVcDomain, using the query methods below - this interface's
 * mutation methods assume their caller already validated arguments and just perform the change (see
 * each method's own doc comment for exactly what's assumed). JSON *snapshot* shaping for pages/
 * widgets lives here instead, because only the concrete implementation knows a widget's real
 * type-specific fields (VcButtonConfig/VcSliderConfig/...) - see vcWidgetSnapshot().
 *
 * controlapi/test/apivcdomain's FakeVcHost is the other implementation: a headless in-memory model
 * (the same one that used to live directly inside ApiVcDomain before this interface existed) used to
 * keep that test suite meaningful without linking qmlui into a controlapi-only test binary.
 */
class ApiVcHost
{
public:
    virtual ~ApiVcHost() {}

    /** Sentinel for "no parent" (page root) / "not found", matching VCWidget::invalidId()'s role but
     *  expressed here without needing to include vcwidget.h. */
    static constexpr quint32 InvalidWidgetId = std::numeric_limits<quint32>::max();

    /*********************************************************************
     * Pages
     *********************************************************************/

    virtual int vcPageCount() const = 0;

    /** JSON per the VcPage schema: {index, name, hasPin}. Caller guarantees
     *  0 <= $index < vcPageCount(). */
    virtual QJsonObject vcPageSnapshot(int index) const = 0;

    virtual int vcSelectedPage() const = 0;

    /** Live/runtime (§4b) - must not affect Doc's modified/revision state. */
    virtual void vcSetSelectedPage(int index) = 0;

    /** Inserts a new page at $index (0 <= index <= vcPageCount()), shifting every existing page/
     *  widget at or after $index and the current selection exactly like VirtualConsole::addPage().
     *  Bumps Doc's modified flag. */
    virtual void vcAddPage(int index) = 0;

    /** Deletes the page at $index and every widget on it (recursively), renumbering later pages'
     *  widgets down by one. Returns false without changing anything if $index is out of range or
     *  this is the last remaining page (mirrors VirtualConsole::deletePage()'s own refusal) - the
     *  caller must not call this without checking vcPageCount() > 1 first, since on true it fills
     *  $deletedWidgetIds with the wire-format (string) ids of every widget removed. */
    virtual bool vcDeletePage(int index, QJsonArray &deletedWidgetIds) = 0;

    virtual void vcRenamePage(int index, const QString &name) = 0;

    /** Mirrors VirtualConsole::setPagePIN(): $newPin has already been validated by the caller to be
     *  either empty or exactly 4 digits. Returns false without changing anything if a PIN is
     *  currently set and $currentPin doesn't match it. */
    virtual bool vcSetPagePin(int index, const QString &currentPin, const QString &newPin) = 0;

    virtual bool vcValidatePagePin(int index, const QString &pin) const = 0;

    /*********************************************************************
     * Widgets - queries
     *********************************************************************/

    virtual bool vcWidgetExists(quint32 id) const = 0;

    /** Wire type string ("Button", "Slider", "Frame", ...) or an empty string if $id doesn't exist. */
    virtual QString vcWidgetType(quint32 id) const = 0;

    /** The page index of $id's top-level ancestor (or of $id itself if it has no parent), or -1 if
     *  $id doesn't exist. */
    virtual int vcWidgetPage(quint32 id) const = 0;

    /** $id's immediate parent widget id, or InvalidWidgetId if $id is a page-root widget or doesn't
     *  exist. */
    virtual quint32 vcWidgetParentId(quint32 id) const = 0;

    /** True if $id exists and is a Frame or SoloFrame - the only widget types that can be used as a
     *  vc.widget.create/reparent parent. */
    virtual bool vcIsContainerWidget(quint32 id) const = 0;

    /** Every id that currently exists, in no particular order - used by vc.widget.list, which does
     *  its own page/parent/type filtering and sorting on top of this. */
    virtual QList<quint32> vcWidgetIds() const = 0;

    /** Full VcWidget detail JSON (id, widgetType, page, parentId if any, geometry, zIndex,
     *  allowResize, isDisabled, isVisible, style{caption,backgroundColor,backgroundImage,
     *  foregroundColor,font}, typeConfig{...}, inputSources, keySequences, externalControls) per the
     *  spec's VcWidgetDetail schema. Caller guarantees vcWidgetExists($id). */
    virtual QJsonObject vcWidgetSnapshot(quint32 id) const = 0;

    /*********************************************************************
     * Widgets - mutations
     *********************************************************************/

    /** Creates a widget of the given wire type string ($widgetType has already been validated
     *  against the known type list) at $page (already validated in range), optionally under
     *  $parentId (InvalidWidgetId = page root; caller has already validated it exists and is a
     *  container). Returns the new widget's id, or InvalidWidgetId on failure with *$error set. */
    virtual quint32 vcCreateWidget(const QString &widgetType, int page, quint32 parentId,
                                    const QJsonObject &geometry, const QJsonObject &style,
                                    const QJsonObject &typeConfig, QString *error) = 0;

    /** Deletes every widget in $ids plus their descendants (recursively) - unknown ids are silently
     *  ignored. Fills $deletedIds with the wire-format (string) ids of everything actually removed
     *  (may end up larger than $ids due to descendants, or smaller/empty if every id was unknown). */
    virtual void vcDeleteWidgets(const QList<quint32> &ids, QJsonArray &deletedIds) = 0;

    /** Applies whichever of these keys are present in $fields, atomically: geometry, zIndex,
     *  allowResize, isDisabled, isVisible, style. Does NOT touch "page" - see
     *  vcMoveTopLevelWidgetToPage() for that, kept separate because the domain validates/resolves it
     *  differently (rejecting it outright on a nested widget). Caller guarantees the widget exists. */
    virtual bool vcUpdateWidgetCommon(quint32 id, const QJsonObject &fields, QString *error) = 0;

    /** Moves a top-level (parentless) widget - and its whole subtree - onto a different page. Caller
     *  guarantees the widget currently has no parent and $newPage is in range. */
    virtual void vcMoveTopLevelWidgetToPage(quint32 id, int newPage) = 0;

    /** Partial-patch merge onto the widget's type-specific config (VcButtonConfig/VcSliderConfig/...
     *  depending on its type). Returns false with *$error set if $id's widget type has no
     *  setConfig support yet, or $configPatch contains a key that type doesn't recognize. */
    virtual bool vcSetWidgetConfig(quint32 id, const QJsonObject &configPatch, QString *error) = 0;

    /** Moves $id under $newParentId (InvalidWidgetId = page root) and repositions its top-left
     *  corner to $newTopLeft. Caller has already validated $newParentId exists, is a container, and
     *  is neither $id nor one of its own descendants. */
    virtual bool vcReparentWidget(quint32 id, quint32 newParentId, QPointF newTopLeft, QString *error) = 0;

    /** Bulk geometry-only update, all-or-nothing - caller has already validated every id in $updates
     *  exists. Each pair's QJsonObject is a geometry {x,y,width,height}. */
    virtual void vcRepositionWidgets(const QList<QPair<quint32, QJsonObject> > &updates) = 0;
};

#endif
