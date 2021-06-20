/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
import org.flowee 1.0

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

        InputPaymentScreen {
            id: inputPaymentScreen
            height: parent.height
            width: parent.width
        }

        ShowQR {
            id: showQR
            height: parent.height
            width: parent.width
        }

        state: "input"
        states: [
            State {
                name: "input"
                when: Payments.paymentStep === Payments.NoPayment
                PropertyChanges {
                    target: showQR; x: width + 10; opacity: 0
                }
                PropertyChanges {
                    target: completedPaymentScreen; x: width + 10; opacity: 0
                }
            },
            State {
                name: "showQr"
                when: Payments.paymentStep >= Payments.ShowPayment
                PropertyChanges {
                    target: inputPaymentScreen; x: -width - 10; opacity: 0
                }
                PropertyChanges {
                    target: completedPaymentScreen; x: -width - 10; opacity: 0
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
