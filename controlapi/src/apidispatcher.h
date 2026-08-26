/*
  Q Light Controller Plus - Control API
  apidispatcher.h

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

#ifndef APIDISPATCHER_H
#define APIDISPATCHER_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>

// Included in full (not just forward-declared) even though this header only
// ever uses ApiSession* by pointer: with this class also being Q_OBJECT/
// moc-processed, letting ApiSession stay incomplete here causes the two
// classes' moc output to be order-sensitive within mocs_compilation.cpp -
// GCC's -Wsfinae-incomplete then rejects it, since a completeness check
// on an incomplete ApiSession runs before ApiSession is later defined
// elsewhere in the same translation unit.
#include "apisession.h"

/**
 * Generic method-name -> handler table. Turns a parsed ApiEnvelope::Request
 * into a call to whichever domain object registered that method name,
 * deliberately avoiding a single giant hand-written if/else chain (the
 * pattern the legacy webaccess/src/webaccessbase.cpp uses, at ~2000 lines
 * per UI variant). Domain classes (e.g. controlapi/src/domains/apiiodomain.h)
 * each register their own methods at construction time; this class knows
 * nothing about any specific domain.
 *
 * A handler is responsible for sending its own response via
 * session->send(ApiEnvelope::build...Response(...)) - the dispatcher does
 * not do this automatically, since some methods (like "hello") need to
 * mutate session state before responding.
 */
class ApiDispatcher : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(ApiSession *session, const QString &id, const QJsonObject &params)>;

    explicit ApiDispatcher(QObject *parent = nullptr);

    /** Register a handler for an exact method name (e.g. "io.universe.list").
     *  Registering the same name twice is a programming error (asserts in
     *  debug builds) - every method must have exactly one owner. */
    void registerMethod(const QString &method, Handler handler);

    /** Parse rawJson as a request and dispatch it to the matching handler.
     *  Sends an INVALID_PARAMS response for malformed frames (if an "id"
     *  could be recovered) or a NOT_FOUND response for an unknown method.
     *  Call this from ApiSession::textMessageReceived. */
    void dispatch(ApiSession *session, const QString &rawJson);

private:
    QHash<QString, Handler> m_handlers;
};

#endif
