import QtQuick 2.15
import QtQuick.Window 2.15

import Vtk 1.0 as Vtk

Window {
    width: 640
    height: 480
    visible: true
    title: qsTr("Hello World")

    Vtk.MyVtkItem {
        anchors.fill: parent
    }
}
