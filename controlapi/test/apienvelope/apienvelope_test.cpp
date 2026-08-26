/*
  Q Light Controller Plus - Control API unit test
  apienvelope_test.cpp

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

#include <QtTest>
#include <QJsonDocument>

#include "apienvelope_test.h"
#include "apienvelope.h"

void ApiEnvelope_Test::parseValidRequest()
{
    ApiEnvelope::Request req;
    QString err;
    bool ok = ApiEnvelope::parseRequest(
        QStringLiteral("{\"type\":\"request\",\"id\":\"c-1\",\"method\":\"io.blackout.get\",\"params\":{\"foo\":42}}"),
        req, err);

    QVERIFY(ok);
    QCOMPARE(req.id, QStringLiteral("c-1"));
    QCOMPARE(req.method, QStringLiteral("io.blackout.get"));
    QCOMPARE(req.params.value(QStringLiteral("foo")).toInt(), 42);
}

void ApiEnvelope_Test::parseRequestMissingParams()
{
    // "params" is optional - a request with none should parse to an empty object
    ApiEnvelope::Request req;
    QString err;
    bool ok = ApiEnvelope::parseRequest(
        QStringLiteral("{\"type\":\"request\",\"id\":\"c-1\",\"method\":\"io.blackout.get\"}"),
        req, err);

    QVERIFY(ok);
    QVERIFY(req.params.isEmpty());
}

void ApiEnvelope_Test::rejectMalformedJson()
{
    ApiEnvelope::Request req;
    QString err;
    QVERIFY(ApiEnvelope::parseRequest(QStringLiteral("{not json"), req, err) == false);
    QVERIFY(err.isEmpty() == false);
}

void ApiEnvelope_Test::rejectWrongType()
{
    ApiEnvelope::Request req;
    QString err;
    bool ok = ApiEnvelope::parseRequest(
        QStringLiteral("{\"type\":\"response\",\"id\":\"c-1\",\"method\":\"x\"}"), req, err);
    QVERIFY(ok == false);
}

void ApiEnvelope_Test::rejectMissingId()
{
    ApiEnvelope::Request req;
    QString err;
    bool ok = ApiEnvelope::parseRequest(
        QStringLiteral("{\"type\":\"request\",\"method\":\"x\"}"), req, err);
    QVERIFY(ok == false);
}

void ApiEnvelope_Test::rejectMissingMethod()
{
    ApiEnvelope::Request req;
    QString err;
    bool ok = ApiEnvelope::parseRequest(
        QStringLiteral("{\"type\":\"request\",\"id\":\"c-1\"}"), req, err);
    QVERIFY(ok == false);
}

void ApiEnvelope_Test::buildOkResponse()
{
    QJsonObject result;
    result.insert(QStringLiteral("value"), 7);
    QString text = ApiEnvelope::buildOkResponse(QStringLiteral("c-1"), result);

    QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value(QStringLiteral("type")).toString(), QStringLiteral("response"));
    QCOMPARE(obj.value(QStringLiteral("id")).toString(), QStringLiteral("c-1"));
    QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(obj.value(QStringLiteral("result")).toObject().value(QStringLiteral("value")).toInt(), 7);
    QVERIFY(obj.contains(QStringLiteral("error")) == false);
}

void ApiEnvelope_Test::buildErrorResponseWithDetails()
{
    QJsonObject details;
    details.insert(QStringLiteral("docRevision"), 3);
    QString text = ApiEnvelope::buildErrorResponse(QStringLiteral("c-2"), ApiEnvelope::ErrConflict,
                                                     QStringLiteral("stale"), details);

    QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value(QStringLiteral("ok")).toBool(), false);
    QJsonObject error = obj.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), ApiEnvelope::ErrConflict);
    QCOMPARE(error.value(QStringLiteral("message")).toString(), QStringLiteral("stale"));
    QCOMPARE(error.value(QStringLiteral("details")).toObject().value(QStringLiteral("docRevision")).toInt(), 3);
}

void ApiEnvelope_Test::buildErrorResponseWithoutDetails()
{
    QString text = ApiEnvelope::buildErrorResponse(QStringLiteral("c-2"), ApiEnvelope::ErrNotFound, QStringLiteral("nope"));
    QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QJsonObject error = obj.value(QStringLiteral("error")).toObject();
    QVERIFY(error.contains(QStringLiteral("details")) == false);
}

void ApiEnvelope_Test::buildEventWithOrigin()
{
    QJsonObject data;
    data.insert(QStringLiteral("state"), true);
    QString text = ApiEnvelope::buildEvent(QStringLiteral("io.blackout.changed"), data, QStringLiteral("cl-1"));

    QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(obj.value(QStringLiteral("type")).toString(), QStringLiteral("event"));
    QCOMPARE(obj.value(QStringLiteral("topic")).toString(), QStringLiteral("io.blackout.changed"));
    QCOMPARE(obj.value(QStringLiteral("originClientId")).toString(), QStringLiteral("cl-1"));
    QCOMPARE(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("state")).toBool(), true);
}

void ApiEnvelope_Test::buildEventWithoutOrigin()
{
    QString text = ApiEnvelope::buildEvent(QStringLiteral("io.blackout.changed"), QJsonObject());
    QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    QVERIFY(obj.value(QStringLiteral("originClientId")).isNull());
}

QTEST_APPLESS_MAIN(ApiEnvelope_Test)
