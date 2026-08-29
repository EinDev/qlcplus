/*
  Q Light Controller Plus
  functionparent.h

  Copyright (C) 2016 Massimo Callegari
                     David Garyga

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

#ifndef FUNCTIONPARENT_H
#define FUNCTIONPARENT_H

/** @addtogroup engine_functions Functions
 * @{
 */

/**
 * Start/Stop source
 */
class FunctionParent final
{
public:
    // The type of the FunctionParent has 2 purposes:
    //
    // 1. It allows to differentiate the ID of a VCWidget
    // and the ID of a Function, because they could overlap
    // otherwise
    //
    // 2. It allows to define a special behavior for some
    // types. Example: a Master FunctionParent can stop any
    // function, regardless of what started it.
    //
    // AutoVCWidget and ManualVCWidget are separated.
    // In order to keep some parts of the current behavior,
    // ManualVCWidget acts like the "Master" type and can stop a
    // running function when the user uses a manual VCWidget.
    enum Type
    {
        // Another function (Chaser, Collection...)
        Function = 0,
        // An automatic VC widget (VCAudioTriggers)
        AutoVCWidget,
        // A manual VC widget (Button, Slider...)
        ManualVCWidget,
        // Override anything (MasterTimer, test facilities...)
        Master = 0xffffffff,
    };

    // When type() == Master, id() further identifies WHICH of a small,
    // fixed set of non-VC, non-container callers requested the override -
    // this keeps Type closed (Function/AutoVCWidget/ManualVCWidget/Master
    // are the only values Function::start()/stop() branch on, see
    // Function::stop()'s "clear all sources" rule) while still letting
    // diagnostics such as SimpleDesk::debugChannelInfo() tell every
    // Master-flavored caller apart. This mirrors how Tardis::ActionCodes
    // (qmlui/tardis/tardis.h) enumerates a fixed, documented registry of
    // callers rather than growing unrelated top-level types per caller.
    //
    // Adding a new caller: add a value here (never renumber/reuse
    // existing ones - these are runtime-only, but code and log output
    // may reference them by name), pass it via master(MasterId), and add
    // a case to SimpleDesk::describeFunctionParent()'s Master switch.
    enum MasterId
    {
        // Generic/unspecified use of the Master override - the default
        // for any caller that hasn't been given its own identifier
        // (engine/ui test facilities, and the legacy QLC+4 Qt Widgets UI
        // call sites, which this MasterId registry does not cover).
        GenericOverride = 0,
        // A Function's own write()/postRun() logic decided, on its own,
        // that it's done (reached the end of its steps/duration, hit an
        // error condition such as a missing fixture group) and is
        // stopping itself - not an external caller at all.
        EngineSelfStop,
        // MasterTimer::timerTickFunctions()'s stop-everything request
        // (m_stopAllFunctions, e.g. Blackout/panic), not any specific
        // Function's own choice.
        MasterTimerStopAll,
        // The project's configured "startup Function", auto-started when
        // a project finishes loading or Doc switches into Operate mode.
        ProjectAutostart,
        // Function Manager's "preview" toggle/selection (the play
        // button next to the function tree, with no editor open)
        // starting/stopping the selected Function(s) directly.
        FunctionManagerPreview,
        // Function Manager's live "Scene preview" toggle, used while a
        // Scene (or a Sequence's bound Scene) is open in an editor, so
        // channel edits are written to the output immediately.
        FunctionManagerScenePreview,
        // Function Manager stopping a Function because it is about to
        // be removed from the project (deleteFunction()/deleteFunctions()).
        FunctionManagerDelete,
        // A Function Editor's own generic preview toggle / function swap
        // (base FunctionEditor::setPreviewEnabled()/setFunctionID(),
        // shared by every function-type editor: Scene, EFX, Audio,
        // Collection, Script, Video, Chaser, RGBMatrix) and
        // RGBMatrixEditor's own preview restart after an internal
        // operation (e.g. saveToSequence()).
        FunctionEditorPreview,
        // ChaserEditor's Sequence step-scrubbing preview, which
        // starts/stops the Sequence's bound Scene directly (not the
        // Chaser/Sequence itself) to show the selected step live.
        ChaserEditorStepPreview,
        // Show Manager's play/pause/stop transport controls.
        ShowManagerPlayback,
        // Tardis replaying a FunctionStart/FunctionStop action code
        // during undo/redo.
        TardisUndoRedo,
        // WebAccess's "setFunctionStatus" web/OSC API command.
        WebAccess,
        // Video::stopFromUI() - the user closed a Video function's
        // preview window from the 2D/3D view.
        VideoWindowClosed,
    };

private:
    quint64 m_id;

public:
    explicit FunctionParent(Type type, quint32 id)
    {
        m_id = quint64((quint64(type) & 0xffffffff) << 32)
            | quint64(id & 0xffffffff);
    }

    bool operator ==(FunctionParent const& right) const
    {
        return m_id == right.m_id;
    }

    quint32 type() const
    {
        return (m_id >> 32) & 0xffffffff;
    }

    quint32 id() const
    {
        return m_id & 0xffffffff;
    }

    static FunctionParent master(MasterId subId = GenericOverride)
    {
        return FunctionParent(Master, quint32(subId));
    }
};

/** @} */

#endif
