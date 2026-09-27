/*
  Q Light Controller Plus - Control API unit test
  apifixturechannelsdomain_test.h

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

#ifndef APIFIXTURECHANNELSDOMAIN_TEST_H
#define APIFIXTURECHANNELSDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

class QTemporaryDir;
class Doc;
class ApiServer;
class QWebSocket;

/**
 * fixtures.channel.setBehaviour, fixtures.modifiers.* and
 * fixtures.colorFilters.list over a real ApiServer + QWebSocket. The user
 * modifiers folder is redirected to a temporary directory through
 * QLCPLUS_USER_MODIFIERS_DIR, so nothing touches the developer's real
 * %UserProfile%\QLC+\ModifiersTemplates.
 */
class ApiFixtureChannelsDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void userDirectoryFollowsEnvironment();

    void setBehaviourForcesLtpAndExcludesFade();
    void setBehaviourRejectsImpossiblePrecedence();
    void setBehaviourAppliesToSameType();
    void setBehaviourWithStaleRevisionConflicts();
    void setBehaviourAttachesModifierToUniverse();
    void setBehaviourUnknownModifierIsNotFound();

    void modifiersSaveWritesUserFileAndLists();
    void modifiersSaveRejectsSystemTemplateAndBadPoints();
    void modifiersSaveUpdatesAttachedModifierInPlace();
    void modifiersRenameMovesFileAndFollowsFixtures();
    void modifiersDeleteDetachesFixtures();
    void modifiersDeleteSystemTemplateIsRejected();

    void colorFiltersListAnswers();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    quint32 patchGeneric(int address, int channels, int quantity = 1);
    QJsonObject saveTemplate(const QString &name, const QList<QPair<int, int>> &points);

private:
    QTemporaryDir *m_modifiersDir;
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
