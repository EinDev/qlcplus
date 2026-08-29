/*
  Q Light Controller Plus - Control API
  apienvelope.h

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

#ifndef APIENVELOPE_H
#define APIENVELOPE_H

#include <QJsonObject>
#include <QString>

/**
 * (De)serialization of the WebSocket control API's JSON message envelope,
 * as defined in docs/api-spec/00-conventions.md §2. Pure functions, no
 * engine/Doc dependency - independently unit-testable.
 *
 * Every frame is exactly one of:
 *   request:  {"type":"request","id":"...","method":"...","params":{...}}
 *   response: {"type":"response","id":"...","ok":true|false,"result"|"error":{...}}
 *   event:    {"type":"event","topic":"...","data":{...},"originClientId":"..."|null}
 */
class ApiEnvelope
{
public:
    /** A parsed incoming request frame. */
    struct Request
    {
        QString id;
        QString method;
        QJsonObject params;
    };

    /**
     * Parse a raw incoming WebSocket text frame as a "request" envelope.
     *
     * @param rawJson The raw text frame received from the client
     * @param outRequest Filled in on success
     * @param outErrorMessage Filled in on failure (malformed JSON, wrong
     *        "type", missing "id"/"method") - suitable for an INVALID_PARAMS
     *        error response, though the caller has no "id" to correlate it
     *        with in that case (per convention, malformed frames are logged/
     *        dropped rather than answered - there is no reliable id to
     *        reply to).
     * @return true if parsing succeeded and outRequest is valid
     */
    static bool parseRequest(const QString &rawJson, Request &outRequest, QString &outErrorMessage);

    /** Build a successful response: {"type":"response","id":id,"ok":true,"result":result} */
    static QString buildOkResponse(const QString &id, const QJsonObject &result);

    /** Build an error response: {"type":"response","id":id,"ok":false,"error":{"code":code,"message":message,"details":details}} */
    static QString buildErrorResponse(const QString &id, const QString &code, const QString &message,
                                       const QJsonObject &details = QJsonObject());

    /** Build an event frame: {"type":"event","topic":topic,"data":data,"originClientId":originClientId|null} */
    static QString buildEvent(const QString &topic, const QJsonObject &data, const QString &originClientId = QString());

    // Standard error codes (docs/api-spec/00-conventions.md §7)
    static const QString ErrConflict;
    static const QString ErrNotFound;
    static const QString ErrInvalidParams;
    static const QString ErrUnauthorized;
    static const QString ErrUnsupported;
    static const QString ErrInternal;
    static const QString ErrInvalidState;
    static const QString ErrNotImplemented;
};

#endif
