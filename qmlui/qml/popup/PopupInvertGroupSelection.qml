/*
  Q Light Controller Plus
  PopupInvertGroupSelection.qml

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

/** Disambiguation dialog for "Invert Selection in Group(s)"
 *  (ContextManager::invertGroupSelection()) whenever the current fixture
 *  selection spans more than one candidate Fixture Group. Lists every
 *  candidate with a checkbox, all checked by default, so confirming without
 *  touching anything reproduces the plain union-of-all-candidates behaviour
 *  used when there's only a single candidate (no dialog at all in that
 *  case). Cancelling (or Escape) leaves the fixture selection untouched -
 *  contextManager.confirmGroupSelectionInversion() is only called from
 *  onAccepted below. */
CustomPopupDialog
{
    id: popupRoot
    title: qsTr("Invert Selection in Group(s)")

    /** Candidate groups for the pending inversion - a QVariantList of
     *  { mLabel, mValue } set by the caller (see MainView.qml's Connections
     *  to contextManager.candidateGroupsForInversionReady) right before
     *  open() is called. */
    property var groups: []

    contentItem:
        ColumnLayout
        {
            spacing: 5

            RobotoText
            {
                Layout.fillWidth: true
                Layout.bottomMargin: 5
                wrapText: true
                height: UISettings.listItemHeight * 2
                label: qsTr("The current selection spans more than one Fixture Group. Choose which group(s) to invert:")
            }

            Repeater
            {
                id: groupRepeater
                model: popupRoot.groups

                RowLayout
                {
                    Layout.fillWidth: true
                    spacing: 5

                    property alias checked: groupCheck.checked

                    CustomCheckBox
                    {
                        id: groupCheck
                        implicitWidth: UISettings.listItemHeight
                        implicitHeight: UISettings.listItemHeight
                        checked: true
                    }
                    RobotoText
                    {
                        Layout.fillWidth: true
                        height: UISettings.listItemHeight
                        label: modelData.mLabel
                    }
                }
            }
        }

    onAccepted:
    {
        var ids = []
        for (var i = 0; i < groupRepeater.count; i++)
        {
            var row = groupRepeater.itemAt(i)
            if (row && row.checked)
                ids.push(popupRoot.groups[i].mValue)
        }
        contextManager.confirmGroupSelectionInversion(ids)
    }
}
