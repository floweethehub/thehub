/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import org.flowee 1.0

import "numpad1"

ApplicationWindow {
    id: mainWindow
    width: 300
    height: 480
    visible: true
    minimumHeight: 200
    minimumWidth: 200
    title: qsTr("PoS")

    Component.onCompleted: {
        Payments.connectToDB()
    }

    Pane {
        id: contentArea
        anchors.fill: parent

        Item {
            id: inputPaymentScreen

            width: parent.width
            height: parent.height

            Calculator {
                id: calculator
            }

            Rectangle {
                id: totalPrice
                width: parent.width * 0.8
                x: parent.width / 10
                height: currencySign.height + 24

                color: "white"
                border.width: 1
                border.color: "#DDDDDD"

                Text {
                    id: currencySign
                    text: "€"
                    x: 15
                    y: 12
                }
                Text {
                    id: price
                    text: calculator.currentValue
                    anchors.top: currencySign.top
                    anchors.right: totalPrice.right
                    anchors.rightMargin: 15
                }
            }
            Text {
                id: subtotal
                text: calculator.totalValue
                visible: calculator.hasTotalValue
                anchors.top: totalPrice.bottom
                anchors.topMargin: 6
                anchors.right: totalPrice.right
                anchors.rightMargin: price.anchors.rightMargin
            }

            ListView {
                clip: true
                model: calculator.historic
                anchors.top: subtotal.bottom
                anchors.bottom: numpad.top
                anchors.left: totalPrice.left
                anchors.right: totalPrice.right

                delegate: Text {
                    text: modelData
                    font.pixelSize: 12
                    color: "#444444"
                    anchors.right: parent.right
                }
            }

            Numpad {
                id: numpad
                anchors.horizontalCenter: totalPrice.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 80
            }

            NavigationButton {
                id: navigationButton
                text: "Betalen »"
                color: preferredButton

                anchors.right: parent.right
                enabled: price.text != "0.00"
                onClicked: {
                    // startNewPayment takes up to 3 arguments. The total price (in local currency)
                    // the merchant-comment (useful to connect to a separate POS)
                    // and optionally a currency-id. (like EUR) Omitted in this example
                    if (Payments.paymentStep === Payments.NoPayment) // avoid double click
                        Payments.startNewPayment(calculator.priceInCents, "test");
                }
            }

            Behavior on x { NumberAnimation { duration: 300 } }
            Behavior on opacity { OpacityAnimator { duration: 200 } }
        }

        Item {
            id: showQR
            width: parent.width
            height: parent.height

            Image {
                id: qr
                source: {
                    if (Payments.paymentStep === Payments.ShowPayment) {
                        return "image://qr/cashaddr"
                    }
                    return ""
                }
                width: 180
                height: 180
                smooth: false

                anchors.horizontalCenter: parent.horizontalCenter
            }

            Column {
                width: 200
                spacing: 10

                anchors.top: qr.bottom
                anchors.topMargin: 16
                x: 20

                Text {
                    id: title
                    text: qsTr("Please pay in Bitcoin Cash:")
                }
                Item {
                    width: bchLogo.width + wholeBch.width
                    height: wholeBch.height + microBch.height + localCurrency.height
                    Image {
                        id: bchLogo
                        source: "qrc:/images/bitcoincash32.png"
                        y: (wholeBch.height + microBch.height - height) / 2
                    }
                    Text {
                        id: wholeBch
                        text: Payments.current.amountFormatted + " BCH"
                        anchors.top: parent.top
                        anchors.left: bchLogo.right
                        anchors.leftMargin: 4
                    }
                    Text {
                        id: microBch
                        text: {
                            var amount = Payments.current.amountBch / 100.0
                            return amount + " ųBCH";
                        }
                        anchors.top: wholeBch.bottom
                        anchors.topMargin: 2
                        anchors.left: wholeBch.left
                    }

                    Text {
                        id: localCurrency
                        text: Qt.locale().currencySymbol()
                        anchors.right: bchLogo.right
                        anchors.top: microBch.bottom
                        anchors.topMargin: 2
                    }
                    Text {
                        text: Number(Payments.current.amountNative / 100).toLocaleString(Qt.locale(), 'f', 2)
                        anchors.left: microBch.left
                        anchors.top: localCurrency.top
                    }
                }

                Text {
                    x: title.width - width
                    visible: Payments.current !== null
                    text:  "1 BCH is " + Qt.locale().currencySymbol() + " "
                           + Number(Payments.current.exchangeRate / 100).toLocaleString(Qt.locale(), 'f', 2)
                    font.pointSize: 8
                    color: "#eeeeee"
                }
            }

            NavigationButton {
                text: qsTr("« Wijzig")
                onClicked: Payments.back()
            }

            Behavior on x { NumberAnimation { duration: 300 } }
            Behavior on opacity { OpacityAnimator { duration: 200 } }
        }

        state: "input"
        states: [
            State {
                name: "input"
                when: Payments.paymentStep === Payments.NoPayment
                PropertyChanges {
                    target: showQR; x: width + 10; opacity: 0
                }
            },
            State {
                name: "showQr"
                when: Payments.paymentStep === Payments.ShowPayment
                PropertyChanges {
                    target: inputPaymentScreen; x: -width - 10; opacity: 0
                }
            }
        ]
    }

    Item {
        id: missingNetwork
        width: parent.width
        height: 80
        y: {
            if (Payments.connected === Payments.Connected)
                return parent.height + 10
            return parent.height - height
        }

        Behavior on y { NumberAnimation { duration: 300 } }

        Rectangle {
            width: parent.width - 10
            x: 5
            height: parent.height + 20
            radius: 20
            color: "#ffae4b"
            border.width: 3
            border.color: "#7d5524"
        }

        Text {
            text: "Connecting"
            font.pointSize: 18
            anchors.centerIn: parent
        }
    }
}
