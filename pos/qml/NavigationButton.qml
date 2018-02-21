import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import org.flowee 1.0

Rectangle {
    signal clicked

    property alias text: label.text
    property color preferredButton: "#33a747"
    property color standardButton: "#e9e9e9"

    color: standardButton
    border.color: "black"
    border.width: 1
    width: 120
    height: 40
    radius: 6
    
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 20

    Text {
        id: label
        anchors.centerIn: parent
    }
    MouseArea {
        anchors.fill: parent
        onClicked: parent.clicked()
    }
}
