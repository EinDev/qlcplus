/*
  Q Light Controller Plus - Control API unit test
  webserver_test.h

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

#ifndef WEBSERVER_TEST_H
#define WEBSERVER_TEST_H

#include <QByteArray>
#include <QMap>
#include <QObject>

class QTemporaryDir;
class WebServer;

class WebServer_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rootServesIndexHtml();
    void nestedFilesWithContentTypes();
    void queryStringIsIgnored();
    void directoryServesItsIndex();
    void directoryWithoutIndexIs404();
    void missingFileIs404();
    void dotDotTraversalIs403();
    void percentEncodedTraversalIs403();
    void dotDotStayingInsideRootIsAllowed();
    void backslashIs403();
    void headSendsHeadersOnly();
    void configJson();
    void postIs405();
    void malformedRequestIs400();
    void missingRootStillListensAnd404s();
    void contentTypeMappings_data();
    void contentTypeMappings();

private:
    struct HttpReply
    {
        int status = 0;
        QMap<QByteArray, QByteArray> headers; // lower-cased names
        QByteArray body;
    };

    /** Send one raw request to the server under test and collect the whole
     *  reply (terminated by the server closing the connection). Returns
     *  false, with a diagnostic message, on timeout/connection failure -
     *  wrap calls in QVERIFY. Runs the event loop while waiting, which the
     *  server (same thread) needs. */
    bool request(const QByteArray &method, const QByteArray &target, HttpReply &reply);
    bool requestRaw(const QByteArray &rawRequest, HttpReply &reply);

    void writeFile(const QString &relativePath, const QByteArray &content);

private:
    QTemporaryDir *m_dir = nullptr;
    WebServer *m_server = nullptr;
};

#endif
