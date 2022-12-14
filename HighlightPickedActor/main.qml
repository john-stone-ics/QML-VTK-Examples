import QtQuick 2.15
import QtQuick.Window 2.15

import Vtk 1.0 as Vtk

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")

    Rectangle {
      anchors.fill: parent
      color: "yellow"
      opacity: 0.2

    }

    Vtk.MyVtkItem {
        anchors.fill: parent
        anchors.margins: 10
        opacity: 0.7
    }

    Rectangle {
      anchors.centerIn: parent
      width: 50
      height: 50
      color: "cyan"
      opacity: 0.7
    }
}
