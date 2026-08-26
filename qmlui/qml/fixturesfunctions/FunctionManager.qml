/*
  Q Light Controller Plus
  FunctionManager.qml

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
    id: fmContainer
    anchors.fill: parent
    color: "transparent"
    clip: true

    property bool allowEditing: true

    signal requestView(int ID, string qmlSrc, bool back)
    signal doubleClicked(int ID, int type)

    function loadFunctionEditor(funcID, funcType)
    {
        if (!(qlcplus.accessMask & App.AC_FunctionEditing))
            return

        //console.log("Request to open Function editor. ID: " + funcID + " type: " + funcType)
        functionManager.setEditorFunction(funcID, false, false)
        functionManager.viewPosition = functionsListView.contentY
        var editorRes = functionManager.getEditorResource(funcID)

        if (funcType === QLCFunction.ShowType)
        {
            showManager.currentShowID = funcID
            mainView.switchToContext("SHOWMGR", editorRes)
        }
        else
            fmContainer.requestView(funcID, editorRes, false)
    }

    function setFunctionFilter(fType, checked)
    {
        if (checked === true)
            functionManager.setFunctionFilter(fType, true);
        else
            functionManager.setFunctionFilter(fType, false);
    }

    ColumnLayout
    {
      anchors.fill: parent
      spacing: 3

      Rectangle
      {
        id: topBar
        Layout.fillWidth: true
        height: UISettings.iconSizeMedium
        z: 5
        gradient: Gradient
        {
            GradientStop { position: 0; color: UISettings.toolbarStartSub }
            GradientStop { position: 1; color: UISettings.toolbarEnd }
        }

        RowLayout
        {
            width: parent.width
            height: parent.height
            y: 1

            spacing: 4

            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/scene.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.SceneType
                tooltip: qsTr("Scenes")
                counter: functionManager.sceneCount
                onCheckedChanged: setFunctionFilter(QLCFunction.SceneType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/chaser.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.ChaserType
                tooltip: qsTr("Chasers")
                counter: functionManager.chaserCount
                onCheckedChanged: setFunctionFilter(QLCFunction.ChaserType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/sequence.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.SequenceType
                tooltip: qsTr("Sequences")
                counter: functionManager.sequenceCount
                onCheckedChanged: setFunctionFilter(QLCFunction.SequenceType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/efx.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.EFXType
                tooltip: qsTr("EFX")
                counter: functionManager.efxCount
                onCheckedChanged: setFunctionFilter(QLCFunction.EFXType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/collection.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.CollectionType
                tooltip: qsTr("Collections")
                counter: functionManager.collectionCount
                onCheckedChanged: setFunctionFilter(QLCFunction.CollectionType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/rgbmatrix.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.RGBMatrixType
                tooltip: qsTr("RGB Matrices")
                counter: functionManager.rgbMatrixCount
                onCheckedChanged: setFunctionFilter(QLCFunction.RGBMatrixType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/showmanager.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.ShowType
                tooltip: qsTr("Shows")
                counter: functionManager.showCount
                onCheckedChanged: setFunctionFilter(QLCFunction.ShowType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/script.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.ScriptType
                tooltip: qsTr("Scripts")
                counter: functionManager.scriptCount
                onCheckedChanged: setFunctionFilter(QLCFunction.ScriptType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/audio.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.AudioType
                tooltip: qsTr("Audio")
                counter: functionManager.audioCount
                onCheckedChanged: setFunctionFilter(QLCFunction.AudioType, checked)
            }
            IconButton
            {
                z: 2
                width: height
                height: topBar.height - 2
                imgSource: "qrc:/video.svg"
                checkable: true
                checked: functionManager.functionsFilter & QLCFunction.VideoType
                tooltip: qsTr("Videos")
                counter: functionManager.videoCount
                onCheckedChanged: setFunctionFilter(QLCFunction.VideoType, checked)
            }

            Rectangle { Layout.fillWidth: true }

            IconButton
            {
                id: searchFunc
                z: 2
                width: height
                height: topBar.height - 2
                bgColor: UISettings.bgMedium
                faColor: checked ? "white" : "gray"
                faSource: FontAwesome.fa_magnifying_glass
                checkable: true
                tooltip: qsTr("Set a Function search filter")
                onToggled:
                {
                    functionManager.searchFilter = ""
                    if (checked)
                        sTextInput.forceActiveFocus()
                }
            }
        }
      }

      Rectangle
      {
          id: searchBox
          visible: searchFunc.checked
          Layout.fillWidth: true
          height: UISettings.iconSizeMedium
          z: 5
          color: UISettings.bgMedium
          radius: 5
          border.width: 2
          border.color: UISettings.borderColorDark

          TextInput
          {
              id: sTextInput
              y: 3
              height: parent.height - 6
              width: parent.width
              color: UISettings.fgMain
              text: functionManager.searchFilter
              font.family: UISettings.robotoFontName
              font.pixelSize: parent.height - 6
              selectionColor: UISettings.highlightPressed
              selectByMouse: true

              onTextEdited: functionManager.searchFilter = text
          }
      }

      TreeFlatModel
      {
          id: flatFunctionsModel
          sourceModel: functionManager.functionsList
      }

      Connections
      {
          target: functionManager
          function onFunctionsListChanged() { flatFunctionsModel.rebuild() }
      }

      ListView
      {
          id: functionsListView
          Layout.fillWidth: true
          height: fmContainer.height - topBar.height - (searchBox.visible ? searchBox.height : 0)
          z: 4
          boundsBehavior: Flickable.StopAtBounds
          Layout.fillHeight: true

          Component.onCompleted: contentY = functionManager.viewPosition
          property bool dragActive: false

          model: flatFunctionsModel
          delegate: FunctionManagerFlatDelegate { width: functionsListView.width }

          ScrollBar.vertical: CustomScrollBar { id: fMgrScrollBar }

              // "deselection" mouse area
              MouseArea
              {
                  y: functionsListView.contentHeight
                  height: Math.max(parent.height - functionsListView.contentHeight, 0)
                  width: parent.width

                  onClicked:
                  {
                      functionManager.selectFunctionID(-1, 0)
                      functionManager.selectFolder("", 0)
                  }
              }

              GenericMultiDragItem
              {
                  id: fDragItem

                  property bool fromFunctionManager: true

                  visible: functionsListView.dragActive

                  Drag.active: functionsListView.dragActive
                  Drag.source: fDragItem
                  Drag.keys: [ "function" ]

                  function itemDropped(id, name)
                  {
                      var path = functionManager.functionPath(id)
                      functionManager.moveFunctions(path)
                  }

                  onItemsListChanged:
                  {
                      console.log("Items in list: " + itemsList.length)
                      if (itemsList.length)
                      {
                          var funcRef = functionManager.getFunction(itemsList[0])
                          itemLabel = funcRef.name
                          itemIcon = functionManager.functionIcon(funcRef.type)
                      }
                  }
              }
      } // ListView

    } // ColumnLayout
}
