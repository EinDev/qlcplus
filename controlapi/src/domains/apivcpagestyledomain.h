/*
  Q Light Controller Plus - Control API
  apivcpagestyledomain.h

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

#ifndef APIVCPAGESTYLEDOMAIN_H
#define APIVCPAGESTYLEDOMAIN_H

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "apivchost.h"

class ApiDispatcher;
class ApiServer;
class ApiSession;
class Doc;

/**
 * Virtual Console page size and widget background images (docs/api-spec/fragments/
 * virtualconsole.yaml, added 2026-09-27), next to ApiVcDomain so neither has to grow:
 *  - vc.page.setSize (§4a): VCPage geometry, what VCPageProperties.qml's Width / Height spin boxes
 *    write; broadcast on vc.page.updated like vc.page.setPin. vc.page.list carries width/height.
 *  - vc.widget.getBackgroundImage (read): the image file a widget's style.backgroundImage points at,
 *    as a data: URL. A browser cannot open a path on the QLC+ host, and this is deliberately a
 *    WebSocket method rather than an HTTP route on WebServer: it only ever reads the one file a
 *    widget currently references (never a client-supplied path), goes through the same session as
 *    every other call, and needs no VC knowledge in the static file server. Only local files are
 *    read (never UNC / network paths), capped at MaxImageBytes, and only when their first bytes
 *    are a known image format.
 *
 * checkBackgroundImagePath() is also the validation ApiVcDomain (vc.widget.create / update) and
 * ApiVcLayoutDomain (vc.widget.bulkStyle) run on style.backgroundImage before any mutation: an
 * empty / null value clears the image, anything else must be an absolute local path (a file: URL
 * without a host is accepted and reduced to its path, like VCWidget::setBackgroundImage does).
 * UNC paths (\\server\share, //server/share, \\?\ and \\.\ device paths), file: URLs with a host
 * and any other URL scheme are refused: the desktop renders the image with QML's Image, so a
 * network path set remotely would make the QLC+ host authenticate against an arbitrary server.
 */
class ApiVcPageStyleDomain : public QObject
{
    Q_OBJECT

public:
    ApiVcPageStyleDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);

    /** Largest image vc.widget.getBackgroundImage returns (bytes, before base64). */
    static constexpr qint64 MaxImageBytes = 8 * 1024 * 1024;

    /** Largest page width / height, VCPageProperties.qml's spin box range (1..100000). */
    static constexpr int MaxPageSize = 100000;

    /** See the class comment. $path may be empty (= no image). On success *$normalized receives the
     *  path the widget should store (file: URL reduced to a local path). */
    static bool checkBackgroundImagePath(const QString &path, QString *normalized, QString *error);

    /** Runs checkBackgroundImagePath() on $style.backgroundImage when present (null = clear) and
     *  rewrites it to the normalized path. Returns false with *$error set on a refused value. */
    static bool checkStyle(QJsonObject &style, QString *error);

    /** Image MIME type from the file's first bytes (png, jpeg, gif, bmp, webp, svg), or an empty
     *  string when it is none of those. */
    static QString sniffImageMimeType(const QByteArray &head);

private:
    void registerMethods(ApiDispatcher *d);
    ApiVcHost *vcHost() const;

private:
    Doc *m_doc;
    ApiServer *m_server;
};

#endif
