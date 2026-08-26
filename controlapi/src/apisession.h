/*
  Q Light Controller Plus - Control API
  apisession.h

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

#ifndef APISESSION_H
#define APISESSION_H

#include <QObject>
#include <QSet>
#include <QString>

class QWebSocket;

/**
 * One connected WebSocket client. Owned by ApiServer, destroyed when the
 * underlying socket disconnects. Holds per-connection session state:
 * identity, hello/handshake status, and subscribed event topics (see
 * docs/api-spec/00-conventions.md §5).
 */
class ApiSession : public QObject
{
    Q_OBJECT

public:
    /** Takes ownership of socket (reparents it to this). clientId should be
     *  unique per connection (ApiServer generates it). */
    ApiSession(const QString &clientId, QWebSocket *socket, QObject *parent = nullptr);

    QString clientId() const;

    /** Has this session completed the hello/welcome handshake? Requests
     *  other than "hello" are rejected (UNAUTHORIZED) until this is true -
     *  keeps the generic session machinery simple for this first slice;
     *  real credential checking is a later concern (see controlapi/README.md). */
    bool helloed() const;
    void setHelloed(bool helloed);

    void subscribe(const QStringList &topics);
    void unsubscribe(const QStringList &topics);
    bool isSubscribedTo(const QString &topic) const;

    /** Send a raw pre-built JSON text frame (see ApiEnvelope) to this client. */
    void send(const QString &jsonText);

signals:
    /** Relayed from the underlying QWebSocket, one raw text frame at a time. */
    void textMessageReceived(ApiSession *session, const QString &message);

    /** Relayed from the underlying QWebSocket's disconnected() signal. */
    void disconnected(ApiSession *session);

private slots:
    void slotTextMessageReceived(const QString &message);
    void slotDisconnected();

private:
    QString m_clientId;
    QWebSocket *m_socket;
    bool m_helloed;
    QSet<QString> m_subscribedTopics;
};

#endif
