/*
  Q Light Controller Plus
  InputChannelFlatDelegate.qml

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

/* Single, fixed-height row for PopupManualInputSource.qml's channelListView (one
 * "Universe: Profile" folder level, then a flat list of input channels underneath), one
 * row per currently-visible entry of the flat TreeFlatModel. Folder nodes are
 * force-expanded on load (there is no double-click-to-expand wired for this popup at all,
 * so this is the only way any channel is ever reachable) - safe to do from a Loader here
 * since a flat, uniform-height ListView only recreates a row's Loader through normal
 * virtualization, unlike the erratic recreation the old recursive delegate this replaces
 * was prone to. */
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
        source: model.depth === 0 ? "qrc:/TreeNodeRow.qml" : "qrc:/InputChannelDelegate.qml"

        onLoaded:
        {
            item.cRef = Qt.binding(function() { return model.classRef })
            item.textLabel = Qt.binding(function() { return model.label })
            item.isSelected = Qt.binding(function() { return model.isSelected })

            if (model.depth === 0)
            {
                item.itemIcon = "qrc:/group.svg"
                item.itemType = Qt.binding(function() { return model.type })
                item.nodePath = Qt.binding(function() { return model.path })

                if (model.type === App.UniverseDragItem)
                    model.isExpanded = true
            }
            else
            {
                item.itemID = Qt.binding(function() { return model.id })
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
                        if (qItem.itemType === App.ChannelDragItem)
                        {
                            popupRoot.universe = qItem.itemID >> 16
                            popupRoot.channel = qItem.itemID & 0x0000FFFF
                        }
                    break;
                }
            }
        }
    }
}
