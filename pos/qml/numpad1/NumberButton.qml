import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import org.flowee 1.0

Rectangle {
    property int size: 40
    property bool doubleWidth: false
    property bool doubleHeight: false
    property string text: "0"

    signal clicked

    width: {
        if (doubleWidth)
            return size * 2 + parent.spacing
        return size
    }
    height: {
        if (doubleHeight) return size * 2 + parent.spacing
        return size
    }

    color: "#e9e9e9"
    border.color: "#444444"
    border.width: 1
    radius: 3
    Text {
        anchors.centerIn: parent
        text: parent.text
    }

    MouseArea {
        anchors.fill: parent
        onClicked: parent.clicked()
    }
}
