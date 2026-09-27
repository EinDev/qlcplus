/*
  Q Light Controller Plus - Control API unit test
  apiwizarddomain_test.h

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

#ifndef APIWIZARDDOMAIN_TEST_H
#define APIWIZARDDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <functional>

#include "apiwizardhost.h"

class QSignalSpy;
class QWebSocket;
class ApiServer;
class Doc;

/**
 * Stand-in for qmlui's App: records the choices it receives and answers with canned,
 * engine-shaped data; generate() creates one fixture group and one Scene in the Doc so the
 * created-event plumbing (ApiDocChanges) can be asserted. The real generation (StageWizard) is
 * exercised in the sandbox end-to-end run (webui/tools/e2e/wizard-import.js), not here.
 */
class FakeWizardHost final : public QObject, public ApiWizardHost
{
    Q_OBJECT

public:
    explicit FakeWizardHost(Doc *doc) : m_doc(doc) {}

    QJsonObject wizardProjectOptions() override;
    QJsonObject wizardPreview(const ApiWizardChoices &choices, QString *error) override;
    bool wizardGenerate(const ApiWizardChoices &choices, QString *error) override;

    ApiWizardChoices lastChoices;
    int generateCalls = 0;
    bool failGenerate = false;

private:
    Doc *m_doc;
};

class ApiWizardDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void unsupportedWithoutHost();
    void getOptionsReturnsCataloguesAndProject();
    void previewConvertsChoicesBothWays();
    void previewRejectsInvalidChoices();
    void generateChecksRevisionAndBroadcasts();
    void generateReportsHostFailure();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic, int timeoutMs = 3000);
    QString errorCode(const QJsonObject &reply) const;
    void connectClient();

private:
    Doc *m_doc = nullptr;
    FakeWizardHost *m_host = nullptr;
    ApiServer *m_apiServer = nullptr;
    QWebSocket *m_client = nullptr;
    QList<quint32> m_fixtures;
    quint32 m_group = 0;
};

#endif
