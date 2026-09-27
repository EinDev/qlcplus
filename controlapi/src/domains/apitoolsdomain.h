/*
  Q Light Controller Plus - Control API
  apitoolsdomain.h

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

#ifndef APITOOLSDOMAIN_H
#define APITOOLSDOMAIN_H

#include <QJsonObject>
#include <QObject>
#include <QSet>

class ApiServer;
class ApiIoDomain;
class ApiCoreDomain;
class Doc;
class Show;

/**
 * Diagnostic and one-off tool methods the Qt UI offers outside any single
 * editor (docs/api-spec/fragments/io.yaml + functions-advanced.yaml):
 *
 * - io.dmx.channel.inspect: SimpleDesk.qml's "channel value debug" popup
 *   (qmlui/simpledesk.cpp SimpleDesk::debugChannelInfo()) as structured
 *   JSON - which GenericFaders currently drive one DMX channel, the
 *   Function owning each fader and what started that Function
 *   (Function::sources()), the engine's GenericDMXSource feature tag when
 *   the fader belongs to a desktop tool, this API's own Simple Desk
 *   override, and Universe::lastChannelWrite() for a latched value no
 *   fader touches any more. Read-only, §4b.
 *
 * - functions.show.legacyTiming.get / .convert / .dismiss: ADR 0001
 *   decision 4 (docs/adr/0001-show-timeline-canonical-time-storage.md) -
 *   the Shows of a project saved before QLC+ 5.3.1 whose timeline may hold
 *   legacy beat-pseudo-count values (Doc::possiblyAffectedLegacyBeatShows()),
 *   and the per-Show conversion the desktop's LegacyShowTimingConvertDialog
 *   runs (qmlui/showmanager.cpp convertLegacyBeatShow(), same formula).
 *   The project's Creator/Version is re-read from the project file on disk
 *   (or taken from the upload, see ApiCoreDomain::uploadedCreatorVersion()),
 *   so no qmlui state is needed. Converted / dismissed Shows are remembered
 *   until the next project load so a reconnecting client is not asked again.
 */
class ApiToolsDomain : public QObject
{
    Q_OBJECT

public:
    ApiToolsDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, ApiCoreDomain *coreDomain,
                   QObject *parent = nullptr);

    /** ms represented by one legacy beat-pseudo-count unit at $bpm
     *  (ShowManager::legacyBeatPseudoUnitToMs()), 0 for bpm <= 0 */
    static double legacyUnitToMs(int bpm);

private:
    void registerMethods();

    /** io.dmx.channel.inspect result for one universe/channel (both
     *  validated by the caller) */
    QJsonObject inspectChannel(quint32 universeId, quint32 channel) const;

    /** Creator/Version of the current project and where it came from:
     *  "file" (re-read from the host's project file), "upload", or "none"
     *  (never loaded from a file: nothing to check). */
    QString projectCreatorVersion(QString *source) const;

    /** Shows Doc::possiblyAffectedLegacyBeatShows() flags for the current
     *  project, minus the ones converted/dismissed since it was loaded */
    QList<Show *> flaggedShows(QString *creatorVersion, QString *source) const;

private slots:
    void slotProjectChanged();

private:
    Doc *m_doc;
    ApiServer *m_server;
    ApiIoDomain *m_ioDomain;
    ApiCoreDomain *m_coreDomain;

    /** Shows converted or dismissed since the last project load */
    QSet<quint32> m_handledShows;
};

#endif
