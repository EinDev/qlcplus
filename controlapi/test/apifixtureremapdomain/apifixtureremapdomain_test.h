/*
  Q Light Controller Plus - Control API unit test
  apifixtureremapdomain_test.h

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

#ifndef APIFIXTUREREMAPDOMAIN_TEST_H
#define APIFIXTUREREMAPDOMAIN_TEST_H

#include <QJsonObject>
#include <QObject>

class Doc;
class ApiServer;
class QWebSocket;
class QLCFixtureDef;

class ApiFixtureRemapDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void suggestChannelMapGenericIsOneToOne();
    void suggestChannelMapMatchesByGroup();
    void applyMovesSceneGroupAndMonitorToNewFixture();
    void applyDeletesUnmappedSources();
    void applyRefusesOverlapAndStaleRevision();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();
    quint32 addGenericFixture(int channels);
    quint32 addMovingHead();

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    quint32 m_nextAddress;
    QLCFixtureDef *m_movingHeadDef;
};

#endif
