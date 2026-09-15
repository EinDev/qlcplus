/*
  Q Light Controller Plus
  TrackDelegate.qml

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
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: trackRoot
    width: 100
    height: UISettings.mediumItemHeight
    clip: true

    color: isSelected ? UISettings.highlight : "#313F4A"

    property Track trackRef: null
    property bool isSelected: false
    /** Index of this track in the Show (what ShowManager's track methods take) */
    property int trackIndex: -1

    /** ShowManager::trackSpoutInfo() of this track: the shared Spout output
     *  size of its Spout-mode Video clips, if it has any */
    property var spoutInfo: ({ hasSpout: false, width: 0, height: 0, fixed: false, clips: [] })

    signal trackSelected()
    /** The user asked to set this track's Spout output size */
    signal spoutSizeRequested(int trackIndex)

    function refreshSpoutInfo()
    {
        if (trackIndex >= 0)
            spoutInfo = showManager.trackSpoutInfo(trackIndex)
    }

    Component.onCompleted: refreshSpoutInfo()
    onTrackIndexChanged: refreshSpoutInfo()

    Connections
    {
        target: showManager
        ignoreUnknownSignals: true
        function onTrackSpoutInfoChanged() { trackRoot.refreshSpoutInfo() }
        function onTracksChanged() { trackRoot.refreshSpoutInfo() }
    }

    CustomTextInput
    {
        x: 2
        width: parent.width - 4
        height: parent.height
        text: trackRef ? trackRef.name : ""
        wrapMode: TextInput.Wrap
        allowDoubleClick: true

        onTextConfirmed:
            function(text)
            {
                if (trackRef)
                    trackRef.name = text
            }
    }

    Rectangle
    {
        width: parent.width
        height: 2
        y: parent.height - 2
        color: "#263039"
    }

    // Spout output size of the track's shared sender (read-only; change it
    // via the right-click menu). Only shown when the track has Spout clips.
    RobotoText
    {
        id: spoutLabel
        x: 2
        y: parent.height - height - 3
        z: 2
        width: parent.width - 4
        height: UISettings.listItemHeight * 0.6
        visible: trackRoot.spoutInfo.hasSpout === true
        fontSize: UISettings.textSizeDefault * 0.7
        labelColor: UISettings.fgLight
        label: trackRoot.spoutInfo.width > 0 ?
                   qsTr("Spout %1x%2%3").arg(trackRoot.spoutInfo.width).arg(trackRoot.spoutInfo.height)
                                        .arg(trackRoot.spoutInfo.fixed ? " •" : "") :
                   qsTr("Spout: size pending")

        MouseArea
        {
            id: spoutLabelArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
        ToolTip
        {
            visible: spoutLabelArea.containsMouse
            delay: 500
            text: trackRoot.spoutInfo.fixed ?
                      qsTr("Spout output size fixed on this track. Right-click to change.") :
                      qsTr("Spout output size, set by the first clip. Right-click to change.")
        }
    }

    Popup
    {
        id: trackMenu
        padding: 0

        background:
            Rectangle
            {
                color: UISettings.bgStrong
                border.color: UISettings.bgStronger
            }

        Column
        {
            ContextMenuEntry
            {
                imgSource: "qrc:/video.svg"
                entryText: qsTr("Set Spout output size...")
                onClicked:
                {
                    trackMenu.close()
                    trackRoot.spoutSizeRequested(trackRoot.trackIndex)
                }
            }
            ContextMenuEntry
            {
                faSource: FontAwesome.fa_trash_can
                faColor: "crimson"
                entryText: qsTr("Delete track")
                onClicked:
                {
                    trackMenu.close()
                    if (trackRef)
                        showManager.requestTrackDeletion(trackRef.id)
                }
            }
        }
    }

    // Delete this track (an empty one right away, one with items after a
    // confirmation, see ShowManager::requestTrackDeletion)
    IconButton
    {
        id: deleteButton
        x: parent.width - width - 2
        y: muteButton.y + muteButton.height + 2
        z: 2
        width: parent.width / 6
        height: parent.height * 0.3
        bgColor: "#8191A0"
        faSource: FontAwesome.fa_trash_can
        faColor: "crimson"
        tooltip: qsTr("Delete track")
        onClicked:
        {
            if (!trackRef)
                return
            // this button sits above the header's MouseArea, so select the
            // track here as a click on the header would
            showManager.selectedTrackId = trackRef.id
            trackRoot.trackSelected()
            showManager.requestTrackDeletion(trackRef.id)
        }
    }

    IconButton
    {
        id: soloButton
        x: parent.width - (width * 2) - 6
        y: 2
        z: 2
        width: parent.width / 6
        height: parent.height * 0.3
        bgColor: "#8191A0"
        checkedColor: "yellow"
        imgSource: ""
        checkable: true
        tooltip: qsTr("Solo this track")
        onToggled: showManager.setTrackSolo(trackRef.id, checked)

        RobotoText
        {
            anchors.centerIn: parent
            height: parent.height - 2
            label: "S"
            labelColor: "#3C4A55"
            fontSize: height - 2
            fontBold: true
        }
    }

    IconButton
    {
        id: muteButton
        x: parent.width - width - 2
        y: 2
        z: 2
        width: parent.width / 6
        height: parent.height * 0.3
        bgColor: "#8191A0"
        checkedColor: "red"
        checked: trackRef ? trackRef.mute : false
        imgSource: ""
        checkable: true
        tooltip: qsTr("Mute this track")
        onToggled: if (trackRef) trackRef.mute = checked

        RobotoText
        {
            anchors.centerIn: parent
            height: parent.height - 2
            label: "M"
            labelColor: "#3C4A55"
            fontSize: height - 2
            fontBold: true
        }
    }

    MouseArea
    {
        anchors.fill: parent
        propagateComposedEvents: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) =>
        {
            showManager.selectedTrackId = trackRef.id
            trackRoot.trackSelected()

            if (mouse.button === Qt.RightButton)
            {
                trackMenu.x = mouse.x
                trackMenu.y = mouse.y
                trackMenu.open()
                return
            }
            mouse.accepted = false
        }
    }
}
