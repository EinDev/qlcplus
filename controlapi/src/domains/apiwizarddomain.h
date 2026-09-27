/*
  Q Light Controller Plus - Control API
  apiwizarddomain.h

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

#ifndef APIWIZARDDOMAIN_H
#define APIWIZARDDOMAIN_H

#include <QJsonObject>
#include <QObject>

class ApiWizardHost;
struct ApiWizardChoices;
class ApiServer;
class Doc;

/**
 * The Show Wizard (docs/api-spec/fragments/core.yaml core.wizard.*): getOptions, preview and
 * generate over qmlui's StageWizard, reached through ApiWizardHost (implemented by App in
 * qmlui/app_apiwizard.cpp). This class owns the wire spelling (enum names, string ids), the
 * static catalogues the QML steps show, choice validation, the baseRevision check and the created
 * events (through ApiDocChanges); the host only drives StageWizard.
 */
class ApiWizardDomain : public QObject
{
    Q_OBJECT

public:
    ApiWizardDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

    /** Parse and validate CoreWizardChoices. Returns false with $error on invalid params. */
    bool parseChoices(const QJsonObject &json, ApiWizardChoices &choices, QString *error) const;

private:
    void registerMethods();
    ApiWizardHost *host() const;

    /** Host preview JSON (engine ints) -> CoreWizardPreview (wire names) */
    QJsonObject previewToWire(const QJsonObject &preview) const;

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
