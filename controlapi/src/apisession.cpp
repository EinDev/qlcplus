/*
  Q Light Controller Plus - Control API
  apisession.cpp

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

#include <QWebSocket>

#include "apisession.h"

ApiSession::ApiSession(const QString &clientId, QWebSocket *socket, QObject *parent)
    : QObject(parent)
    , m_clientId(clientId)
    , m_socket(socket)
    , m_helloed(false)
{
    m_socket->setParent(this);

    connect(m_socket, &QWebSocket::textMessageReceived, this, &ApiSession::slotTextMessageReceived);
    connect(m_socket, &QWebSocket::disconnected, this, &ApiSession::slotDisconnected);
}

QString ApiSession::clientId() const
{
    return m_clientId;
}

bool ApiSession::helloed() const
{
    return m_helloed;
}

void ApiSession::setHelloed(bool helloed)
{
    m_helloed = helloed;
}

void ApiSession::subscribe(const QStringList &topics)
{
    for (const QString &topic : topics)
        m_subscribedTopics.insert(topic);
}

void ApiSession::unsubscribe(const QStringList &topics)
{
    for (const QString &topic : topics)
        m_subscribedTopics.remove(topic);
}

bool ApiSession::isSubscribedTo(const QString &topic) const
{
    return m_subscribedTopics.contains(topic);
}

void ApiSession::send(const QString &jsonText)
{
    m_socket->sendTextMessage(jsonText);
}

void ApiSession::slotTextMessageReceived(const QString &message)
{
    emit textMessageReceived(this, message);
}

void ApiSession::slotDisconnected()
{
    emit disconnected(this);
}
