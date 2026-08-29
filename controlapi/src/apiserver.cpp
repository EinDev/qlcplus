/*
  Q Light Controller Plus - Control API
  apiserver.cpp

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

#include <QJsonArray>
#include <QWebSocketServer>
#include <QWebSocket>

#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "domains/apiiodomain.h"
#include "domains/apicoredomain.h"
#include "qlcconfig.h"
#include "doc.h"

ApiServer::ApiServer(QObject *parent, Doc *doc)
    : QObject(parent)
    , m_doc(doc)
    , m_dispatcher(new ApiDispatcher(this))
    , m_nextClientSeq(0)
{
    Q_ASSERT(m_doc != nullptr);

    m_webSocketServer = new QWebSocketServer(QStringLiteral("QLC+ Control API"),
                                              QWebSocketServer::NonSecureMode, this);
    connect(m_webSocketServer, &QWebSocketServer::newConnection, this, &ApiServer::slotNewConnection);

    registerSessionMethods();

    m_ioDomain = new ApiIoDomain(m_doc, this, this);
    m_coreDomain = new ApiCoreDomain(m_doc, this, this);
}

ApiServer::~ApiServer()
{
    m_webSocketServer->close();
}

bool ApiServer::listen(quint16 port)
{
    return m_webSocketServer->listen(QHostAddress::Any, port);
}

QString ApiServer::errorString() const
{
    return m_webSocketServer->errorString();
}

quint16 ApiServer::serverPort() const
{
    return m_webSocketServer->serverPort();
}

Doc *ApiServer::doc() const
{
    return m_doc;
}

ApiDispatcher *ApiServer::dispatcher() const
{
    return m_dispatcher;
}

QString ApiServer::nextClientId()
{
    return QStringLiteral("cl-%1").arg(++m_nextClientSeq);
}

void ApiServer::slotNewConnection()
{
    while (m_webSocketServer->hasPendingConnections())
    {
        QWebSocket *socket = m_webSocketServer->nextPendingConnection();
        QString clientId = nextClientId();
        ApiSession *session = new ApiSession(clientId, socket, this);

        connect(session, &ApiSession::textMessageReceived, m_dispatcher, &ApiDispatcher::dispatch);
        connect(session, &ApiSession::disconnected, this, &ApiServer::slotSessionDisconnected);

        m_sessions.insert(clientId, session);
    }
}

void ApiServer::slotSessionDisconnected(ApiSession *session)
{
    m_sessions.remove(session->clientId());
    session->deleteLater();
}

void ApiServer::broadcast(const QString &topic, const QJsonObject &data, const QString &originClientId, bool subscribeGated)
{
    for (ApiSession *session : std::as_const(m_sessions))
    {
        if (subscribeGated == false || session->isSubscribedTo(topic))
            session->send(ApiEnvelope::buildEvent(topic, data, originClientId));
    }
}

void ApiServer::registerSessionMethods()
{
    m_dispatcher->registerMethod(QStringLiteral("hello"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        session->setHelloed(true);

        QJsonObject result;
        result.insert(QStringLiteral("clientId"), session->clientId());
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        result.insert(QStringLiteral("serverVersion"), QStringLiteral(APPVERSION));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    m_dispatcher->registerMethod(QStringLiteral("subscribe"), [](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QStringList topics;
        for (const QJsonValue &v : params.value(QStringLiteral("topics")).toArray())
            topics << v.toString();
        session->subscribe(topics);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    m_dispatcher->registerMethod(QStringLiteral("unsubscribe"), [](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QStringList topics;
        for (const QJsonValue &v : params.value(QStringLiteral("topics")).toArray())
            topics << v.toString();
        session->unsubscribe(topics);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}
