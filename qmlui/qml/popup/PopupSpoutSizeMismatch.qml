/*
  Q Light Controller Plus
  PopupSpoutSizeMismatch.qml

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
import QtQuick.Controls.Basic

import "."

/** Asked by the Show Manager (ShowManager::spoutSizeMismatch) whenever a
 *  Spout-mode Video clip lands on a track whose shared Spout sender has a
 *  different output size. The clip is already on the track and is being
 *  aspect-fit into the existing output - that is the default, and what
 *  closing this dialog any other way (Enter, Escape) leaves in place.
 *  "Switch" fixes the track's output size to the clip's instead
 *  (ShowManager::setTrackSpoutSize), which resizes the live sender and
 *  makes every receiver (OBS, ...) re-initialize its source.
 *
 *  Several clips can land at once (a multi-item paste): the requests are
 *  queued and shown one after the other. */
CustomPopupDialog
{
    id: popupRoot
    title: qsTr("Spout output size differs")
    standardButtons: Dialog.NoButton

    property int trackIdx: -1
    property string trackName: ""
    property int trackWidth: 0
    property int trackHeight: 0
    property string clipName: ""
    property int clipWidth: 0
    property int clipHeight: 0

    /** Pending requests, oldest first */
    property var queue: []

    function ask(trackIdx, trackName, trackWidth, trackHeight, clipName, clipWidth, clipHeight)
    {
        queue.push({ trackIdx: trackIdx, trackName: trackName,
                     trackWidth: trackWidth, trackHeight: trackHeight,
                     clipName: clipName, clipWidth: clipWidth, clipHeight: clipHeight })
        if (!visible)
            showNext()
    }

    function showNext()
    {
        if (queue.length === 0)
            return

        var m = queue.shift()
        popupRoot.trackIdx = m.trackIdx
        popupRoot.trackName = m.trackName
        popupRoot.trackWidth = m.trackWidth
        popupRoot.trackHeight = m.trackHeight
        popupRoot.clipName = m.clipName
        popupRoot.clipWidth = m.clipWidth
        popupRoot.clipHeight = m.clipHeight
        open()
    }

    // whichever way it went, serve the next queued request
    onClosed: Qt.callLater(showNext)

    contentItem:
        ColumnLayout
        {
            spacing: 8

            RobotoText
            {
                Layout.fillWidth: true
                wrapText: true
                height: UISettings.listItemHeight * 2
                label: qsTr("Track '%1' outputs Spout at %2x%3. '%4' is %5x%6.")
                            .arg(popupRoot.trackName).arg(popupRoot.trackWidth).arg(popupRoot.trackHeight)
                            .arg(popupRoot.clipName).arg(popupRoot.clipWidth).arg(popupRoot.clipHeight)
            }

            RobotoText
            {
                Layout.fillWidth: true
                wrapText: true
                height: UISettings.listItemHeight * 2
                fontSize: UISettings.textSizeDefault * 0.85
                labelColor: UISettings.fgLight
                label: qsTr("Switching the track output resizes its Spout sender: every receiver (e.g. OBS) re-initializes that source.")
            }

            GenericButton
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                bgColor: UISettings.highlight
                label: qsTr("Keep %1x%2 (scale the clip)").arg(popupRoot.trackWidth).arg(popupRoot.trackHeight)
                onClicked: popupRoot.close()
            }

            GenericButton
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                label: qsTr("Switch track output to %1x%2").arg(popupRoot.clipWidth).arg(popupRoot.clipHeight)
                onClicked:
                {
                    showManager.setTrackSpoutSize(popupRoot.trackIdx, popupRoot.clipWidth, popupRoot.clipHeight)
                    popupRoot.close()
                }
            }
        }
}
