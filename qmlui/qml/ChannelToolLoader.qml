/*
  Q Light Controller Plus
  ChannelToolLoader.qml

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
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Item
{
    id: itemRoot
    visible: false

    property int fixtureId: -1
    property int channelType: -1
    property int channelIndex: -1
    property int channelValue: 0
    property int yPos: 0
    property Item targetItem: null

    signal valueChanged(int fixtureID, int channelIndex, int value)

    // Positions this floating tool adjacent to targetItem (the channel icon that
    // opened it) on whichever side fits within parentItem's bounds, without
    // overlapping targetItem itself. Each of the four candidates must be
    // computed from targetItem's own position/size so it sits flush against the
    // icon's near edge:
    //   rightX = icon's right edge (posInParent.x + targetItem.width)
    //   leftX  = icon's left edge minus the tool's own width (posInParent.x - toolWidth)
    //   belowY = icon's bottom edge (posInParent.y + targetItem.height)
    //   aboveY = icon's top edge minus the tool's own height (posInParent.y - toolHeight)
    // It's easy to get one of these backwards - e.g. reusing a fixed icon size
    // instead of targetItem.width/height, or basing an offset on the icon's own
    // near edge instead of its far edge - and end up with the popup rendered on
    // top of (rather than beside) the icon that triggered it, since nothing here
    // will visibly fail; it just silently overlaps.
    function adjustToolPosition()
    {
        var parentItem = itemRoot.parent
        if (!parentItem || !targetItem)
            return

        var posInParent = targetItem.mapToItem(parentItem, 0, 0)
        var toolWidth = Math.max(itemRoot.width, itemRoot.implicitWidth)
        var toolHeight = Math.max(itemRoot.height, itemRoot.implicitHeight)

        var rightX = posInParent.x + targetItem.width
        var leftX = posInParent.x - toolWidth
        var preferredX = (rightX + toolWidth <= parentItem.width) ? rightX : leftX

        var belowY = posInParent.y + targetItem.height
        var aboveY = posInParent.y - toolHeight
        var preferredY = (belowY + toolHeight <= parentItem.height) ? belowY : aboveY

        x = Math.max(0, Math.min(preferredX, parentItem.width - toolWidth))
        y = Math.max(0, Math.min(preferredY, parentItem.height - toolHeight))
    }

    function closeChannelTool()
    {
        itemRoot.visible = false
        toolLoader.source = ""
        targetItem = null
        fixtureId = -1
        channelType = -1
        channelIndex = -1
    }

    function loadChannelTool(cItem, fxId, chIdx, val)
    {
        if (itemRoot.visible && fixtureId === fxId && channelIndex === chIdx)
        {
            closeChannelTool()
            return
        }

        channelType = fixtureManager.channelType(fxId, chIdx)

        if (channelType === QLCChannel.NoGroup || channelType === QLCChannel.Nothing)
            return

        targetItem = cItem
        toolLoader.source = ""
        fixtureId = fxId
        channelIndex = chIdx
        channelValue = val

        switch (channelType)
        {
            case QLCChannel.Intensity:
                toolLoader.source = "qrc:/IntensityTool.qml"
            break
            case QLCChannel.Pan:
            case QLCChannel.Tilt:
                toolLoader.source = "qrc:/SingleAxisTool.qml"
            break
            case QLCChannel.Colour:
            case QLCChannel.Gobo:
            case QLCChannel.Speed:
            case QLCChannel.Shutter:
            case QLCChannel.Prism:
            case QLCChannel.Beam:
            case QLCChannel.Effect:
            case QLCChannel.Maintenance:
                toolLoader.source = "qrc:/PresetsTool.qml"
            break
            default:
                toolLoader.source = "qrc:/ColorToolPrimary.qml"
            break
        }
    }

    Loader
    {
        id: toolLoader

        onLoaded:
        {
            item.showPalette = false
            if (item.hasOwnProperty('dragTarget'))
                item.dragTarget = itemRoot

            if (channelType >= 0xFF)
            {
                item.currentValue = itemRoot.channelValue
                item.targetColor = itemRoot.channelType
            }
            else if (channelType === QLCChannel.Intensity)
            {
                item.setValue(itemRoot.channelValue)
            }
            else if (channelType === QLCChannel.Pan ||
                     channelType === QLCChannel.Tilt)
            {
                item.currentValue = itemRoot.channelValue
                item.maxDegrees = fixtureManager.channelDegrees(itemRoot.fixtureId, itemRoot.channelIndex)
            }
            else
            {
                // for PresetsTool this sets the tab model, which its width depends
                // on - must happen before itemRoot picks up the tool's width below
                item.updatePresets(fixtureManager.presetChannel(itemRoot.fixtureId, itemRoot.channelIndex))
            }

            itemRoot.width = width
            itemRoot.height = height
            itemRoot.adjustToolPosition()

            item.closeOnSelect = true
            itemRoot.visible = true
        }

        Connections
        {
            ignoreUnknownSignals: true
            target: toolLoader.item
            function onValueChanged(value)
            {
                itemRoot.valueChanged(itemRoot.fixtureId, itemRoot.channelIndex, value)
            }
            function onClose()
            {
                itemRoot.closeChannelTool()
            }
        }
    }
}
