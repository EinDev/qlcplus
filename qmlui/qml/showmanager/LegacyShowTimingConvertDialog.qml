/*
  Q Light Controller Plus
  LegacyShowTimingConvertDialog.qml

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
import "TimeUtils.js" as TimeUtils

// Per-Show conversion step of the ADR 0001 decision 4 flow, opened from
// LegacyShowTimingDialog's "Convert…" button. Lets the user pick which
// interpretation is correct instead of guessing, previews the effect
// before committing, and (on confirm) applies ShowManager.convertLegacyBeatShow(),
// which goes through Tardis like any other Show timeline edit.
Dialog
{
    id: control
    anchors.centerIn: parent
    width: mainView.width / 2
    parent: mainView

    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Convert \"%1\"").arg(control.showName)
    standardButtons: Dialog.Ok | Dialog.Cancel
    onVisibleChanged: mainView.setDimScreen(visible)

    property int showId: -1
    property string showName: ""
    property int bpmValue: 120
    property int beatsDivision: 4
    // false = "already correct real-time values" (no-op), true = "convert from beats"
    property bool convertFromBeats: false

    signal converted(int showId)

    function openFor(id, name)
    {
        showId = id
        showName = name
        convertFromBeats = false
        playAfterConvertCheck.checked = false

        var info = showManager.legacyShowConversionInfo(id)
        bpmValue = info.bpm > 0 ? info.bpm : 120
        beatsDivision = info.beatsDivision > 0 ? info.beatsDivision : 4

        control.open()
    }

    onAccepted:
    {
        if (convertFromBeats)
        {
            // grab the earliest item's post-conversion start time before
            // convertLegacyBeatShow() mutates it in place
            var preview = showManager.legacyShowConversionPreview(showId, bpmValue)

            showManager.convertLegacyBeatShow(showId, bpmValue)

            // convenience only - convertLegacyBeatShow() above is the real,
            // Tardis-tracked commit either way, so there is nothing to revert
            // if the user doesn't like the result: they can just undo (Ctrl+Z)
            // or not save
            if (playAfterConvertCheck.checked && preview.length > 0)
            {
                if (showManager.isPlaying())
                    showManager.stopShow()

                showManager.currentShowID = showId
                showManager.currentTime = Math.max(0, preview[0].newStart - 1000)
                mainView.switchToContext("SHOWMGR", "qrc:/ShowManager.qml")
                showManager.playShow()
            }
        }
        control.converted(showId)
    }

    header:
        Label
        {
            text: control.title
            color: "white"
            elide: Label.ElideRight
            font.family: UISettings.robotoFontName
            font.pixelSize: UISettings.textSizeDefault
            font.bold: true
            padding: 12
            background:
                Rectangle
                {
                    color: UISettings.sectionHeader
                    x: 2
                    y: 2
                    width: parent.width - 4
                    height: parent.height - 2
                }
        }

    background:
        Rectangle
        {
            color: UISettings.bgMedium
            border.color: UISettings.bgLight
            border.width: 2
        }

    contentItem:
        ColumnLayout
        {
            spacing: 8

            Text
            {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault
                color: UISettings.fgMain
                text: qsTr("Item positions/durations in this Show may be inaccurate due to a " +
                           "legacy unit-handling issue. Choose which interpretation is correct:")
            }

            RadioButton
            {
                text: qsTr("These are already correct real-time values (no changes will be made)")
                checked: !control.convertFromBeats
                onClicked: control.convertFromBeats = false
            }

            RadioButton
            {
                text: qsTr("These were entered as beats - convert using this BPM")
                checked: control.convertFromBeats
                onClicked: control.convertFromBeats = true
            }

            RowLayout
            {
                Layout.fillWidth: true
                visible: control.convertFromBeats

                Text
                {
                    color: UISettings.fgMain
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault
                    text: qsTr("BPM:")
                }

                TextField
                {
                    id: bpmField
                    implicitWidth: UISettings.bigItemHeight * 2
                    text: control.bpmValue.toString()
                    validator: IntValidator { bottom: 1; top: 999 }
                    onEditingFinished:
                    {
                        var parsed = parseInt(text)
                        if (parsed > 0)
                            control.bpmValue = parsed
                    }
                }

                Text
                {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: UISettings.fgMain
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault
                    text: qsTr("Beats per bar: %1. Tempo may have changed since these items " +
                               "were placed - the value above is only this Show's current " +
                               "setting, not necessarily what was used originally.").arg(control.beatsDivision)
                }
            }

            CheckBox
            {
                id: playAfterConvertCheck
                visible: control.convertFromBeats
                text: qsTr("Play back the Show from its converted timing after converting")
            }

            Text
            {
                visible: control.convertFromBeats
                Layout.fillWidth: true
                font.bold: true
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault
                color: UISettings.fgMain
                text: qsTr("Preview (start / duration, before -> after):")
            }

            Repeater
            {
                model: control.convertFromBeats ?
                           showManager.legacyShowConversionPreview(control.showId, control.bpmValue) : []

                delegate:
                    Text
                    {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.family: UISettings.robotoFontName
                        font.pixelSize: UISettings.textSizeDefault
                        color: UISettings.fgMain
                        text: modelData.name + ":  " +
                              TimeUtils.msToString(modelData.oldStart) + " → " + TimeUtils.msToString(modelData.newStart) +
                              "  /  " +
                              TimeUtils.msToString(modelData.oldDuration) + " → " + TimeUtils.msToString(modelData.newDuration)
                    }
            }
        }
}
