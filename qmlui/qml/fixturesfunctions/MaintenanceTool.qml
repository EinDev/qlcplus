/*
  Q Light Controller Plus
  MaintenanceTool.qml

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

// Unlike PresetsTool (a single-selection picker: one channel's capabilities
// are shown at a time, which fits Shutter/Gobo/ColorWheel where you're
// picking ONE mutually-exclusive setting), Maintenance channels are
// independent, fixture-specific functions (fan speed, lamp reset, display
// brightness...) that make sense to see and adjust simultaneously. This
// shows every channel at once, grouped by fixture model (parsed from the
// "<model> - <channel name>" label FixtureManager::presetsChannels() already
// produces), with a capability picker for channels that have named presets
// and a plain 0-255 fader for ones that don't.
Rectangle
{
    id: toolRoot
    width: UISettings.bigItemHeight * 4
    height: Math.min(flick.contentHeight, UISettings.bigItemHeight * 8)
    color: UISettings.bgStrong
    border.color: UISettings.bgLight
    border.width: 2
    clip: true

    property var m_rawModel: null
    property var m_groups: []

    function updatePresets(presetModel)
    {
        m_rawModel = presetModel
        rebuildGroups()
    }

    function rebuildGroups()
    {
        var groupsMap = {}
        var order = []

        if (m_rawModel)
        {
            for (var i = 0; i < m_rawModel.length; i++)
            {
                var entry = m_rawModel[i]
                var sepIdx = entry.name.indexOf(" - ")
                var header = sepIdx >= 0 ? entry.name.substring(0, sepIdx) : entry.name
                var chName = sepIdx >= 0 ? entry.name.substring(sepIdx + 3) : entry.name

                if (!(header in groupsMap))
                {
                    groupsMap[header] = { header: header, channels: [] }
                    order.push(header)
                }

                var caps = fixtureManager.presetCapabilities(entry.fixtureID, entry.channelIdx)

                groupsMap[header].channels.push({
                    name: chName,
                    fixtureID: entry.fixtureID,
                    channelIdx: entry.channelIdx,
                    capabilities: caps
                })
            }
        }

        var groups = []
        for (var g = 0; g < order.length; g++)
            groups.push(groupsMap[order[g]])

        m_groups = groups
    }

    MouseArea
    {
        anchors.fill: parent
        onWheel: { return false }
    }

    Flickable
    {
        id: flick
        anchors.fill: parent
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: groupsColumn.height

        Column
        {
            id: groupsColumn
            width: flick.width
            spacing: 2

            Repeater
            {
                model: toolRoot.m_groups

                delegate: Column
                {
                    width: groupsColumn.width

                    Rectangle
                    {
                        width: parent.width
                        height: UISettings.listItemHeight * 0.8
                        color: UISettings.bgStronger

                        RobotoText
                        {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 4
                            width: parent.width - 8
                            label: modelData.header
                            fontSize: UISettings.textSizeDefault * 0.85
                            fontBold: true
                            wrapText: true
                        }
                    }

                    Repeater
                    {
                        model: modelData.channels

                        delegate: Column
                        {
                            id: channelDelegate
                            width: groupsColumn.width

                            readonly property int chFixtureID: modelData.fixtureID
                            readonly property int chChannelIdx: modelData.channelIdx

                            RobotoText
                            {
                                x: 4
                                width: parent.width - 8
                                label: modelData.name
                                fontSize: UISettings.textSizeDefault * 0.75
                                wrapText: true
                            }

                            // channel has named capabilities: pick one
                            Flow
                            {
                                width: parent.width - 8
                                x: 4
                                visible: modelData.capabilities.length > 0

                                Repeater
                                {
                                    model: modelData.capabilities

                                    delegate: PresetCapabilityItem
                                    {
                                        capability: modelData
                                        capIndex: index + 1
                                        onValueChanged: function(value)
                                        {
                                            fixtureManager.setPresetValue(
                                                        channelDelegate.chFixtureID,
                                                        channelDelegate.chChannelIdx, value)
                                        }
                                    }
                                }
                            }

                            // no named capabilities: plain 0-255 fader
                            Row
                            {
                                width: parent.width - 8
                                x: 4
                                spacing: 4
                                visible: modelData.capabilities.length === 0
                                height: visible ? UISettings.iconSizeDefault : 0

                                QLCPlusFader
                                {
                                    id: rawFader
                                    width: parent.width - valueLabel.width - parent.spacing
                                    height: UISettings.iconSizeDefault
                                    from: 0
                                    to: 255
                                    value: 0
                                    onMoved:
                                    {
                                        var val = valueAt(position)
                                        valueLabel.label = val.toFixed(0)
                                        fixtureManager.setPresetValue(channelDelegate.chFixtureID,
                                                                       channelDelegate.chChannelIdx, val)
                                    }
                                }
                                RobotoText
                                {
                                    id: valueLabel
                                    width: UISettings.bigItemHeight * 0.5
                                    label: "0"
                                    fontSize: UISettings.textSizeDefault * 0.75
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
