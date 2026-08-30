/*
  Q Light Controller Plus
  FixtureGroupFlatDelegate.qml

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

import org.qlcplus.classes 1.0
import "."

/* Single, fixed-height row for FixtureGroupManager.qml's groupListView, one row per
 * currently-visible entry of the flat TreeFlatModel (depth 0 = Universe/Group,
 * depth 1 = Fixture, depth 2 = Head/Channel). Because every row is a real, uniform-height
 * ListView delegate now (instead of a recursively-nested, unvirtualized Repeater tree),
 * ListView's own contentHeight/scroll-position handling is simply correct.
 *
 * This consolidates what used to be three separate, nested "forward the event up" handlers
 * (in TreeNodeDelegate.qml, FixtureNodeDelegate.qml and FixtureGroupManager.qml itself)
 * into one, since there's only one level now. It relies on ids from its enclosing
 * FixtureGroupManager.qml tree (gfhcDragItem, groupListView, fgmContainer, mainView,
 * fixtureManager, contextManager) being visible the same way TreeNodeDelegate.qml/
 * FixtureNodeDelegate.qml already relied on them from separate files. */
Item
{
    id: flatRow
    height: UISettings.listItemHeight

    readonly property int rowDepth: model.depth
    readonly property int rowIndent: rowDepth * 20

    Loader
    {
        id: rowLoader
        x: flatRow.rowIndent
        y: 0
        width: flatRow.width - x
        height: flatRow.height
        source:
        {
            if (rowDepth === 0)
                return "qrc:/TreeNodeRow.qml"
            if (rowDepth === 1)
                return "qrc:/FixtureNodeRow.qml"
            return model.type === App.ChannelDragItem ? "qrc:/FixtureChannelDelegate.qml" : "qrc:/FixtureHeadDelegate.qml"
        }

        onLoaded:
        {
            item.textLabel = Qt.binding(function() { return model.label })
            item.itemType = Qt.binding(function() { return model.type })
            item.isSelected = Qt.binding(function() { return model.isSelected })

            if (rowDepth === 0)
            {
                // top-level Universe/Group row: matches the previous behavior of never
                // wiring isCheckable here, even though the underlying model role may be set
                item.cRef = Qt.binding(function() { return model.classRef })
                item.itemIcon = "qrc:/group.svg"
                item.nodePath = Qt.binding(function() { return model.path })
                item.dragItem = gfhcDragItem
            }
            else if (rowDepth === 1)
            {
                item.cRef = Qt.binding(function() { return model.classRef })
                item.itemID = Qt.binding(function() { return model.id })
                item.subID = Qt.binding(function() { return model.subid })
                item.inGroup = Qt.binding(function() { return model.inGroup })
                item.isCheckable = Qt.binding(function() { return model.isCheckable })
                item.isChecked = Qt.binding(function() { return model.isChecked })
                item.nodePath = Qt.binding(function() { return model.path })
                item.hasChildren = Qt.binding(function() { return model.hasChildren })
                item.dragItem = gfhcDragItem

                if (model.flags !== undefined)
                {
                    item.showFlags = true
                    item.itemFlags = Qt.binding(function() { return model.flags })
                }
            }
            else if (model.type === App.ChannelDragItem)
            {
                item.itemID = Qt.binding(function() { return model.id })
                item.chIndex = Qt.binding(function() { return model.chIdx })
                item.isCheckable = Qt.binding(function() { return model.isCheckable })
                item.isChecked = Qt.binding(function() { return model.isChecked })
                item.itemIcon = model.classRef ? fixtureManager.channelIcon(model.classRef.id, model.chIdx) : ""
                // channels aren't draggable (no dragItem wiring), matching the original

                if (model.flags !== undefined)
                {
                    item.showFlags = true
                    item.itemFlags = Qt.binding(function() { return model.flags })
                    item.canFade = Qt.binding(function() { return model.canFade })
                    item.precedence = Qt.binding(function() { return model.precedence })
                    item.modifier = Qt.binding(function() { return model.modifier })
                }
            }
            else // HeadDragItem
            {
                item.itemID = Qt.binding(function() { return model.id })
                item.headIndex = Qt.binding(function() { return model.chIdx })
                item.isCheckable = Qt.binding(function() { return model.isCheckable })
                item.isChecked = Qt.binding(function() { return model.isChecked })
                item.dragItem = gfhcDragItem
            }
        }

        Connections
        {
            target: rowLoader.item

            function onMouseEvent(type, iID, iType, qItem, mouseMods)
            {
                switch (type)
                {
                    case App.Pressed:
                        var posnInWindow = qItem.mapToItem(mainView, qItem.x, qItem.y)
                        gfhcDragItem.parent = mainView
                        gfhcDragItem.x = posnInWindow.x - (gfhcDragItem.width / 4)
                        gfhcDragItem.y = posnInWindow.y - (gfhcDragItem.height / 4)
                        if (!qItem.isSelected)
                        {
                            if ((mouseMods & Qt.ControlModifier) == 0)
                                gfhcDragItem.itemsList = []

                            gfhcDragItem.itemsList.push(qItem)

                            if (gfhcDragItem.itemsList.length === 1)
                            {
                                gfhcDragItem.itemLabel = qItem.textLabel
                                if (qItem.hasOwnProperty("itemIcon"))
                                    gfhcDragItem.itemIcon = qItem.itemIcon
                                else
                                    gfhcDragItem.itemIcon = ""
                            }
                        }
                    break;
                    case App.Clicked:
                        if ((mouseMods & Qt.ShiftModifier) && groupListView.shiftAnchorIndex >= 0)
                        {
                            // Range-select from the last plain/Ctrl-clicked row to this
                            // one, additive to whatever is already selected - same
                            // semantics as ModelSelector's Shift handling elsewhere
                            fgmContainer.selectFlatRange(groupListView.shiftAnchorIndex, index)
                            fgmContainer.updateButtons(qItem.itemType, qItem.itemType === App.HeadDragItem ? qItem.itemID : iID)
                            break;
                        }

                        model.isSelected = (mouseMods & Qt.ControlModifier) ? 2 : 1

                        if (!(mouseMods & Qt.ControlModifier))
                            contextManager.resetFixtureSelection()

                        var itemID = iID

                        switch (qItem.itemType)
                        {
                            case App.FixtureDragItem:
                                contextManager.setFixtureSelection(iID, -1, true)
                            break
                            case App.HeadDragItem:
                                itemID = qItem.itemID
                                contextManager.setFixtureSelection(qItem.itemID, iID, true)
                            break
                            case App.UniverseDragItem:
                                contextManager.setFixtureGroupSelection(iID, true, true)
                            break
                            case App.FixtureGroupDragItem:
                                contextManager.setFixtureGroupSelection(iID, true, false)
                            break
                        }

                        fgmContainer.updateButtons(qItem.itemType, itemID)
                        groupListView.shiftAnchorIndex = index
                    break;
                    case App.DoubleClicked:
                        if (model.hasChildren)
                            model.isExpanded = !model.isExpanded
                        else if (!fgmContainer.allowEditing && qItem.itemType === App.FixtureDragItem)
                            fgmContainer.doubleClicked(iID, qItem.itemType)
                    break;
                    case App.DragStarted:
                        if (!model.isSelected)
                        {
                            model.isSelected = 1
                            mouseMods = -1
                        }
                        groupListView.dragActive = true
                    break;
                    case App.DragFinished:
                        gfhcDragItem.Drag.drop()
                        gfhcDragItem.parent = groupListView
                        gfhcDragItem.x = 0
                        gfhcDragItem.y = 0
                        groupListView.dragActive = false
                    break;
                    case App.Checked:
                        model.isChecked = iType
                    break;
                }
            }
        }

        Connections
        {
            ignoreUnknownSignals: true
            target: rowLoader.item

            function onPathChanged(oldPath, newPath)
            {
                for (var i = 0; i < gfhcDragItem.itemsList.length; i++)
                {
                    var listItem = gfhcDragItem.itemsList[i]
                    if (listItem.itemType === App.FixtureGroupDragItem)
                    {
                        fixtureManager.renameFixtureGroup(listItem.itemID, newPath)
                        return
                    }
                }
            }
        }
    }
}
