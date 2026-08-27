/*
  Q Light Controller Plus
  PopupArrangeFixtures.qml

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

import "."

CustomPopupDialog
{
    id: popupRoot
    width: mainView.width / 3
    title: qsTr("Arrange fixtures")
    footer.visible: false

    // 0 = circle, 1 = grid, 2 = line
    property int arrangeMode: 0

    property real circleDiameter: 2000
    property real gridWidth: 2000
    property real gridHeight: 2000
    property int gridColumns: 0 // 0 = auto (near-square)
    property real gridAngle: 0
    property real lineLength: 2000
    property real lineAngle: 0

    // Circle/Line only - Grid has no single well-defined shape to detect a
    // size/angle from, or a middle to face fixtures towards.
    property bool detectFromPlacement: false
    property bool lookAtCenter: false

    function applyDetectedValues()
    {
        if (popupRoot.arrangeMode === 0)
        {
            popupRoot.circleDiameter = Math.max(circleSlider.from, Math.min(circleSlider.to,
                                                 contextManager.detectedCircleDiameter()))
        }
        else if (popupRoot.arrangeMode === 2)
        {
            popupRoot.lineLength = Math.max(lineLengthSlider.from, Math.min(lineLengthSlider.to,
                                             contextManager.detectedLineLength()))
            popupRoot.lineAngle = contextManager.detectedLineAngle()
        }
    }

    contentItem:
        ColumnLayout
        {
            width: popupRoot.width
            spacing: 8

            RowLayout
            {
                Layout.fillWidth: true
                Layout.margins: 4
                spacing: 4

                IconButton
                {
                    checkable: true
                    checked: popupRoot.arrangeMode === 0
                    faSource: FontAwesome.fa_circle
                    faColor: "white"
                    tooltip: qsTr("Circle")
                    onClicked:
                    {
                        popupRoot.arrangeMode = 0
                        if (popupRoot.detectFromPlacement)
                            popupRoot.applyDetectedValues()
                    }
                }
                IconButton
                {
                    checkable: true
                    checked: popupRoot.arrangeMode === 1
                    faSource: FontAwesome.fa_table_cells
                    faColor: "white"
                    tooltip: qsTr("Grid")
                    onClicked: popupRoot.arrangeMode = 1
                }
                IconButton
                {
                    checkable: true
                    checked: popupRoot.arrangeMode === 2
                    faSource: FontAwesome.fa_grip_lines
                    faColor: "white"
                    tooltip: qsTr("Line")
                    onClicked:
                    {
                        popupRoot.arrangeMode = 2
                        if (popupRoot.detectFromPlacement)
                            popupRoot.applyDetectedValues()
                    }
                }
            }

            // Detect from placement / face center - not available for Grid
            RowLayout
            {
                Layout.fillWidth: true
                Layout.margins: 4
                visible: popupRoot.arrangeMode !== 1
                spacing: 4

                IconButton
                {
                    checkable: true
                    checked: popupRoot.detectFromPlacement
                    faSource: FontAwesome.fa_crosshairs
                    faColor: "white"
                    tooltip: qsTr("Detect size/angle from the fixtures' current 3D placement, instead of the sliders below")
                    onClicked:
                    {
                        popupRoot.detectFromPlacement = !popupRoot.detectFromPlacement
                        if (popupRoot.detectFromPlacement)
                            popupRoot.applyDetectedValues()
                    }
                }
                RobotoText
                {
                    label: qsTr("Detect from placement")
                }

                Item { Layout.fillWidth: true }

                IconButton
                {
                    checkable: true
                    checked: popupRoot.lookAtCenter
                    faSource: FontAwesome.fa_location_crosshairs
                    faColor: "white"
                    tooltip: qsTr("Rotate each fixture to face the center of the arrangement")
                    onClicked: popupRoot.lookAtCenter = !popupRoot.lookAtCenter
                }
                RobotoText
                {
                    label: qsTr("Face center")
                }
            }

            // Circle controls
            ColumnLayout
            {
                Layout.fillWidth: true
                Layout.margins: 4
                visible: popupRoot.arrangeMode === 0
                spacing: 2

                RobotoText
                {
                    label: qsTr("Diameter: ") + circleSlider.value.toFixed(0) + " mm"
                }
                CustomSlider
                {
                    id: circleSlider
                    Layout.fillWidth: true
                    enabled: !popupRoot.detectFromPlacement
                    from: 100
                    to: 10000
                    value: popupRoot.circleDiameter
                    onValueChanged: popupRoot.circleDiameter = value
                }
            }

            // Grid controls
            ColumnLayout
            {
                Layout.fillWidth: true
                Layout.margins: 4
                visible: popupRoot.arrangeMode === 1
                spacing: 2

                RobotoText
                {
                    label: qsTr("Width: ") + gridWidthSlider.value.toFixed(0) + " mm"
                }
                CustomSlider
                {
                    id: gridWidthSlider
                    Layout.fillWidth: true
                    from: 100
                    to: 10000
                    value: popupRoot.gridWidth
                    onValueChanged: popupRoot.gridWidth = value
                }

                RobotoText
                {
                    label: qsTr("Height: ") + gridHeightSlider.value.toFixed(0) + " mm"
                }
                CustomSlider
                {
                    id: gridHeightSlider
                    Layout.fillWidth: true
                    from: 100
                    to: 10000
                    value: popupRoot.gridHeight
                    onValueChanged: popupRoot.gridHeight = value
                }

                RobotoText
                {
                    label: qsTr("Columns (0 = auto): ") + gridColumnsSpin.value
                }
                CustomSpinBox
                {
                    id: gridColumnsSpin
                    Layout.fillWidth: true
                    from: 0
                    to: 32
                    value: popupRoot.gridColumns
                    onValueModified: popupRoot.gridColumns = value
                }

                RobotoText
                {
                    label: qsTr("Angle: ") + gridAngleSlider.value.toFixed(0) + "°"
                }
                CustomSlider
                {
                    id: gridAngleSlider
                    Layout.fillWidth: true
                    from: -180
                    to: 180
                    value: popupRoot.gridAngle
                    onValueChanged: popupRoot.gridAngle = value
                }
            }

            // Line controls
            ColumnLayout
            {
                Layout.fillWidth: true
                Layout.margins: 4
                visible: popupRoot.arrangeMode === 2
                spacing: 2

                RobotoText
                {
                    label: qsTr("Length: ") + lineLengthSlider.value.toFixed(0) + " mm"
                }
                CustomSlider
                {
                    id: lineLengthSlider
                    Layout.fillWidth: true
                    enabled: !popupRoot.detectFromPlacement
                    from: 100
                    to: 10000
                    value: popupRoot.lineLength
                    onValueChanged: popupRoot.lineLength = value
                }

                RobotoText
                {
                    label: qsTr("Angle: ") + lineAngleSlider.value.toFixed(0) + "°"
                }
                CustomSlider
                {
                    id: lineAngleSlider
                    Layout.fillWidth: true
                    enabled: !popupRoot.detectFromPlacement
                    from: -180
                    to: 180
                    value: popupRoot.lineAngle
                    onValueChanged: popupRoot.lineAngle = value
                }
            }

            GenericButton
            {
                Layout.fillWidth: true
                Layout.margins: 4
                label: qsTr("Apply")
                onClicked:
                {
                    switch (popupRoot.arrangeMode)
                    {
                        case 0:
                            contextManager.arrangeFixturesInCircle(popupRoot.circleDiameter, popupRoot.lookAtCenter)
                        break
                        case 1:
                            contextManager.arrangeFixturesInGrid(popupRoot.gridWidth, popupRoot.gridHeight, popupRoot.gridColumns, popupRoot.gridAngle)
                        break
                        case 2:
                            contextManager.arrangeFixturesInLine(popupRoot.lineLength, popupRoot.lineAngle, popupRoot.lookAtCenter)
                        break
                    }
                }
            }
        }
}
