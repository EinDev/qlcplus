/*
  Q Light Controller Plus
  ImportGroupsFlatDelegate.qml

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

/* Single, fixed-height row for PopupImportProject.qml's groupListView (Universe ->
 * Fixture -> Head/Channel, same shape as the Fixture Groups tree elsewhere), one row per
 * currently-visible entry of the flat TreeFlatModel. Unlike FixtureGroupManager's tree,
 * this one is a pure checkbox-selection UI (no drag, no rename): checkboxes appear at
 * every depth, and Universe nodes are force-expanded on load since double-click-to-expand
 * was never wired for this popup's top level - without the auto-expand there would be no
 * way to reveal any fixture to check. That's safe to do from a Loader now (unlike the old
 * per-scroll-recreated recursive delegate this replaces) since a flat, uniform-height
 * ListView only recreates a row's Loader through normal virtualization, not the erratic
 * recreation that made the same pattern cause visible scrollbar jank before. */
Item
{
    id: flatRow
    height: UISettings.listItemHeight

    readonly property int rowIndent: model.depth * 20

    Loader
    {
        id: rowLoader
        x: flatRow.rowIndent
        y: 0
        width: flatRow.width - x
        height: flatRow.height
        source:
        {
            if (model.depth === 0)
                return "qrc:/TreeNodeRow.qml"
            if (model.depth === 1)
                return "qrc:/FixtureNodeRow.qml"
            return model.type === App.ChannelDragItem ? "qrc:/FixtureChannelDelegate.qml" : "qrc:/FixtureHeadDelegate.qml"
        }

        onLoaded:
        {
            item.textLabel = Qt.binding(function() { return model.label })
            item.isSelected = Qt.binding(function() { return model.isSelected })
            item.isCheckable = Qt.binding(function() { return model.isCheckable })
            item.isChecked = Qt.binding(function() { return model.isChecked })

            if (model.depth === 0)
            {
                item.cRef = Qt.binding(function() { return model.classRef })
                item.itemIcon = "qrc:/group.svg"
                item.itemType = Qt.binding(function() { return model.type })
                item.nodePath = Qt.binding(function() { return model.path })

                if (model.type === App.UniverseDragItem)
                    model.isExpanded = true
            }
            else if (model.depth === 1)
            {
                item.cRef = Qt.binding(function() { return model.classRef })
                item.itemID = Qt.binding(function() { return model.id })
                item.subID = Qt.binding(function() { return model.subid })
                item.inGroup = Qt.binding(function() { return model.inGroup })
                item.nodePath = Qt.binding(function() { return model.path })
                item.hasChildren = Qt.binding(function() { return model.hasChildren })

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
                item.itemIcon = model.classRef ? fixtureManager.channelIcon(model.classRef.id, model.chIdx) : ""

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
            }
        }

        Connections
        {
            target: rowLoader.item

            function onMouseEvent(type, iID, iType, qItem, mouseMods)
            {
                switch (type)
                {
                    case App.Clicked:
                        model.isSelected = (mouseMods & Qt.ControlModifier) ? 2 : 1
                    break;
                    case App.Checked:
                        model.isChecked = iType
                    break;
                }
            }
        }
    }
}
