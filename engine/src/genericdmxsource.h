/*
  Q Light Controller
  genericdmxsource.h

  Copyright (C) Heikki Junnila

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

#ifndef GENERICDMXSOURCE_H
#define GENERICDMXSOURCE_H

#include <QMutex>
#include <QPair>
#include <QMap>
#include <QList>

#include "scenevalue.h"
#include "dmxsource.h"

class Doc;
class GenericFader;

/** @addtogroup engine Engine
 * @{
 */

/**
 * This is a generic DMX source, that registers itself to doc->masterTimer() when
 * started and unregisters when deleted. Values set with set() are written to
 * UniverseArray on each writeDMX() call (called by MasterTimer); HTP values continuously
 * and LTP values only once (after which they will be removed from m_values).
 */
class GenericDMXSource final : public DMXSource
{
public:
    // Identifies which UI feature/call site produced a given set() call, so
    // debugging tools (SimpleDesk::debugChannelInfo()) can report WHY a
    // GenericDMXSource-driven channel holds its current value. This is the
    // GenericDMXSource equivalent of FunctionParent::MasterId - see
    // functionparent.h - but at per-set()-call granularity rather than
    // per-owning-instance, since a single GenericDMXSource instance (e.g.
    // ContextManager's own) is routinely reused by several unrelated
    // features that would otherwise be indistinguishable.
    enum Feature
    {
        // Caller didn't specify one - expected only from engine/UI test
        // facilities or a call site not yet given its own identity. Every
        // real qmlui call site should pass its own value instead (mirrors
        // FunctionParent::GenericOverride's role).
        Unspecified = 0,
        // ContextManager::pushPositionDelta()/pushRotationDelta(): dragging
        // a DMX-position/rotation-driven fixture in the 2D/3D view.
        DragPositionPush,
        // ContextManager::restorePersistedDmxTransforms(): re-pushing a
        // saved position/rotation onto live DMX channels on project load.
        PersistedTransformRestore,
        // ContextManager::setPositionPickPoint(): the 3D view's "click a
        // point to aim this fixture's Pan/Tilt at it" tool.
        PositionPickPoint,
        // ContextManager::slotChannelValueChanged(): a raw per-channel value
        // set from the Fixture Console / DMX View / Scene fixture console.
        FixtureConsoleChannelSet,
        // ContextManager::setChannelValueByType() invoked directly for
        // LeftPanel's Intensity slider (its only caller that doesn't pass
        // one of the more specific tags below).
        IntensityTool,
        // ContextManager::setColorValue(): LeftPanel's Color tool live preview.
        ColorTool,
        // ContextManager::setPositionCenter(): Position tool's "center" button.
        PositionCenterTool,
        // ContextManager::slotPresetChanged(): a capability/preset picker
        // (Maintenance/Speed/Prism/Effect tools, Gobo/Color macro pickers...).
        PresetTool,
        // ContextManager::highlightFixtureSelection(): the "Highlight"
        // button flashing the selected fixtures.
        FixtureHighlight,
        // ContextManager::setPositionValue(): the Position tool's Pan/Tilt drag.
        PositionTool,
        // ContextManager::setBeamDegrees(): the Beam/zoom tool.
        BeamTool,
        // ContextManager::setChannelValues(), whose sole caller is
        // PaletteManager::previewPalette(): Palette preview.
        PalettePreview,
        // Tardis replaying a FixtureSetDumpValue/FixtureResetDumpValues
        // action during undo/redo.
        DumpUndoRedo,
        // SceneEditor's own m_source: live preview of the Scene being edited.
        SceneEditorPreview,
        // SceneEditor's own m_highlightSource: external-control (a VC
        // fader/joystick mapped to Scene channels) fixture/position
        // highlighting while editing.
        SceneEditorExternalControlHighlight,
    };

    GenericDMXSource(Doc* doc);
    ~GenericDMXSource();

    /** Set the value of a fixture channel. $feature identifies the calling
     *  UI feature for debugging purposes only (see Feature above) - it has
     *  no effect on DMX output. */
    void set(quint32 fxi, quint32 ch, uchar value, Feature feature = Unspecified);

    /** Unset the value of a fixture channel */
    void unset(quint32 fxi, quint32 ch);

    /** Unset all the previously set channels/values */
    void unsetAll();

    /** Enable/disable output */
    void setOutputEnabled(bool enable);

    /** Check, whether output is enabled */
    bool isOutputEnabled() const;

    /** Returns how many channels this source is handling */
    quint32 channelsCount() const;

    /** Return the currently set values as a list of SceneValue */
    QList<SceneValue> channels();

    /** @reimp */
    void writeDMX(MasterTimer* timer, QList<Universe*> ua) override;

    /** Debug helper for SimpleDesk::debugChannelInfo(): given a live
     *  $fader (as returned by Universe::faders(), i.e. one this or some
     *  other GenericDMXSource instance requested from a Universe) and the
     *  $fxi/$ch it is currently driving, looks up which Feature tag - if
     *  any - the GenericDMXSource instance that owns $fader last associated
     *  with that channel. Matching is done by fader identity (not just by
     *  $fxi/$ch) so a channel simultaneously tracked - but not necessarily
     *  output - by more than one live GenericDMXSource instance is still
     *  attributed to the correct one. Returns false if no currently-live
     *  GenericDMXSource instance owns $fader (e.g. it belongs to a Function,
     *  SimpleDesk, or a Virtual Console widget instead). */
    static bool findFeatureForFader(const GenericFader *fader, quint32 fxi, quint32 ch, Feature &feature);

private:
    Doc *m_doc;
    QMutex m_mutex;
    QMap <QPair<quint32,quint32>,uchar> m_values;
    /** Parallel to m_values: which Feature last set() each entry. Kept in
     *  sync with m_values by every codepath that inserts into or clears it
     *  (set()/unset()/the writeDMX() clear-request branch). */
    QMap <QPair<quint32,quint32>,Feature> m_features;
    bool m_outputEnabled;
    bool m_clearRequest;
    bool m_changed;
    /** Map used to lookup a GenericFader instance for a Universe ID */
    QMap<quint32, QSharedPointer<GenericFader> > m_fadersMap;

    /** Registry of every currently-live GenericDMXSource instance, used by
     *  findFeatureForFader() to resolve a fader back to its owning instance.
     *  Guarded by s_registryMutex, held only across the list append/remove
     *  in the constructor/destructor and the scan in findFeatureForFader() -
     *  never nested with m_mutex of any instance. */
    static QMutex s_registryMutex;
    static QList<GenericDMXSource*> s_instances;
};

/** @} */

#endif
