/*
  Q Light Controller Plus
  LegacyShowTimingDialog.qml

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

// ADR 0001 decision 4: warns about Shows that may still hold Show timeline
// values written under the old, ambiguous beat-pseudo-count convention
// (see Doc::possiblyAffectedLegacyBeatShows()). Shown once per file-open,
// triggered from App::loadXML via showLegacyShowTimingWarning() in
// MainView.qml. Per-Show "Dismiss" only removes it from this dialog's own
// session-local list - it is not persisted, so a Show still flagged by
// Doc's detection will be listed again on the next file-open.
Dialog
{
    id: control
    anchors.centerIn: parent
    width: mainView.width / 2
    parent: mainView

    modal: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("Legacy Show timing values detected")
    standardButtons: Dialog.Close
    onVisibleChanged: mainView.setDimScreen(visible)

    // list of { id: int, name: string }
    property var shows: []

    signal convertRequested(int showId, string showName)

    function dismissAt(index)
    {
        var newList = shows.slice()
        newList.splice(index, 1)
        shows = newList
        if (shows.length === 0)
            control.close()
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
                Layout.margins: 8
                wrapMode: Text.Wrap
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault
                color: UISettings.fgMain
                text: qsTr("This project was saved by an older QLC+ version. The Show(s) below use a " +
                           "beat-based timeline, so their item positions/durations may be inaccurate " +
                           "due to a legacy unit-handling issue. Review and convert each Show " +
                           "individually below, or dismiss/close to review later.")
            }

            Repeater
            {
                model: control.shows

                delegate:
                    RowLayout
                    {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8

                        Text
                        {
                            Layout.fillWidth: true
                            font.family: UISettings.robotoFontName
                            font.pixelSize: UISettings.textSizeDefault
                            color: UISettings.fgMain
                            elide: Text.ElideRight
                            text: modelData.name
                        }

                        Button
                        {
                            text: qsTr("Convert…")
                            onClicked: control.convertRequested(modelData.id, modelData.name)
                        }

                        Button
                        {
                            text: qsTr("Dismiss")
                            onClicked: control.dismissAt(index)
                        }
                    }
            }
        }
}
