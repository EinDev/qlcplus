/*
  Q Light Controller Plus - Control API
  apidispatcher.cpp

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

#include <QDebug>

#include "apidispatcher.h"
#include "apienvelope.h"
#include "apisession.h"

ApiDispatcher::ApiDispatcher(QObject *parent)
    : QObject(parent)
{
}

void ApiDispatcher::registerMethod(const QString &method, Handler handler)
{
    Q_ASSERT_X(m_handlers.contains(method) == false, "ApiDispatcher::registerMethod",
               qPrintable(QStringLiteral("method '%1' already has a handler").arg(method)));
    m_handlers.insert(method, handler);
}

void ApiDispatcher::dispatch(ApiSession *session, const QString &rawJson)
{
    ApiEnvelope::Request request;
    QString parseError;

    if (ApiEnvelope::parseRequest(rawJson, request, parseError) == false)
    {
        // No reliable "id" to correlate a response with a fully malformed
        // frame - log and drop, matching typical WS server behaviour for
        // garbage input rather than guessing at a reply target.
        qWarning() << "ApiDispatcher: dropping malformed request:" << parseError;
        return;
    }

    if (session->helloed() == false && request.method != QStringLiteral("hello"))
    {
        session->send(ApiEnvelope::buildErrorResponse(request.id, ApiEnvelope::ErrUnauthorized,
                                                        QStringLiteral("Send \"hello\" first")));
        return;
    }

    Handler handler = m_handlers.value(request.method);
    if (handler == nullptr)
    {
        session->send(ApiEnvelope::buildErrorResponse(request.id, ApiEnvelope::ErrNotFound,
                                                        QStringLiteral("Unknown method \"%1\"").arg(request.method)));
        return;
    }

    handler(session, request.id, request.params);
}
