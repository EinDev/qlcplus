/*
  Q Light Controller Plus
  ShortcutsEditor.qml

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
import QtQuick.Controls
import QtQuick.Dialogs

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: editorRoot
    anchors.fill: parent
    color: UISettings.bgMedium
    focus: true

    /** id of the action currently waiting for a key press, or "" */
    property string capturingId: ""
    property var actionsList: []
    /** { id, storage, display } for a binding waiting on the collision
     *  popup's answer, or null */
    property var pendingBinding: null

    function reload()
    {
        actionsList = shortcutManager.listActions()
    }

    function actionForId(id)
    {
        for (var i = 0; i < actionsList.length; i++)
        {
            if (actionsList[i].id === id)
                return actionsList[i]
        }
        return null
    }

    /** Flattens actionsList (already sorted by scope) into a list of
     *  { isHeader, label } / { isHeader: false, action } rows for a single
     *  ListView - actionsList is small (registry-sized, not project data),
     *  so no separate grouped model is needed */
    function buildDisplayItems()
    {
        var items = []
        var lastScope = -1

        for (var i = 0; i < actionsList.length; i++)
        {
            var action = actionsList[i]
            if (action.scope !== lastScope)
            {
                items.push({ isHeader: true, label: action.scopeName })
                lastScope = action.scope
            }
            items.push({ isHeader: false, action: action })
        }

        return items
    }

    property var displayItems: buildDisplayItems()

    function cancelCapture()
    {
        capturingId = ""
        shortcutManager.capturing = false
    }

    function startCapture(id)
    {
        if (capturingId === id)
        {
            cancelCapture()
            return
        }

        capturingId = id
        shortcutManager.capturing = true
        editorRoot.forceActiveFocus()
    }

    onVisibleChanged:
    {
        if (!visible)
            cancelCapture()
    }

    Component.onCompleted: reload()

    Connections
    {
        target: shortcutManager
        function onActionsChanged() { reload() }
    }

    Keys.onPressed: function(event)
    {
        if (capturingId === "")
            return

        event.accepted = true

        if (event.key === Qt.Key_Escape)
        {
            cancelCapture()
            return
        }

        var res = shortcutManager.sequenceFromKeyEvent(event.key, event.modifiers)
        // a bare modifier (Ctrl/Alt/Shift/Meta) isn't a usable sequence on
        // its own - keep listening for the real key
        if (res.empty)
            return

        var action = actionForId(capturingId)
        if (action === null)
        {
            cancelCapture()
            return
        }

        var collision = shortcutManager.findCollision(res.storage, action.scope, capturingId)

        if (collision.id)
        {
            pendingBinding = { id: capturingId, storage: res.storage, display: res.display }
            collisionPopup.message =
                qsTr("\"%1\" is already assigned to \"%2\" (%3).\nAssigning it here too means only one of the two will react when the key is pressed.\n\nAssign it anyway?")
                    .arg(res.display).arg(collision.description).arg(collision.scopeName)
            collisionPopup.open()
        }
        else
        {
            shortcutManager.saveOverride(capturingId, res.storage)
            cancelCapture()
        }
    }

    CustomPopupDialog
    {
        id: collisionPopup
        width: mainView.width / 2.5
        title: qsTr("Keyboard shortcut collision")
        standardButtons: Dialog.Yes | Dialog.No

        onClicked: function(role)
        {
            if (role === Dialog.Yes && pendingBinding !== null)
                shortcutManager.saveOverride(pendingBinding.id, pendingBinding.storage)

            pendingBinding = null
            cancelCapture()
        }
    }

    CustomPopupDialog
    {
        id: confirmLoadDefaultsPopup
        width: mainView.width / 3
        title: qsTr("Load default shortcuts")
        message: qsTr("This will discard every customized keyboard shortcut and restore the built-in defaults. Continue?")
        standardButtons: Dialog.Yes | Dialog.No

        onClicked: function(role)
        {
            if (role === Dialog.Yes)
                shortcutManager.resetAllToDefaults()
        }
    }

    CustomPopupDialog
    {
        id: messagePopup
        standardButtons: Dialog.Ok
        onAccepted: close()
    }

    FileDialog
    {
        id: exportDialog
        title: qsTr("Export keyboard shortcuts")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [ qsTr("JSON files") + " (*.json)", qsTr("All files") + " (*)" ]

        onAccepted:
        {
            if (shortcutManager.exportOverrides(selectedFile))
            {
                messagePopup.title = qsTr("Operation completed")
                messagePopup.message = qsTr("Shortcuts successfully exported")
            }
            else
            {
                messagePopup.title = qsTr("Error")
                messagePopup.message = qsTr("Unable to export shortcuts to the selected file")
            }
            messagePopup.open()
        }
    }

    FileDialog
    {
        id: importDialog
        title: qsTr("Import keyboard shortcuts")
        fileMode: FileDialog.OpenFile
        nameFilters: [ qsTr("JSON files") + " (*.json)", qsTr("All files") + " (*)" ]

        onAccepted:
        {
            if (shortcutManager.importOverrides(selectedFile))
            {
                messagePopup.title = qsTr("Operation completed")
                messagePopup.message = qsTr("Shortcuts successfully imported")
            }
            else
            {
                messagePopup.title = qsTr("Error")
                messagePopup.message = qsTr("Unable to import shortcuts from the selected file")
            }
            messagePopup.open()
        }
    }

    ColumnLayout
    {
        anchors.fill: parent
        spacing: 0

        RowLayout
        {
            Layout.fillWidth: true
            Layout.preferredHeight: UISettings.iconSizeDefault * 1.4
            Layout.margins: 5
            spacing: 5

            RobotoText
            {
                Layout.fillWidth: true
                fontSize: UISettings.textSizeDefault
                fontBold: true
                label: qsTr("Keyboard Shortcuts")
            }

            GenericButton
            {
                height: UISettings.iconSizeDefault
                width: UISettings.iconSizeMedium * 4
                fontSize: UISettings.textSizeDefault
                label: qsTr("Load Defaults")
                onClicked: confirmLoadDefaultsPopup.open()
            }

            GenericButton
            {
                height: UISettings.iconSizeDefault
                width: UISettings.iconSizeMedium * 3
                fontSize: UISettings.textSizeDefault
                label: qsTr("Export")
                onClicked: exportDialog.open()
            }

            GenericButton
            {
                height: UISettings.iconSizeDefault
                width: UISettings.iconSizeMedium * 3
                fontSize: UISettings.textSizeDefault
                label: qsTr("Import")
                onClicked: importDialog.open()
            }

            GenericButton
            {
                height: UISettings.iconSizeDefault
                width: height
                border.color: UISettings.bgMedium
                useFontawesome: true
                label: FontAwesome.fa_xmark
                onClicked:
                {
                    cancelCapture()
                    mainView.loadResource("qrc:/FixturesAndFunctions.qml")
                }
            }
        }

        Rectangle
        {
            Layout.fillWidth: true
            height: 1
            color: UISettings.sectionHeaderDiv
        }

        ListView
        {
            id: actionsListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: displayItems

            delegate:
                Loader
                {
                    width: actionsListView.width
                    sourceComponent: modelData.isHeader ? headerDelegate : rowDelegate
                    onLoaded: item.rowData = modelData
                }
        }
    }

    Component
    {
        id: headerDelegate

        Rectangle
        {
            // Loader.onLoaded assigns the real value right after creation (see
            // the established Loader/sourceComponent idiom in
            // UISettingsEditor.qml's colorSelector) - this default just keeps
            // the very first binding evaluation, before that assignment runs,
            // from dereferencing undefined
            property var rowData: ({ label: "" })

            width: parent ? parent.width : 0
            height: UISettings.listItemHeight
            color: UISettings.sectionHeader

            RobotoText
            {
                anchors.fill: parent
                leftMargin: 8
                fontBold: true
                label: rowData.label
            }

            Rectangle
            {
                width: parent.width
                height: 1
                y: parent.height - 1
                color: UISettings.sectionHeaderDiv
            }
        }
    }

    Component
    {
        id: rowDelegate

        Rectangle
        {
            id: rowRoot

            // See headerDelegate's rowData comment above
            property var rowData: ({ action: { id: "", description: "", sequence: "", isDefault: true } })

            width: parent ? parent.width : 0
            height: UISettings.listItemHeight
            color: "transparent"

            readonly property bool listening: editorRoot.capturingId === rowData.action.id

            RowLayout
            {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                RobotoText
                {
                    Layout.fillWidth: true
                    label: rowData.action.description
                }

                Rectangle
                {
                    id: seqField
                    width: UISettings.iconSizeMedium * 5
                    height: UISettings.listItemHeight * 0.7
                    color: UISettings.bgLight
                    border.width: 1
                    border.color: UISettings.bgStrong

                    // Drives the "listening" flash via a plain boolean instead of a
                    // property-targeted Animation, which would otherwise permanently
                    // sever labelColor's binding to isDefault once triggered
                    property bool blinkPhase: false

                    Timer
                    {
                        interval: 500
                        repeat: true
                        running: listening
                        onTriggered: seqField.blinkPhase = !seqField.blinkPhase
                    }

                    RobotoText
                    {
                        anchors.centerIn: parent
                        labelColor: listening
                                    ? (seqField.blinkPhase ? "red" : UISettings.fgMain)
                                    : (rowData.action.isDefault ? UISettings.fgMain : UISettings.highlight)
                        label: listening ? qsTr("Press a key... (Esc to cancel)") : rowData.action.sequence
                    }

                    MouseArea
                    {
                        anchors.fill: parent
                        onClicked: editorRoot.startCapture(rowData.action.id)
                    }
                }

                IconButton
                {
                    width: UISettings.iconSizeMedium
                    height: width
                    enabled: !rowData.action.isDefault
                    imgSource: "qrc:/undo.svg"
                    tooltip: qsTr("Reset to default")
                    onClicked: shortcutManager.resetToDefault(rowData.action.id)
                }
            }
        }
    }
}
