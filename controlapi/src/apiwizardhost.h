/*
  Q Light Controller Plus - Control API
  apiwizardhost.h

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

#ifndef APIWIZARDHOST_H
#define APIWIZARDHOST_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <climits>

/**
 * One ticked group box of the Show Wizard (docs/api-spec/fragments/core.yaml CoreWizardChoices).
 * Already validated and converted to engine values by ApiWizardDomain.
 */
struct ApiWizardGroupChoice
{
    /** Existing FixtureGroup id, or UINT_MAX for a group the wizard creates */
    quint32 groupId = UINT_MAX;
    QString name;
    /** Membership; for an existing group only applied when hasFixtureIds */
    QList<quint32> fixtureIds;
    bool hasFixtureIds = false;
    /** StageWizard::FixtureRole, or -1 to keep the auto-detected role */
    int role = -1;
};

/** A full Show Wizard choice set, in StageWizard's own enum values. */
struct ApiWizardChoices
{
    int showType = 0;                       ///< StageWizard::ShowType
    QList<ApiWizardGroupChoice> groups;
    int stageType = -1;                     ///< MonitorProperties::StageType, -1 = wizard default
    bool hasEnvSize = false;
    double envWidth = 0, envHeight = 0, envDepth = 0;
    bool hasEffects = false;                ///< false = show-type defaults
    QList<int> effectFlags;                 ///< StageWizard::EffectFlag values to enable
    bool hasController = false;             ///< false = the wizard's own pick (the only patched one)
    int ctrlUniverse = -1;                  ///< -1 = no controller
    bool ctrlMap = true;
    bool ctrlFeedback = true;
    bool ctrlColors = true;
};

/**
 * The Show Wizard (core.wizard.*), implemented by qmlui's App over a headless StageWizard
 * (qmlui/stagewizard/). ApiWizardDomain reaches it with dynamic_cast on ApiServer's parent, like
 * ApiVcHost; with no host the methods answer UNSUPPORTED.
 *
 * Every value in and out is engine-shaped (enum ints, numeric ids); ApiWizardDomain owns the wire
 * spelling. Keys of the returned objects are documented per method.
 */
class ApiWizardHost
{
public:
    virtual ~ApiWizardHost() {}

    /** Project-dependent part of core.wizard.getOptions:
     *  { groups: [{ groupId(uint), name, fixtureIds[uint], hasMovement, hasRGB, hasColorWheel, hasGobo,
     *               hasShutter, hasDimmer, suggestedRole(int) }],
     *    controllers: [StageWizard::controllersModel() maps],
     *    envSize: { width, height, depth } } */
    virtual QJsonObject wizardProjectOptions() = 0;

    /** Resolve $choices the way the wizard steps would and return what the summary shows:
     *  { showType(int), groups: [{ groupId(uint|null), name, fixtureIds[uint], role(int), has* }],
     *    placesFixtures, stageType(int), envSize{}, effects: [{ flag(int), name, family, enabled,
     *    available, preview }], controller: { universe, map, feedback, colors, mappingPreview },
     *    summary: [{ section, detail }] }.
     *  Returns an empty object and sets $error when the choices cannot be applied. */
    virtual QJsonObject wizardPreview(const ApiWizardChoices &choices, QString *error) = 0;

    /** Run the generation for $choices. Returns false and sets $error when nothing was generated. */
    virtual bool wizardGenerate(const ApiWizardChoices &choices, QString *error) = 0;
};

#endif
