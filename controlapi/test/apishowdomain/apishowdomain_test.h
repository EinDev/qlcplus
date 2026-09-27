/*
  Q Light Controller Plus - Control API unit test
  apishowdomain_test.h

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

#ifndef APISHOWDOMAIN_TEST_H
#define APISHOWDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

class QSignalSpy;
class QWebSocket;
class ApiServer;
class Chaser;
class Scene;
class Track;
class Show;
class Doc;

/**
 * End-to-end test of the functions.show.* methods: a real ApiServer on an
 * ephemeral localhost port, a real QWebSocket client, a Doc with a running
 * MasterTimer (the playhead test starts the Show for real), two Scenes, a
 * Chaser and one Show with one track holding one Scene item at 0..5000 ms.
 */
class ApiShowDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void getReturnsShowTypeDetail();
    void setTimeDivisionAppliesAndBroadcasts();
    void setTimeDivisionRequiresBpmForBeats();
    void trackAddRemoveRename();
    void trackAddRejectsNonScene();
    void trackSetMuteAndSolo();
    void trackMoveSwapsIdsAndBroadcastsTracksChanged();
    void itemAddUsesDefaultsAndBroadcasts();
    void itemAddRejectsOverlapWithSuggestion();
    void itemAddRejectsTheShowItself();
    void itemMoveSameAndOtherTrack();
    void itemMoveRejectsOverlapAndLocked();
    void itemResizeRejectsOverlapAndMinimum();
    void itemSetColorSetLockedRemove();
    void rippleInsertShiftsAndExtends();
    void rippleCutShrinksAndPulls();
    void rippleWithNothingAtCursorIsNoOp();
    void rippleSkipsLockedItems();
    void staleRevisionIsConflict();
    void playheadEventIsGatedAndFollowsStartOffset();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic,
                             const std::function<bool(const QJsonObject &)> &accept, int timeoutMs = 3000);

    /** functions.get for m_show, returning result.typeDetail */
    QJsonObject getDetail();
    /** the current docRevision as the server reports it */
    int revision();
    /** params with showId + the current baseRevision merged in */
    QJsonObject showParams(const QJsonObject &extra = QJsonObject());
    /** a mutation on m_show: params get showId + baseRevision, result is asserted ok */
    QJsonObject mutate(const QString &method, const QJsonObject &extra);
    QJsonObject firstItem(const QJsonObject &detail, int trackIndex = 0, int itemIndex = 0);

private:
    Doc *m_doc;
    Scene *m_scene;
    Scene *m_scene2;
    Chaser *m_chaser;
    Show *m_show;
    Track *m_track;
    quint32 m_itemId;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
