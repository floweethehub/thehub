import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import org.flowee 1.0

Row {
    id: numpad1
    width: buttonSize * 5 + spacing * 5
    height: buttonSize * 4 + spacing * 3
    spacing: 10

    property int buttonSize: 40

    Column {
        id: mainButtons
        spacing: parent.spacing
        Repeater {
            model: 3
            delegate: Row {
                id: row
                property int rowNum: index
                spacing: numpad1.spacing
                Repeater {
                    model: 3
                    delegate:  NumberButton {
                        size: numpad1.buttonSize
                        text: index + 3 * (3 - row.rowNum) - 2
                        onClicked: calculator.addCharacter(text)
                    }
                }
            }
        }
        
        Row {
            spacing: parent.spacing
            NumberButton {
                size: numpad1.buttonSize
                doubleWidth: true
                text: "0"
                onClicked: calculator.addCharacter(text)
            }
            NumberButton {
                size: numpad1.buttonSize
                text: "."
                onClicked: calculator.addCharacter(text)
            }
        }
    }
    Column {
        spacing: parent.spacing

        NumberButton {
            size: numpad1.buttonSize
            text: "X"
            onClicked: calculator.startMultiplication()
        }
        NumberButton {
            size: numpad1.buttonSize
            text: "+"
            onClicked: calculator.addToTotal()
        }
        NumberButton {
            size: numpad1.buttonSize
            doubleHeight: true
            text: "="
            onClicked: calculator.subtotalButtonPresssed()
        }
    }
    Item { height: 10; width: 1 }
    Column {
        spacing: parent.spacing

        NumberButton {
            size: numpad1.buttonSize
            text: "←"
            onClicked: calculator.backspace()
        }
        NumberButton {
            size: numpad1.buttonSize
            text: "AC"
            onClicked: calculator.clearAll()
        }
    }
}
