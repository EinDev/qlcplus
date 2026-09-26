/*
  Q Light Controller Plus
  MainView.qml

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
import QtQuick.Controls

import org.qlcplus.classes 1.0
import "."

Rectangle
{
    id: mainView
    visible: true
    width: 800
    height: 600
    anchors.fill: parent
    color: UISettings.bgMedium

    property string currentContext: ""

    Component.onCompleted: UISettings.sidePanelWidth = Math.min(width / 3, UISettings.bigItemHeight * 5)
    onWidthChanged: UISettings.sidePanelWidth = Math.min(width / 3, UISettings.bigItemHeight * 5)

    property bool showWizardVisible: false
    function openShowWizard() { mainView.showWizardVisible = true }

    function enableContext(ctx, setChecked)
    {
        var item = null

        if (ctx === "FIXANDFUNC")
            item = fnfEntry
        else if (ctx === "VC")
            item = vcEntry
        else if (ctx === "SDESK")
            item = sdEntry
        else if (ctx === "SHOWMGR")
            item = smEntry
        else if (ctx === "IOMGR")
            item = ioEntry

        if (item)
        {
            item.visible = true
            if (setChecked)
                item.checked = true
            return true
        }
        return false
    }

    // Each top-level context is backed by its own persistent Loader (declared
    // in mainViewArea below) so switching tabs only toggles visibility
    // instead of destroying and recreating the whole view (and, for Fixtures
    // & Functions, every fixture item in the active 2D/3D preview) every time.
    readonly property var contextResources: ({
        "FIXANDFUNC": "qrc:/FixturesAndFunctions.qml",
        "VC": "qrc:/VirtualConsole.qml",
        "SDESK": "qrc:/SimpleDesk.qml",
        "SHOWMGR": "qrc:/ShowManager.qml",
        "IOMGR": "qrc:/InputOutputManager.qml"
    })

    function loaderForContext(ctx)
    {
        switch (ctx)
        {
            case "FIXANDFUNC": return fnfLoader
            case "VC": return vcLoader
            case "SDESK": return sdeskLoader
            case "SHOWMGR": return showmgrLoader
            case "IOMGR": return iomgrLoader
        }
        return null
    }

    function switchToContext(ctx, qmlRes)
    {
        if (currentContext === ctx)
            return

        // let the context being left clean up after itself (the Show
        // Manager stops its cursor preview, see ShowManager::enableContext)
        if (currentContext !== "")
            contextManager.enableContext(currentContext, false, null)

        if (enableContext(ctx, true) === true)
        {
            currentContext = ctx
            contextManager.enableContext(ctx, true, null)
            // show toolbar only if not in kiosk mode
            if (qlcplus.accessMask !== App.AC_VCControl)
                mainToolbar.visible = true
        }
        else
        {
            mainToolbar.visible = false
            currentContext = ""
        }

        shortcutManager.currentContext = currentContext

        // For the 5 standard tabs, qmlRes is always the same fixed resource
        // for a given ctx (see contextResources) - activate that context's
        // own persistent Loader rather than loading qmlRes into a shared one.
        // Anything else (e.g. the native Fixture Editor's "FXEDITOR"
        // pseudo-context, which has no tab of its own) falls back to the
        // transient overlay, same as loadResource() below.
        var ldr = loaderForContext(ctx)
        if (ldr)
        {
            overlayLoader.source = ""
            ldr.active = true
        }
        else if (qmlRes)
        {
            overlayLoader.source = qmlRes
        }
    }

    function setDimScreen(enable)
    {
        dimScreen.visible = enable
    }

    function openAccessRequest(sessionId, clientName, peerAddress, peerPort)
    {
        clientAccessPopup.deciding = false
        clientAccessPopup.sessionId = sessionId
        clientAccessPopup.clientName = clientName
        clientAccessPopup.peerAddress = peerAddress
        clientAccessPopup.peerPort = peerPort
        clientAccessPopup.open()
    }

    function closeAccessRequest(sessionId)
    {
        if (clientAccessPopup.sessionId === sessionId)
            clientAccessPopup.close()
    }

    function saveProject()
    {
        actionsMenu.handleSaveAction()
    }

    function openProject()
    {
        actionsMenu.openDialog(App.OpenMode)
    }

    function saveProjectAs()
    {
        actionsMenu.openDialog(App.SaveAsMode)
    }

    function triggerDmxDump()
    {
        sceneDump.updateDumpVariables()
        dmxDumpDialog.open()
        dmxDumpDialog.focusEditItem()
    }

    function showShortcutCollisionWarning(sequence, actionDescription)
    {
        shortcutCollisionPopup.message = qsTr("The key combination \"%1\" you just assigned is already used by the built-in shortcut \"%2\". Your Virtual Console binding will always take priority over it while this project is loaded.").arg(sequence).arg(actionDescription)
        shortcutCollisionPopup.open()
    }

    // ADR 0001 decision 4 - called once per file-open by App::loadXML() when
    // Doc::possiblyAffectedLegacyBeatShows() found at least one Show that may
    // still hold legacy beat-pseudo-count timeline values. $shows is a list
    // of { id, name }.
    function showLegacyShowTimingWarning(shows)
    {
        legacyShowTimingDialog.shows = shows
        legacyShowTimingDialog.open()
    }

    // Called once per file-open by App::loadXML() with the number of
    // Audio/Video sources living outside the project's media store (0 hides
    // the banner, also sent by App::clearDocument()). A non-blocking banner
    // (not a modal dialog): the user can keep working and collect later
    // from the actions menu - nothing is ever collected unasked.
    function showExternalMediaNotice(count)
    {
        externalMediaBanner.externalCount = count
        externalMediaBanner.refresh()
    }

    // Managed copies whose origin file was re-rendered since the import
    // (0 hides it). Shares the banner with the notice above: the external
    // one comes first, this one follows once that is dealt with.
    function showChangedMediaNotice(count)
    {
        externalMediaBanner.changedCount = count
        externalMediaBanner.refresh()
    }

    // "Collect media into project" (actions menu / the banner above):
    // copies every external Audio/Video source into <project>.qxw.assets
    // and repoints the functions, then reports what happened.
    function collectMediaIntoProject()
    {
        externalMediaBanner.externalCount = 0
        externalMediaBanner.refresh()
        var result = qlcplus.collectMedia()
        var text = qsTr("%1 file(s) copied into the project.").arg(result.copied)
        if (result.queued > 0)
            text += "\n" + qsTr("%1 large file(s) are being copied in the background.").arg(result.queued)
        if (result.failed > 0)
            text += "\n" + qsTr("%1 file(s) could not be copied and keep their external path.").arg(result.failed)
        if (result.error)
            text += "\n" + result.error
        mediaResultPopup.title = qsTr("Collect media into project")
        mediaResultPopup.message = text
        mediaResultPopup.open()
    }

    // "Reload changed media" (actions menu / the banner): re-imports every
    // managed copy whose origin file changed on disk and repoints the
    // functions (the previous copies stay on disk), then reports what
    // happened. Never runs unasked.
    function reloadChangedMedia()
    {
        externalMediaBanner.changedCount = 0
        externalMediaBanner.refresh()
        var result = qlcplus.reloadChangedMedia()
        var text = qsTr("%1 file(s) reloaded from their origin.").arg(result.reloaded)
        if (result.queued > 0)
            text += "\n" + qsTr("%1 large file(s) are being copied in the background.").arg(result.queued)
        text += "\n" + qsTr("%1 file(s) unchanged.").arg(result.unchanged)
        if (result.missing > 0)
            text += "\n" + qsTr("%1 file(s) whose origin is no longer on disk.").arg(result.missing)
        if (result.busy > 0)
            text += "\n" + qsTr("%1 file(s) skipped because the function is running.").arg(result.busy)
        if (result.failed > 0)
            text += "\n" + qsTr("%1 file(s) could not be copied.").arg(result.failed)
        if (result.error)
            text += "\n" + result.error
        mediaResultPopup.title = qsTr("Reload changed media")
        mediaResultPopup.message = text
        mediaResultPopup.open()
    }

    // "Remove unused media": lists the store files no function references
    // anymore and deletes them only after an explicit Yes.
    function removeUnusedMedia()
    {
        var files = qlcplus.unusedMedia()
        if (files.length === 0)
        {
            mediaResultPopup.title = qsTr("Remove unused media")
            mediaResultPopup.message = qsTr("Every file in the project's media folder is still in use.")
            mediaResultPopup.open()
            return
        }
        unusedMediaPopup.files = files
        unusedMediaPopup.open()
    }

    function saveBeforeExit()
    {
        //actionsMenu.open()
        actionsMenu.saveBeforeExit()
    }

    function loadResource(qmlRes)
    {
        // if qmlRes is one of the standard tab views, go through the normal
        // context-switch path so it uses (and keeps alive) its persistent
        // Loader instead of the transient overlay below.
        for (var ctx in contextResources)
        {
            if (contextResources[ctx] === qmlRes)
            {
                overlayLoader.source = ""
                switchToContext(ctx, qmlRes)
                return
            }
        }

        overlayLoader.source = qmlRes
    }

    FontLoader
    {
        source: "qrc:/RobotoCondensed-Regular.ttf"
    }

    // Load the "FontAwesome" font for the monochrome icons
    FontLoader
    {
        id: faFontLoader
        source: "qrc:/FontAwesome7-Free-Solid-900.otf"
        onStatusChanged:
        {
            if (status === FontLoader.Ready)
                UISettings.fontAwesomeFontName = faFontLoader.name
        }
    }

    Rectangle
    {
        id: mainToolbar
        visible: qlcplus.accessMask & App.AC_VCControl ? false : true // this is kiosk mode
        width: parent.width
        height: UISettings.iconSizeDefault
        z: 50
        gradient: Gradient
        {
            GradientStop { position: 0; color: UISettings.toolbarStartMain }
            GradientStop { position: 1; color: UISettings.toolbarEnd }
        }

        RowLayout
        {
            spacing: 5
            anchors.fill: parent

            ButtonGroup { id: menuBarGroup }

            MenuBarEntry
            {
                id: actEntry
                Layout.alignment: Qt.AlignTop
                imgSource: "qrc:/qlcplus.svg"
                entryText: qsTr("Actions")
                onPressed: actionsMenu.open()
                autoExclusive: false
                checkable: false

                Image
                {
                    visible: qlcplus.docModified
                    source: "qrc:/filesave.svg"
                    x: 1
                    y: parent.height - height - 1
                    height: parent.height / 3
                    width: height
                    sourceSize: Qt.size(width, height)
                }
            }
            MenuBarEntry
            {
                id: fnfEntry
                property string ctxName: "FIXANDFUNC"
                Layout.alignment: Qt.AlignTop
                property string ctxRes: "qrc:/FixturesAndFunctions.qml"

                //visible: qlcplus.accessMask & App.AC_FunctionEditing
                imgSource: "qrc:/editor.svg"
                entryText: qsTr("Fixtures & Functions")
                checked: false
                ButtonGroup.group: menuBarGroup
                onCheckedChanged:
                {
                    if (checked === true)
                        switchToContext(fnfEntry.ctxName, fnfEntry.ctxRes)
                }
            }
            MenuBarEntry
            {
                id: vcEntry
                Layout.alignment: Qt.AlignTop
                property string ctxName: "VC"
                property string ctxRes: "qrc:/VirtualConsole.qml"

                visible: qlcplus.accessMask & App.AC_VCControl
                imgSource: "qrc:/virtualconsole.svg"
                entryText: qsTr("Virtual Console")
                ButtonGroup.group: menuBarGroup
                onCheckedChanged:
                {
                    if (checked === true)
                        switchToContext(vcEntry.ctxName, vcEntry.ctxRes)
                }
                onRightClicked:
                {
                    vcEntry.visible = false
                    contextManager.detachContext("VC")
                }
            }
            MenuBarEntry
            {
                id: sdEntry
                Layout.alignment: Qt.AlignTop
                property string ctxName: "SDESK"
                property string ctxRes: "qrc:/SimpleDesk.qml"

                visible: qlcplus.accessMask & App.AC_SimpleDesk
                imgSource: "qrc:/simpledesk.svg"
                entryText: qsTr("Simple Desk")
                ButtonGroup.group: menuBarGroup
                onCheckedChanged:
                {
                    if (checked === true)
                        switchToContext(sdEntry.ctxName, sdEntry.ctxRes)
                }
                onRightClicked:
                {
                    sdEntry.visible = false
                    contextManager.detachContext("SDESK")
                }
            }
            MenuBarEntry
            {
                id: smEntry
                Layout.alignment: Qt.AlignTop
                property string ctxName: "SHOWMGR"
                property string ctxRes: "qrc:/ShowManager.qml"

                visible: qlcplus.accessMask & App.AC_ShowManager
                imgSource: "qrc:/showmanager.svg"
                entryText: qsTr("Show Manager")
                ButtonGroup.group: menuBarGroup
                onCheckedChanged:
                {
                    if (checked === true)
                        switchToContext(smEntry.ctxName, smEntry.ctxRes)
                }
                onRightClicked:
                {
                    smEntry.visible = false
                    contextManager.detachContext("SHOWMGR")
                }
            }
            MenuBarEntry
            {
                id: ioEntry
                Layout.alignment: Qt.AlignTop
                property string ctxName: "IOMGR"
                property string ctxRes: "qrc:/InputOutputManager.qml"

                visible: qlcplus.accessMask & App.AC_InputOutput
                imgSource: "qrc:/inputoutput.svg"
                entryText: qsTr("Input/Output")
                ButtonGroup.group: menuBarGroup
                onCheckedChanged:
                {
                    if (checked === true)
                        switchToContext(ioEntry.ctxName, ioEntry.ctxRes)
                }
                onRightClicked:
                {
                    ioEntry.visible = false
                    contextManager.detachContext("IOMGR")
                }
            }
            Rectangle
            {
                // acts like an horizontal spacer
                Layout.fillWidth: true
                implicitHeight: parent.height
                color: "transparent"
            }

            // ################## DMX DUMP ##################
            IconButton
            {
                id: sceneDump
                z: 2
                implicitWidth: UISettings.iconSizeDefault
                implicitHeight: UISettings.iconSizeDefault
                Layout.alignment: Qt.AlignTop
                bgColor: "transparent"
                imgSource: "qrc:/dmxdump.svg"
                imgMargins: 10
                tooltip: qsTr("Dump DMX values on a Scene")
                counter: (qlcplus.accessMask & App.AC_FunctionEditing)

                property string bubbleLabel: {
                    if (currentContext === sdEntry.ctxName)
                        return simpleDesk ? simpleDesk.dumpValuesCount : ""
                    else
                        return contextManager ? contextManager.dumpValuesCount : ""
                }

                function updateDumpVariables()
                {
                    if (currentContext === sdEntry.ctxName)
                    {
                        dmxDumpDialog.capabilityMask = simpleDesk ? simpleDesk.dumpChannelMask : 0
                        dmxDumpDialog.channelSetMask = simpleDesk ? simpleDesk.dumpChannelMask : 0
                    }
                    else
                    {
                        dmxDumpDialog.capabilityMask = fixtureManager ? fixtureManager.capabilityMask : 0
                        dmxDumpDialog.channelSetMask = contextManager ? contextManager.dumpChannelMask : 0
                    }
                }

                // channel count bubble
                Rectangle
                {
                    x: -3
                    y: parent.height - height + 3
                    width: sceneDump.width * 0.4
                    height: width
                    color: "red"
                    border.width: 1
                    border.color: UISettings.fgMain
                    radius: 3
                    clip: true
                    visible: sceneDump.bubbleLabel !== "0" ? true : false

                    RobotoText
                    {
                        anchors.centerIn: parent
                        height: parent.height * 0.7
                        label: sceneDump.bubbleLabel
                        fontSize: height
                    }
                }

                MouseArea
                {
                    id: dumpDragArea
                    anchors.fill: parent
                    drag.target: dumpDragItem
                    drag.threshold: 10

                    onClicked: (mouse) =>
                    {
                        sceneDump.updateDumpVariables()
                        dmxDumpDialog.open()
                        dmxDumpDialog.focusEditItem()
                    }

                    property bool dragActive: drag.active

                    onDragActiveChanged:
                    {
                        console.log("Drag active changed: " + dragActive)
                        if (dragActive == false)
                        {
                            dumpDragItem.Drag.drop()
                            dumpDragItem.parent = sceneDump
                            dumpDragItem.x = 0
                            dumpDragItem.y = 0
                        }
                        else
                        {
                            dumpDragItem.parent = mainView
                        }

                        dumpDragItem.Drag.active = dragActive
                    }
                }

                Item
                {
                    id: dumpDragItem
                    z: 99
                    visible: dumpDragArea.drag.active

                    Drag.source: dumpDragItem
                    Drag.keys: [ "dumpValues" ]

                    function itemDropped(id, name)
                    {
                        console.log("Dump values dropped on " + id)
                        functionManager.selectFunctionID(id, false)
                        sceneDump.updateDumpVariables()
                        dmxDumpDialog.sceneName = name
                        dmxDumpDialog.existingScene = true
                        dmxDumpDialog.open()
                        dmxDumpDialog.focusEditItem()
                    }

                    Rectangle
                    {
                        width: UISettings.iconSizeMedium
                        height: width
                        radius: width / 4
                        color: "red"

                        RobotoText
                        {
                            anchors.centerIn: parent
                            label: sceneDump.bubbleLabel
                        }
                    }
                }

                PopupDMXDump
                {
                    id: dmxDumpDialog
                    implicitWidth: Math.min(UISettings.bigItemHeight * 4, mainView.width / 3)

                    onAccepted:
                    {
                        if (currentContext === sdEntry.ctxName)
                        {
                            simpleDesk.dumpDmxChannels(sceneName, getChannelsMask(), existingScene && func ? func.id : -1, nonZeroOnly)
                        }
                        else
                        {
                            contextManager.dumpDmxChannels(getChannelsMask(), sceneName, existingScene && func ? func.id : -1,
                                                           allChannels, nonZeroOnly);
                        }
                    }
                }
            }

            // spacer
            Rectangle
            {
                width: UISettings.iconSizeDefault / 2
                color: "transparent"
            }

            // ################## BEATS ##################
            RobotoText
            {
                label: "BPM: " + (ioManager.bpmNumber > 0 ? ioManager.bpmNumber : qsTr("Off"))
                color: gsMouseArea.containsMouse ? UISettings.bgLight : "transparent"
                fontSize: UISettings.textSizeDefault
                Layout.alignment: Qt.AlignTop
                implicitWidth: width
                implicitHeight: parent.height

                MouseArea
                {
                    id: gsMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: beatSelectionPanel.visible = !beatSelectionPanel.visible
                }
                BeatGeneratorsPanel
                {
                    id: beatSelectionPanel
                    parent: mainView
                    y: mainToolbar.height
                    x: beatIndicator.x - width
                    z: 51
                    visible: false
                }
            }
            Rectangle
            {
                id: beatIndicator
                implicitWidth: height
                implicitHeight: parent.height * 0.5
                Layout.alignment: Qt.AlignVCenter
                radius: height / 2
                border.width: 2
                border.color: UISettings.bgMedium
                color: UISettings.fgMedium

                ColorAnimation on color
                {
                    id: cAnim
                    from: "#00FF00"
                    to: UISettings.fgMedium
                    // half the duration of the current BPM
                    duration: ioManager.bpmNumber ? 30000 / ioManager.bpmNumber : 200
                    running: false
                }

                Connections
                {
                    id: beatSignal
                    target: ioManager
                    function onBeat()
                    {
                        cAnim.restart()
                    }
                }
            }

            // ################## MEDIA COPY PROGRESS ##################
            // Non-modal status of a large media file being copied into the
            // project's store on a worker thread (App::mediaImportStatus)
            RobotoText
            {
                visible: qlcplus.mediaImportStatus !== ""
                label: qlcplus.mediaImportStatus
                labelColor: UISettings.fgLight
                fontSize: UISettings.textSizeDefault * 0.85
                Layout.alignment: Qt.AlignTop
                implicitWidth: width
                implicitHeight: parent.height
            }

            // spacer
            Rectangle
            {
                width: UISettings.iconSizeDefault / 2
                color: "transparent"
            }

            // ################## STOP ALL FUNCTIONS ##################
            IconButton
            {
                id: stopAllButton
                implicitWidth: UISettings.iconSizeDefault
                implicitHeight: UISettings.iconSizeDefault
                Layout.alignment: Qt.AlignTop
                enabled: runningCount ? true : false
                bgColor: "transparent"
                faSource: FontAwesome.fa_octagon
                faColor: "red"
                tooltip: qsTr("Stop all the running functions")

                onClicked: qlcplus.stopAllFunctions()

                property int runningCount: qlcplus.runningFunctionsCount

                onRunningCountChanged: console.log("Functions running: " + runningCount)

                RobotoText
                {
                    anchors.centerIn: parent
                    height: parent.height * 0.2
                    fontSize: height
                    label: "STOP"
                }

                Rectangle
                {
                    x: parent.width / 2
                    y: parent.height / 2
                    width: parent.width * 0.4
                    height: width
                    color: UISettings.highlight
                    border.width: 1
                    border.color: UISettings.fgMain
                    radius: 3
                    clip: true
                    visible: stopAllButton.runningCount

                    RobotoText
                    {
                        anchors.centerIn: parent
                        height: parent.height * 0.7
                        label: stopAllButton.runningCount
                        fontSize: height
                    }
                }
            }

        } // end of RowLayout
    } // end of mainToolbar

    Loader
    {
        id: showWizardOverlay
        width: parent.width
        height: parent.height
        source: parent.showWizardVisible ? "qrc:/ShowWizard.qml" : ""
        z: 100
        onLoaded: if (item) { item.closeRequested.connect(function() { mainView.showWizardVisible = false }); item.open() }
    }

    Item
    {
        id: mainViewArea
        width: parent.width
        height: parent.height - (mainToolbar.visible ? mainToolbar.height : 0)
        y: mainToolbar.visible ? mainToolbar.height : 0

        // One Loader per top-level tab, activated lazily on first visit and
        // then kept alive for the lifetime of the app - switching tabs only
        // changes which one is visible. This avoids destroying and rebuilding
        // an entire tab's item tree (in particular every fixture item in
        // Fixtures & Functions' 2D/3D preview) on every switch.
        Loader
        {
            id: fnfLoader
            anchors.fill: parent
            active: false
            visible: mainView.currentContext === "FIXANDFUNC"
            source: mainView.contextResources["FIXANDFUNC"]

            // Now that this tab's item tree survives a switch away instead of
            // being destroyed (see the comment above), anything that used to
            // rely on that destruction to implicitly close itself - e.g. a
            // floating channel-tool popup - needs closing explicitly here.
            onVisibleChanged:
            {
                if (!visible && item && typeof item.closeChannelTools === "function")
                    item.closeChannelTools()
            }
        }
        Loader
        {
            id: vcLoader
            anchors.fill: parent
            active: false
            visible: mainView.currentContext === "VC"
            source: mainView.contextResources["VC"]

            // See fnfLoader's onVisibleChanged above - same guard, applied
            // uniformly to every persistent tab Loader so any future
            // floating popup exposing closeChannelTools() is covered
            // without needing this file touched again.
            onVisibleChanged:
            {
                if (!visible && item && typeof item.closeChannelTools === "function")
                    item.closeChannelTools()
            }
        }
        Loader
        {
            id: sdeskLoader
            anchors.fill: parent
            active: false
            visible: mainView.currentContext === "SDESK"
            source: mainView.contextResources["SDESK"]

            // Simple Desk has its own independent ChannelToolLoader (see
            // SimpleDesk.qml's closeChannelTools()) - this is the same bug
            // class as fnfLoader above, just for the Simple Desk tab.
            onVisibleChanged:
            {
                if (!visible && item && typeof item.closeChannelTools === "function")
                    item.closeChannelTools()
            }
        }
        Loader
        {
            id: showmgrLoader
            anchors.fill: parent
            active: false
            visible: mainView.currentContext === "SHOWMGR"
            source: mainView.contextResources["SHOWMGR"]

            onVisibleChanged:
            {
                if (!visible && item && typeof item.closeChannelTools === "function")
                    item.closeChannelTools()
            }
        }
        Loader
        {
            id: iomgrLoader
            anchors.fill: parent
            active: false
            visible: mainView.currentContext === "IOMGR"
            source: mainView.contextResources["IOMGR"]

            onVisibleChanged:
            {
                if (!visible && item && typeof item.closeChannelTools === "function")
                    item.closeChannelTools()
            }
        }

        // transient overlay used by loadResource() for one-off resources
        // that aren't one of the standard tabs above (e.g. the UI Settings
        // editor) - unlike the Loaders above, this one is torn down again
        // once its resource is cleared.
        Loader
        {
            id: overlayLoader
            anchors.fill: parent
            z: 10
            visible: source !== ""
        }

        Component.onCompleted:
        {
            var ctx = "FIXANDFUNC"
            // handle Kiosk mode on startup
            if (qlcplus.accessMask === App.AC_VCControl)
                ctx = "VC"
            enableContext(ctx, true)
        }
    }

    PopupNetworkConnect { id: clientAccessPopup }

    // "Invert Selection in Group(s)" disambiguation dialog - lives at the
    // root so it's reachable regardless of which view/panel triggered
    // contextManager.invertGroupSelection() (Ctrl+G, the Fixture Groups
    // panel button, or the 2D view's "Groups" settings row all call the same
    // C++ method).
    PopupInvertGroupSelection { id: invertGroupSelectionPopup }

    // Warns a user capturing a new Virtual Console key binding (the
    // auto-detect flow in KeyboardSequenceDelegate.qml) that the sequence
    // they just picked already shadows a built-in ShortcutManager action -
    // see VirtualConsole::handleKeyEvent()'s auto-detection branch.
    CustomPopupDialog
    {
        id: shortcutCollisionPopup
        standardButtons: Dialog.Ok
        title: qsTr("Keyboard shortcut already in use")
    }

    // ADR 0001 decision 4 - legacy Show timeline beat-value warning + its
    // per-Show conversion step. See showLegacyShowTimingWarning() above.
    LegacyShowTimingDialog
    {
        id: legacyShowTimingDialog
        onConvertRequested: (showId, showName) => legacyShowTimingConvertDialog.openFor(showId, showName)
    }

    LegacyShowTimingConvertDialog
    {
        id: legacyShowTimingConvertDialog
        onConverted: (showId) =>
        {
            var idx = -1
            for (var i = 0; i < legacyShowTimingDialog.shows.length; i++)
            {
                if (legacyShowTimingDialog.shows[i].id === showId)
                {
                    idx = i
                    break
                }
            }
            if (idx >= 0)
                legacyShowTimingDialog.dismissAt(idx)
        }
    }

    Connections
    {
        target: contextManager
        ignoreUnknownSignals: true
        function onCandidateGroupsForInversionReady(groups)
        {
            invertGroupSelectionPopup.groups = groups
            invertGroupSelectionPopup.open()
        }
    }

    // Non-blocking media notice shown after a project was opened, in one of
    // two variants: Audio/Video sources living outside the media folder
    // (showExternalMediaNotice, offers Collect) or managed copies whose
    // origin file changed on disk since the import (showChangedMediaNotice,
    // offers Reload). The external variant comes first; the changed one
    // follows once that is collected or dismissed. Sits just under the
    // main toolbar, above the views. Nothing is ever collected or reloaded
    // unasked.
    Rectangle
    {
        id: externalMediaBanner
        visible: false
        width: parent.width
        height: UISettings.iconSizeDefault
        y: mainToolbar.visible ? mainToolbar.height : 0
        z: 98
        color: UISettings.bgStrong
        border.width: 1
        border.color: UISettings.bgLight

        property int externalCount: 0
        property int changedCount: 0
        // which variant is showing: "external", "changed" or "" (hidden)
        property string mode: ""

        function refresh()
        {
            if (externalCount > 0)
                mode = "external"
            else if (changedCount > 0)
                mode = "changed"
            else
                mode = ""
            visible = mode !== ""
        }

        function dismiss()
        {
            if (mode === "external")
                externalCount = 0
            else if (mode === "changed")
                changedCount = 0
            refresh()
        }

        RowLayout
        {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            RobotoText
            {
                Layout.fillWidth: true
                height: parent.height
                label: externalMediaBanner.mode === "changed"
                       ? qsTr("%1 media file(s) changed on disk since they were imported. " +
                              "Reload them to pick up the new version.").arg(externalMediaBanner.changedCount)
                       : qsTr("%1 audio/video file(s) referenced by this project live outside its media folder. " +
                              "Collect them into the project so it can be moved as a whole.").arg(externalMediaBanner.externalCount)
            }

            // inside a Layout only the Layout.* sizes count, a plain
            // width would be overridden by the (zero) implicit width
            GenericButton
            {
                Layout.preferredWidth: contentWidth + 24
                Layout.preferredHeight: externalMediaBanner.height - 6
                label: externalMediaBanner.mode === "changed" ? qsTr("Reload") : qsTr("Collect into project")
                onClicked:
                {
                    if (externalMediaBanner.mode === "changed")
                        mainView.reloadChangedMedia()
                    else
                        mainView.collectMediaIntoProject()
                }
            }

            GenericButton
            {
                Layout.preferredWidth: contentWidth + 24
                Layout.preferredHeight: externalMediaBanner.height - 6
                label: qsTr("Dismiss")
                onClicked: externalMediaBanner.dismiss()
            }
        }
    }

    // Result of a media action (collect, nothing to clean up, ...)
    CustomPopupDialog
    {
        id: mediaResultPopup
        standardButtons: Dialog.Ok
    }

    // "Remove unused media" confirmation: the files are deleted only when
    // Yes is clicked; Escape, No or closing the dialog deletes nothing.
    CustomPopupDialog
    {
        id: unusedMediaPopup
        width: mainView.width / 2
        title: qsTr("Remove unused media")
        standardButtons: Dialog.Yes | Dialog.No

        property var files: []

        contentItem:
            ColumnLayout
            {
                spacing: 8

                Text
                {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    wrapMode: Text.Wrap
                    font.family: UISettings.robotoFontName
                    font.pixelSize: UISettings.textSizeDefault
                    color: UISettings.fgMain
                    text: qsTr("The following %1 file(s) in the project's media folder are not used by any function anymore. " +
                               "Delete them from disk? This cannot be undone.").arg(unusedMediaPopup.files.length)
                }

                ListView
                {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: Math.min(count, 10) * UISettings.listItemHeight
                    clip: true
                    model: unusedMediaPopup.files
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: CustomScrollBar { }

                    delegate:
                        RobotoText
                        {
                            width: ListView.view.width
                            height: UISettings.listItemHeight
                            fontSize: UISettings.textSizeDefault * 0.8
                            labelColor: UISettings.fgLight
                            label: modelData
                        }
                }
            }

        onClicked: function(role)
        {
            if (role !== Dialog.Yes)
                return

            var ok = qlcplus.removeUnusedMedia(unusedMediaPopup.files)
            mediaResultPopup.title = qsTr("Remove unused media")
            mediaResultPopup.message = ok ? qsTr("%1 file(s) deleted.").arg(unusedMediaPopup.files.length)
                                          : qsTr("Not every file could be deleted - see the log for details.")
            mediaResultPopup.open()
        }
    }

    /** Menu to open/load/save a project */
    ActionsMenu
    {
        id: actionsMenu
        x: 1
        y: actEntry.height + 1
        visible: false
        z: visible ? 99 : 0
    }

    /** Allow a project (.qxw/.qxw.gz) or fixture (.qxf/.d4) file to be opened
      * by dragging it from the OS file manager and dropping it on the window.
      * Qt Quick delivers drag hover/position events to only the topmost
      * DropArea under the pointer - "keys" only gates acceptance
      * (containsDrag/onDropped), not hit testing. Being a full-window
      * overlay, this would otherwise always win that hit test and starve any
      * nested DropArea underneath it (e.g. the fixture editor's channel
      * reordering) of position updates. So it drops below the main view's
      * content (z < 0) for as long as UISettings.internalDragActive says an
      * in-app drag is going on anywhere, and only then. */
    DropArea
    {
        id: fileDropArea
        anchors.fill: parent
        z: UISettings.internalDragActive ? -1 : 100
        keys: [ "text/uri-list" ]

        onDropped: function(drop)
        {
            if (drop.urls.length)
                actionsMenu.openFile(drop.urls[0])
        }
    }

    Rectangle
    {
        anchors.fill: parent
        z: 100
        visible: fileDropArea.containsDrag
        color: Qt.rgba(0, 0, 0, 0.6)
        border.width: 3
        border.color: UISettings.activeDropArea

        Text
        {
            anchors.centerIn: parent
            text: qsTr("Drop a project or fixture file to open it")
            font.pixelSize: UISettings.textSizeDefault * 1.4
            color: "white"
        }
    }

    /* Rectangle covering the whole window to
     * have a dimmered background for popups */
    Rectangle
    {
        id: dimScreen
        anchors.fill: parent
        visible: false
        z: 99
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    //PopupDisclaimer { }
}
