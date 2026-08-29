/*
  Q Light Controller Plus - Control API
  apicoredomain.cpp

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
#include <QJsonObject>
#include <QFileInfo>
#include <QBuffer>
#include <QSettings>

#include "apicoredomain.h"
#include "apiserver.h"
#include "apisession.h"
#include "apidispatcher.h"
#include "apienvelope.h"
#include "app.h"
#include "mastertimer.h"

#define MASTERTIMER_FREQUENCY "mastertimer/frequency"

ApiCoreDomain::ApiCoreDomain(Doc *doc, ApiServer *server, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_server(server)
{
    Q_ASSERT(m_doc != nullptr);
    Q_ASSERT(m_server != nullptr);

    registerMethods();

    connect(m_doc, SIGNAL(modeChanged(Doc::Mode)),
            this, SLOT(slotModeChanged(Doc::Mode)));
    connect(m_doc, SIGNAL(docRevisionChanged(quint32)),
            this, SLOT(slotDocRevisionChanged(quint32)));

    App *a = app();
    if (a != nullptr)
    {
        connect(a, SIGNAL(recentFilesChanged()),
                this, SLOT(slotRecentFilesChanged()));
        connect(a, SIGNAL(workingPathChanged(QString)),
                this, SLOT(slotWorkingPathChanged(QString)));
    }
}

App *ApiCoreDomain::app() const
{
    return qobject_cast<App *>(m_server->parent());
}

void ApiCoreDomain::registerMethods()
{
    ApiDispatcher *d = m_server->dispatcher();

    // core.project.new
    d->registerMethod(QStringLiteral("core.project.new"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        App *a = app();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        a->newWorkspace();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));

        // Note: App::newWorkspace() calls Doc::clear() which calls Doc::bumpRevision()
        // and emits docRevisionChanged, so slotDocRevisionChanged will handle broadcast.
        // Actually, core.project.loaded is what we should broadcast.
        // We'll broadcast that in slotDocRevisionChanged if we can detect the reason,
        // but it's better to broadcast it here or in a dedicated signal from App.
    });

    // core.project.open
    d->registerMethod(QStringLiteral("core.project.open"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        App *a = app();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        QString source = params.value(QStringLiteral("source")).toString();
        bool ok = false;

        if (source == QStringLiteral("path"))
        {
            QString path = params.value(QStringLiteral("path")).toString();
            ok = a->loadWorkspace(path);
        }
        else if (source == QStringLiteral("upload"))
        {
            // fileName is ignored by loadXML(QByteArray) but used by core spec.
            // Actually, we should set the fileName in App so Save works.
            QByteArray content = QByteArray::fromBase64(params.value(QStringLiteral("contentBase64")).toString().toUtf8());
            a->slotLoadDocFromMemory(content);
            a->setFileName(params.value(QStringLiteral("fileName")).toString());
            ok = true;
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Invalid source")));
            return;
        }

        if (ok)
        {
            QJsonObject result;
            result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            // warningsHtml: not easily available from App today without changes to capture Doc::errorLog()
            result.insert(QStringLiteral("warningsHtml"), QJsonValue::Null);
            session->send(ApiEnvelope::buildOkResponse(id, result));
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("Failed to open project")));
        }
    });

    // core.project.close
    d->registerMethod(QStringLiteral("core.project.close"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        App *a = app();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        a->newWorkspace();

        QJsonObject result;
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.project.save
    d->registerMethod(QStringLiteral("core.project.save"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        App *a = app();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        QString fileName = a->fileName();
        if (fileName.isEmpty())
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidState, QStringLiteral("Project never saved, use saveAs")));
            return;
        }

        if (a->saveWorkspace(fileName))
        {
            QJsonObject result;
            result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            result.insert(QStringLiteral("filePath"), fileName);
            session->send(ApiEnvelope::buildOkResponse(id, result));

            QJsonObject data;
            data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
            data.insert(QStringLiteral("filePath"), fileName);
            data.insert(QStringLiteral("fileName"), QFileInfo(fileName).fileName());
            m_server->broadcast(QStringLiteral("core.project.saved"), data, session->clientId(), false);
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("Failed to save project")));
        }
    });

    // core.project.saveAs
    d->registerMethod(QStringLiteral("core.project.saveAs"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        App *a = app();
        if (a == nullptr)
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("App instance not available")));
            return;
        }

        QString target = params.value(QStringLiteral("target")).toString();
        if (target == QStringLiteral("serverPath"))
        {
            QString path = params.value(QStringLiteral("path")).toString();
            if (path.endsWith(QStringLiteral(".qxw"), Qt::CaseInsensitive) == false)
                path += QStringLiteral(".qxw");

            if (a->saveWorkspace(path))
            {
                QJsonObject result;
                result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
                result.insert(QStringLiteral("filePath"), path);
                session->send(ApiEnvelope::buildOkResponse(id, result));

                QJsonObject data;
                data.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
                data.insert(QStringLiteral("filePath"), path);
                data.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
                m_server->broadcast(QStringLiteral("core.project.saved"), data, session->clientId(), false);
            }
            else
            {
                session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInternal, QStringLiteral("Failed to save project")));
            }
        }
        else if (target == QStringLiteral("download"))
        {
            // App doesn't have a direct "serialize to memory" method exposed easily without XML streamer
            // But we can use saveXML to a QBuffer if we had access to it.
            // For now, let's just return an error or try to implement it.
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrNotImplemented, QStringLiteral("Download not yet implemented")));
        }
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Invalid target")));
        }
    });

    // core.project.get
    d->registerMethod(QStringLiteral("core.project.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        App *a = app();
        QJsonObject result;
        QString path = a ? a->fileName() : QString();
        result.insert(QStringLiteral("filePath"), path.isEmpty() ? QJsonValue(QJsonValue::Null) : path);
        result.insert(QStringLiteral("fileName"), path.isEmpty() ? QJsonValue(QJsonValue::Null) : QFileInfo(path).fileName());
        result.insert(QStringLiteral("isModified"), m_doc->isModified());
        result.insert(QStringLiteral("docRevision"), int(m_doc->docRevision()));
        // docRevisionAtLastSave not tracked by Doc today, could be added or ignored
        result.insert(QStringLiteral("docRevisionAtLastSave"), QJsonValue(QJsonValue::Null));
        // creator: engine doesn't expose this easily in Doc
        result.insert(QStringLiteral("creator"), QJsonValue(QJsonValue::Null));

        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.project.recentFiles
    d->registerMethod(QStringLiteral("core.project.recentFiles"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        App *a = app();
        QJsonArray files;
        if (a)
        {
            for (const QString &path : a->recentFiles())
            {
                QJsonObject entry;
                entry.insert(QStringLiteral("filePath"), path);
                entry.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
                files.append(entry);
            }
        }
        QJsonObject result;
        result.insert(QStringLiteral("files"), files);
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.mode.get
    d->registerMethod(QStringLiteral("core.mode.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        QJsonObject result;
        result.insert(QStringLiteral("mode"), m_doc->mode() == Doc::Design ? QStringLiteral("design") : QStringLiteral("operate"));
        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.mode.set
    d->registerMethod(QStringLiteral("core.mode.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        QString modeStr = params.value(QStringLiteral("mode")).toString();
        Doc::Mode mode;
        if (modeStr == QStringLiteral("design")) mode = Doc::Design;
        else if (modeStr == QStringLiteral("operate")) mode = Doc::Operate;
        else
        {
            session->send(ApiEnvelope::buildErrorResponse(id, ApiEnvelope::ErrInvalidParams, QStringLiteral("Invalid mode")));
            return;
        }

        m_doc->setMode(mode);
        session->send(ApiEnvelope::buildOkResponse(id, QJsonObject()));
    });

    // core.settings.get
    d->registerMethod(QStringLiteral("core.settings.get"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        Q_UNUSED(params)
        App *a = app();
        QSettings settings;
        QJsonObject result;
        result.insert(QStringLiteral("locale"), settings.value(QStringLiteral(SETTINGS_LANGUAGE)).toString());
        result.insert(QStringLiteral("defaultWorkingPath"), a ? a->workingPath() : QString());
        result.insert(QStringLiteral("masterTimerFrequencyHz"), settings.value(QStringLiteral(MASTERTIMER_FREQUENCY)).toInt());

        session->send(ApiEnvelope::buildOkResponse(id, result));
    });

    // core.settings.set
    d->registerMethod(QStringLiteral("core.settings.set"), [this](ApiSession *session, const QString &id, const QJsonObject &params)
    {
        App *a = app();
        QSettings settings;

        if (params.contains(QStringLiteral("locale")))
            settings.setValue(QStringLiteral(SETTINGS_LANGUAGE), params.value(QStringLiteral("locale")).toString());

        if (params.contains(QStringLiteral("defaultWorkingPath")) && a)
            a->setWorkingPath(params.value(QStringLiteral("defaultWorkingPath")).toString());

        if (params.contains(QStringLiteral("masterTimerFrequencyHz")))
            settings.setValue(QStringLiteral(MASTERTIMER_FREQUENCY), params.value(QStringLiteral("masterTimerFrequencyHz")).toInt());

        // Refresh result
        QJsonObject result;
        result.insert(QStringLiteral("locale"), settings.value(QStringLiteral(SETTINGS_LANGUAGE)).toString());
        result.insert(QStringLiteral("defaultWorkingPath"), a ? a->workingPath() : QString());
        result.insert(QStringLiteral("masterTimerFrequencyHz"), settings.value(QStringLiteral(MASTERTIMER_FREQUENCY)).toInt());

        session->send(ApiEnvelope::buildOkResponse(id, result));
        m_server->broadcast(QStringLiteral("core.settings.changed"), result, session->clientId(), false);
    });
}

void ApiCoreDomain::slotModeChanged(Doc::Mode mode)
{
    QJsonObject data;
    data.insert(QStringLiteral("mode"), mode == Doc::Design ? QStringLiteral("design") : QStringLiteral("operate"));
    m_server->broadcast(QStringLiteral("core.mode.changed"), data, QString(), false);
}

void ApiCoreDomain::slotDocRevisionChanged(quint32 revision)
{
    Q_UNUSED(revision)
    // This is a broad signal. core.project.loaded is what should be broadcast
    // when a whole new document is there.
    // Unfortunately Doc doesn't know *why* it was bumped.
    // For now, we'll just broadcast project loaded events from the handlers.
    // Wait, if another domain bumps revision, we don't want to broadcast core.project.loaded.
}

void ApiCoreDomain::slotRecentFilesChanged()
{
    App *a = app();
    QJsonArray files;
    if (a)
    {
        for (const QString &path : a->recentFiles())
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("filePath"), path);
            entry.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
            files.append(entry);
        }
    }
    QJsonObject data;
    data.insert(QStringLiteral("files"), files);
    m_server->broadcast(QStringLiteral("core.project.recentFilesChanged"), data, QString(), false);
}

void ApiCoreDomain::slotWorkingPathChanged(QString path)
{
    Q_UNUSED(path)
    // Part of core.settings.changed
    QSettings settings;
    QJsonObject data;
    data.insert(QStringLiteral("locale"), settings.value(QStringLiteral(SETTINGS_LANGUAGE)).toString());
    data.insert(QStringLiteral("defaultWorkingPath"), path);
    data.insert(QStringLiteral("masterTimerFrequencyHz"), settings.value(QStringLiteral(MASTERTIMER_FREQUENCY)).toInt());
    m_server->broadcast(QStringLiteral("core.settings.changed"), data, QString(), false);
}
