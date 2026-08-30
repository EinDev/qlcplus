/*
  Q Light Controller Plus
  ShowItem.qml

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
import "TimeUtils.js" as TimeUtils
import "."

Item
{
    id: itemRoot
    height: UISettings.mediumItemHeight
    y: trackIndex >= 0 ? parseInt(height) * trackIndex : 0
    z: 2
    property ShowFunction sfRef: null
    property QLCFunction funcRef: null
    property int startTime: sfRef ? sfRef.startTime : -1
    property int duration: sfRef ? sfRef.duration : -1
    property int trackIndex: -1
    property int timeDivision: showManager.timeDivision
    property real timeScale: showManager.timeScale
    property real tickSize: showManager.tickSize
    property int beatsDivision: showManager.beatsDivision
    property bool isSelected: false
    property bool isDragging: false
    property color globalColor: showManager.itemsColor
    property string infoText: ""
    property string toolTipText: ""

    // Snap-to-item properties
    property var snapEdges: []
    property real snapThreshold: 15
    property real pressMouseX: 0
    property real pressMouseY: 0
    property bool dragActive: false
    property bool itemSnapped: false

    // Snap preview / old position ghost properties
    property real oldPosX: 0
    property real oldPosY: 0
    property real oldPosWidth: 0
    property bool showOldPosGhost: false
    property real previewOffsetX: 0
    property real previewOffsetY: 0
    property real previewWidthDelta: 0
    property bool showLandingPreview: false

    function getVisibleSnapEdges()
    {
        // itemRoot.parent is the Flickable's contentItem,
        // itemRoot.parent.parent is the Flickable (itemsArea)
        var flickable = itemRoot.parent ? itemRoot.parent.parent : null
        if (flickable && flickable.contentX !== undefined)
            return showManager.getSnapEdges(sfRef.functionID, flickable.contentX, flickable.contentX + flickable.width)
        return showManager.getSnapEdges(sfRef.functionID)
    }

    onStartTimeChanged: updateGeometry()
    onDurationChanged: updateGeometry()
    onTimeScaleChanged: updateGeometry()
    onTimeDivisionChanged: updateGeometry()

    onGlobalColorChanged:
    {
        if (isSelected && sfRef)
            sfRef.color = globalColor
    }

    onFuncRefChanged:
    {
        updateGeometry()
        updateTooltipText()
    }

    function updateGeometry()
    {
        if (isDragging || funcRef == null)
            return

        if (timeDivision === Show.Time)
        {
            x = TimeUtils.timeToSize(startTime, timeScale, tickSize)
            width = TimeUtils.timeToSize(duration, timeScale, tickSize)
        }
        else
        {
            x = TimeUtils.beatsToSize(startTime, tickSize, beatsDivision)
            width = TimeUtils.beatsToSize(duration, tickSize, beatsDivision)
        }
    }

    function updateTooltipText()
    {
        var tooltip = funcRef ? funcRef.name + "\n" : ""
        var pos = 0
        var dur = 0

        if (timeDivision === Show.Time)
        {
            pos = TimeUtils.msToString(TimeUtils.posToMs(itemRoot.x + showItemBody.x, timeScale, tickSize))
            dur = TimeUtils.msToString(TimeUtils.posToMs(itemRoot.width, timeScale, tickSize))
        }
        else
        {
            pos = TimeUtils.beatsToString((itemRoot.x + showItemBody.x) / (tickSize / beatsDivision), beatsDivision)
            dur = TimeUtils.beatsToString(itemRoot.width / (tickSize / beatsDivision), beatsDivision)
        }

        tooltip += qsTr("Position: ") + pos
        tooltip += "\n" + qsTr("Duration: ") + dur
        toolTipText = tooltip
    }

    // Mirrors ShowManager::checkAndMoveItem's grid-snap round-trip (showmanager.cpp)
    // exactly, including its use of time-based m_timeScale/m_tickSize math even when
    // newStartTime is actually a beat count (Show.Beats) rather than milliseconds -
    // a pre-existing inconsistency in the C++ code that this preview intentionally
    // mirrors rather than "fixes", so it always matches what release will really do.
    function gridSnappedPreviewOffsetX(rawDx)
    {
        var pxX = itemRoot.x + rawDx
        var newStartTime = (timeDivision === Show.Time)
                ? TimeUtils.posToMs(pxX, timeScale, tickSize)
                : TimeUtils.posToBeat(pxX, tickSize, beatsDivision)

        // onReleased clamps a negative landing time to 0 before calling
        // checkAndMoveItem - mirror that here too
        if (newStartTime < 0)
            newStartTime = 0

        var xPos = (newStartTime * tickSize) / (timeScale * 1000.0)
        xPos = Math.round(xPos / tickSize) * tickSize
        var newTime = xPos * (1000 * timeScale) / tickSize

        var snappedPxX = (timeDivision === Show.Time)
                ? TimeUtils.timeToSize(newTime, timeScale, tickSize)
                : TimeUtils.beatsToSize(newTime, tickSize, beatsDivision)

        return snappedPxX - itemRoot.x
    }

    /* Locker image */
    Image
    {
        x: Math.max(0, itemRoot.width - width - 1)
        y: itemRoot.height - height - 3
        z: 4
        width: itemRoot.height / 3
        height: width
        source: "qrc:/lock.svg"
        sourceSize: Qt.size(width, height)
        visible: sfRef ? (sfRef.locked ? true : false) : false
    }

    /* Waveform for audio items */
    Item
    {
        z: 3
        anchors.fill: parent
        clip: true
        visible: funcRef && funcRef.type === QLCFunction.AudioType

        Image
        {
            id: waveformImage
            x: 0
            y: 0
            // Natural width spans the full audio duration so the waveform is
            // not stretched; the parent Item's clip:true crops it to the
            // show item's visible width.
            width: (funcRef && funcRef.totalDuration && sfRef && sfRef.duration)
                   ? itemRoot.width * (funcRef.totalDuration / sfRef.duration)
                   : itemRoot.width
            height: itemRoot.height
            cache: false
            fillMode: Image.Stretch

            source: (funcRef && funcRef.type === QLCFunction.AudioType) ? "image://waveform/" + funcRef.id : ""

            function reload()
            {
                const old = source;
                source = "";
                source = old;
            }

            Connections
            {
                target: waveformProvider

                function onWaveformUpdated(fid)
                {
                    if (funcRef && fid === funcRef.id)
                        waveformImage.reload()
                }
            }
        }
    }

    Canvas
    {
        id: prCanvas
        z: 3
        anchors.fill: parent
        contextType: "2d"

        onPaint:
        {
            if (sfRef === null || funcRef === null)
                return

            var previewData = showManager.previewData(funcRef)

            if (previewData === null || previewData === undefined)
                return

            context.strokeStyle = "#ddd"
            context.fillStyle = "transparent"
            context.lineWidth = 1

            context.beginPath()
            context.clearRect(0, 0, width, height)

            //console.log("About to paint " + previewData.length + " values")

            var lastTime = 0
            var xPos = 0
            var stepsCount = 0

            for (var i = 0; i < previewData.length; i += 2)
            {
                if (i + 1 >= previewData.length)
                    break

                switch (previewData[i])
                {
                    case ShowManager.RepeatingDuration:
                        var loopCount = funcRef.totalDuration ? Math.floor(sfRef.duration / funcRef.totalDuration) : 0
                        for (var l = 0; l < loopCount; l++)
                        {
                            lastTime += previewData[1]
                            if (timeDivision === Show.Time)
                                xPos = TimeUtils.timeToSize(lastTime, timeScale, tickSize)
                            else
                                xPos = TimeUtils.beatsToSize(lastTime, tickSize, beatsDivision)
                            context.moveTo(xPos, 0)
                            context.lineTo(xPos, itemRoot.height)
                        }
                        context.stroke()
                        lastTime = 0
                        xPos = 0
                    break
                    case ShowManager.FadeIn:
                        var fiEnd
                        if (timeDivision === Show.Time)
                            fiEnd = TimeUtils.timeToSize(lastTime + previewData[i + 1], timeScale, tickSize)
                        else
                            fiEnd = TimeUtils.beatsToSize(lastTime + previewData[i + 1], tickSize, beatsDivision)
                        context.moveTo(xPos, itemRoot.height)
                        context.lineTo(fiEnd, 0)
                    break
                    case ShowManager.StepDivider:
                        lastTime = previewData[i + 1]
                        if (timeDivision === Show.Time)
                            xPos = TimeUtils.timeToSize(lastTime, timeScale, tickSize)
                        else
                            xPos = TimeUtils.beatsToSize(lastTime, tickSize, beatsDivision)
                        context.moveTo(xPos, 0)
                        context.lineTo(xPos, itemRoot.height)
                        stepsCount++
                    break
                    case ShowManager.FadeOut:
                        var foEnd
                        if (timeDivision === Show.Time)
                            foEnd = TimeUtils.timeToSize(lastTime + previewData[i + 1], timeScale, tickSize)
                        else
                            foEnd = TimeUtils.beatsToSize(lastTime + previewData[i + 1], tickSize, beatsDivision)
                        context.moveTo(stepsCount ? xPos : itemRoot.width - foEnd, 0)
                        context.lineTo(stepsCount ? foEnd : itemRoot.width, itemRoot.height)
                    break
                }

            }
            context.stroke()
        }
    }

    /* Ghost of the item's position before the current drag/resize started */
    Rectangle
    {
        id: oldPositionGhost
        x: oldPosX - itemRoot.x
        y: oldPosY - itemRoot.y
        z: 1
        width: oldPosWidth
        height: itemRoot.height
        radius: 2
        color: "#30FFFFFF"
        border.width: 1
        border.color: "#AAFFFFFF"
        visible: showOldPosGhost
    }

    /* Preview of the item's position if the mouse were released now */
    Rectangle
    {
        id: landingPreview
        x: previewOffsetX
        y: previewOffsetY
        z: 1
        width: itemRoot.width + previewWidthDelta
        height: itemRoot.height
        radius: 2
        color: Qt.rgba(globalColor.r, globalColor.g, globalColor.b, 0.3)
        border.width: 2
        border.color: itemSnapped ? "#00FF00" : "#80FFFFFF"
        visible: showLandingPreview
    }

    /* Body mouse area (covers the whole item) */
    MouseArea
    {
        id: sfMouseArea
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true

        Rectangle
        {
            id: showItemBody
            width: itemRoot.width
            height: itemRoot.height
            color: sfRef ? sfRef.color : UISettings.bgLight
            border.width: isSelected ? 2 : 1
            border.color: isSelected ? UISettings.selection : "white"
            clip: true

            Drag.active: itemRoot.dragActive
            Drag.keys: [ "function" ]

            Image
            {
                x: 3
                y: itemRoot.height - height - 3
                visible: infoText ? false : true
                width: itemRoot.height / 3
                height: width
                source: funcRef ? functionManager.functionIcon(funcRef.type) : ""
                sourceSize: Qt.size(width, height)
            }

            RobotoText
            {
                x: 3
                y: 3
                width: parent.width - 6
                height: parent.height - 6
                label: funcRef ? funcRef.name : ""
                fontSize: UISettings.textSizeDefault * 0.7
                textVAlign: Text.AlignTop
                wrapText: true
            }

            RobotoText
            {
                id: infoTextBox
                x: 3
                y: itemRoot.height - height - 3
                width: itemRoot.width - 6
                height: itemRoot.height / 4
                fontSize: UISettings.textSizeDefault * 0.6
                textHAlign: Text.AlignLeft
                wrapText: true
                label: infoText
            }
        }

        onPressed: (mouse) =>
        {
            if (sfRef && sfRef.locked)
                return;
            showManager.enableFlicking(false)
            pressMouseX = mouse.x
            pressMouseY = mouse.y
            isDragging = true
            dragActive = false
            itemSnapped = false
            snapEdges = getVisibleSnapEdges()
            oldPosX = itemRoot.x
            oldPosY = itemRoot.y
            oldPosWidth = itemRoot.width
            previewOffsetX = 0
            previewWidthDelta = 0
        }
        onPositionChanged: (mouse) =>
        {
            if (!isDragging)
                return

            var dx = mouse.x - pressMouseX
            var dy = mouse.y - pressMouseY

            if (!dragActive)
            {
                if (Math.abs(dx) < 30 && Math.abs(dy) < 30)
                    return
                dragActive = true
                itemRoot.z++
                infoTextBox.height = itemRoot.height / 4
                infoTextBox.textHAlign = Text.AlignLeft
                showOldPosGhost = true
                showLandingPreview = true
            }

            // snap-to-item: check start edge if clicked on first half,
            // end edge if clicked on second half
            var checkStart = (pressMouseX < itemRoot.width / 2)
            var edgePos = checkStart ? (itemRoot.x + dx) : (itemRoot.x + dx + itemRoot.width)
            var bestDelta = snapThreshold + 1
            var bestSnapX = -1

            for (var i = 0; i < snapEdges.length; i++)
            {
                var d = snapEdges[i] - edgePos
                if (Math.abs(d) < Math.abs(bestDelta))
                {
                    bestDelta = d
                    bestSnapX = snapEdges[i]
                }
            }

            if (Math.abs(bestDelta) <= snapThreshold)
            {
                dx += bestDelta
                showManager.snapGuideX = bestSnapX
                itemSnapped = true
            }
            else
            {
                showManager.snapGuideX = -1
                itemSnapped = false
            }

            showItemBody.x = dx
            showItemBody.y = dy

            previewOffsetX = (showManager.gridEnabled && !itemSnapped) ? gridSnappedPreviewOffsetX(dx) : dx
            previewOffsetY = (Math.round((itemRoot.y + dy) / itemRoot.height) * itemRoot.height) - itemRoot.y
            previewWidthDelta = 0

            var txt
            if (timeDivision === Show.Time)
                txt = TimeUtils.msToString(TimeUtils.posToMs(itemRoot.x + showItemBody.x, timeScale, tickSize))
            else
                txt = TimeUtils.beatsToString((itemRoot.x + showItemBody.x) / (tickSize / beatsDivision), beatsDivision)

            infoText = qsTr("Position: ") + txt
        }
        onReleased: (mouse) =>
        {
            if (sfRef && sfRef.locked)
                return;

            showManager.snapGuideX = -1

            if (dragActive)
            {
                infoText = ""

                var newTime
                if (timeDivision === Show.Time)
                    newTime = TimeUtils.posToMs(itemRoot.x + showItemBody.x, timeScale, tickSize)
                else
                    newTime = TimeUtils.posToBeat(itemRoot.x + showItemBody.x, tickSize, beatsDivision)

                var newTrackIdx = Math.round((itemRoot.y + showItemBody.y) / itemRoot.height)
                if (newTime < 0)
                    newTime = 0

                if (newTrackIdx >= 0)
                {
                    var res = showManager.checkAndMoveItem(sfRef, trackIndex, newTrackIdx, newTime, itemSnapped)

                    if (res === true)
                        trackIndex = newTrackIdx

                    prCanvas.requestPaint()
                }

                showItemBody.x = 0
                showItemBody.y = 0
                itemRoot.z--
            }

            showManager.enableFlicking(true)
            updateTooltipText()
            isDragging = false
            dragActive = false
            itemSnapped = false
            showOldPosGhost = false
            showLandingPreview = false
            updateGeometry()
        }

        onClicked: (mouse) =>
        {
            if (dragActive)
                return
            var multi = ((mouse.modifiers & Qt.ControlModifier) || (mouse.modifiers & Qt.ShiftModifier))
                    || (showManager && showManager.multipleSelection)
            if (multi)
                itemRoot.isSelected = !itemRoot.isSelected
            else
                itemRoot.isSelected = true
            showManager.setItemSelection(trackIndex, sfRef, itemRoot, itemRoot.isSelected, mouse.modifiers)
        }

        onDoubleClicked: functionManager.setEditorFunction(sfRef.functionID, true, false)
    }

    Text
    {
        anchors.fill: parent
        ToolTip.visible: sfMouseArea.containsMouse
        ToolTip.delay: 1000
        ToolTip.text: toolTipText
    }

    /* horizontal left handler */
    Rectangle
    {
        id: horLeftHandler
        z: 2
        width: 10
        height: itemRoot.height
        color: horLeftHdlMa.containsMouse ? "#7FFFFF00" : "transparent"
        visible: sfRef ? (sfRef.locked ? false : true) : false

        MouseArea
        {
            id: horLeftHdlMa
            anchors.fill: parent
            preventStealing: true
            hoverEnabled: true
            cursorShape: containsMouse ? Qt.SizeHorCursor : Qt.ArrowCursor

            property real pressX: 0
            property real origItemX: 0
            property real origItemW: 0

            onPressed: (mouse) =>
            {
                isDragging = true
                itemSnapped = false
                snapEdges = getVisibleSnapEdges()
                pressX = mapToItem(itemRoot.parent, mouse.x, mouse.y).x
                origItemX = itemRoot.x
                origItemW = itemRoot.width
                oldPosX = itemRoot.x
                oldPosY = itemRoot.y
                oldPosWidth = itemRoot.width
                showOldPosGhost = true
                showLandingPreview = true
                previewOffsetX = 0
                previewOffsetY = 0
                previewWidthDelta = 0
            }

            onPositionChanged: (mouse) =>
            {
                if (!pressed)
                    return

                var globalX = mapToItem(itemRoot.parent, mouse.x, mouse.y).x
                var dx = globalX - pressX
                var newX = origItemX + dx

                // snap-to-item: check left edge
                var bestDist = snapThreshold + 1
                var bestSnapX = -1
                for (var i = 0; i < snapEdges.length; i++)
                {
                    var dist = Math.abs(snapEdges[i] - newX)
                    if (dist < bestDist)
                    {
                        bestDist = dist
                        bestSnapX = snapEdges[i]
                    }
                }
                if (bestSnapX >= 0 && bestDist <= snapThreshold)
                {
                    newX = bestSnapX
                    showManager.snapGuideX = bestSnapX
                    itemSnapped = true
                }
                else
                {
                    showManager.snapGuideX = -1
                    itemSnapped = false
                }

                // clamp: don't allow shrinking past minimum width
                var maxX = origItemX + origItemW - horLeftHandler.width
                if (newX > maxX)
                    newX = maxX

                itemRoot.width = origItemW + (origItemX - newX)
                itemRoot.x = newX
                infoTextBox.height = itemRoot.height / 2
                infoTextBox.textHAlign = Text.AlignLeft
                updateTooltipText()

                // grid-snap preview (skip if item-snapped) - mirrors the release-time
                // logic below exactly; only x/width compensate, right edge stays fixed.
                // Uses "itemRoot.x > 0" rather than a plain truthy check: the
                // release-time code only reaches its (falsy-on-0) check *after*
                // clamping a negative x to 0, so a negative itemRoot.x here (still
                // truthy) must be excluded up front to match that outcome.
                if (showManager.gridEnabled && !itemSnapped && itemRoot.x > 0)
                {
                    var snappedX = Math.round(itemRoot.x / tickSize) * tickSize
                    previewOffsetX = snappedX - itemRoot.x
                    previewWidthDelta = itemRoot.x - snappedX
                }
                else
                {
                    previewOffsetX = 0
                    previewWidthDelta = 0
                }
            }
            onReleased: (mouse) =>
            {
                showManager.snapGuideX = -1

                if (sfRef)
                {
                    if (itemRoot.x < 0)
                    {
                        itemRoot.width += itemRoot.x
                        itemRoot.x = 0
                    }

                    // check grid snapping (skip if item-snapped)
                    if (!itemSnapped && itemRoot.x && showManager.gridEnabled)
                    {
                        var currX = itemRoot.x
                        itemRoot.x = Math.round(itemRoot.x / tickSize) * tickSize
                        itemRoot.width += (currX - itemRoot.x)
                    }

                    var newDuration, newStartTime

                    if (timeDivision === Show.Time)
                    {
                        newStartTime = TimeUtils.posToMs(itemRoot.x, timeScale, tickSize)
                        newDuration = TimeUtils.posToMs(itemRoot.width, timeScale, tickSize)
                    }
                    else
                    {
                        newStartTime = TimeUtils.posToBeat(itemRoot.x, tickSize, beatsDivision)
                        newDuration = TimeUtils.posToBeat(itemRoot.width, tickSize, beatsDivision)
                    }

                    if (showManager.setShowItemStartTime(sfRef, newStartTime) === true)
                        showManager.setShowItemDuration(sfRef, newDuration)
                    else
                        updateGeometry()

                    if (funcRef && showManager.stretchFunctions === true)
                        funcRef.totalDuration = sfRef.duration

                    prCanvas.requestPaint()
                }
                infoText = ""
                isDragging = false
                itemSnapped = false
                showOldPosGhost = false
                showLandingPreview = false
                updateGeometry()
            }
        }
    }

    /* horizontal right handler */
    Rectangle
    {
        id: horRightHandler
        x: itemRoot.width - 10
        z: 2
        width: 10
        height: itemRoot.height
        color: horRightHdlMa.containsMouse ? "#7FFFFF00" : "transparent"
        visible: sfRef ? (sfRef.locked ? false : true) : false

        MouseArea
        {
            id: horRightHdlMa
            anchors.fill: parent
            preventStealing: true
            hoverEnabled: true
            cursorShape: containsMouse ? Qt.SizeHorCursor : Qt.ArrowCursor

            drag.target: horRightHandler
            drag.axis: Drag.XAxis
            drag.minimumX: horLeftHandler.x + width

            onPressed:
            {
                isDragging = true
                itemSnapped = false
                snapEdges = getVisibleSnapEdges()
                oldPosX = itemRoot.x
                oldPosY = itemRoot.y
                oldPosWidth = itemRoot.width
                showOldPosGhost = true
                showLandingPreview = true
                previewOffsetX = 0
                previewOffsetY = 0
                previewWidthDelta = 0
            }

            onPositionChanged: (mouse) =>
            {
                if (drag.active === true)
                {
                    var obj = mapToItem(itemRoot, mouseX, mouseY)
                    var newWidth = obj.x + (horRightHdlMa.width - mouse.x)

                    // snap-to-item: check right edge
                    var rightEdge = itemRoot.x + newWidth
                    var bestDist = snapThreshold + 1
                    var bestSnapX = -1
                    for (var i = 0; i < snapEdges.length; i++)
                    {
                        var dist = Math.abs(snapEdges[i] - rightEdge)
                        if (dist < bestDist)
                        {
                            bestDist = dist
                            bestSnapX = snapEdges[i]
                        }
                    }
                    if (bestSnapX >= 0 && bestDist <= snapThreshold)
                    {
                        newWidth = bestSnapX - itemRoot.x
                        showManager.snapGuideX = bestSnapX
                        itemSnapped = true
                    }
                    else
                    {
                        showManager.snapGuideX = -1
                        itemSnapped = false
                    }

                    itemRoot.width = newWidth
                    infoTextBox.height = itemRoot.height / 4
                    infoTextBox.textHAlign = Text.AlignRight
                    updateTooltipText()

                    // grid-snap preview (skip if item-snapped) - mirrors the
                    // release-time logic below exactly; only width changes here,
                    // the left edge/x is fixed for a right-edge resize
                    if (showManager.gridEnabled && !itemSnapped)
                    {
                        var snappedEndPos = Math.round((itemRoot.x + itemRoot.width) / tickSize) * tickSize
                        previewWidthDelta = snappedEndPos - (itemRoot.x + itemRoot.width)
                    }
                    else
                    {
                        previewWidthDelta = 0
                    }
                    previewOffsetX = 0
                }
            }
            onReleased:
            {
                showOldPosGhost = false
                showLandingPreview = false

                if (drag.active === false)
                    return

                showManager.snapGuideX = -1

                if (sfRef)
                {
                    // check grid snapping (skip if item-snapped)
                    if (!itemSnapped && showManager.gridEnabled)
                    {
                        var snappedEndPos = Math.round((itemRoot.x + itemRoot.width) / tickSize) * tickSize
                        itemRoot.width = snappedEndPos - itemRoot.x
                    }

                    var newDuration

                    if (timeDivision === Show.Time)
                        newDuration = TimeUtils.posToMs(itemRoot.width, timeScale, tickSize)
                    else
                        newDuration = (Math.round(itemRoot.width / (tickSize / beatsDivision)) * 1000)

                    if (showManager.setShowItemDuration(sfRef, newDuration) === false)
                        updateGeometry()

                    if (funcRef && showManager.stretchFunctions === true)
                        funcRef.totalDuration = sfRef.duration

                    prCanvas.requestPaint()
                }
                infoText = ""
                isDragging = false
                itemSnapped = false
                updateGeometry()
            }
        }
    }
}
