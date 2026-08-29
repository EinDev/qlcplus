/*
  Q Light Controller Plus
  tardis.h

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

#ifndef TARDIS_H
#define TARDIS_H

#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QVariant>
#include <QSemaphore>
#include <QQuickView>
#include <QElapsedTimer>

class FixtureManager;
class FunctionManager;
class ContextManager;
class VirtualConsole;
class NetworkManager;
class ShowManager;
class SimpleDesk;
class Doc;

typedef struct
{
    int m_action;
    quint64 m_timestamp;
    quint32  m_objID;
    QVariant m_oldValue;
    QVariant m_newValue;
} TardisAction;

Q_DECLARE_METATYPE(TardisAction)

typedef QPair<quint32, uint> UIntPair;
Q_DECLARE_METATYPE(UIntPair)

typedef QPair<QString, int> StringIntPair;
Q_DECLARE_METATYPE(StringIntPair)

typedef QPair<QString, double> StringDoublePair;
Q_DECLARE_METATYPE(StringDoublePair)

typedef QPair<QString, QString> StringStringPair;
Q_DECLARE_METATYPE(StringStringPair)

#define LIVE_ACTIONS_START_CODE     0xF000

/**
 * Tardis is QLC+'s undo/redo engine: every user-facing mutation (fixture edits,
 * function edits, VC widget edits, live VC interactions, ...) is funneled through
 * enqueueAction() as a (code, objID, oldValue, newValue) tuple and later replayed
 * (in reverse, for undo) by processAction().
 *
 * Threading model - easy to get backwards from the QThread base class alone:
 * Tardis::run() (the code that actually executes on the separate thread) only
 * dequeues incoming actions and maintains m_history/m_historyIndex (batching
 * near-simultaneous duplicate actions within TARDIS_ACTION_INTERTIME ms, capping
 * at TARDIS_MAX_ACTIONS_NUMBER, forwarding to the network). It never calls
 * processAction(). undoAction()/redoAction() are Q_INVOKABLE, called directly
 * from QML on the *main* thread, and call processAction() synchronously right
 * there - which is what actually pokes ContextManager/FixtureManager/
 * FunctionManager/VirtualConsole/etc. So despite subclassing QThread, none of
 * the code that touches Doc or UI objects ever runs off the main thread; the
 * worker thread's only job is bookkeeping the action queue/history.
 *
 * Adding a new undoable action requires ALL of the following - missing any one
 * compiles fine and looks wired up, but silently does nothing (or crashes on
 * undo) the first time it's exercised:
 *   1. A new value in the ActionCodes enum below, in the right numeric block
 *      (blocks are grouped by subsystem). Two separate thresholds gate live
 *      behaviour, easy to conflate: enqueueAction() skips setModified() for
 *      any code >= LIVE_ACTIONS_START_CODE (0xF000), but run() only skips
 *      recording history at all for the narrower range >= VCButtonSetPressed
 *      (0xF200) - so e.g. FixtureSetDumpValue/FunctionStart/FunctionStop
 *      (0xF000-0xF1FF) don't dirty the project, yet still go through the same
 *      history/undo machinery as any other action.
 *   2. A Tardis::instance()->enqueueAction(...) call at the actual mutation
 *      site (e.g. in FixtureManager/FunctionManager/VirtualConsole/...).
 *   3. A `case` for it in processAction() (tardis.cpp) that applies m_oldValue
 *      (undo) or m_newValue (redo) back onto the real object.
 *   4. For a create/delete-style action (an object that stops existing on one
 *      side of the toggle - see the Create/Delete pairs below): the value
 *      passed to enqueueAction() must be a QByteArray produced by
 *      actionToByteArray(), and processAction()'s case must call
 *      processBufferedAction() to recreate the object from it - a plain
 *      QVariant of the object's old state doesn't work once the object itself
 *      has been deleted. processBufferedAction() only warns (qWarning,
 *      "Action 0x.. is not buffered!") that a non-QByteArray value reached it
 *      for a code it already knows is supposed to be buffered - that code list
 *      is itself hardcoded in processBufferedAction() (tardis.cpp), so a new
 *      create/delete action also needs adding there, or forgetting the
 *      QByteArray payload at the enqueueAction() call site fails with no
 *      warning at all.
 */
class Tardis final : public QThread
{
    Q_OBJECT

public:
    enum ActionCodes
    {
        /* Preview settings */
        EnvironmentSetSize = 0x0000,
        EnvironmentBackgroundImage,
        FixtureSetPosition,
        FixtureSetRotation,
        GenericItemSetPosition,
        GenericItemSetRotation,
        GenericItemSetScale,

        /* Input/Output mapping actions */
        IOAddUniverse = 0x0100,
        IORemoveUniverse,

        /* Fixture editing actions */
        FixtureCreate = 0x0200,
        FixtureDelete,
        FixtureMove,
        FixtureSetName,
        FixtureSetChannelModifier,

        /* Fixture group editing actions */
        FixtureGroupCreate = 0x0300,
        FixtureGroupDelete,

        /* Function editing actions */
        FunctionCreate = 0x1000,
        FunctionDelete,
        FunctionSetName,
        FunctionSetPath,
        FunctionSetRunOrder,
        FunctionSetDirection,
        FunctionSetTempoType,
        FunctionSetFadeIn,
        FunctionSetFadeOut,
        FunctionSetDuration,

        /* Scene editing actions */
        SceneSetChannelValue = 0x1100,
        SceneUnsetChannelValue,
        SceneAddFixture,
        SceneRemoveFixture,
        SceneAddFixtureGroup,
        SceneRemoveFixtureGroup,
        SceneAddPalette,
        SceneRemovePalette,

        /* Chaser editing actions */
        ChaserAddStep = 0x1200,
        ChaserRemoveStep,
        ChaserMoveStep,
        ChaserSetStepFadeIn,
        ChaserSetStepHold,
        ChaserSetStepFadeOut,
        ChaserSetStepDuration,

        /* EFX editing actions */
        EFXAddFixture = 0x1300,
        EFXRemoveFixture,
        EFXFixturePropagation,
        EFXSetAlgorithmIndex,
        EFXSetRelative,
        EFXSetWidth,
        EFXSetHeight,
        EFXSetXOffset,
        EFXSetYOffset,
        EFXSetRotation,
        EFXSetStartOffset,
        EFXSetXFrequency,
        EFXSetYFrequency,
        EFXSetXPhase,
        EFXSetYPhase,

        /* Collection editing actions */
        CollectionAddFunction = 0x1400,
        CollectionRemoveFunction,

        /* RGB Matrix editing actions */
        RGBMatrixSetFixtureGroup = 0x1500,
        RGBMatrixSetAlgorithmIndex,
        RGBMatrixSetColor1,
        RGBMatrixSetColor2,
        RGBMatrixSetColor3,
        RGBMatrixSetColor4,
        RGBMatrixSetColor5,
        RGBMatrixSetScriptIntValue,
        RGBMatrixSetScriptDoubleValue,
        RGBMatrixSetScriptStringValue,
        RGBMatrixSetText,
        RGBMatrixSetTextFont,
        RGBMatrixSetImage,
        RGBMatrixSetOffset,
        RGBMatrixSetAnimationStyle,

        /* Audio editing actions */
        AudioSetSource = 0x1600,
        AudioSetVolume,

        /* Video editing actions */
        VideoSetSource = 0x1700,
        VideoSetScreenIndex,
        VideoSetFullscreen,
        VideoSetGeometry,
        VideoSetRotation,
        VideoSetLayer,

        /* Show Manager actions */
        ShowManagerAddTrack = 0xB000,
        ShowManagerDeleteTrack,
        ShowManagerAddFunction,
        ShowManagerDeleteFunction,
        ShowManagerItemSetStartTime,
        ShowManagerItemSetDuration,

        /* Simple Desk actions */
        SimpleDeskSetChannel = 0xC000,
        SimpleDeskResetChannel,

        /* Virtual console editing actions */
        VCWidgetCreate = 0xE000,
        VCWidgetDelete,
        VCWidgetGeometry,
        VCWidgetReparent,
        VCWidgetAllowResize,
        VCWidgetDisabled,
        VCWidgetVisible,
        VCWidgetCaption,
        VCWidgetBackgroundColor,
        VCWidgetBackgroundImage,
        VCWidgetForegroundColor,
        VCWidgetFont,
        VCWidgetPage,
        VCWidgetZIndex,

        VCButtonSetActionType = 0xE100,
        VCButtonSetFunctionID,
        VCButtonEnableStartupIntensity,
        VCButtonSetStartupIntensity,

        VCSliderSetMode = 0xE200,
        VCSliderSetWidgetStyle,
        VCSliderSetDisplayStyle,
        VCSliderSetInverted,
        VCSliderSetFunctionID,
        VCSliderSetControlledAttribute,
        VCSliderSetLowLimit,
        VCSliderSetHighLimit,

        VCCueListSetChaserID = 0xE300,

        /* Live actions */
        FixtureSetDumpValue = LIVE_ACTIONS_START_CODE,
        FixtureResetDumpValues,

        FunctionStart = LIVE_ACTIONS_START_CODE + 0x100,
        FunctionStop,

        VCButtonSetPressed = LIVE_ACTIONS_START_CODE + 0x200,

        VCSliderSetValue = LIVE_ACTIONS_START_CODE + 0x300,
        VCSliderButtonPress,

        VCCueListPlayClicked = LIVE_ACTIONS_START_CODE + 0x400,
        VCCueListStopClicked,
        VCCueListNextClicked,
        VCCueListPreviousClicked,
        VCCueListSetIndex,
        VCCueListSideFaderLevel,

        VCSpeedDialSetTime = LIVE_ACTIONS_START_CODE + 0x500,
        VCSpeedDialSetFactor,
        VCSpeedDialApply,

        VCXYPadSetPosition = LIVE_ACTIONS_START_CODE + 0x600,
        VCXYPadSetGeometry,
        VCXYPadActivatePreset,
        VCXYPadSetFloorPosition,

        VCAudioTriggersSetCaptureEnabled = LIVE_ACTIONS_START_CODE + 0x700,
        VCAudioTriggersSetLevel,

        VCClockSetEnabled = LIVE_ACTIONS_START_CODE + 0x800,
        VCClockReset,

        VCAnimationSetFaderLevel = LIVE_ACTIONS_START_CODE + 0x900,
        VCAnimationSetAlgorithmIndex,
        VCAnimationSetColor1,
        VCAnimationSetColor2,
        VCAnimationSetColor3,
        VCAnimationSetColor4,
        VCAnimationSetColor5,
        VCAnimationActivatePreset,

        /* Network protocol actions */
        NetAnnounce = 0xFF00,
        NetAnnounceReply,
        NetAuthentication,
        NetAuthenticationReply,
        NetPoll,
        NetPollReply,
        NetProjectTransfer,
        NetProjectChanging,
        NetProjectLoaded,
        NetProjectRequest
    };

    Q_ENUM(ActionCodes)

    explicit Tardis(QQuickView *view, Doc *doc, NetworkManager *netMgr,
                    FixtureManager *fxMgr, FunctionManager *funcMgr,
                    ContextManager *ctxMgr, SimpleDesk *sDesk,
                    ShowManager *showMgr, VirtualConsole *vc,
                    QObject *parent = 0);

    ~Tardis();

    /** Get the singleton instance */
    static Tardis* instance();

    /** Build a TardisAction with the provided data and enqueue it
     *  to be processed by the Tardis thread */
    void enqueueAction(int code, quint32 objID, QVariant oldVal, QVariant newVal);

    /** Undo an action or a batch of actions taken from history */
    Q_INVOKABLE void undoAction();

    /** Redo an action or a batch of actions taken from history */
    Q_INVOKABLE void redoAction();

    /** Process an action and return the reversed action if undoing */
    int processAction(TardisAction &action, bool undo);

    QByteArray actionToByteArray(int code, quint32 objID, QVariant data = QVariant());

    /** Reset the actions history */
    void resetHistory();

    void forwardActionToNetwork(int code, TardisAction &action);

    /** @reimp */
    void run() override; // thread run function

    /** Return the symbolic name of an action code, for logging purposes */
    static QString actionToString(int action);

protected:
    bool processBufferedAction(int action, quint32 objID, QVariant &value);

protected slots:
    void slotProcessNetworkAction(int code, quint32 id, QVariant value);

private:
    /** The singleton Tardis instance */
    static Tardis* s_instance;
    /** Thread running status flag */
    bool m_running;

    /** Reference to the QML view root */
    QQuickView *m_view;
    /** Reference to the project workspace */
    Doc *m_doc;
    /** Reference to the Network Manager */
    NetworkManager *m_networkManager;

    /** Reference to the Fixture Manager */
    FixtureManager *m_fixtureManager;
    /** Reference to the Function Manager */
    FunctionManager *m_functionManager;
    /** Reference to the Context Manager */
    ContextManager *m_contextManager;
    /** Reference to the Simple Desk */
    SimpleDesk *m_simpleDesk;
    /** Reference to the Show Manager */
    ShowManager *m_showManager;
    /** Reference to the Virtual Console */
    VirtualConsole *m_virtualConsole;

    /** Time reference since application starts */
    QElapsedTimer m_uptime;

    /** A inter-thread queue to desynchronize actions processing */
    QQueue<TardisAction> m_actionsQueue;

    /** Synchronization variables between threads */
    QMutex m_queueMutex;
    QSemaphore m_queueSem;

    /** The actual history of actions */
    QList<TardisAction> m_history;

    /** An index pointing to the history last
     *  undone action or the last item */
    int m_historyIndex;

    /** Count the actions (or batch of actions) recorded */
    int m_historyCount;

    /** Flag to prevent actions looping */
    bool m_busy;
};

#endif /* TARDIS_H */
