/*
  Q Light Controller Plus - Control API unit test
  apicoredomain_test.h

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

#ifndef APICOREDOMAIN_TEST_H
#define APICOREDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <QTemporaryDir>

class Doc;
class ApiServer;
class QWebSocket;

class ApiCoreDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void projectGetReturnsMetadata();
    void modeGetSetBroadcastsEvent();
    void settingsGetSetBroadcastsEvent();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    QTemporaryDir m_settingsDir;
};

#endif
