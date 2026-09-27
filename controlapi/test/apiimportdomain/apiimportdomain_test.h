/*
  Q Light Controller Plus - Control API unit test
  apiimportdomain_test.h

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

#ifndef APIIMPORTDOMAIN_TEST_H
#define APIIMPORTDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <QTemporaryDir>
#include <functional>

class QSignalSpy;
class QWebSocket;
class ApiServer;
class Doc;

/**
 * core.project.importList / core.project.import against the real engine: a source project is
 * built in memory and written to a temporary .qxw, the target Doc already holds a fixture with
 * the same name as one source fixture, an address-blocking fixture, a function and a palette with
 * the same name as the source palette, so every remapping rule (name match, new id, shifted
 * address, dependency closure, reference rewriting) is observable.
 *
 * Source project: generic dimmers "Dimmer A" (id 0, address 0) and "Dimmer B" (id 1, address 1),
 * group "Both" (A,B), palette "Warm", Scene "Look" (values on A and B + palette "Warm"),
 * EFX "Move" (heads A and B), Chaser "Run" (steps Look, Move), Scene "Solo" (value on B).
 */
class ApiImportDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void listReportsContentsAndDependencies();
    void importRemapsEveryReference();
    void importFromUploadedContent();
    void importFixtureGroupBringsFixtures();
    void errors();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic,
                             const std::function<bool(const QJsonObject &)> &accept, int timeoutMs = 3000);
    int revision();
    QJsonObject pathSource() const;

private:
    QTemporaryDir m_dir;
    QString m_sourcePath;
    Doc *m_doc = nullptr;
    ApiServer *m_apiServer = nullptr;
    QWebSocket *m_client = nullptr;
    quint32 m_targetA = 0;
    quint32 m_targetPalette = 0;
};

#endif
