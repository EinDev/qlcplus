/*
  Q Light Controller Plus
  FixtureGroupManager.qml

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

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: fgmContainer
    objectName: "fixtureGroupManager"
    anchors.fill: parent
    color: "transparent"

    /** By default, fixtureManager is the model provider, unless a
      * specific provider is set here. In that case, modelProvider
      * must provide a 'groupsTreeModel' method */
    property var modelProvider: null
    property bool allowEditing: true
    property string previousView: ""
    property int currentItemType: -1

    signal doubleClicked(int ID, int type)

    function updateButtons(itemType, itemID)
    {
        if (itemType === App.ChannelDragItem || itemType === App.HeadDragItem)
            return

        // update info button
        infoButton.enabled = itemType !== App.HeadDragItem ? true : false
        updateInfoView(infoButton.checked)
        updateEditingView(editButton.checked)

        // update rename button
        renameButton.enabled = true

        // update linked button
        if (fixtureManager.propertyEditEnabled === false)
            return

        linkedButton.enabled = itemType === App.FixtureDragItem ? true : false
        var linkedIndex = fixtureManager.fixtureLinkedIndex(itemID)
        linkedButton.faSource = linkedIndex ? FontAwesome.fa_link_slash : FontAwesome.fa_link
    }

    function updateInfoView(checked)
    {
        if (editButton.checked)
            return

        if (checked)
        {
            if (gfhcDragItem.itemsList.length === 0)
                return

            currentItemType = gfhcDragItem.itemsList[0].itemType
            if (previousView == "")
                previousView = fixtureAndFunctions.currentViewQML

            switch (currentItemType)
            {
                case App.UniverseDragItem:
                    fixtureManager.itemID = (gfhcDragItem.itemsList[0].cRef.id | (App.UniverseDragItem << 16))
                    fixtureAndFunctions.currentViewQML = "qrc:/UniverseSummary.qml"
                break
                case App.FixtureGroupDragItem:
                    fixtureManager.itemID = (gfhcDragItem.itemsList[0].cRef.id | (App.FixtureGroupDragItem << 16))
                    fixtureAndFunctions.currentViewQML = "qrc:/UniverseSummary.qml"
                break
                case App.FixtureDragItem:
                    fixtureManager.itemID = gfhcDragItem.itemsList[0].itemID
                    fixtureAndFunctions.currentViewQML = "qrc:/FixtureSummary.qml"
                break
            }
        }
        else
        {
            if (previousView != "")
            {
                fixtureAndFunctions.currentViewQML = previousView
                currentItemType = -1
                previousView = ""
            }
        }
    }

    function updateEditingView(checked)
    {
        if (infoButton.checked)
            return

        if (checked)
            currentItemType = gfhcDragItem.itemsList[0].itemType

        switch(currentItemType)
        {
            case App.UniverseDragItem:
            case App.FixtureDragItem:
            {
                if (previousView != "")
                {
                    fixtureAndFunctions.currentViewQML = previousView
                    previousView = ""
                }

                if (fixtureManager.propertyEditEnabled !== checked)
                {
                    if (checked)
                        leftSidePanel.width += UISettings.sidePanelWidth
                    else
                        leftSidePanel.width -= UISettings.sidePanelWidth

                    fixtureManager.propertyEditEnabled = checked
                }
            }
            break
            case App.FixtureGroupDragItem:
            {
                if (checked)
                {
                    if (previousView == "")
                        previousView = fixtureAndFunctions.currentViewQML
                    fixtureGroupEditor.setEditGroup(gfhcDragItem.itemsList[0].cRef)
                    fixtureAndFunctions.currentViewQML = "qrc:/FixtureGroupEditor.qml"
                }
                else
                {
                    fixtureGroupEditor.setEditGroup(null)
                    fixtureAndFunctions.currentViewQML = previousView
                }
            }
            break
        }

        if (!checked)
            currentItemType = -1
    }

    function showChannelModifierEditor(itemID, channelIndex, modifierName)
    {
        chModifierEditor.itemID = itemID
        chModifierEditor.chIndex = channelIndex
        chModifierEditor.modName = modifierName
        chModifierEditor.open()
    }

    /** Open the rename popup for the currently selected item, same as
     *  clicking renameButton. Used by the global F2 shortcut (see
     *  Connections below) - returns false if there is nothing selected here. */
    function renameSelected()
    {
        if (allowEditing === false || gfhcDragItem.itemsList.length === 0)
            return false

        renamePopup.showNumbering = gfhcDragItem.itemsList.length > 1 ? true : false
        renamePopup.editText = gfhcDragItem.itemsList[0].textLabel
        renamePopup.open()
        return true
    }

    /** Show and focus the Group/Fixture search field. Used by the global
     *  Ctrl+F shortcut (see Connections below). */
    function focusSearch()
    {
        searchItem.checked = true
        sTextInput.forceActiveFocus()
    }

    /** Select every currently-visible flat-tree row between $fromRow and
     *  $toRow (inclusive), the same way individually Ctrl-clicking each of
     *  them would: additive to whatever is already selected, keeping the
     *  tree's isSelected role, contextManager's fixture/group selection and
     *  the drag payload (gfhcDragItem.itemsList) all in sync. Used by
     *  FixtureGroupFlatDelegate.qml to implement Shift range-select. Rows
     *  hidden behind a collapsed ancestor aren't part of the flat model at
     *  all, so (like a real file explorer) they're simply not reachable by
     *  a range that spans a collapsed group. */
    function selectFlatRange(fromRow, toRow)
    {
        var lo = Math.min(fromRow, toRow)
        var hi = Math.max(fromRow, toRow)

        for (var i = lo; i <= hi; i++)
        {
            var rIdx = flatGroupsModel.index(i, 0)
            if (flatGroupsModel.data(rIdx, TreeFlatModel.IsSelectedRole))
                continue // already selected - avoid a duplicate itemsList entry

            flatGroupsModel.setData(rIdx, 2, TreeFlatModel.IsSelectedRole)

            var rType = flatGroupsModel.data(rIdx, TreeFlatModel.TypeRole)
            var rClassRef = flatGroupsModel.data(rIdx, TreeFlatModel.ClassRefRole)
            var rId = flatGroupsModel.data(rIdx, TreeFlatModel.IdRole)
            var rSubId = flatGroupsModel.data(rIdx, TreeFlatModel.SubIdRole)
            var rChIdx = flatGroupsModel.data(rIdx, TreeFlatModel.ChIdxRole)
            var rInGroup = flatGroupsModel.data(rIdx, TreeFlatModel.InGroupRole)
            var rLabel = flatGroupsModel.data(rIdx, TreeFlatModel.LabelRole)

            switch (rType)
            {
                case App.FixtureDragItem:
                    contextManager.setFixtureSelection(rId, -1, true)
                break
                case App.HeadDragItem:
                    contextManager.setFixtureSelection(rId, rChIdx, true)
                break
                case App.UniverseDragItem:
                    contextManager.setFixtureGroupSelection(rClassRef ? rClassRef.id : -1, true, true)
                break
                case App.FixtureGroupDragItem:
                    contextManager.setFixtureGroupSelection(rClassRef ? rClassRef.id : -1, true, false)
                break
            }

            // Universe/Group rows (depth 0) don't carry an "id" role value of
            // their own - like TreeNodeRow.qml, their identity comes from cRef
            var itemID = (rType === App.UniverseDragItem || rType === App.FixtureGroupDragItem) ?
                        (rClassRef ? rClassRef.id : -1) : rId

            gfhcDragItem.itemsList.push({
                itemType: rType,
                cRef: rClassRef,
                itemID: itemID,
                subID: rSubId,
                headIndex: rChIdx,
                chIndex: rChIdx,
                textLabel: rLabel,
                inGroup: rInGroup
            })
        }
    }

    // Note: since both this panel and RightPanel.qml's Function Manager side
    // can be open at once, both react independently to "item.renameSelected"/
    // "ff.focusSearch" - if both sides have a selection/are open simultaneously,
    // both popups/focus calls can fire for the same keypress. Not resolved here.
    Connections
    {
        target: shortcutManager
        function onActionTriggered(actionId)
        {
            if (actionId === "item.renameSelected")
                renameSelected()
            else if (actionId === "ff.focusSearch")
                focusSearch()
        }
    }

    CustomPopupDialog
    {
        id: fmGenericPopup
        visible: false
        title: qsTr("Error")
        message: ""
        onAccepted: {}
    }

    PopupChannelModifiers
    {
        id: chModifierEditor
        visible: false

        onAccepted: fixtureManager.setChannelModifier(itemID, chIndex)
    }

    ColumnLayout
    {
        anchors.fill: parent
        spacing: 0

        Rectangle
        {
            id: topBar
            implicitWidth: fgmContainer.width
            implicitHeight: UISettings.iconSizeMedium
            z: 5
            gradient: Gradient
            {
                GradientStop { position: 0; color: UISettings.toolbarStartSub }
                GradientStop { position: 1; color: UISettings.toolbarEnd }
            }

            RowLayout
            {
                id: topBarRowLayout
                width: parent.width
                y: 1

                spacing: 4

                IconButton
                {
                    id: addGrpButton
                    visible: allowEditing
                    z: 2
                    width: height
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_plus
                    faColor: "limegreen"
                    tooltip: qsTr("Add a new fixture group")
                    onClicked: contextManager.createFixtureGroup()
                }

                IconButton
                {
                    id: invertGroupSelectionButton
                    z: 2
                    width: height
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_retweet
                    faColor: "white"
                    tooltip: qsTr("Invert Selection in Group(s)") + " (CTRL+G)"
                    enabled: contextManager.selectedFixturesCount > 0
                    onClicked: contextManager.invertGroupSelection()
                }

                IconButton
                {
                    id: delItemButton
                    visible: allowEditing
                    z: 2
                    width: height
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_minus
                    faColor: "crimson"
                    tooltip: qsTr("Remove the selected items")
                    onClicked:
                    {
                        if (gfhcDragItem.itemsList.length === 0)
                            return

                        var fxDeleteList = []
                        var fxGroupDeleteList = []

                        for (var i = 0; i < gfhcDragItem.itemsList.length; i++)
                        {
                            var item = gfhcDragItem.itemsList[i]

                            switch (item.itemType)
                            {
                                case App.UniverseDragItem:
                                break
                                case App.FixtureGroupDragItem:
                                    fxGroupDeleteList.push(item.cRef.id)
                                break
                                case App.FixtureDragItem:
                                    if (item.inGroup)
                                        fixtureManager.deleteFixtureInGroup(item.subID, item.itemID, item.nodePath)
                                    else
                                        fxDeleteList.push(item.itemID)
                                break
                            }
                        }

                        if (fxDeleteList.length)
                        {
                            contextManager.resetFixtureSelection()
                            fixtureManager.deleteFixtures(fxDeleteList)
                        }

                        if (fxGroupDeleteList.length)
                            fixtureManager.deleteFixtureGroups(fxGroupDeleteList)
                    }
                }

                IconButton
                {
                    visible: !allowEditing || fixtureManager.propertyEditEnabled
                    z: 2
                    width: height
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_check_double
                    //faColor: UISettings.fgMain
                    tooltip: qsTr("Apply changes to fixtures of the same type")
                    checkable: true

                    onToggled: modelProvider ? modelProvider.applyToSameType(checked) :
                                               fixtureManager.applyToSameType(checked)
                }

                // Spacer
                Rectangle { Layout.fillWidth: true }

                IconButton
                {
                    id: searchItem
                    z: 2
                    width: height
                    height: topBar.height - 2
                    bgColor: UISettings.bgMedium
                    faColor: checked ? "white" : "gray"
                    faSource: FontAwesome.fa_magnifying_glass
                    checkable: true
                    tooltip: qsTr("Set a Group/Fixture/Channel search filter")
                    onToggled:
                    {
                        fixtureManager.searchFilter = ""
                        if (checked)
                            sTextInput.forceActiveFocus()
                    }
                }

                IconButton
                {
                    id: renameButton
                    visible: allowEditing
                    z: 2
                    width: height
                    height: topBar.height - 2
                    imgSource: "qrc:/rename.svg"
                    tooltip: qsTr("Rename the selected items")
                    enabled: false

                    onClicked:
                    {
                        renamePopup.showNumbering = gfhcDragItem.itemsList.length > 1 ? true : false
                        renamePopup.editText = gfhcDragItem.itemsList[0].textLabel
                        renamePopup.open()
                    }

                    PopupRenameItems
                    {
                        id: renamePopup
                        title: qsTr("Rename items")
                        onAccepted:
                        {
                            var item
                            var ret

                            if (numberingEnabled)
                            {
                                var currNum = startNumber
                                var i, zeroes = ""

                                for (i = 0; i < digits; i++)
                                    zeroes += '0'

                                for (i = 0; i < gfhcDragItem.itemsList.length; i++)
                                {
                                    item = gfhcDragItem.itemsList[i]
                                    var zerofilled = (zeroes + currNum).slice(-digits);
                                    var finalName = editText + " " + zerofilled
                                    currNum++

                                    if (item.itemType === App.FixtureDragItem)
                                        ret = fixtureManager.renameFixture(item.itemID, finalName)
                                    else if (item.itemType === App.FixtureGroupDragItem)
                                        ret = fixtureManager.renameFixtureGroup(item.itemID, finalName)

                                    if (ret === false)
                                        break
                                }
                            }
                            else
                            {
                                item = gfhcDragItem.itemsList[0];

                                if (item.itemType === App.FixtureDragItem)
                                    ret = fixtureManager.renameFixture(item.itemID, editText)
                                else if (item.itemType === App.FixtureGroupDragItem)
                                    ret = fixtureManager.renameFixtureGroup(item.itemID, editText)
                            }

                            if (ret === false)
                            {
                                fmGenericPopup.message = qsTr("An item with the same name already exists.\nPlease provide a different name.")
                                fmGenericPopup.open()
                            }
                        }
                    }
                }

                IconButton
                {
                    id: infoButton
                    visible: allowEditing
                    z: 2
                    width: height
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_circle_info
                    faColor: "skyblue"
                    tooltip: qsTr("Inspect the selected item")
                    enabled: false
                    checkable: true

                    onToggled: updateInfoView(checked)
                }

                IconButton
                {
                    id: editButton
                    visible: allowEditing
                    width: height
                    height: topBar.height - 2
                    imgSource: "qrc:/edit.svg"
                    tooltip: qsTr("Toggle selected item editing")
                    checkable: true

                    onToggled: updateEditingView(checked)
                }

                IconButton
                {
                    id: linkedButton
                    visible: fixtureManager.propertyEditEnabled
                    enabled: false
                    width: height
                    height: topBar.height - 2
                    faSource: FontAwesome.fa_link
                    faColor: "white"
                    tooltip: qsTr("Add/Remove a linked fixture")
                    onClicked: contextManager.setLinkedFixture(gfhcDragItem.itemsList[0].itemID)
                }
            }
        } // RowLayout

        Rectangle
        {
            id: propertiesHeader
            visible: fixtureManager.propertyEditEnabled
            implicitHeight: UISettings.iconSizeMedium
            implicitWidth: fgmContainer.width - (gEditScrollBar.visible ? gEditScrollBar.width : 0)
            z: 5
            color: UISettings.bgMedium

            RowLayout
            {
                anchors.fill: parent

                RobotoText { label: qsTr("Name"); Layout.fillWidth: true; height: parent.height }
                Rectangle { width: 1; height: parent.height }
                RobotoText { label: qsTr("Mode"); width: UISettings.chPropsModesWidth; height: parent.height }
                Rectangle { width: 1; height: parent.height }
                RobotoText { label: qsTr("Flags"); width: UISettings.chPropsFlagsWidth; height: parent.height }
                Rectangle { width: 1; height: parent.height }
                RobotoText { label: qsTr("Can fade"); width: UISettings.chPropsCanFadeWidth; height: parent.height }
                Rectangle { width: 1; height: parent.height }
                RobotoText { label: qsTr("Behaviour"); width: UISettings.chPropsPrecedenceWidth; height: parent.height }
                Rectangle { width: 1; height: parent.height }
                RobotoText { label: qsTr("Modifier"); width: UISettings.chPropsModifierWidth; height: parent.height }
            }
        }

        Rectangle
        {
            id: searchBox
            visible: searchItem.checked
            width: fgmContainer.width
            implicitHeight: UISettings.iconSizeMedium
            z: 5
            color: UISettings.bgMedium
            radius: 5
            border.width: 2
            border.color: UISettings.borderColorDark

            TextInput
            {
                id: sTextInput
                y: 3
                height: parent.height - 6
                width: parent.width
                color: UISettings.fgMain
                text: modelProvider ? modelProvider.searchFilter : fixtureManager.searchFilter
                font.family: "Roboto Condensed"
                font.pixelSize: parent.height - 6
                selectionColor: UISettings.highlightPressed
                selectByMouse: true

                onTextEdited: modelProvider ? modelProvider.searchFilter = text : fixtureManager.searchFilter = text
            }
        }

        TreeFlatModel
        {
            id: flatGroupsModel
            sourceModel: modelProvider ? modelProvider.groupsTreeModel : fixtureManager.groupsTreeModel
        }

        Connections
        {
            target: modelProvider ? modelProvider : fixtureManager
            function onGroupsTreeModelChanged()
            {
                flatGroupsModel.rebuild()
                // row indices are meaningless across a rebuild - drop the stale anchor
                groupListView.shiftAnchorIndex = -1
            }
        }

        ListView
        {
            id: groupListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            z: 4
            boundsBehavior: Flickable.StopAtBounds

            property bool dragActive: false
            // Anchor row for Shift range-select (see FixtureGroupFlatDelegate.qml's
            // App.Clicked handler) - only updated by a plain or Ctrl click, same as
            // ModelSelector's previousIndex
            property int shiftAnchorIndex: -1

            model: flatGroupsModel
            delegate: FixtureGroupFlatDelegate
            {
                width: fgmContainer.width - (gEditScrollBar.visible ? gEditScrollBar.width : 0)
            }

            ScrollBar.vertical: CustomScrollBar {
                id: gEditScrollBar
            }

            // Group / Fixture / Head / Channel draggable item
            GenericMultiDragItem
            {
                id: gfhcDragItem

                visible: groupListView.dragActive

                Drag.active: groupListView.dragActive
                Drag.source: gfhcDragItem
                Drag.keys: [ "fixture" ]
            }
        } // ListView
    } // ColumnLayout
}
