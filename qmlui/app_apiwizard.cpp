/*
  Q Light Controller Plus
  app_apiwizard.cpp

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
 * ApiWizardHost (controlapi/src/apiwizardhost.h): core.wizard.* over StageWizard.
 *
 * Every call builds its OWN StageWizard and walks it forward through the steps exactly like the
 * QML dialog does (qmlui/qml/stagewizard/ShowWizard.qml), so the step-entry side effects run in the
 * same order: step 2 loads the project's groups as unticked boxes, step 3 applies the show type's
 * stage default and suggests a size, step 4 rebuilds the effect list and turns on the show type's
 * defaults (so explicit effect choices are applied only AFTER entering it), step 5 re-scans the
 * patched controllers. App's own m_stageWizard (the one the QML dialog binds to) is never touched,
 * so a wizard open on the desktop keeps its state.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <QVariantMap>

#include "app.h"
#include "stagewizard.h"
#include "fixturegroup.h"
#include "monitorproperties.h"

namespace {

QJsonObject envSizeJson(const StageWizard *wiz)
{
    QJsonObject env;
    env.insert(QStringLiteral("width"), wiz->envWidth());
    env.insert(QStringLiteral("height"), wiz->envHeight());
    env.insert(QStringLiteral("depth"), wiz->envDepth());
    return env;
}

QJsonArray groupFixtureIds(const StageWizard *wiz, int index)
{
    QJsonArray ids;
    for (const QVariant &v : wiz->groupFixtures(index).toList())
        ids.append(double(v.toMap().value(QStringLiteral("id")).toUInt()));
    return ids;
}

void copyCapabilities(const QVariantMap &from, QJsonObject &to)
{
    for (const char *key : { "hasMovement", "hasRGB", "hasColorWheel", "hasGobo", "hasShutter", "hasDimmer" })
        to.insert(QString::fromLatin1(key), from.value(QString::fromLatin1(key)).toBool());
}

} // namespace

StageWizard *App::wizardFromChoices(const ApiWizardChoices &choices, QString *error)
{
    std::unique_ptr<StageWizard> wiz(new StageWizard(m_doc, m_fixtureManager, m_functionManager,
                                                    m_virtualConsole, m_contextManager, this));

    // Step 1 -> 2: the project's own fixture groups become unticked boxes
    wiz->setShowType(choices.showType);
    wiz->setCurrentStep(1);

    const QVariantList boxes = wiz->groupsModel().toList();
    for (const ApiWizardGroupChoice &g : choices.groups)
    {
        int index = -1;
        if (g.groupId != UINT_MAX)
        {
            for (const QVariant &b : boxes)
            {
                const QVariantMap box = b.toMap();
                if (box.value(QStringLiteral("groupId")).toUInt() == g.groupId)
                    index = box.value(QStringLiteral("index")).toInt();
            }
            if (index < 0)
            {
                *error = QStringLiteral("No such fixture group: %1").arg(g.groupId);
                return nullptr;
            }
            if (g.hasFixtureIds)
            {
                for (const QJsonValue &v : groupFixtureIds(wiz.get(), index))
                    if (g.fixtureIds.contains(quint32(v.toDouble())) == false)
                        wiz->removeFixtureFromGroup(index, quint32(v.toDouble()));
            }
        }
        else
        {
            index = wiz->addGroup();
            wiz->renameGroup(index, g.name);
        }

        // assignFixtureToGroup() moves a fixture out of any other box: last one wins
        if (g.hasFixtureIds)
        {
            const QJsonArray current = groupFixtureIds(wiz.get(), index);
            for (quint32 fid : g.fixtureIds)
                if (current.contains(double(fid)) == false)
                    wiz->assignFixtureToGroup(index, fid);
        }

        wiz->setGroupSelected(index, true);
        if (g.role >= 0)
            wiz->setGroupRole(index, g.role);
    }

    bool anyFixtures = false;
    for (const QVariant &v : wiz->fixtureRoleModel().toList())
        if (v.toMap().value(QStringLiteral("fixtureCount")).toInt() > 0)
            anyFixtures = true;
    if (anyFixtures == false)
    {
        *error = QStringLiteral("No selected group contains fixtures");
        return nullptr;
    }

    // Step 3 (skipped by the wizard when no new group is placed): stage default + suggested size
    wiz->setCurrentStep(2);
    if (choices.stageType >= 0)
        wiz->setStageType(choices.stageType);
    if (choices.hasEnvSize)
    {
        wiz->setEnvWidth(choices.envWidth);
        wiz->setEnvHeight(choices.envHeight);
        wiz->setEnvDepth(choices.envDepth);
    }

    // Step 4: entering it applies the show type's effect defaults
    wiz->setCurrentStep(3);
    if (choices.hasEffects)
    {
        for (const QVariant &v : wiz->effectsModel().toList())
        {
            const QVariantMap e = v.toMap();
            const int flag = e.value(QStringLiteral("flag")).toInt();
            wiz->setEffectEnabled(flag, e.value(QStringLiteral("available")).toBool() &&
                                        choices.effectFlags.contains(flag));
        }
    }

    // Step 5: controllers are re-scanned on entry (auto-picks a single patched one)
    wiz->setCurrentStep(4);
    if (choices.hasController)
    {
        if (choices.ctrlUniverse >= 0)
        {
            bool patched = false;
            for (const QVariant &v : wiz->controllersModel().toList())
                if (v.toMap().value(QStringLiteral("universe")).toInt() == choices.ctrlUniverse)
                    patched = true;
            if (patched == false)
            {
                *error = QStringLiteral("Universe %1 has no patched input controller").arg(choices.ctrlUniverse + 1);
                return nullptr;
            }
        }
        wiz->setControllerUniverse(choices.ctrlUniverse);
        wiz->setMapController(choices.ctrlMap);
        wiz->setSendFeedback(choices.ctrlFeedback);
        wiz->setMapColors(choices.ctrlColors);
    }

    wiz->setCurrentStep(5);
    return wiz.release();
}

QJsonObject App::wizardProjectOptions()
{
    std::unique_ptr<StageWizard> wiz(new StageWizard(m_doc, m_fixtureManager, m_functionManager,
                                                    m_virtualConsole, m_contextManager, this));
    wiz->setCurrentStep(1);

    QJsonArray groups;
    for (const QVariant &b : wiz->groupsModel().toList())
    {
        const QVariantMap box = b.toMap();
        QJsonObject g;
        g.insert(QStringLiteral("groupId"), double(box.value(QStringLiteral("groupId")).toUInt()));
        g.insert(QStringLiteral("name"), box.value(QStringLiteral("name")).toString());
        g.insert(QStringLiteral("fixtureIds"), groupFixtureIds(wiz.get(), box.value(QStringLiteral("index")).toInt()));
        g.insert(QStringLiteral("suggestedRole"), box.value(QStringLiteral("role")).toInt());
        copyCapabilities(box, g);
        groups.append(g);
    }

    wiz->refreshControllers();

    QJsonObject result;
    result.insert(QStringLiteral("groups"), groups);
    result.insert(QStringLiteral("controllers"), QJsonArray::fromVariantList(wiz->controllersModel().toList()));
    result.insert(QStringLiteral("envSize"), envSizeJson(wiz.get()));
    return result;
}

QJsonObject App::wizardPreview(const ApiWizardChoices &choices, QString *error)
{
    std::unique_ptr<StageWizard> wiz(wizardFromChoices(choices, error));
    if (!wiz)
        return QJsonObject();

    QJsonArray groups;
    for (const QVariant &v : wiz->fixtureRoleModel().toList())
    {
        const QVariantMap m = v.toMap();
        QJsonObject g;
        quint32 gid = m.value(QStringLiteral("groupId")).toUInt();
        g.insert(QStringLiteral("groupId"), gid == FixtureGroup::invalidId() ? QJsonValue() : QJsonValue(double(gid)));
        g.insert(QStringLiteral("name"), m.value(QStringLiteral("name")).toString());
        g.insert(QStringLiteral("fixtureIds"), groupFixtureIds(wiz.get(), m.value(QStringLiteral("index")).toInt()));
        g.insert(QStringLiteral("role"), m.value(QStringLiteral("role")).toInt());
        copyCapabilities(m, g);
        groups.append(g);
    }

    QJsonObject controller;
    controller.insert(QStringLiteral("universe"), wiz->controllerUniverse());
    controller.insert(QStringLiteral("map"), wiz->mapController());
    controller.insert(QStringLiteral("feedback"), wiz->sendFeedback());
    controller.insert(QStringLiteral("colors"), wiz->mapColors());
    controller.insert(QStringLiteral("mappingPreview"),
                      wiz->controllerUniverse() >= 0 ? wiz->controllerMappingPreview() : QString());

    QJsonObject result;
    result.insert(QStringLiteral("showType"), wiz->showType());
    result.insert(QStringLiteral("groups"), groups);
    result.insert(QStringLiteral("placesFixtures"), wiz->hasNewGroups());
    result.insert(QStringLiteral("stageType"), wiz->stageType());
    result.insert(QStringLiteral("envSize"), envSizeJson(wiz.get()));
    result.insert(QStringLiteral("effects"), QJsonArray::fromVariantList(wiz->effectsModel().toList()));
    result.insert(QStringLiteral("controller"), controller);
    result.insert(QStringLiteral("summary"), QJsonArray::fromVariantList(wiz->summaryModel().toList()));
    return result;
}

bool App::wizardGenerate(const ApiWizardChoices &choices, QString *error)
{
    std::unique_ptr<StageWizard> wiz(wizardFromChoices(choices, error));
    if (!wiz)
        return false;

    wiz->generate();
    return true;
}
