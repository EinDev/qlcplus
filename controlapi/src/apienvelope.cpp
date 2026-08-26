/*
  Q Light Controller Plus - Control API
  apienvelope.cpp

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

#include <QJsonDocument>
#include <QJsonValue>

#include "apienvelope.h"

const QString ApiEnvelope::ErrConflict = QStringLiteral("CONFLICT");
const QString ApiEnvelope::ErrNotFound = QStringLiteral("NOT_FOUND");
const QString ApiEnvelope::ErrInvalidParams = QStringLiteral("INVALID_PARAMS");
const QString ApiEnvelope::ErrUnauthorized = QStringLiteral("UNAUTHORIZED");
const QString ApiEnvelope::ErrUnsupported = QStringLiteral("UNSUPPORTED");

bool ApiEnvelope::parseRequest(const QString &rawJson, Request &outRequest, QString &outErrorMessage)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        outErrorMessage = QStringLiteral("Malformed JSON: %1").arg(parseError.errorString());
        return false;
    }

    if (doc.isObject() == false)
    {
        outErrorMessage = QStringLiteral("Frame is not a JSON object");
        return false;
    }

    QJsonObject obj = doc.object();

    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("request"))
    {
        outErrorMessage = QStringLiteral("Frame \"type\" is not \"request\"");
        return false;
    }

    if (obj.contains(QStringLiteral("id")) == false || obj.value(QStringLiteral("id")).isString() == false)
    {
        outErrorMessage = QStringLiteral("Missing or non-string \"id\"");
        return false;
    }

    if (obj.contains(QStringLiteral("method")) == false || obj.value(QStringLiteral("method")).isString() == false)
    {
        outErrorMessage = QStringLiteral("Missing or non-string \"method\"");
        return false;
    }

    outRequest.id = obj.value(QStringLiteral("id")).toString();
    outRequest.method = obj.value(QStringLiteral("method")).toString();
    outRequest.params = obj.value(QStringLiteral("params")).toObject();

    return true;
}

QString ApiEnvelope::buildOkResponse(const QString &id, const QJsonObject &result)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("response"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("ok"), true);
    obj.insert(QStringLiteral("result"), result);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString ApiEnvelope::buildErrorResponse(const QString &id, const QString &code, const QString &message,
                                         const QJsonObject &details)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    if (details.isEmpty() == false)
        error.insert(QStringLiteral("details"), details);

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("response"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("ok"), false);
    obj.insert(QStringLiteral("error"), error);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString ApiEnvelope::buildEvent(const QString &topic, const QJsonObject &data, const QString &originClientId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("event"));
    obj.insert(QStringLiteral("topic"), topic);
    obj.insert(QStringLiteral("data"), data);
    obj.insert(QStringLiteral("originClientId"), originClientId.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(originClientId));
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}
