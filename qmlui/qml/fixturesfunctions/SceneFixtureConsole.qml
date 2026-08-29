/*
  Q Light Controller Plus
  SceneFixtureConsole.qml

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

import QtQuick
import QtQuick.Layouts

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: sfcContainer
    anchors.fill: parent
    color: "transparent"
    objectName: "sceneFixtureConsole"

    // sceneEditor can already be null by the time this component is torn down:
    // switching directly from editing one Scene to another deletes the old
    // SceneEditor (which nulls this context property) before the new one is
    // constructed, and the delete also closes the bottom panel, destroying
    // this component while sceneEditor is still null.
    Component.onCompleted: if (sceneEditor) sceneEditor.sceneConsoleLoaded(true)
    Component.onDestruction: if (sceneEditor) sceneEditor.sceneConsoleLoaded(false)

    property bool isSceneEditor: true
    property bool multipleSelection: false

    /** Bumped on every external controller mapping change, to make the
     *  Fixture consoles re-evaluate their channel highlights */
    property int externalMapRevision: 0

    function scrollToItem(fxIdx)
    {
        console.log("[scrollToItem] fxIdx: " + fxIdx)
        fixtureList.positionViewAtIndex(fxIdx, ListView.Beginning)
        fixtureList.currentIndex = fxIdx
    }

    /** Closes any floating channel-tool popup currently open on this
     *  console. Called from BottomPanel.qml when the Fixtures & Functions
     *  tab itself is being hidden (top-level tab switch) - see the
     *  onCurrentSubContextChanged handler below for the narrower
     *  sub-context-switch case this doesn't cover. */
    function closeChannelTools()
    {
        channelToolLoader.closeChannelTool()
    }

    /** Refresh the channel highlights when the external controller
     *  mapping changes. Scrolling and Fixture selection are handled
     *  by the Scene editor through the standard Fixture selection */
    Connections
    {
        target: sceneEditor
        ignoreUnknownSignals: true

        function onExternalMapChanged()
        {
            sfcContainer.externalMapRevision++
        }
    }

    // Unlike the DMX/2D/3D/Universe Grid views (which live inside
    // FixturesAndFunctions.qml's previewLoader and get destroyed whenever
    // the sub-view changes), this console lives in BottomPanel, a sibling
    // of that loader - so switching sub-views does not tear it down, and
    // any open channel tool popup would otherwise stay visible on top of
    // the newly-selected view.
    Connections
    {
        target: contextManager
        ignoreUnknownSignals: true

        function onCurrentSubContextChanged()
        {
            channelToolLoader.closeChannelTool()
        }
    }

    ChannelToolLoader
    {
        id: channelToolLoader
        z: 2

        onValueChanged:
            function (fixtureID, channelIndex, value)
            {
                functionManager.setChannelValue(fixtureID, channelIndex, value)
            }
    }

    ListView
    {
        id: fixtureList
        anchors.fill: parent
        orientation: ListView.Horizontal
        model: sceneEditor ? sceneEditor.fixtureList : null
        boundsBehavior: Flickable.StopAtBounds
        highlightFollowsCurrentItem: false
        currentIndex: -1
        z: 1

        delegate:
            Rectangle
            {
                height: parent.height
                width: fxConsole.width + 4
                color: UISettings.bgMedium

                Component.onCompleted: sceneEditor.registerFixtureConsole(index, fxConsole)
                Component.onDestruction: sceneEditor.unRegisterFixtureConsole(index)

                FixtureConsole
                {
                    id: fxConsole
                    x: 2
                    fixtureObj: model.cRef
                    isSelected: model.isSelected
                    height: parent.height
                    color: index % 2 ? UISettings.bgFixtureEven : UISettings.bgFixtureOdd
                    showEnablers: true
                    sceneConsole: true
                    multipleSelection: sfcContainer.multipleSelection
                    externalMapRevision: sfcContainer.externalMapRevision

                    onRequestTool:
                        function (item, fixtureID, chIndex, value)
                        {
                            channelToolLoader.loadChannelTool(item, fixtureID, chIndex, value)
                        }
                    onCloseTool: channelToolLoader.visible = false
                }
                // Fixture divider
                Rectangle
                {
                    anchors.fill: parent
                    width: 2
                    color: "transparent"
                    radius: 3
                    border.width: 2
                    border.color: fixtureList.currentIndex == index ? UISettings.selection : "transparent"
                }
            }
    }
}
