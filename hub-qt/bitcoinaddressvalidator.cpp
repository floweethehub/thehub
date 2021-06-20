/*
 * This file is part of the Flowee project
 * Copyright (c) 2011-2014 The Bitcoin Core developers
 * Copyright (C) 2017,2019 Tom Zander <tom@flowee.org>
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

#include "bitcoinaddressvalidator.h"

#include <encodings_legacy.h>

#include <Application.h>
#include <cashaddr.h>

/* Base58 characters are:
     "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

  This is:
  - All numbers except for '0'
  - All upper-case letters except for 'I' and 'O'
  - All lower-case letters except for 'l'
*/

BitcoinAddressEntryValidator::BitcoinAddressEntryValidator(QObject *parent) :
    QValidator(parent)
{
}

QValidator::State BitcoinAddressEntryValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);

    // Empty address is "intermediate" input
    if (input.isEmpty())
        return QValidator::Intermediate;

    enum Type {
        Old,
        Cash
    };
    static const QString CashPrefix = "bitcoincash:";
    Type type = (input.startsWith('q') || input.startsWith('p')
                 || input.startsWith(CashPrefix.left(10))) ? Cash : Old;

    // Correction
    for (int idx = 0; idx < input.size();) {
        bool removeChar = false;
        QChar ch = input.at(idx);
        // Corrections made are very conservative on purpose, to avoid
        // users unexpectedly getting away with typos that would normally
        // be detected, and thus sending to the wrong address.
        switch(ch.unicode())
        {
        // Qt categorizes these as "Other_Format" not "Separator_Space"
        case 0x200B: // ZERO WIDTH SPACE
        case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE
            removeChar = true;
            break;
        default:
            break;
        }

        // Remove whitespace
        if (ch.isSpace())
            removeChar = true;

        // To next character
        if (removeChar) {
            input.remove(idx, 1);
        } else {
            if (type == Cash && ch.isUpper())
                input[idx] = input.at(idx).toLower();
            ++idx;
        }
    }

    int idx = 0;
    if (type == Cash) { // skip over the 'bitcoincash:' prefix
        QString left = input.left(12);
        if (CashPrefix.left(left.size()) == left)
            idx = left.size();
    }
    for (; idx < input.size(); ++idx) {
        int ch = input.at(idx).unicode();

        switch (type) {
        case Old:
            // Alphanumeric and not a 'forbidden' character
            if (!(((ch >= '0' && ch<='9') || (ch >= 'a' && ch<='z') || (ch >= 'A' && ch<='Z'))
                  && ch != 'l' && ch != 'I' && ch != '0' && ch != 'O'))
                return QValidator::Invalid;
            break;

        case Cash:
            if (!(((ch >= '0' && ch<='9') || (ch >= 'a' && ch<='z'))
                    && ch != 'i' && ch != 'b' && ch != 'i' && ch != 'o'))
                return QValidator::Invalid;
        break;
        }
    }

    return QValidator::Acceptable;
}

BitcoinAddressCheckValidator::BitcoinAddressCheckValidator(QObject *parent) :
    QValidator(parent)
{
}

QValidator::State BitcoinAddressCheckValidator::validate(QString &input, int &pos) const
{
    Q_UNUSED(pos);
    // Validate the passed Bitcoin address
    CBitcoinAddress addr(input.toStdString());
    if (addr.IsValid())
        return QValidator::Acceptable;

    CashAddress::Content c = CashAddress::decodeCashAddrContent(input.toStdString(), "bitcoincash");
    if ((c.type == CashAddress::PUBKEY_TYPE || c.type == CashAddress::SCRIPT_TYPE) && c.hash.size() == 20)
        return QValidator::Acceptable;

    return QValidator::Invalid;
}
