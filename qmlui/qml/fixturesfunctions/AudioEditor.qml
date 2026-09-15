/*
  Q Light Controller Plus
  AudioEditor.qml

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
import QtQuick.Dialogs
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "TimeUtils.js" as TimeUtils
import "."

Rectangle
{
    anchors.fill: parent
    color: "transparent"

    property int functionID: -1
    property var mediaInfo: audioEditor ? audioEditor.mediaInfo : null

    signal requestView(int ID, string qmlSrc, bool back)

    TimeEditTool
    {
        id: timeEditTool

        parent: mainView
        z: 99
        x: rightSidePanel.x - width
        visible: false

        onValueChanged: (val) =>
        {
            if (speedType == QLCFunction.FadeIn)
                audioEditor.fadeInSpeed = val
            else if (speedType == QLCFunction.FadeOut)
                audioEditor.fadeOutSpeed = val
        }
    }

    EditorTopBar
    {
        id: topBar
        text: audioEditor.functionName
        onTextChanged: audioEditor.functionName = text

        onBackClicked:
        {
            var prevID = audioEditor.previousID
            requestView(prevID, functionManager.getEditorResource(prevID), true)
        }
    }

    FileDialog
    {
        id: openAudioDialog
        visible: false

        onAccepted:
        {
            audioEditor.sourceFileName = selectedFile
        }
    }

    onWidthChanged: aeGrid.width = width

    GridLayout
    {
        id: aeGrid
        columns: 2
        columnSpacing: 5
        rowSpacing: 10
        y: topBar.height

        // row 1
        RobotoText
        {
            height: selFileBtn.height
            //anchors.verticalCenter: parent.verticalCenter
            label: qsTr("File name")
        }
        Rectangle
        {
            Layout.fillWidth: true
            height: selFileBtn.height
            color: "transparent"

            RobotoText
            {
                width: reloadFileBtn.x - 5
                fontSize: UISettings.textSizeDefault * 0.8
                labelColor: audioEditor.originChanged ? UISettings.selection : UISettings.fgLight
                wrapText: true
                // a managed copy shows its plain file name, an external
                // reference its full path - the long .qxw.assets/<sha>/ path
                // is nothing the user needs to read. A managed copy whose
                // origin was re-rendered since the import says so, the
                // Reload button next to it picks the new version up.
                label: (audioEditor.sourceManaged ? qsTr("Managed: %1").arg(audioEditor.sourceDisplayName)
                                                  : audioEditor.sourceDisplayName)
                       + (audioEditor.originChanged ? " - " + qsTr("changed on disk") : "")
            }
            IconButton
            {
                id: reloadFileBtn
                x: selFileBtn.x - width - 3
                faSource: FontAwesome.fa_arrows_rotate
                faColor: audioEditor.originChanged ? UISettings.selection : UISettings.fgMain
                tooltip: audioEditor.reloadTooltip
                enabled: audioEditor.originAvailable
                onClicked: audioEditor.reloadSource()
            }
            IconButton
            {
                id: selFileBtn
                x: parent.width - width - 3
                tooltip: qsTr("Replace file... (copies the new file into the project and repoints this function only)")
                RobotoText { anchors.centerIn: parent; label: "..." }

                onClicked:
                {
                    var extList = audioEditor.audioExtensions
                    var exts = qsTr("Audio files") + " ("
                    for (var i = 0; i < extList.length; i++)
                        exts += extList[i] + " "
                    exts += ")"
                    openAudioDialog.nameFilters = [ exts, qsTr("All files") + " (*)" ]
                    openAudioDialog.visible = true
                    openAudioDialog.open()
                }
            }
        }

        // row 2
        RobotoText
        {
            label: qsTr("Duration")
            height: UISettings.listItemHeight
        }
        RobotoText
        {
            height: UISettings.listItemHeight
            Layout.fillWidth: true
            label: mediaInfo ? mediaInfo.duration : ""
            labelColor: UISettings.fgLight
        }

        // row 3
        RobotoText
        {
            label: qsTr("Channels")
            height: UISettings.listItemHeight
        }
        RobotoText
        {
            height: UISettings.listItemHeight
            Layout.fillWidth: true
            label: mediaInfo ? mediaInfo.channels : ""
            labelColor: UISettings.fgLight
        }

        // row 4
        RobotoText
        {
            label: qsTr("Sample Rate")
            height: UISettings.listItemHeight
        }
        RobotoText
        {
            height: UISettings.listItemHeight
            Layout.fillWidth: true
            label: mediaInfo ? mediaInfo.sampleRate : ""
            labelColor: UISettings.fgLight
        }

        // row 5
        RobotoText
        {
            label: qsTr("Bitrate")
            height: UISettings.listItemHeight
        }
        RobotoText
        {
            height: UISettings.listItemHeight
            Layout.fillWidth: true
            label: mediaInfo ? mediaInfo.bitrate : ""
            labelColor: UISettings.fgLight
        }

        // row 5b
        RobotoText
        {
            label: qsTr("BPM")
            height: UISettings.listItemHeight
        }
        RowLayout
        {
            height: UISettings.listItemHeight

            RobotoText
            {
                height: UISettings.listItemHeight
                labelColor: UISettings.fgLight
                label:
                {
                    switch (audioEditor.bpmState)
                    {
                        case 1: return qsTr("Detecting...")
                        case 2: return audioEditor.bpm.toFixed(1)
                        case 3: return qsTr("Detection failed")
                        default: return qsTr("Not analyzed")
                    }
                }
            }
            IconButton
            {
                id: detectBpmBtn
                faSource: FontAwesome.fa_rotate_right
                faColor: UISettings.fgMain
                tooltip: qsTr("Detect BPM")
                enabled: audioEditor.bpmState !== 1
                onClicked: audioEditor.detectBpm()
            }
        }

        // row 6
        RobotoText
        {
            label: qsTr("Playback mode")
            height: UISettings.listItemHeight
        }
        RowLayout
        {
            height: UISettings.listItemHeight
            //Layout.fillWidth: true

            ButtonGroup { id: playbackModeGroup }

            CustomCheckBox
            {
                implicitWidth: UISettings.iconSizeMedium
                implicitHeight: implicitWidth
                ButtonGroup.group: playbackModeGroup
                checked: !audioEditor.looped
                onClicked: if (checked) audioEditor.looped = false
            }
            RobotoText
            {
                height: UISettings.listItemHeight
                label: qsTr("Single shot")
            }

            CustomCheckBox
            {
                implicitWidth: UISettings.iconSizeMedium
                implicitHeight: implicitWidth
                ButtonGroup.group: playbackModeGroup
                checked: audioEditor.looped
                onClicked: if (checked) audioEditor.looped = true
            }
            RobotoText
            {
                height: UISettings.listItemHeight
                label: qsTr("Looped")
            }
        }

        // row 7
        RobotoText
        {
            label: qsTr("Output device")
            height: UISettings.listItemHeight
        }
        CustomComboBox
        {
            height: UISettings.listItemHeight
            Layout.fillWidth: true
            model: ioManager.audioOutputSources
            currentIndex: audioEditor.cardLineIndex
            onCurrentIndexChanged: audioEditor.cardLineIndex = currentIndex
        }

        // row 8
        RobotoText
        {
            label: qsTr("Volume")
            height: UISettings.listItemHeight
        }
        RowLayout
        {
            height: UISettings.listItemHeight
            Layout.fillWidth: true

            CustomSpinBox
            {
                height: UISettings.listItemHeight
                Layout.fillWidth: true
                from: 0
                to: 100
                value: audioEditor.volume
                suffix: "%"
                onValueChanged: audioEditor.volume = value
            }
            CustomCheckBox
            {
                implicitWidth: UISettings.iconSizeMedium
                implicitHeight: implicitWidth
                checked: audioEditor.muted
                onClicked: audioEditor.muted = checked
            }
            RobotoText
            {
                height: UISettings.listItemHeight
                label: qsTr("Mute")
            }
        }

        // row 9
        RobotoText
        {
            id: fiLabel
            label: qsTr("Fade in")
            height: UISettings.listItemHeight
        }

        Rectangle
        {
            Layout.fillWidth: true
            height: UISettings.listItemHeight
            color: UISettings.bgMedium

            RobotoText
            {
                id: fiTimeLabel
                x: 3
                height: parent.height
                label: TimeUtils.timeToQlcString(audioEditor.fadeInSpeed, QLCFunction.Time)
            }
            MouseArea
            {
                anchors.fill: parent
                onDoubleClicked:
                {
                    timeEditTool.show(-1, this.mapToItem(mainView, 0, 0).y,
                                      fiLabel.label, fiTimeLabel.label, QLCFunction.FadeIn)
                }
            }

            IconButton
            {
                x: parent.width - width
                width: height
                height: UISettings.listItemHeight
                faSource: FontAwesome.fa_clock
                faColor: UISettings.fgMain
                onClicked: timeEditTool.show(-1, this.mapToItem(mainView, 0, 0).y,
                                             fiLabel.label, fiTimeLabel.label, QLCFunction.FadeIn)
            }
        }

        // row 10
        RobotoText
        {
            id: foLabel
            height: UISettings.listItemHeight
            label: qsTr("Fade out")
        }

        Rectangle
        {
            Layout.fillWidth: true
            height: UISettings.listItemHeight
            color: UISettings.bgMedium

            RobotoText
            {
                id: foTimeLabel
                x: 3
                height: parent.height
                label: TimeUtils.timeToQlcString(audioEditor.fadeOutSpeed, QLCFunction.Time)
            }
            MouseArea
            {
                anchors.fill: parent
                onDoubleClicked:
                {
                    timeEditTool.show(-1, this.mapToItem(mainView, 0, 0).y,
                                      foLabel.label, foTimeLabel.label, QLCFunction.FadeOut)
                }
            }
            IconButton
            {
                x: parent.width - width
                width: height
                height: UISettings.listItemHeight
                faSource: FontAwesome.fa_clock
                faColor: UISettings.fgMain
                onClicked: timeEditTool.show(-1, this.mapToItem(mainView, 0, 0).y,
                                             foLabel.label, foTimeLabel.label, QLCFunction.FadeOut)
            }
        }
    }
}
