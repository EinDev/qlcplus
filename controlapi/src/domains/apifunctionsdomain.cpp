/*
  Q Light Controller Plus - Control API
  apifunctionsdomain.cpp

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

#include "apifunctionsdomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apienvelope.h"
#include "function.h"
#include "functionparent.h"
#include "mastertimer.h"
#include "doc.h"

namespace {

// functionId is carried as a JSON string on the wire (functions-core.yaml),
// unlike io.*'s plain-integer universeId - toUInt()'s ok-flag distinguishes
// "missing/non-numeric" from a real (if unpatched) id, both of which are
// reported as NOT_FOUND here rather than INVALID_PARAMS, matching how
// ApiIoDomain treats an out-of-range universeId.
Function *findFunction(Doc *doc, const QJsonObject &params)
{
    bool ok = false;
    quint32 functionId = params.value(QStringLiteral("functionId")).toString().toUInt(&ok);
    return ok ? doc->function(functionId) : nullptr;
}

Function::TempoType tempoTypeFromJson(const QJsonValue &value)
{
    QString str = value.toString();
    if (str == QStringLiteral("Time"))
        return Function::Time;
    if (str == QStringLiteral("Beats"))
        return Function::Beats;
    return Function::Original;
}

} // namespace

ApiFunctionsDomain::ApiFunctionsDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();
}

void ApiFunctionsDomain::registerMethods()
{
    ApiDispatcher *dispatcher = m_server->dispatcher();
    Doc *doc = m_doc;

    dispatcher->registerMethod(QStringLiteral("functions.start"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        uint fadeIn = params.contains(QStringLiteral("overrideFadeIn"))
                ? uint(params.value(QStringLiteral("overrideFadeIn")).toInt())
                : Function::defaultSpeed();
        uint fadeOut = params.contains(QStringLiteral("overrideFadeOut"))
                ? uint(params.value(QStringLiteral("overrideFadeOut")).toInt())
                : Function::defaultSpeed();
        uint duration = params.contains(QStringLiteral("overrideDuration"))
                ? uint(params.value(QStringLiteral("overrideDuration")).toInt())
                : Function::defaultSpeed();
        Function::TempoType tempoType = params.contains(QStringLiteral("overrideTempoType"))
                ? tempoTypeFromJson(params.value(QStringLiteral("overrideTempoType")))
                : Function::Original;

        function->start(doc->masterTimer(), FunctionParent::master(FunctionParent::ControlApi),
                         0, fadeIn, fadeOut, duration, tempoType);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("functions.stop"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        bool preserveAttributes = params.value(QStringLiteral("preserveAttributes")).toBool(false);
        function->stop(FunctionParent::master(FunctionParent::ControlApi), preserveAttributes);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    dispatcher->registerMethod(QStringLiteral("functions.setPause"), [doc](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Function *function = findFunction(doc, params);
        if (function == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotFound,
                                                            QStringLiteral("No such function")));
            return;
        }

        function->setPause(params.value(QStringLiteral("paused")).toBool());
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });
}
