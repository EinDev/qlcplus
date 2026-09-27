/*
  Q Light Controller Plus
  app_apivcconfig_layout.cpp

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

/*
 * vc.widget.setConfig / typeConfig for Frame, SoloFrame and Label - see app_apivcconfig.h for the
 * contract and docs/api-spec/fragments/virtualconsole.yaml for the VcFrameConfig / VcSoloFrameConfig
 * schemas being implemented (VCLabel has no type-specific config by design: everything an operator
 * can set on a label - caption, colours, font - is VcWidgetStyle, i.e. vc.widget.update).
 *
 * Every field mirrors VCFrameProperties.qml one to one: Header (showHeader, showEnable), Solo Frame
 * Options (excludeMonitoredFunctions; soloframeMixing is engine-only but persisted, so it is exposed
 * too), Pages (multiPageMode, pagesLoop, totalPagesNumber) and Shortcuts (pageLabels via
 * VCFrame::setShortcutName()). isCollapsed is the header's collapse button. hasPin is read-only here;
 * the PIN itself goes through vc.frame.setPin (ApiVcLayoutDomain) so the current PIN can be checked.
 */

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include "app_apivcconfig.h"
#include "virtualconsole/vcframe.h"
#include "virtualconsole/vcsoloframe.h"
#include "virtualconsole/vclabel.h"

namespace
{
// VCFrameProperties.qml's "Pages number" spin box range.
const int kMinFramePages = 1;
const int kMaxFramePages = 100;

QString defaultPageLabel(int pageIndex)
{
    // Same wording VCFrame::ensureFirstPage()/setTotalPagesNumber() use for a page that was never
    // renamed.
    return QObject::tr("Page %1").arg(pageIndex + 1);
}

bool boolField(const QJsonObject &patch, const QString &key, bool &out, QString *error)
{
    if (patch.contains(key) == false)
        return true;
    QJsonValue v = patch.value(key);
    if (v.isBool() == false)
    {
        if (error) *error = QStringLiteral("%1 must be a boolean").arg(key);
        return false;
    }
    out = v.toBool();
    return true;
}
}

namespace ApiVcConfig
{

QJsonObject frameConfigToJson(VCWidget *w)
{
    VCFrame *f = qobject_cast<VCFrame *>(w);
    QJsonObject obj;
    if (f == nullptr)
        return obj;

    obj.insert(QStringLiteral("showHeader"), f->showHeader());
    obj.insert(QStringLiteral("showEnable"), f->showEnable());
    obj.insert(QStringLiteral("isCollapsed"), f->isCollapsed());
    obj.insert(QStringLiteral("multiPageMode"), f->multiPageMode());
    obj.insert(QStringLiteral("totalPagesNumber"), f->totalPagesNumber());
    obj.insert(QStringLiteral("pagesLoop"), f->pagesLoop());

    // VCFrame::pageLabels() is the label map's values in page order, but the map is only populated
    // once multipage mode was enabled (ensureFirstPage()) and can be shorter than totalPagesNumber
    // for a frame loaded from an older file - fill the gaps with the engine's own default wording so
    // the client always gets exactly totalPagesNumber entries.
    QStringList labels = f->pageLabels();
    QJsonArray pageLabels;
    for (int i = 0; i < f->totalPagesNumber(); i++)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("pageIndex"), i);
        entry.insert(QStringLiteral("label"), i < labels.size() && labels.at(i).isEmpty() == false ? labels.at(i) : defaultPageLabel(i));
        pageLabels.append(entry);
    }
    obj.insert(QStringLiteral("pageLabels"), pageLabels);
    obj.insert(QStringLiteral("hasPin"), f->PIN() != 0);

    VCSoloFrame *solo = qobject_cast<VCSoloFrame *>(w);
    if (solo != nullptr)
    {
        obj.insert(QStringLiteral("soloframeMixing"), solo->soloframeMixing());
        obj.insert(QStringLiteral("excludeMonitoredFunctions"), solo->excludeMonitoredFunctions());
    }
    return obj;
}

bool applyFrameConfig(VCWidget *w, const QJsonObject &patch, QString *error)
{
    static const QSet<QString> frameKeys = {
        QStringLiteral("showHeader"), QStringLiteral("showEnable"), QStringLiteral("isCollapsed"),
        QStringLiteral("multiPageMode"), QStringLiteral("totalPagesNumber"), QStringLiteral("pagesLoop"),
        QStringLiteral("pageLabels")
    };
    static const QSet<QString> soloKeys = {
        QStringLiteral("soloframeMixing"), QStringLiteral("excludeMonitoredFunctions")
    };

    VCFrame *f = qobject_cast<VCFrame *>(w);
    VCSoloFrame *solo = qobject_cast<VCSoloFrame *>(w);
    if (f == nullptr)
    {
        if (error) *error = QStringLiteral("Widget is not a Frame");
        return false;
    }

    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it)
    {
        if (frameKeys.contains(it.key()))
            continue;
        if (soloKeys.contains(it.key()))
        {
            if (solo != nullptr)
                continue;
            if (error) *error = QStringLiteral("'%1' is only valid on a SoloFrame").arg(it.key());
            return false;
        }
        if (it.key() == QStringLiteral("hasPin"))
        {
            if (error) *error = QStringLiteral("hasPin is read-only - use vc.frame.setPin");
            return false;
        }
        if (error) *error = QStringLiteral("Unknown VcFrameConfig key '%1'").arg(it.key());
        return false;
    }

    // ---- validate everything first (all-or-nothing) ----
    bool showHeader = f->showHeader(), showEnable = f->showEnable(), isCollapsed = f->isCollapsed();
    bool multiPageMode = f->multiPageMode(), pagesLoop = f->pagesLoop();
    bool soloMixing = solo != nullptr ? solo->soloframeMixing() : false;
    bool excludeMonitored = solo != nullptr ? solo->excludeMonitoredFunctions() : false;
    if (!boolField(patch, QStringLiteral("showHeader"), showHeader, error) ||
        !boolField(patch, QStringLiteral("showEnable"), showEnable, error) ||
        !boolField(patch, QStringLiteral("isCollapsed"), isCollapsed, error) ||
        !boolField(patch, QStringLiteral("multiPageMode"), multiPageMode, error) ||
        !boolField(patch, QStringLiteral("pagesLoop"), pagesLoop, error) ||
        !boolField(patch, QStringLiteral("soloframeMixing"), soloMixing, error) ||
        !boolField(patch, QStringLiteral("excludeMonitoredFunctions"), excludeMonitored, error))
        return false;

    int totalPages = f->totalPagesNumber();
    if (patch.contains(QStringLiteral("totalPagesNumber")))
    {
        QJsonValue v = patch.value(QStringLiteral("totalPagesNumber"));
        if (v.isDouble() == false || v.toDouble() < kMinFramePages || v.toDouble() > kMaxFramePages || v.toDouble() != int(v.toDouble()))
        {
            if (error) *error = QStringLiteral("totalPagesNumber must be an integer %1..%2").arg(kMinFramePages).arg(kMaxFramePages);
            return false;
        }
        totalPages = v.toInt();
    }

    // pageLabels is a partial list: only the listed pages are renamed, validated against the page
    // count this same patch will leave behind.
    QList<QPair<int, QString> > labels;
    if (patch.contains(QStringLiteral("pageLabels")))
    {
        QJsonValue v = patch.value(QStringLiteral("pageLabels"));
        if (v.isArray() == false)
        {
            if (error) *error = QStringLiteral("pageLabels must be an array of {pageIndex, label}");
            return false;
        }
        for (const QJsonValue &entryValue : v.toArray())
        {
            QJsonObject entry = entryValue.toObject();
            QJsonValue idx = entry.value(QStringLiteral("pageIndex"));
            QJsonValue label = entry.value(QStringLiteral("label"));
            if (idx.isDouble() == false || idx.toDouble() < 0 || idx.toDouble() >= totalPages || label.isString() == false)
            {
                if (error) *error = QStringLiteral("pageLabels: pageIndex must be 0..%1 and label a string").arg(totalPages - 1);
                return false;
            }
            labels.append(qMakePair(idx.toInt(), label.toString()));
        }
    }

    // ---- apply, in cascade order ----
    // multiPageMode first (setMultiPageMode(true) registers page 0's label/shortcut), then the page
    // count (which registers/unregisters the other pages' shortcut controls), then labels.
    if (patch.contains(QStringLiteral("multiPageMode")))
        f->setMultiPageMode(multiPageMode);
    if (patch.contains(QStringLiteral("totalPagesNumber")))
    {
        f->setTotalPagesNumber(totalPages);
        // setTotalPagesNumber() does not touch currentPage: shrinking below it would leave every
        // child hidden with no page to show them on. VCFrame::currentPage() reports 0 while not in
        // multipage mode, and setCurrentPage() refuses out-of-range values, so this only fires when
        // it is really needed.
        if (f->currentPage() >= totalPages)
            f->setCurrentPage(totalPages - 1);
    }
    for (const auto &l : labels)
        f->setShortcutName(l.first, l.second);

    if (patch.contains(QStringLiteral("showHeader")))
        f->setShowHeader(showHeader);
    if (patch.contains(QStringLiteral("showEnable")))
        f->setShowEnable(showEnable);
    if (patch.contains(QStringLiteral("isCollapsed")))
        f->setCollapsed(isCollapsed);
    if (patch.contains(QStringLiteral("pagesLoop")))
        f->setPagesLoop(pagesLoop);
    if (solo != nullptr)
    {
        if (patch.contains(QStringLiteral("soloframeMixing")))
            solo->setSoloframeMixing(soloMixing);
        if (patch.contains(QStringLiteral("excludeMonitoredFunctions")))
            solo->setExcludeMonitoredFunctions(excludeMonitored);
    }

    return true;
}

QJsonObject labelConfigToJson(VCWidget *)
{
    // VCLabel adds nothing to VCWidget - the spec's vc.widget.create description says as much
    // ("VCLabel has no type-specific config"); caption/colours/font are style, see vc.widget.update.
    return QJsonObject();
}

bool applyLabelConfig(VCWidget *, const QJsonObject &patch, QString *error)
{
    if (patch.isEmpty())
        return true; // an empty patch is a valid no-op (e.g. vc.widget.create with typeConfig: {})
    if (error)
        *error = QStringLiteral("Label has no type-specific config ('%1' is not a VcLabel field) - use vc.widget.update's style instead")
                     .arg(patch.constBegin().key());
    return false;
}

}
