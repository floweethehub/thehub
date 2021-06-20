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

import "numpad1"

Item {
    id: inputPaymentScreen
    
    Calculator {
        id: calculator
    }
    
    Connections {
        target: Payments
        onCurrentChanged: {
            if (Payments.current === null)
                calculator.clearAll()
        }
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
        text: qsTr("Pay »")
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
