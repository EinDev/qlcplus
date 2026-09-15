/*
  Q Light Controller Plus
  PopupTrackSpoutSize.qml

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

/** "Set Spout output size..." of a Show track header (TrackDelegate.qml).
 *  Lists the track's current output size, the size of every Spout clip on
 *  it and a custom W x H; picking one calls ShowManager::setTrackSpoutSize,
 *  which fixes the size on the Track (saved, undoable) and resizes the
 *  live sender - receivers re-initialize. */
CustomPopupDialog
{
    id: popupRoot
    title: qsTr("Set Spout output size")
    standardButtons: Dialog.Cancel

    property int trackIdx: -1
    property string trackName: ""
    /** ShowManager::trackSpoutInfo() of the track */
    property var info: ({ hasSpout: false, width: 0, height: 0, fixed: false, clips: [] })
    /** Distinct [ { label, width, height } ] choices built from info */
    property var choices: []

    function openFor(trackIdx, trackName)
    {
        popupRoot.trackIdx = trackIdx
        popupRoot.trackName = trackName
        popupRoot.info = showManager.trackSpoutInfo(trackIdx)

        var list = []
        var seen = {}
        function add(w, h, who)
        {
            if (w <= 0 || h <= 0)
                return
            var key = w + "x" + h
            if (seen[key])
            {
                seen[key].who.push(who)
                return
            }
            seen[key] = { width: w, height: h, who: [ who ] }
            list.push(seen[key])
        }
        add(info.width, info.height, info.fixed ? qsTr("current, fixed") : qsTr("current"))
        for (var i = 0; i < info.clips.length; i++)
            add(info.clips[i].width, info.clips[i].height, info.clips[i].name)

        var result = []
        for (var c = 0; c < list.length; c++)
            result.push({ width: list[c].width, height: list[c].height,
                          label: list[c].width + "x" + list[c].height + "  (" + list[c].who.join(", ") + ")" })
        popupRoot.choices = result

        customWSpin.value = info.width > 0 ? info.width : 1920
        customHSpin.value = info.height > 0 ? info.height : 1080
        open()
    }

    function apply(w, h)
    {
        showManager.setTrackSpoutSize(popupRoot.trackIdx, w, h)
        popupRoot.close()
    }

    contentItem:
        ColumnLayout
        {
            spacing: 6

            RobotoText
            {
                Layout.fillWidth: true
                wrapText: true
                height: UISettings.listItemHeight * 2
                label: popupRoot.info.width > 0 ?
                           qsTr("Track '%1' outputs Spout at %2x%3%4.")
                               .arg(popupRoot.trackName).arg(popupRoot.info.width).arg(popupRoot.info.height)
                               .arg(popupRoot.info.fixed ? qsTr(" (fixed on the track)") : qsTr(" (size of its first clip)")) :
                           qsTr("Track '%1' has no Spout output size yet: the first clip whose resolution is known will set it.")
                               .arg(popupRoot.trackName)
            }

            Repeater
            {
                model: popupRoot.choices

                GenericButton
                {
                    Layout.fillWidth: true
                    height: UISettings.listItemHeight
                    label: modelData.label
                    onClicked: popupRoot.apply(modelData.width, modelData.height)
                }
            }

            RowLayout
            {
                Layout.fillWidth: true
                spacing: 5

                RobotoText { height: UISettings.listItemHeight; label: qsTr("Custom") }
                CustomSpinBox
                {
                    id: customWSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 16384
                    value: 1920
                }
                RobotoText { height: UISettings.listItemHeight; label: "x" }
                CustomSpinBox
                {
                    id: customHSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 16384
                    value: 1080
                }
                GenericButton
                {
                    height: UISettings.listItemHeight
                    label: qsTr("Apply")
                    onClicked: popupRoot.apply(customWSpin.value, customHSpin.value)
                }
            }

            GenericButton
            {
                Layout.fillWidth: true
                height: UISettings.listItemHeight
                visible: popupRoot.info.fixed
                label: qsTr("Unset fixed size (first clip decides at next load)")
                onClicked: popupRoot.apply(0, 0)
            }
        }
}
