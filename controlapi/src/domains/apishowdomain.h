/*
  Q Light Controller Plus - Control API
  apishowdomain.h

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

#ifndef APISHOWDOMAIN_H
#define APISHOWDOMAIN_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QHash>

class ApiServer;
class ShowFunction;
class Function;
class Track;
class Show;
class Doc;

/**
 * docs/api-spec/fragments/functions-advanced.yaml, the functions.show.*
 * slice: the Show Manager timeline's server side.
 *
 * - functions.get's typeDetail for Function::ShowType (FunctionsShowDetail:
 *   time division, every track with its items, totalDuration) is registered
 *   through ApiFunctionsDomain::setTypeDetailProvider() from the constructor.
 * - Document edits (§4a, baseRevision): setTimeDivision, track.add/remove/
 *   rename/move/setMute/setSolo, item.add/move/remove/resize/setColor/
 *   setLocked, rippleInsertTime/rippleCutTime. Each one broadcasts the
 *   specced event; the bulk ones (track.move, track.setSolo, the ripples)
 *   carry an RFC 6902 patch against FunctionsShowDetail.
 * - Overlap rules are the Show Manager's: half-open intervals, an item whose
 *   Function is gone blocks nothing, locked items are not moved/resized/
 *   rippled. item.move/resize/add refuse an overlapping spot with
 *   INVALID_PARAMS and say where it would fit (ShowMoveHelper, shared with
 *   qmlui/showmanager.cpp). The ripple edits mirror ShowManager::
 *   insertTimeAtCursor/cutTimeAtCursor including the Chaser step surgery,
 *   minus the Tardis undo bookkeeping (API edits are not undoable).
 * - Live playhead (§4b, subscribe-gated): functions.show.<id>.playhead
 *   {functionId, time} from Show::timeChanged while a Show runs, throttled
 *   to one event per PlayheadIntervalMs per Show (the runner emits every
 *   MasterTimer tick), except that a jump backwards (seek) and the first
 *   tick after a start are always sent.
 *
 * The Show's engine objects are only ever touched on this (the main) thread;
 * a running ShowRunner works on an immutable schedule snapshot and picks up
 * the rebuilt one on its next tick (see engine/src/show.h), which is what
 * makes editing a playing Show through the API safe.
 */
class ApiShowDomain : public QObject
{
    Q_OBJECT

public:
    ApiShowDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

    /** Minimum spacing of functions.show.<id>.playhead events per Show */
    static const int PlayheadIntervalMs = 100;

    /** FunctionsShowDetail for one Show, as functions.get carries it */
    static QJsonObject detailToJson(Doc *doc, Show *show);

    /** FunctionsShowTrack (with its items) / FunctionsShowItem */
    static QJsonObject trackToJson(Doc *doc, Track *track);
    static QJsonObject itemToJson(Doc *doc, ShowFunction *sf);
    static QJsonArray tracksToJson(Doc *doc, Show *show);

private slots:
    void slotFunctionAdded(quint32 id);
    void slotShowTimeChanged(quint32 ms);
    void slotShowStopped(quint32 id);

private:
    void registerMethods();

    /** Connect the playhead relay to $function if it is a Show */
    void watchShow(Function *function);

private:
    Doc *m_doc;
    ApiServer *m_server;

    /** Playhead throttle: per Show, the clock reading and the time last sent */
    QElapsedTimer m_clock;
    QHash<quint32, qint64> m_playheadSentAt;
    QHash<quint32, quint32> m_playheadSentTime;
};

#endif
