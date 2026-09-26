/*
  Q Light Controller - Unit test
  shortcutmanager_test.cpp

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

#include <QtTest>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSettings>

#include "shortcutmanager_test.h"
#include "shortcutmanager.h"

namespace
{
    QString nativeText(const QKeySequence &seq)
    {
        return seq.toString(QKeySequence::NativeText);
    }

    /** Register the handful of actions the tests below rely on. Mirrors the
     *  shape of App::registerBuiltinShortcuts(): one Global toggle, one
     *  scoped action, and one that is registered without any binding */
    void registerSampleActions(ShortcutManager &mgr)
    {
        mgr.registerAction("io.blackoutToggle", QKeySequence(Qt::CTRL | Qt::Key_B),
                           ShortcutManager::Global, "Toggle blackout", nullptr);
        mgr.registerAction("vc.editMode", QKeySequence(Qt::CTRL | Qt::Key_E),
                           ShortcutManager::VirtualConsole, "Toggle Virtual Console edit mode", nullptr);
        mgr.registerAction("test.unbound", QKeySequence(),
                           ShortcutManager::Global, "Deliberately unbound", nullptr);
    }
}

void ShortcutManager_Test::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_overridesFile = m_tempDir.path() + "/shortcuts.json";

    // hintsEnabled is persisted through QSettings - keep that out of the
    // registry/real ini for this process, same idea as the overrides file
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    QCoreApplication::setOrganizationName("qlcplus-test");
    QCoreApplication::setApplicationName("shortcutmanager_test");
}

void ShortcutManager_Test::sequenceTextForRegisteredAction()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QCOMPARE(mgr.sequenceTextForAction("io.blackoutToggle"), nativeText(QKeySequence(Qt::CTRL | Qt::Key_B)));
    QCOMPARE(mgr.sequenceTextForAction("vc.editMode"), nativeText(QKeySequence(Qt::CTRL | Qt::Key_E)));
    QVERIFY(mgr.sequenceTextForAction("io.blackoutToggle").isEmpty() == false);
}

void ShortcutManager_Test::sequenceTextForUnknownActionIsEmpty()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QCOMPARE(mgr.sequenceTextForAction("does.notExist"), QString());
    QCOMPARE(mgr.sequenceTextForAction(QString()), QString());
    // registered but bound to nothing reads as "unbound", not as a key
    QCOMPARE(mgr.sequenceTextForAction("test.unbound"), QString());
}

void ShortcutManager_Test::sequenceTextFollowsOverride()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QSignalSpy changedSpy(&mgr, &ShortcutManager::actionsChanged);

    QKeySequence newSeq(Qt::CTRL | Qt::SHIFT | Qt::Key_K);
    mgr.saveOverride("io.blackoutToggle", newSeq.toString());

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(mgr.sequenceTextForAction("io.blackoutToggle"), nativeText(newSeq));
    // other actions are untouched by the override
    QCOMPARE(mgr.sequenceTextForAction("vc.editMode"), nativeText(QKeySequence(Qt::CTRL | Qt::Key_E)));

    // and the override survives a restart with the same overrides file
    ShortcutManager reloaded(nullptr, m_overridesFile);
    registerSampleActions(reloaded);
    QCOMPARE(reloaded.sequenceTextForAction("io.blackoutToggle"), nativeText(newSeq));

    reloaded.resetAllToDefaults();
}

void ShortcutManager_Test::sequenceTextFollowsResetToDefault()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    mgr.saveOverride("vc.editMode", QKeySequence(Qt::Key_F9).toString());
    QCOMPARE(mgr.sequenceTextForAction("vc.editMode"), nativeText(QKeySequence(Qt::Key_F9)));

    mgr.resetToDefault("vc.editMode");
    QCOMPARE(mgr.sequenceTextForAction("vc.editMode"), nativeText(QKeySequence(Qt::CTRL | Qt::Key_E)));
}

void ShortcutManager_Test::descriptionForAction()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QCOMPARE(mgr.descriptionForAction("io.blackoutToggle"), QString("Toggle blackout"));
    QCOMPARE(mgr.descriptionForAction("does.notExist"), QString());
}

void ShortcutManager_Test::shortcutFiredCarriesSequenceAndDescription()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QSignalSpy firedSpy(&mgr, &ShortcutManager::shortcutFired);
    QSignalSpy triggeredSpy(&mgr, &ShortcutManager::actionTriggered);

    QKeyEvent press(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
    QCOMPARE(mgr.handleKeyEvent(&press), true);

    QCOMPARE(triggeredSpy.count(), 1);
    QCOMPARE(firedSpy.count(), 1);
    QList<QVariant> args = firedSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("io.blackoutToggle"));
    QCOMPARE(args.at(1).toString(), nativeText(QKeySequence(Qt::CTRL | Qt::Key_B)));
    QCOMPARE(args.at(2).toString(), QString("Toggle blackout"));

    // after a remap the fired signal reports the new binding, not the default
    QKeySequence newSeq(Qt::CTRL | Qt::Key_K);
    mgr.saveOverride("io.blackoutToggle", newSeq.toString());

    QKeyEvent oldPress(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
    QCOMPARE(mgr.handleKeyEvent(&oldPress), false);
    QCOMPARE(firedSpy.count(), 0);

    QKeyEvent newPress(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    QCOMPARE(mgr.handleKeyEvent(&newPress), true);
    QCOMPARE(firedSpy.count(), 1);
    args = firedSpy.takeFirst();
    QCOMPARE(args.at(1).toString(), nativeText(newSeq));

    mgr.resetAllToDefaults();
}

void ShortcutManager_Test::shortcutFiredRespectsScope()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QSignalSpy firedSpy(&mgr, &ShortcutManager::shortcutFired);

    // vc.editMode is VirtualConsole-scoped: nothing fires outside that tab
    mgr.setCurrentContext("FIXANDFUNC");
    QKeyEvent press(QEvent::KeyPress, Qt::Key_E, Qt::ControlModifier);
    QCOMPARE(mgr.handleKeyEvent(&press), false);
    QCOMPARE(firedSpy.count(), 0);

    mgr.setCurrentContext("VC");
    QKeyEvent pressInVc(QEvent::KeyPress, Qt::Key_E, Qt::ControlModifier);
    QCOMPARE(mgr.handleKeyEvent(&pressInVc), true);
    QCOMPARE(firedSpy.count(), 1);
    QCOMPARE(firedSpy.first().at(0).toString(), QString("vc.editMode"));
}

void ShortcutManager_Test::notifyButtonClickedEmitsHint()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);
    mgr.setHintsEnabled(true);

    QSignalSpy hintSpy(&mgr, &ShortcutManager::clickHintRequested);
    QSignalSpy firedSpy(&mgr, &ShortcutManager::shortcutFired);

    mgr.notifyButtonClicked("vc.editMode");

    QCOMPARE(hintSpy.count(), 1);
    QList<QVariant> args = hintSpy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("vc.editMode"));
    QCOMPARE(args.at(1).toString(), nativeText(QKeySequence(Qt::CTRL | Qt::Key_E)));
    QCOMPARE(args.at(2).toString(), QString("Toggle Virtual Console edit mode"));

    // a click hint is not a key-cast: the key-cast signal stays quiet, so a
    // single gesture can never produce two toasts
    QCOMPARE(firedSpy.count(), 0);
}

void ShortcutManager_Test::notifyButtonClickedIgnoresUnknownAndUnbound()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);
    mgr.setHintsEnabled(true);

    QSignalSpy hintSpy(&mgr, &ShortcutManager::clickHintRequested);

    mgr.notifyButtonClicked("does.notExist");
    mgr.notifyButtonClicked(QString());
    mgr.notifyButtonClicked("test.unbound");

    QCOMPARE(hintSpy.count(), 0);
}

void ShortcutManager_Test::notifyButtonClickedRespectsHintsToggle()
{
    ShortcutManager mgr(nullptr, m_overridesFile);
    registerSampleActions(mgr);

    QSignalSpy hintSpy(&mgr, &ShortcutManager::clickHintRequested);
    QSignalSpy firedSpy(&mgr, &ShortcutManager::shortcutFired);

    mgr.setHintsEnabled(false);
    mgr.notifyButtonClicked("io.blackoutToggle");
    QCOMPARE(hintSpy.count(), 0);

    // the key-cast signal itself is unconditional - the listener gates it -
    // so a disabled toggle must not silence it at this layer
    QKeyEvent press(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
    QCOMPARE(mgr.handleKeyEvent(&press), true);
    QCOMPARE(firedSpy.count(), 1);

    mgr.setHintsEnabled(true);
    mgr.notifyButtonClicked("io.blackoutToggle");
    QCOMPARE(hintSpy.count(), 1);
}

void ShortcutManager_Test::hintsEnabledDefaultsToTrueAndNotifies()
{
    {
        // fresh settings (initTestCase pointed QSettings at an empty dir)
        QSettings settings;
        settings.remove("shortcuts/showhints");
        settings.sync();
    }

    ShortcutManager mgr(nullptr, m_overridesFile);
    QCOMPARE(mgr.hintsEnabled(), true);

    QSignalSpy toggledSpy(&mgr, &ShortcutManager::hintsEnabledChanged);

    mgr.setHintsEnabled(true);
    QCOMPARE(toggledSpy.count(), 0);

    mgr.setHintsEnabled(false);
    QCOMPARE(toggledSpy.count(), 1);
    QCOMPARE(mgr.hintsEnabled(), false);

    // persisted: a second instance reads the stored value back
    ShortcutManager reloaded(nullptr, m_overridesFile);
    QCOMPARE(reloaded.hintsEnabled(), false);

    reloaded.setHintsEnabled(true);
}

// QLCFile::userDirectory() (the default overrides path, not used here but
// compiled in) and QSettings both want a QCoreApplication; no window needed.
QTEST_GUILESS_MAIN(ShortcutManager_Test)
