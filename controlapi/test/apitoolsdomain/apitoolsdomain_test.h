/*
  Q Light Controller Plus - Control API unit test
  apitoolsdomain_test.h

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

#ifndef APITOOLSDOMAIN_TEST_H
#define APITOOLSDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

class QSignalSpy;
class Doc;
class ApiServer;
class QWebSocket;
class Show;

class ApiToolsDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void inspectRejectsBadParams();
    void inspectReportsFixtureOverrideAndFader();
    void inspectAttributesRunningScene();

    void legacyTimingNothingFlaggedWithoutAProjectFile();
    void legacyTimingFlagsOldUploadPreviewsAndConverts();
    void legacyTimingDismissAndNewerFileAreNotFlagged();

private:
    QJsonObject call(const QString &method, const QJsonObject &params);
    QJsonObject result(const QString &method, const QJsonObject &params);
    QString errorCode(const QJsonObject &reply) const;
    void useFakeHost();
    Show *addShow(const QString &name, const QList<QPair<quint32, quint32>> &items);

private:
    Doc *m_doc = nullptr;
    QObject *m_host = nullptr;
    ApiServer *m_apiServer = nullptr;
    QWebSocket *m_client = nullptr;
    int m_nextId = 0;
};

#endif
