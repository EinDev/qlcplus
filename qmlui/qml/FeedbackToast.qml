/*
  Q Light Controller Plus
  FeedbackToast.qml

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

/**
 * A generic, short-lived, non-interactive feedback strip: a bold primary
 * text (a key combination, a keypad command, ...) next to an optional
 * secondary text, fading in on show(), holding for holdTime and fading out
 * again. Calling show() while it is visible replaces the text and restarts
 * the hold. Nothing here knows about keyboard shortcuts - MainView.qml owns
 * the single instance and feeds it from whatever wants to give feedback
 * (today: ShortcutManager's key-cast and click hints).
 *
 * It never takes focus or mouse input (enabled: false, no handlers), so it
 * can sit above everything else without getting in the way.
 */
Item
{
    id: toast

    property string primaryText: ""
    property string secondaryText: ""

    /** how long the strip stays fully visible after a show(), in ms */
    property int holdTime: 1500
    property int fadeInTime: 120
    property int fadeOutTime: 350

    /** the strip never grows wider than this; long secondary texts elide */
    property real maxWidth: parent ? parent.width - UISettings.iconSizeDefault : UISettings.bigItemHeight * 4

    implicitWidth: strip.width
    implicitHeight: strip.height
    width: implicitWidth
    height: implicitHeight

    // purely informational: never takes focus, never eats mouse input
    enabled: false
    opacity: 0
    visible: opacity > 0

    function show(primary, secondary)
    {
        primaryText = primary === undefined || primary === null ? "" : String(primary)
        secondaryText = secondary === undefined || secondary === null ? "" : String(secondary)

        // a new event while fading out must win over the pending fade,
        // otherwise it would be swallowed by the animation already running
        fadeOut.stop()
        fadeIn.restart()
        hideTimer.restart()
    }

    function hide()
    {
        hideTimer.stop()
        fadeIn.stop()
        fadeOut.restart()
    }

    NumberAnimation
    {
        id: fadeIn
        target: toast
        property: "opacity"
        to: 1
        duration: toast.fadeInTime
    }

    NumberAnimation
    {
        id: fadeOut
        target: toast
        property: "opacity"
        to: 0
        duration: toast.fadeOutTime
    }

    Timer
    {
        id: hideTimer
        interval: toast.holdTime
        onTriggered: fadeOut.restart()
    }

    Rectangle
    {
        id: strip

        readonly property real hPadding: UISettings.textSizeDefault
        readonly property real vPadding: UISettings.textSizeDefault * 0.5

        width: contentRow.width + 2 * hPadding
        height: contentRow.height + 2 * vPadding
        color: UISettings.bgStronger
        border.width: 1
        border.color: UISettings.bgLight
        radius: 6

        RowLayout
        {
            id: contentRow
            anchors.centerIn: parent
            spacing: UISettings.textSizeDefault * 0.8

            // shrink to the texts, but never past the strip's allowed width
            width: Math.min(primaryLabel.implicitWidth +
                            (secondaryLabel.visible ? spacing + secondaryLabel.implicitWidth : 0),
                            Math.max(0, toast.maxWidth - 2 * strip.hPadding))
            height: Math.max(primaryLabel.implicitHeight, secondaryLabel.implicitHeight)

            Text
            {
                id: primaryLabel
                visible: text !== ""
                Layout.preferredWidth: Math.min(implicitWidth, contentRow.width)
                text: toast.primaryText
                color: UISettings.toolbarSelectionMain
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault
                font.bold: true
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Text
            {
                id: secondaryLabel
                visible: text !== ""
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                text: toast.secondaryText
                color: UISettings.fgMain
                font.family: UISettings.robotoFontName
                font.pixelSize: UISettings.textSizeDefault
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
