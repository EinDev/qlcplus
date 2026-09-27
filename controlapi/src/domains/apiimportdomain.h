/*
  Q Light Controller Plus - Control API
  apiimportdomain.h

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

#ifndef APIIMPORTDOMAIN_H
#define APIIMPORTDOMAIN_H

#include <QJsonObject>
#include <QObject>

class ProjectImporter;
class ApiServer;
class Doc;

/**
 * "Import from project" (docs/api-spec/fragments/core.yaml core.project.importList /
 * core.project.import) over the engine's ProjectImporter - the same code qmlui's ImportManager
 * uses, so no host interface is involved. Stateless: every call parses the source project again
 * (a server path or an uploaded file), so nothing is held between the list and the import.
 */
class ApiImportDomain : public QObject
{
    Q_OBJECT

public:
    ApiImportDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

private:
    void registerMethods();

    /** Load the CoreProjectImportSource in $params into $importer. Returns false and fills
     *  $errorCode / $error when it cannot. */
    bool loadSource(ProjectImporter &importer, const QJsonObject &params,
                    QString *errorCode, QString *error) const;

    /** CoreProjectImportContents of the loaded source */
    QJsonObject contentsToJson(ProjectImporter &importer) const;

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
