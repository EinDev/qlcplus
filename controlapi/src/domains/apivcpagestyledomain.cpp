/*
  Q Light Controller Plus - Control API
  apivcpagestyledomain.cpp

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

#include <QFile>
#include <QFileInfo>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include "apivcpagestyledomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "doc.h"

namespace {

const QString kHostUnavailable = QStringLiteral("App instance not available");

/** Strict whole-number check: QJsonValue::toInt() would turn "abc"/true/1.5 into 0/1/1. */
bool jsonIsInteger(const QJsonValue &v)
{
    if (v.isDouble() == false)
        return false;
    double d = v.toDouble();
    if (d < -2147483648.0 || d > 2147483647.0)
        return false;
    return d == double(qint32(d));
}

bool isNetworkPath(const QString &path)
{
    // \\server\share, //server/share, \\?\UNC\..., \\?\C:\..., \\.\device - every
    // double-separator prefix, whatever the slash direction
    return path.size() >= 2 && (path.at(0) == QLatin1Char('\\') || path.at(0) == QLatin1Char('/'))
                            && (path.at(1) == QLatin1Char('\\') || path.at(1) == QLatin1Char('/'));
}

} // namespace

ApiVcPageStyleDomain::ApiVcPageStyleDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods(m_server->dispatcher());
}

ApiVcHost *ApiVcPageStyleDomain::vcHost() const
{
    return dynamic_cast<ApiVcHost *>(m_server->parent());
}

/*****************************************************************************
 * Validation helpers
 *****************************************************************************/

bool ApiVcPageStyleDomain::checkBackgroundImagePath(const QString &path, QString *normalized, QString *error)
{
    QString p = path.trimmed();
    if (p.isEmpty())
    {
        *normalized = QString();
        return true;
    }

    if (p.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
    {
        QUrl url(p);
        if (url.isValid() == false || url.host().isEmpty() == false)
        {
            *error = QStringLiteral("backgroundImage: file URLs with a host (network paths) are not allowed");
            return false;
        }
        p = url.toLocalFile();
    }
    else
    {
        // any other scheme ("http:", "smb:", ...) - a single letter before the
        // colon is a Windows drive, not a scheme
        static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
        if (scheme.match(p).hasMatch())
        {
            *error = QStringLiteral("backgroundImage must be a local file path, not a URL");
            return false;
        }
    }

    if (isNetworkPath(p))
    {
        *error = QStringLiteral("backgroundImage: UNC / network paths are not allowed");
        return false;
    }

    if (QFileInfo(p).isAbsolute() == false)
    {
        *error = QStringLiteral("backgroundImage must be an absolute path on the QLC+ host");
        return false;
    }

    *normalized = p;
    return true;
}

bool ApiVcPageStyleDomain::checkStyle(QJsonObject &style, QString *error)
{
    if (style.contains(QStringLiteral("backgroundImage")) == false)
        return true;

    QJsonValue v = style.value(QStringLiteral("backgroundImage"));
    if (v.isNull())
        return true;
    if (v.isString() == false)
    {
        *error = QStringLiteral("backgroundImage must be a string or null");
        return false;
    }

    QString normalized;
    if (checkBackgroundImagePath(v.toString(), &normalized, error) == false)
        return false;

    style.insert(QStringLiteral("backgroundImage"), normalized.isEmpty() ? QJsonValue() : QJsonValue(normalized));
    return true;
}

QString ApiVcPageStyleDomain::sniffImageMimeType(const QByteArray &head)
{
    if (head.startsWith("\x89PNG\r\n\x1a\n"))
        return QStringLiteral("image/png");
    if (head.startsWith("\xff\xd8\xff"))
        return QStringLiteral("image/jpeg");
    if (head.startsWith("GIF87a") || head.startsWith("GIF89a"))
        return QStringLiteral("image/gif");
    if (head.startsWith("BM"))
        return QStringLiteral("image/bmp");
    if (head.size() >= 12 && head.startsWith("RIFF") && head.mid(8, 4) == "WEBP")
        return QStringLiteral("image/webp");

    // SVG: text whose first tag (after an optional BOM / XML prolog / comments) is <svg.
    // It is only ever shown through <img> / CSS background-image, where scripts never run.
    QByteArray text = head.left(1024).trimmed();
    if (text.startsWith("\xef\xbb\xbf"))
        text = text.mid(3);
    if (text.startsWith("<?xml") || text.startsWith("<svg") || text.startsWith("<!--") || text.startsWith("<!DOCTYPE svg"))
    {
        if (text.contains("<svg"))
            return QStringLiteral("image/svg+xml");
    }
    return QString();
}

/*****************************************************************************
 * Methods
 *****************************************************************************/

void ApiVcPageStyleDomain::registerMethods(ApiDispatcher *d)
{
    Doc *doc = m_doc;

    // vc.page.setSize {index, width, height, baseRevision} - §4a
    d->registerMethod(QStringLiteral("vc.page.setSize"), [this, doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        quint32 baseRevision = quint32(params.value(QStringLiteral("baseRevision")).toInt());
        if (baseRevision != doc->docRevision())
        {
            QJsonObject details;
            details.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrConflict,
                                                            QStringLiteral("baseRevision is stale"), details));
            return;
        }

        QJsonValue indexValue = params.value(QStringLiteral("index"));
        if (jsonIsInteger(indexValue) == false || indexValue.toInt() < 0 || indexValue.toInt() >= host->vcPageCount())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such page")));
            return;
        }
        int index = indexValue.toInt();

        QJsonValue w = params.value(QStringLiteral("width"));
        QJsonValue h = params.value(QStringLiteral("height"));
        if (jsonIsInteger(w) == false || jsonIsInteger(h) == false
            || w.toInt() < 1 || w.toInt() > MaxPageSize || h.toInt() < 1 || h.toInt() > MaxPageSize)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams,
                                                            QStringLiteral("width and height must be integers in 1..%1").arg(MaxPageSize)));
            return;
        }

        QJsonObject before = host->vcPageSnapshot(index);
        if (before.value(QStringLiteral("width")).toInt() != w.toInt()
            || before.value(QStringLiteral("height")).toInt() != h.toInt())
        {
            host->vcSetPageSize(index, w.toInt(), h.toInt());
            doc->setModified();

            QJsonObject data;
            data.insert(QStringLiteral("page"), host->vcPageSnapshot(index));
            data.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
            m_server->broadcast(QStringLiteral("vc.page.updated"), data, session->clientId(), false);
        }

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // vc.widget.getBackgroundImage {widgetId} - read
    d->registerMethod(QStringLiteral("vc.widget.getBackgroundImage"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        ApiVcHost *host = vcHost();
        if (host == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, kHostUnavailable));
            return;
        }

        bool ok = false;
        quint32 wid = params.value(QStringLiteral("widgetId")).toString().toUInt(&ok);
        if (ok == false || host->vcWidgetExists(wid) == false)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound, QStringLiteral("No such widget")));
            return;
        }

        QJsonObject style = host->vcWidgetSnapshot(wid).value(QStringLiteral("style")).toObject();
        QString path = style.value(QStringLiteral("backgroundImage")).toString();

        QJsonObject result;
        result.insert(QStringLiteral("widgetId"), QString::number(wid));
        result.insert(QStringLiteral("path"), path.isEmpty() ? QJsonValue() : QJsonValue(path));
        result.insert(QStringLiteral("mimeType"), QJsonValue());
        result.insert(QStringLiteral("dataUrl"), QJsonValue());

        auto unavailable = [&](const QString &reason)
        {
            result.insert(QStringLiteral("reason"), reason);
            session->send(ApiEnvelope::buildOkResponse(id, result));
        };

        if (path.isEmpty())
        {
            session->send(ApiEnvelope::buildOkResponse(id, result));
            return;
        }

        // A project file can carry any path: re-check it before touching the file system,
        // so loading someone else's project never makes this host open a network share.
        QString normalized, error;
        if (checkBackgroundImagePath(path, &normalized, &error) == false)
            return unavailable(QStringLiteral("refused: ") + error);

        QFileInfo info(normalized);
        if (info.exists() == false || info.isFile() == false)
            return unavailable(QStringLiteral("file not found"));
        if (info.size() > MaxImageBytes)
            return unavailable(QStringLiteral("file larger than %1 MB").arg(MaxImageBytes / (1024 * 1024)));

        QFile file(normalized);
        if (file.open(QIODevice::ReadOnly) == false)
            return unavailable(QStringLiteral("file not readable"));
        QByteArray bytes = file.read(MaxImageBytes + 1);
        file.close();
        if (bytes.size() > MaxImageBytes)
            return unavailable(QStringLiteral("file larger than %1 MB").arg(MaxImageBytes / (1024 * 1024)));

        QString mime = sniffImageMimeType(bytes.left(2048));
        if (mime.isEmpty())
            return unavailable(QStringLiteral("not a supported image (png, jpeg, gif, bmp, webp, svg)"));

        result.insert(QStringLiteral("mimeType"), mime);
        result.insert(QStringLiteral("dataUrl"), QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(bytes.toBase64())));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });
}
