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
#include <QUrl>
#include <algorithm>
#include <utility>

#include "shortcutmanager.h"
#include "qlcconfig.h"
#include "qlcfile.h"

#define SHORTCUTS_FILE "qlcplusShortcuts.json"

namespace {

/** ActionsMenu.qml's own FileDialog/App load-save flow strips this same
 *  prefix off a selected file URL before handing it to App - mirrored here
 *  so exportOverrides()/importOverrides() accept either a plain path or a
 *  file:// URL from a QML FileDialog */
QString localFilePath(const QString &path)
{
    if (path.startsWith(QLatin1String("file:")))
        return QUrl(path).toLocalFile();
    return path;
}

} // namespace

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
    persistOverrides();

    emit actionsChanged();
}

QVariantList ShortcutManager::listActions() const
{
    QVector<ShortcutAction> sorted = m_actions;
    std::stable_sort(sorted.begin(), sorted.end(),
                      [](const ShortcutAction &a, const ShortcutAction &b) { return a.scope < b.scope; });

    QVariantList list;
    for (const ShortcutAction &action : std::as_const(sorted))
    {
        QVariantMap map;
        map.insert("id", action.id);
        map.insert("description", action.description);
        map.insert("sequence", action.sequence.toString(QKeySequence::NativeText));
        map.insert("sequenceStorage", action.sequence.toString());
        map.insert("scope", int(action.scope));
        map.insert("scopeName", scopeDisplayName(action.scope));
        map.insert("isDefault", action.sequence == action.defaultSequence);
        list.append(map);
    }

    return list;
}

void ShortcutManager::resetToDefault(const QString &id)
{
    bool found = false;

    for (ShortcutAction &action : m_actions)
    {
        if (action.id == id)
        {
            action.sequence = action.defaultSequence;
            found = true;
            break;
        }
    }

    if (found == false)
        return;

    m_overrides.remove(id);
    persistOverrides();

    emit actionsChanged();
}

void ShortcutManager::resetAllToDefaults()
{
    for (ShortcutAction &action : m_actions)
        action.sequence = action.defaultSequence;

    m_overrides.clear();
    persistOverrides();

    emit actionsChanged();
}

QVariantMap ShortcutManager::findCollision(const QString &sequence, int scope, const QString &excludeId) const
{
    QVariantMap result;

    QKeySequence candidate(sequence);
    if (candidate.isEmpty())
        return result;

    ShortcutScope candidateScope = static_cast<ShortcutScope>(scope);

    for (const ShortcutAction &action : std::as_const(m_actions))
    {
        if (action.id == excludeId)
            continue;

        if (action.sequence != candidate)
            continue;

        if (scopesOverlap(candidateScope, action.scope) == false)
            continue;

        result.insert("id", action.id);
        result.insert("description", action.description);
        result.insert("scopeName", scopeDisplayName(action.scope));
        return result;
    }

    return result;
}

bool ShortcutManager::exportOverrides(const QString &path) const
{
    QJsonObject obj;
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it)
        obj.insert(it.key(), it.value().toString());

    QFile file(localFilePath(path));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) == false)
        return false;

    file.write(QJsonDocument(obj).toJson());
    return true;
}

bool ShortcutManager::importOverrides(const QString &path)
{
    QFile file(localFilePath(path));
    if (file.open(QIODevice::ReadOnly) == false)
        return false;

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError)
        return false;

    QJsonObject obj = jsonDoc.object();
    m_overrides.clear();
    for (const QString &key : obj.keys())
        m_overrides.insert(key, QKeySequence(obj.value(key).toString()));

    for (ShortcutAction &action : m_actions)
        action.sequence = m_overrides.contains(action.id) ? m_overrides.value(action.id) : action.defaultSequence;

    persistOverrides();

    emit actionsChanged();
    return true;
}

QVariantMap ShortcutManager::sequenceFromKeyEvent(int key, int modifiers) const
{
    QVariantMap map;

    if (key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Shift || key == Qt::Key_Meta)
    {
        map.insert("empty", true);
        return map;
    }

    QKeySequence seq(key | modifiers);
    map.insert("storage", seq.toString());
    map.insert("display", seq.toString(QKeySequence::NativeText));
    map.insert("empty", seq.isEmpty());
    return map;
}

bool ShortcutManager::isCapturing() const
{
    return m_capturing;
}

void ShortcutManager::setCapturing(bool capturing)
{
    if (m_capturing == capturing)
        return;

    m_capturing = capturing;
    emit capturingChanged();
}

bool ShortcutManager::handleKeyEvent(QKeyEvent *event)
{
    if (m_capturing)
        return false;

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
    // "regardless of scope" is exactly what findCollision() does when asked
    // about a Global-scope candidate: scopesOverlap(Global, x) is true for
    // every x, so this reuses the same match loop instead of a second,
    // near-identical one. excludeId is unused here (no action to exclude),
    // and an empty sequence never matches a real registered action, so
    // findCollision()'s early-out for that case doesn't change behavior
    return findCollision(sequence.toString(), Global, QString()).value(QStringLiteral("description")).toString();
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

void ShortcutManager::persistOverrides() const
{
    QJsonObject obj;
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it)
        obj.insert(it.key(), it.value().toString());

    QFile file(userConfFilepath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(obj).toJson());
}

bool ShortcutManager::scopesOverlap(ShortcutScope a, ShortcutScope b)
{
    return a == b || a == Global || b == Global;
}

QString ShortcutManager::scopeDisplayName(ShortcutScope scope)
{
    switch (scope)
    {
        case Global:
            return tr("Global");
        case FixturesAndFunctions:
            return tr("Fixtures & Functions");
        case VirtualConsole:
            return tr("Virtual Console");
        case SimpleDesk:
            return tr("Simple Desk");
        case ShowManager:
            return tr("Show Manager");
        case IOManager:
            return tr("Input/Output Manager");
    }

    return QString();
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
