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
#include <QVariant>
#include <functional>

class QKeyEvent;

class ShortcutManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentContext READ currentContext WRITE setCurrentContext NOTIFY currentContextChanged)
    /** True while a rebinding UI (see qml/ShortcutsEditor.qml) is waiting for
     *  the next key press to capture. While true, handleKeyEvent() takes no
     *  action on any key, so the QML capture UI sees the raw event instead of
     *  a matching built-in action's callback firing at the same time */
    Q_PROPERTY(bool capturing READ isCapturing WRITE setCapturing NOTIFY capturingChanged)
    /** Master switch for the shortcut learning aids: the key-cast toast shown
     *  when a shortcut fires (shortcutFired) and the click hints shown when a
     *  button bound to an action is clicked with the mouse
     *  (clickHintRequested). Persisted via QSettings; defaults to true.
     *  Tooltip suffixes ("(Ctrl+B)") are not affected by this switch */
    Q_PROPERTY(bool hintsEnabled READ hintsEnabled WRITE setHintsEnabled NOTIFY hintsEnabledChanged)

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

    /** $overridesFilePath is the JSON file user overrides are read from and
     *  persisted to. Empty (the default, what App uses) means the standard
     *  per-user QLC+ config directory; unit tests pass a temporary file so
     *  they never touch a real user's overrides */
    explicit ShortcutManager(QObject *parent = nullptr, const QString &overridesFilePath = QString());

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

    /** Every registered action as a QVariantMap (id, description, sequence -
     *  NativeText for display, scope - int, scopeName - display string,
     *  isDefault), sorted by scope (Global first) with registration order
     *  preserved within a scope */
    Q_INVOKABLE QVariantList listActions() const;

    /** Drop any override for $id and revert it to its built-in sequence */
    Q_INVOKABLE void resetToDefault(const QString &id);

    /** Drop every override and revert every action to its built-in sequence */
    Q_INVOKABLE void resetAllToDefaults();

    /** Would binding $sequence to an action currently in $scope collide with
     *  another registered action, other than $excludeId itself? Two actions
     *  only collide if their scopes overlap (same scope, or either is
     *  Global) - eg. vc.copy and showmgr.copy may share a sequence.
     *  Returns an empty map if there is no collision, otherwise the
     *  colliding action's id/description/scopeName */
    Q_INVOKABLE QVariantMap findCollision(const QString &sequence, int scope, const QString &excludeId) const;

    /** Write the current overrides to $path, in the same id -> key sequence
     *  JSON shape as the user settings file, so the result can be re-imported
     *  via importOverrides() below */
    Q_INVOKABLE bool exportOverrides(const QString &path) const;

    /** Replace every current override with the contents of $path (same JSON
     *  shape as exportOverrides()/the user settings file) and persist the
     *  result. Any action not mentioned in $path reverts to its default */
    Q_INVOKABLE bool importOverrides(const QString &path);

    bool isCapturing() const;
    void setCapturing(bool capturing);

    bool hintsEnabled() const;
    void setHintsEnabled(bool enabled);

    /** The key sequence currently bound to the action $id, in display
     *  (NativeText) form - eg. "Ctrl+B" - or an empty string if $id is not
     *  a registered action or is currently unbound. Used by IconButton/
     *  MenuBarEntry to append a live "(Ctrl+B)" suffix to their tooltip;
     *  callers re-read it on actionsChanged() so remaps stay truthful */
    Q_INVOKABLE QString sequenceTextForAction(const QString &id) const;

    /** The description registered for the action $id, or an empty string
     *  if $id is not a registered action */
    Q_INVOKABLE QString descriptionForAction(const QString &id) const;

    /** Called by a QML button bound to the action $id when it is clicked
     *  with the mouse. Emits clickHintRequested() with the action's display
     *  strings so the UI can point the user at the equivalent shortcut.
     *  A no-op for an unknown or currently unbound $id, and while
     *  hintsEnabled is false */
    Q_INVOKABLE void notifyButtonClicked(const QString &id);

    /** Build a QKeySequence out of a raw Qt::Key + Qt::KeyboardModifiers
     *  combo, as delivered by a QML Keys.onPressed KeyEvent, and return it
     *  both in storage form ("storage", QKeySequence::toString()'s default
     *  PortableText - what saveOverride()/findCollision() expect) and in
     *  display form ("display", NativeText). "empty" is true for a bare
     *  modifier press (Ctrl/Alt/Shift/Meta alone), which isn't a usable
     *  sequence on its own */
    Q_INVOKABLE QVariantMap sequenceFromKeyEvent(int key, int modifiers) const;

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
    void capturingChanged();
    void hintsEnabledChanged();

    /** Emitted right before a matched action's callback is invoked.
     *  Not load-bearing for any Phase 1 action (every one of them has a
     *  direct C++ callback) - infrastructure for QML-only actions and for
     *  a future shortcut cheat-sheet */
    void actionTriggered(QString actionId);

    /** Emitted alongside actionTriggered() for every matched key press,
     *  carrying the display strings the key-cast toast needs
     *  ($sequenceText is NativeText, eg. "Ctrl+B") so QML never has to
     *  search listActions() on a keypress. Emitted regardless of
     *  hintsEnabled - the listener decides whether to show anything */
    void shortcutFired(QString actionId, QString sequenceText, QString description);

    /** Emitted by notifyButtonClicked() - see there. Only emitted while
     *  hintsEnabled is true */
    void clickHintRequested(QString actionId, QString sequenceText, QString description);

    /** Emitted whenever the registry's bindings change (override saved/reset/
     *  imported) - qml/ShortcutsEditor.qml reloads listActions() on this */
    void actionsChanged();

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
    const ShortcutAction *findAction(const QString &id) const;

    /** Write m_overrides in full to userConfFilepath(), replacing whatever
     *  was there before. m_overrides is always kept as the complete override
     *  state (loaded in full at startup, updated in full by every mutator
     *  above), so this never needs to merge with what's already on disk */
    void persistOverrides() const;

    static bool scopesOverlap(ShortcutScope a, ShortcutScope b);
    static QString scopeDisplayName(ShortcutScope scope);

    QString m_currentContext;
    QVector<ShortcutAction> m_actions;
    bool m_capturing = false;
    bool m_hintsEnabled = true;

    /** Explicit overrides file (see the constructor), or empty for the
     *  default per-user location */
    QString m_overridesFilePath;

    /** Action id -> user-overridden key sequence, read once at startup
     *  from userConfFilepath() and applied as each action is registered */
    QHash<QString, QKeySequence> m_overrides;
};

#endif // SHORTCUTMANAGER_H
