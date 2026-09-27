/*
  Q Light Controller Plus - Control API
  apifixturedefsdomain.h

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

#ifndef APIFIXTUREDEFSDOMAIN_H
#define APIFIXTUREDEFSDOMAIN_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class ApiServer;
class ApiSession;
class Doc;
class QLCFixtureDef;
class QLCFixtureMode;
class QLCChannel;
class QLCCapability;

/**
 * fixturedefs.* domain (docs/api-spec/fragments/fixturedefs.yaml +
 * fixturedefs-notes.md): the fixture *definition* editor backend - the
 * .qxf authoring side of qmlui/fixtureeditor/, exposed over the Control API.
 *
 * Two tiers of state, two revision counters (00-conventions.md §4c), neither
 * of them the show's docRevision:
 *
 * - The shared definition library (Doc::fixtureDefCache()). Every
 *   manufacturer+model has a per-definition `defRevision` (kept in
 *   m_defRevisions, 0 until the first save/delete through this domain);
 *   fixturedefs.save and fixturedefs.delete take it as `baseRevision`.
 * - Editing sessions. A session owns a private QLCFixtureDef clone (exactly
 *   what EditorView does with `new QLCFixtureDef(fixtureDef)`), has its own
 *   `sessionRevision` starting at 0, and every channel/mode/capability/alias
 *   mutation takes that as `baseRevision`. Nothing reaches the cache or the
 *   disk before fixturedefs.save.
 *
 * Channels and modes are addressed by synthetic ids ("ch-N"/"mode-N") that
 * are stable for the life of one session (see the notes for why not names).
 * The maps live in the Session; QLCFixtureDef itself is untouched.
 *
 * Sessions deliberately outlive the client connection that created them
 * (mirrors FixtureEditor keeping its editors open until the user closes
 * them); they are only released by fixturedefs.session.close or when this
 * domain is destroyed.
 *
 * Everything here is engine-level (engine/src/qlcfixturedef.h and friends),
 * so no qmlui host interface is involved. The colour-name detection behind
 * fixturedefs.channel.capability.autoPatchColors is a self-contained copy of
 * qmlui/fixtureeditor/channeledit.cpp's logic reading namedrgb.qxcf directly,
 * rather than a link against qmlui's ColorFilters.
 */
class ApiFixtureDefsDomain : public QObject
{
    Q_OBJECT

public:
    ApiFixtureDefsDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiFixtureDefsDomain();

    /** One in-memory editing session. */
    struct Session
    {
        QString id;
        QLCFixtureDef *def = nullptr;
        int revision = 0;
        bool modified = false;
        /** defRevision of the library entry this session was opened/last
         *  saved against; hasBaseRevision=false means "null" (never saved,
         *  no library counterpart yet). */
        bool hasBaseRevision = false;
        int baseRevision = 0;
        /** Absolute path the definition will be saved to (empty until the
         *  first save of a brand-new session). */
        QString fileName;
        QHash<const QLCChannel *, QString> channelIds;
        QHash<const QLCFixtureMode *, QString> modeIds;
        int nextChannelSeq = 1;
        int nextModeSeq = 1;

        QString channelId(const QLCChannel *channel);
        QString modeId(const QLCFixtureMode *mode);
        QLCChannel *channelById(const QString &id) const;
        QLCFixtureMode *modeById(const QString &id) const;
        void assignIds();
    };

private:
    void registerMethods();

    // ---- library helpers ----
    QString defKey(const QString &manufacturer, const QString &model) const;
    int defRevision(const QString &manufacturer, const QString &model) const;
    int bumpDefRevision(const QString &manufacturer, const QString &model);

    // ---- session helpers ----
    Session *session(const QString &id) const;
    Session *createSession(QLCFixtureDef *def);
    void destroySession(Session *s);
    /** Resolves params.sessionId and checks params.baseRevision against the
     *  session revision. Sends NOT_FOUND / CONFLICT itself and returns
     *  nullptr on failure. checkRevision=false skips the baseRevision test
     *  (read-only methods). */
    Session *resolveSession(ApiSession *client, const QString &id, const QJsonObject &params, bool checkRevision);
    /** Bumps the session revision, marks it modified and broadcasts
     *  fixturedefs.session.updated with the full definition snapshot. */
    void commitSession(Session *s, const QString &changeKind, const QString &originClientId);
    void sendSessionAck(ApiSession *client, const QString &id, Session *s, const QJsonObject &extra = QJsonObject());

    // ---- JSON ----
    QJsonObject definitionToJson(Session *s) const;
    QJsonObject sessionInfoToJson(Session *s) const;
    QJsonObject sessionOpenedResult(Session *s) const;
    QStringList validate(Session *s) const;

    // ---- mode bookkeeping ----
    /** Rebuild a mode's channel slot list from an ordered list of channels
     *  (with per-slot acts-on channel, nullptr for none), remapping the
     *  existing heads by channel identity so they survive reordering and
     *  dropping channels that vanished. */
    void rebuildModeChannels(QLCFixtureMode *mode, const QList<QLCChannel *> &channels, const QList<QLCChannel *> &actsOn);

private:
    Doc *m_doc;
    ApiServer *m_server;
    QHash<QString, Session *> m_sessions;
    QHash<QString, int> m_defRevisions;
    int m_nextSessionSeq;
};

#endif
