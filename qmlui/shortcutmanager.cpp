/*
  Q Light Controller Plus
  shortcutmanager.cpp

  Copyright (c) Massimo Callegari

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
#include <QJsonObject>
#include <QKeyEvent>
#include <QFile>
#include <QDir>
#include <utility>

#include "shortcutmanager.h"
#include "qlcconfig.h"
#include "qlcfile.h"

#define SHORTCUTS_FILE "qlcplusShortcuts.json"

ShortcutManager::ShortcutManager(QObject *parent)
    : QObject(parent)
{
    loadOverrides();
}

QString ShortcutManager::currentContext() const
{
    return m_currentContext;
}

void ShortcutManager::setCurrentContext(const QString &context)
{
    if (m_currentContext == context)
        return;

    m_currentContext = context;
    emit currentContextChanged();
}

void ShortcutManager::registerAction(const QString &id, const QKeySequence &defaultSequence,
                                      ShortcutScope scope, const QString &description,
                                      std::function<void()> callback)
{
    ShortcutAction action;
    action.id = id;
    action.defaultSequence = defaultSequence;
    action.sequence = m_overrides.contains(id) ? m_overrides.value(id) : defaultSequence;
    action.scope = scope;
    action.description = description;
    action.callback = callback;

    m_actions.append(action);
}

void ShortcutManager::saveOverride(const QString &id, const QString &sequence)
{
    QKeySequence seq(sequence);

    for (ShortcutAction &action : m_actions)
    {
        if (action.id == id)
        {
            action.sequence = seq;
            break;
        }
    }
    m_overrides.insert(id, seq);

    QJsonObject rootObj;
    QFile readFile(userConfFilepath());
    if (readFile.exists() && readFile.open(QIODevice::ReadOnly))
    {
        rootObj = QJsonDocument::fromJson(readFile.readAll()).object();
        readFile.close();
    }

    rootObj[id] = seq.toString();

    QFile writeFile(userConfFilepath());
    if (writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        writeFile.write(QJsonDocument(rootObj).toJson());
        writeFile.close();
    }
}

bool ShortcutManager::handleKeyEvent(QKeyEvent *event)
{
    int key = event->key();

    if (key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Shift || key == Qt::Key_Meta)
        return false;

    QKeySequence seq(event->key() | event->modifiers());

    for (const ShortcutAction &action : std::as_const(m_actions))
    {
        if (action.sequence != seq)
            continue;

        if (!scopeMatchesCurrentContext(action.scope))
            continue;

        emit actionTriggered(action.id);
        if (action.callback)
            action.callback();

        return true;
    }

    return false;
}

QString ShortcutManager::actionDescriptionForSequence(const QKeySequence &sequence) const
{
    for (const ShortcutAction &action : std::as_const(m_actions))
    {
        if (action.sequence == sequence)
            return action.description;
    }

    return QString();
}

bool ShortcutManager::scopeMatchesCurrentContext(ShortcutScope scope) const
{
    switch (scope)
    {
        case Global:
            return true;
        case FixturesAndFunctions:
            return m_currentContext == QLatin1String("FIXANDFUNC");
        case VirtualConsole:
            return m_currentContext == QLatin1String("VC");
        case SimpleDesk:
            return m_currentContext == QLatin1String("SDESK");
        case ShowManager:
            return m_currentContext == QLatin1String("SHOWMGR");
        case IOManager:
            return m_currentContext == QLatin1String("IOMGR");
    }

    return false;
}

QString ShortcutManager::userConfFilepath() const
{
    QDir userConfDir = QLCFile::userDirectory(QString(USERQLCPLUSDIR), QString(USERQLCPLUSDIR), QStringList());
    return userConfDir.absolutePath() + QDir::separator() + SHORTCUTS_FILE;
}

void ShortcutManager::loadOverrides()
{
    QFile jsonFile(userConfFilepath());
    if (jsonFile.exists() == false)
        return;

    if (jsonFile.open(QIODevice::ReadOnly) == false)
        return;

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    jsonFile.close();

    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "Shortcuts override parse error at" << parseError.offset << ":" << parseError.errorString();
        return;
    }

    QJsonObject jsonObject = jsonDoc.object();
    for (const QString &actionId : jsonObject.keys())
        m_overrides.insert(actionId, QKeySequence(jsonObject.value(actionId).toString()));
}
