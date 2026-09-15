/*
  Q Light Controller Plus
  VideoContext.qml

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
import QtMultimedia

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: ctxRoot
    anchors.fill: parent
    //width: 800
    //height: 600
    color: "black"

    // array of IDs of the contents currently playing
    property var mediaArray: []
    property var mediaItems: []

    Keys.onEscapePressed: stopAllContentFromUI()

    function addVideo(vContent, fadeIn, fadeOut, startTime)
    {
        var item = videoComponent.createObject(ctxRoot,
                                               { "video": vContent,
                                                 "fadeIn": (fadeIn > 0 ? fadeIn : 0),
                                                 "fadeOut": (fadeOut > 0 ? fadeOut : 0),
                                                 "startTime": (startTime > 0 ? startTime : 0) });
        if (videoComponent.status !== Component.Ready)
            console.log("Video component is not ready !!")

        if (item.fadeIn > 0)
            item.requestFadeIn(item.fadeIn)

        mediaArray.push(vContent.id)
        mediaItems.push(item)
    }

    function addPicture(pContent, fadeIn, fadeOut)
    {
        var item = pictureComponent.createObject(ctxRoot,
                                                 { "picture": pContent,
                                                   "fadeIn": (fadeIn > 0 ? fadeIn : 0),
                                                   "fadeOut": (fadeOut > 0 ? fadeOut : 0) });
        if (pictureComponent.status !== Component.Ready)
            console.log("Picture component is not ready !!")

        if (item.fadeIn > 0)
            item.requestFadeIn(item.fadeIn)

        mediaArray.push(pContent.id)
        mediaItems.push(item)
    }

    function pauseContent(id, enable)
    {
        var cIdx = mediaArray.indexOf(id)
        if (cIdx > -1)
        {
            if (enable)
                mediaItems[cIdx].pausePlayback()
            else
                mediaItems[cIdx].resumePlayback()
        }
    }

    function seekContent(id, ms)
    {
        var cIdx = mediaArray.indexOf(id)
        if (cIdx > -1)
            mediaItems[cIdx].seekPlayback(ms)
    }

    function cleanupItem(item)
    {
        var cIdx = mediaItems.indexOf(item)
        if (cIdx > -1)
        {
            var id = mediaArray[cIdx]
            mediaItems[cIdx].stopPlayback()
            mediaItems[cIdx].destroy()
            mediaArray.splice(cIdx, 1)
            mediaItems.splice(cIdx, 1)
        }

        if (mediaArray.length === 0)
            videoContent.destroyContext()
    }

    function removeContent(id)
    {
        var cIdx = mediaArray.indexOf(id)
        if (cIdx === -1)
            return

        var item = mediaItems[cIdx]
        if (item.removalRequested)
            return

        item.removalRequested = true
        if (item.fadeOut > 0)
            item.requestFadeOut(item.fadeOut)
        else
            cleanupItem(item)
    }

    function cleanupContent(id)
    {
        var cIdx = mediaArray.indexOf(id)
        if (cIdx > -1)
            cleanupItem(mediaItems[cIdx])
    }

    function stopAllContentFromUI()
    {
        // Iterate over a copy because stopFromUI() triggers async removal.
        var items = mediaItems.slice()
        for (var i = 0; i < items.length; i++)
        {
            var item = items[i]
            if (item && item.video)
                item.video.stopFromUI()
            else if (item && item.picture)
                item.picture.stopFromUI()
        }
    }

    // Immediately stop playback on all media items, without going through the
    // engine stop round-trip. Used when the context window is closed manually,
    // so the MediaPlayer doesn't keep decoding in the background.
    function stopAllPlayback()
    {
        for (var i = 0; i < mediaItems.length; i++)
        {
            var item = mediaItems[i]
            if (item)
                item.stopPlayback()
        }
    }

    function translateUrl(url)
    {
        if (url.indexOf("://") !== -1)
            return url

        if (Qt.platform.os === "windows")
            return "file:///" + url
        else
            return "file://" + url
    }

    MouseArea
    {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: ctxRoot.forceActiveFocus()
    }

    // Component representing a video content
    Component
    {
        id: videoComponent

        Rectangle
        {
            id: mediaRect
            objectName: "media-" + video.id
            color: "black"
            opacity: video ? effectiveIntensity * fadeMultiplier : 1.0
            z: video ? video.zIndex : 1

            property VideoFunction video: null
            // Volume attribute (0..1) and mute flag of the Video function.
            // Plain properties (not an alias) so that videoContent pushing a
            // new volume doesn't break the AudioOutput binding below
            property real volume: video ? video.volume / 100.0 : 1.0
            property bool muted: video ? video.muted : false
            property vector3d rotation: video.rotation
            property rect geometry: video.customGeometry
            property int fadeIn: 0
            property int fadeOut: 0
            property int startTime: 0 // resume position in ms (0 = from the beginning)
            property real fadeMultiplier: 1.0
            property int fadeState: 0 // 0 idle, 1 fade in, 2 fade out
            property bool removalRequested: false
            property real frozenIntensity: -1.0
            property real effectiveIntensity: (fadeState === 2 && frozenIntensity >= 0.0) ?
                                                  frozenIntensity :
                                                  (video ? video.intensity : 1.0)

            function requestFadeIn(duration)
            {
                if (fadeState === 1)
                    return

                if (duration <= 0)
                {
                    fadeState = 0
                    fadeMultiplier = 1.0
                    return
                }

                fadeState = 1
                frozenIntensity = -1.0
                fadeMultiplier = 0.0
                fadeAnim.to = 1.0
                fadeAnim.duration = duration
                fadeAnim.start()
            }

            function requestFadeOut(duration)
            {
                if (fadeState === 2)
                    return

                if (duration <= 0)
                {
                    frozenIntensity = video ? video.intensity : 1.0
                    fadeState = 0
                    fadeMultiplier = 0.0
                    ctxRoot.cleanupItem(mediaRect)
                    return
                }

                frozenIntensity = video ? video.intensity : 1.0
                fadeState = 2
                fadeAnim.to = 0.0
                fadeAnim.duration = duration
                fadeAnim.start()
            }

            function stopPlayback()
            {
                player.stop()
            }

            // A MediaPlayer paused before it has presented a frame shows
            // nothing, and a seek while paused presents nothing either (the
            // window stays black - verified with the Show Manager's scrub
            // preview, which pauses a clip ~40ms after starting it). So a
            // pause is deferred until a frame at the wanted position has
            // really been presented: the player keeps playing and pauses on
            // the first frame whose own timestamp (VideoFrameProbe below)
            // is at the seek target. The player's position is no substitute:
            // the ffmpeg backend reports the target at once while it keeps
            // presenting the frames decoded before the seek for a while.
            property bool holdRequested: false   // the engine wants it paused
            property bool holdArmed: false       // waiting for a frame at holdTarget
            property bool holdPending: false     // target frame seen: pause on the next one
            property int holdTarget: -1          // position that frame must be at, or -1 for any
            property int holdFrames: 0           // frames presented since armHold()

            function armHold(target)
            {
                holdTarget = target
                holdArmed = true
                holdPending = false
                holdFrames = 0
                holdSettle.stop()
                if (player.playbackState !== MediaPlayer.PlayingState)
                    player.play()
            }

            function holdNow()
            {
                holdArmed = false
                holdPending = false
                holdSettle.stop()
                player.pause()
            }

            function onFramePresented(frameMs)
            {
                if (!holdRequested)
                    return
                if (holdPending)
                {
                    holdNow()
                    return
                }
                if (!holdArmed)
                    return
                holdFrames++
                if (holdTarget >= 0)
                {
                    if (frameMs >= 0)
                    {
                        // a frame from before the seek, or one presented
                        // while the seek target was still being reached
                        if (frameMs < holdTarget - 100 || frameMs > holdTarget + 1000)
                            return
                    }
                    else if (holdFrames < 2 || Math.abs(player.position - holdTarget) > 500)
                    {
                        // no timestamps: the best the position can tell
                        return
                    }
                }
                // Pausing inside this frame's own delivery makes the backend
                // re-present the frame before it (the pre-seek one after a
                // backward seek): pause on the next delivery instead, or
                // after a moment if none comes (end of media, still image).
                holdArmed = false
                holdPending = true
                holdSettle.restart()
            }

            Timer
            {
                id: holdSettle
                interval: 250
                repeat: false
                onTriggered: if (mediaRect.holdPending) mediaRect.holdNow()
            }

            function pausePlayback()
            {
                holdRequested = true
                armHold(-1)
            }

            function resumePlayback()
            {
                holdRequested = false
                holdArmed = false
                holdPending = false
                holdSettle.stop()
                player.play()
            }

            function seekPlayback(ms)
            {
                // Before the media has loaded the position is applied by
                // onMediaStatusChanged, like the initial start time
                if (player.mediaStatus === MediaPlayer.NoMedia ||
                    player.mediaStatus === MediaPlayer.LoadingMedia)
                {
                    mediaRect.startTime = ms
                    return
                }

                player.position = ms
                if (holdRequested)
                {
                    // After a backward seek the VideoOutput keeps showing
                    // the frame from before it even though the new frames
                    // reach its sink (verified with VideoFrameProbe);
                    // re-binding the output before playing on makes it
                    // present them again - within a second or two, not at
                    // once, on the ffmpeg backend of Qt 6.11.
                    player.videoOutput = null
                    player.videoOutput = pVideoOutput
                    armHold(ms)
                }
            }

            NumberAnimation on fadeMultiplier
            {
                id: fadeAnim
                running: false
                onStopped:
                {
                    var state = mediaRect.fadeState
                    mediaRect.fadeState = 0
                    mediaRect.frozenIntensity = -1.0
                    if (state === 2)
                        ctxRoot.cleanupItem(mediaRect)
                }
            }

            onVideoChanged:
            {
                if (geometry.width !== 0 && geometry.height !== 0)
                {
                    if (video.fullscreen)
                    {
                        x = Qt.binding(function() { return geometry.x })
                        y = Qt.binding(function() { return geometry.y })
                    }
                    width = Qt.binding(function() { return geometry.width })
                    height = Qt.binding(function() { return geometry.height })
                }
                else
                    anchors.fill = parent

                player.source = translateUrl(video.sourceUrl)
            }

            transform: [
                Rotation
                {
                    origin.x: width / 2
                    origin.y: rotation.x > 0 ? height : 0
                    axis { x: 1; y: 0; z: 0 }
                    angle: rotation.x
                },
                Rotation
                {
                    origin.x: rotation.y > 0 ? 0 : width
                    origin.y: height / 2
                    axis { x: 0; y: 1; z: 0 }
                    angle: rotation.y
                },
                Rotation
                {
                    origin.x: width / 2
                    origin.y: height / 2
                    axis { x: 0; y: 0; z: 1 }
                    angle: rotation.z
                }
            ]

            MediaPlayer
            {
                id: player
                autoPlay: true
                audioOutput:
                    AudioOutput {
                        volume: mediaRect.muted ? 0.0 :
                                mediaRect.volume * mediaRect.effectiveIntensity * mediaRect.fadeMultiplier
                    }

                videoOutput: pVideoOutput

                onMediaStatusChanged:
                {
                    // Seek to the resume position once the media is loaded.
                    // This is needed e.g. when the Show Manager resumes a
                    // video item that was paused at a non-zero position.
                    if (mediaRect.startTime > 0 &&
                        (mediaStatus == MediaPlayer.LoadedMedia || mediaStatus == MediaPlayer.BufferedMedia))
                    {
                        player.position = mediaRect.startTime
                        if (mediaRect.holdRequested)
                            mediaRect.armHold(mediaRect.startTime)
                        mediaRect.startTime = 0
                    }

                    if (mediaStatus == MediaPlayer.EndOfMedia)
                    {
                        if (mediaRect.video.runOrder === QLCFunction.Loop)
                        {
                            player.play()
                        }
                        else
                        {
                            mediaRect.video.stopFromUI()
                        }
                    }
                }
            }

            VideoOutput
            {
                id: pVideoOutput
                anchors.fill: parent
            }

            VideoFrameProbe
            {
                sink: pVideoOutput.videoSink
                onFramePresented: (startTimeMs) => mediaRect.onFramePresented(startTimeMs)
            }
        }
    }

    // Component representing a picture content
    Component
    {
        id: pictureComponent

        Image
        {
            id: pictureItem
            objectName: "media-" + picture.id
            opacity: picture ? effectiveIntensity * fadeMultiplier : 1.0
            z: picture ? picture.zIndex : 1

            property VideoFunction picture: null
            property vector3d rotation: picture.rotation
            property rect geometry: picture.customGeometry
            property int fadeIn: 0
            property int fadeOut: 0
            property real fadeMultiplier: 1.0
            property int fadeState: 0 // 0 idle, 1 fade in, 2 fade out
            property bool removalRequested: false
            property real frozenIntensity: -1.0
            property real effectiveIntensity: (fadeState === 2 && frozenIntensity >= 0.0) ?
                                                  frozenIntensity :
                                                  (picture ? picture.intensity : 1.0)

            function requestFadeIn(duration)
            {
                if (fadeState === 1)
                    return

                if (duration <= 0)
                {
                    fadeState = 0
                    fadeMultiplier = 1.0
                    return
                }

                fadeState = 1
                frozenIntensity = -1.0
                fadeMultiplier = 0.0
                picFadeAnim.to = 1.0
                picFadeAnim.duration = duration
                picFadeAnim.start()
            }

            function requestFadeOut(duration)
            {
                if (fadeState === 2)
                    return

                if (duration <= 0)
                {
                    frozenIntensity = picture ? picture.intensity : 1.0
                    fadeState = 0
                    fadeMultiplier = 0.0
                    ctxRoot.cleanupItem(pictureItem)
                    return
                }

                frozenIntensity = picture ? picture.intensity : 1.0
                fadeState = 2
                picFadeAnim.to = 0.0
                picFadeAnim.duration = duration
                picFadeAnim.start()
            }

            function stopPlayback() { }
            function pausePlayback() { }
            function resumePlayback() { }
            function seekPlayback(ms) { }

            NumberAnimation on fadeMultiplier
            {
                id: picFadeAnim
                running: false
                onStopped:
                {
                    var state = pictureItem.fadeState
                    pictureItem.fadeState = 0
                    pictureItem.frozenIntensity = -1.0
                    if (state === 2)
                        ctxRoot.cleanupItem(pictureItem)
                }
            }

            onPictureChanged:
            {
                if (geometry.width !== 0 && geometry.height !== 0)
                {
                    if (picture.fullscreen)
                    {
                        x = Qt.binding(function() { return geometry.x })
                        y = Qt.binding(function() { return geometry.y })
                    }
                    width = Qt.binding(function() { return geometry.width })
                    height = Qt.binding(function() { return geometry.height })
                }
                else
                    anchors.fill = parent

                source = translateUrl(picture.sourceUrl)
            }

            transform: [
                Rotation
                {
                    origin.x: width / 2
                    origin.y: rotation.x > 0 ? height : 0
                    axis { x: 1; y: 0; z: 0 }
                    angle: rotation.x
                },
                Rotation
                {
                    origin.x: rotation.y > 0 ? 0 : width
                    origin.y: height / 2
                    axis { x: 0; y: 1; z: 0 }
                    angle: rotation.y
                },
                Rotation
                {
                    origin.x: width / 2
                    origin.y: height / 2
                    axis { x: 0; y: 0; z: 1 }
                    angle: rotation.z
                }
            ]
        }
    }
}
