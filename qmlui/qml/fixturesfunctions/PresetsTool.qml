/*
  Q Light Controller Plus
  PresetsTool.qml

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
import QtQuick.Window

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: toolRoot
    readonly property real tabItemWidth: UISettings.bigItemHeight * 1.3
    readonly property real minToolWidth: UISettings.bigItemHeight * 3
    readonly property real maxToolWidth: toolRoot.Window.window ?
                Math.max(minToolWidth, toolRoot.Window.width * 0.8) : minToolWidth
    width: Math.min(Math.max(minToolWidth, tabItemWidth * prList.count), maxToolWidth)
    height: presetsArea.height + (showPalette ? paletteBox.height : 0)
    color: UISettings.bgStrong
    border.color: UISettings.bgLight
    border.width: 2
    clip: true

    property bool closeOnSelect: false
    property alias presetModel: prList.model
    property int selectedFixture: -1
    property int selectedChannel: -1
    property bool showPalette: false
    property int paletteType: QLCPalette.Undefined
    property int currentValue: 0 // as DMX value
    property int currentPreset: QLCCapability.Custom
    property int rangeLowLimit: 0
    property int rangeHighLimit: 255

    signal presetSelected(QLCCapability cap, int fxID, int chIdx, int value)
    signal valueChanged(int value)

    function updatePresets(presetModel)
    {
        selectedFixture = -1
        prList.model = null // force reload
        prList.model = presetModel
    }

    MouseArea
    {
        anchors.fill: parent
        onWheel: { return false }
    }

    Item
    {
        id: presetsArea
        width: parent.width
        height: UISettings.bigItemHeight * 3

        // toolbar area containing the available preset channels
        Rectangle
        {
            id: presetToolBar
            width: parent.width
            height: UISettings.iconSizeDefault
            z: 10
            clip: true
            gradient: Gradient
            {
                GradientStop { position: 0; color: UISettings.toolbarStartSub }
                GradientStop { position: 1; color: UISettings.toolbarEnd }
            }

            // true when the tabs don't all fit even at toolRoot's capped width
            readonly property bool tabsOverflow: (toolRoot.tabItemWidth * prList.count) > presetToolBar.width

            ListView
            {
                id: prList
                anchors.fill: parent
                anchors.leftMargin: presetToolBar.tabsOverflow ? prevTabButton.width : 0
                anchors.rightMargin: presetToolBar.tabsOverflow ? nextTabButton.width : 0
                orientation: ListView.Horizontal
                boundsBehavior: Flickable.StopAtBounds

                delegate:
                    Rectangle
                    {
                        id: delegateRoot
                        width: toolRoot.tabItemWidth
                        height: presetToolBar.height
                        property bool isCurrentPreset: toolRoot.selectedFixture === fxID &&
                                                        toolRoot.selectedChannel === chIdx
                        color: isCurrentPreset ? UISettings.highlight :
                                (prMouseArea.pressed ? UISettings.bgLight : UISettings.bgMedium)
                        border.width: 1
                        border.color: isCurrentPreset ? UISettings.highlight : UISettings.bgLight

                        property int fxID: modelData.fixtureID
                        property int chIdx: modelData.channelIdx

                        Component.onCompleted:
                        {
                            if (selectedFixture === -1)
                            {
                                selectedFixture = fxID
                                selectedChannel = chIdx
                                capRepeater.model = fixtureManager.presetCapabilities(selectedFixture, selectedChannel)
                                prFlickable.contentY = 0
                            }
                        }

                        RobotoText
                        {
                            x: 3
                            width: parent.width - 6
                            height: parent.height
                            label: modelData.name
                            fontSize: UISettings.textSizeDefault * 0.70
                            wrapText: true
                        }
                        MouseArea
                        {
                            id: prMouseArea
                            anchors.fill: parent
                            hoverEnabled: true

                            onClicked:
                            {
                                selectedFixture = delegateRoot.fxID
                                selectedChannel = delegateRoot.chIdx
                                capRepeater.model = fixtureManager.presetCapabilities(selectedFixture, selectedChannel)
                                prFlickable.contentY = 0
                            }
                        }
                    }
            }

            IconButton
            {
                id: prevTabButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: UISettings.iconSizeDefault
                height: width
                visible: presetToolBar.tabsOverflow && !prList.atXBeginning
                faSource: FontAwesome.fa_angle_left
                tooltip: qsTr("Show previous channels")
                onClicked: prList.contentX = Math.max(0, prList.contentX - toolRoot.tabItemWidth)
            }

            IconButton
            {
                id: nextTabButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: UISettings.iconSizeDefault
                height: width
                visible: presetToolBar.tabsOverflow && !prList.atXEnd
                faSource: FontAwesome.fa_angle_right
                tooltip: qsTr("Show next channels")
                onClicked: prList.contentX = Math.min(prList.contentWidth - prList.width,
                                                       prList.contentX + toolRoot.tabItemWidth)
            }
        }

        // flickable layout containing the actual preset capabilities
        Flickable
        {
            id: prFlickable
            width: parent.width
            height: parent.height - presetToolBar.height
            y: presetToolBar.height
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: flowView.height

            Flow
            {
                id: flowView
                width: parent.width
                Repeater
                {
                    id: capRepeater
                    delegate: PresetCapabilityItem
                    {
                        capability: modelData
                        capIndex: index + 1
                        visible: (capability.min <= toolRoot.rangeHighLimit || capability.max <= toolRoot.rangeLowLimit)
                        onValueChanged: function(value)
                        {
                            var val = Math.min(Math.max(value, rangeLowLimit), rangeHighLimit)
                            toolRoot.currentValue = val
                            toolRoot.currentPreset = capability.preset
                            toolRoot.presetSelected(capability, selectedFixture, selectedChannel, val)
                            toolRoot.valueChanged(val)

                            if (toolRoot.showPalette)
                            {
                                let pct = (capability.max > capability.min)
                                    ? Math.round((val - capability.min) * 100 / (capability.max - capability.min))
                                    : 0
                                paletteBox.updateValues(capability.preset, pct)
                                paletteBox.updatePreview()
                            }

                            if (closeOnSelect)
                                toolRoot.visible = false
                        }
                    }
                }
            }
        }
    }

    PaletteFanningBox
    {
        id: paletteBox
        visible: toolRoot.showPalette
        y: presetsArea.height
        width: parent.width
        paletteType: toolRoot.paletteType
    }
}
