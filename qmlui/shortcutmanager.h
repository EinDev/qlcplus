/*
  Q Light Controller Plus
  shortcutmanager.h

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

#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QKeySequence>
#include <QString>
#include <QHash>
#include <QVector>
#include <functional>

class QKeyEvent;

class ShortcutManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentContext READ currentContext WRITE setCurrentContext NOTIFY currentContextChanged)

public:
    /** The tab/area a registered action is meaningful in. Global actions
     *  match regardless of currentContext(); every other scope matches
     *  only while currentContext() holds the corresponding MainView.qml
     *  context string (FIXANDFUNC/VC/SDESK/SHOWMGR/IOMGR) */
    enum ShortcutScope
    {
        Global,
        FixturesAndFunctions,
        VirtualConsole,
        SimpleDesk,
        ShowManager,
        IOManager
    };
    Q_ENUM(ShortcutScope)

    explicit ShortcutManager(QObject *parent = nullptr);

    /** Get/Set the QML context (tab) currently visible.
     *  Set by MainView.qml from switchToContext() - not computed here */
    QString currentContext() const;
    void setCurrentContext(const QString &context);

    /** Register a built-in action under the stable string $id, bound by
     *  default to $defaultSequence, meaningful only while $scope matches
     *  currentContext() (or always, for Global). $callback is invoked
     *  whenever the sequence is matched by handleKeyEvent() */
    void registerAction(const QString &id, const QKeySequence &defaultSequence,
                         ShortcutScope scope, const QString &description,
                         std::function<void()> callback);

    /** Persist a user override of the key sequence bound to $id, and apply
     *  it immediately to the already-registered action, if any */
    Q_INVOKABLE void saveOverride(const QString &id, const QString &sequence);

    /** Look up $event against every registered action whose scope matches
     *  the current context. Invokes the first match's callback and returns
     *  true. Returns false if nothing matched (the event is not consumed) */
    bool handleKeyEvent(QKeyEvent *event);

    /** Return the description of the registered action bound to $sequence,
     *  regardless of scope, or an empty string if none matches. Used to warn
     *  a user binding a per-show Virtual Console key that it will shadow a
     *  built-in action - VC bindings are tab-global so any scope could be
     *  affected once bound, not just whatever scope is current right now */
    QString actionDescriptionForSequence(const QKeySequence &sequence) const;

signals:
    void currentContextChanged();

    /** Emitted right before a matched action's callback is invoked.
     *  Not load-bearing for any Phase 1 action (every one of them has a
     *  direct C++ callback) - infrastructure for QML-only actions and for
     *  a future shortcut cheat-sheet */
    void actionTriggered(QString actionId);

private:
    struct ShortcutAction
    {
        QString id;
        QKeySequence sequence;
        QKeySequence defaultSequence;
        ShortcutScope scope;
        QString description;
        std::function<void()> callback;
    };

    void loadOverrides();
    bool scopeMatchesCurrentContext(ShortcutScope scope) const;
    QString userConfFilepath() const;

    QString m_currentContext;
    QVector<ShortcutAction> m_actions;

    /** Action id -> user-overridden key sequence, read once at startup
     *  from userConfFilepath() and applied as each action is registered */
    QHash<QString, QKeySequence> m_overrides;
};

#endif // SHORTCUTMANAGER_H
