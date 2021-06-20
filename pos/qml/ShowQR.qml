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
import org.flowee 1.0

Item {
    id: showQR
    Image {
        id: qr
        source: {
            if (Payments.paymentStep >= Payments.ShowPayment)
                return "image://qr/cashaddr"
            return ""
        }
        width: 180
        height: 180
        smooth: false
        opacity: Payments.paymentStep === Payments.CompletedPayment ? 0.3 : 1.0

        anchors.horizontalCenter: parent.horizontalCenter
    }
    
    Column {
        id: payInstructions
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
            color: "#444444"
        }
    }

    Text {
        width: parent.width
        anchors.top: payInstructions.bottom
        anchors.topMargin: 6
        font.pointSize: 10
        color: Payments.paymentStep === Payments.CompletedPayment ? "#478559" : "#c6374a"
        wrapMode: Text.Wrap

        text: {
            if (Payments.current === null)
                return ""
            var paid = Payments.current.amountPaid
            if (paid === 0)
                return "" // waiting
            var wantedAmount = Payments.current.amountBch
            if (paid === wantedAmount)
                return "Paid"
            if (paid < wantedAmount) {
                return qsTr("Partial payment received: %1 ųBCH. Still due: %2 %3")
                        .arg(Number(paid / 100).toLocaleString(Qt.locale(), 'f', 2))
                        .arg(Qt.locale().currencySymbol())
                        .arg(Number((wantedAmount - paid) * Payments.current.exchangeRate / 1E10).toLocaleString(Qt.locale(), 'f', 2))
            }
            return qsTr("Customer tipped: %1 %2")
                        .arg(Qt.locale().currencySymbol())
                        .arg(Number((paid - wantedAmount ) * Payments.current.exchangeRate / 1E10).toLocaleString(Qt.locale(), 'f', 2))
        }
    }
    Text {
        visible: Payments.paymentStep === Payments.CompletedPayment
        text: qsTr("Paid")
        rotation: -30
        color: "#478559"
        font.pointSize: 32
        font.bold: true
        anchors.horizontalCenter: parent.horizontalCenter
        y: 60
    }

    NavigationButton {
        visible: Payments.paymentStep === Payments.ShowPayment
        text: qsTr("« Adjust")
        onClicked: Payments.back()
    }

    NavigationButton {
        anchors.right: parent.right
        text: qsTr("Close")
        onClicked: Payments.close()
    }

    Behavior on x { NumberAnimation { duration: 300 } }
    Behavior on opacity { OpacityAnimator { duration: 200 } }
}
