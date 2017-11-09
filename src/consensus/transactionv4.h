/*
 * This file is part of the Flowee project
 * Copyright (C) 2016 Tom Zander <tomz@freedommail.ch>
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
#ifndef TRANSACTION_4_H
#define TRANSACTION_4_H

namespace Consensus {
enum TxMessageFields {
    TxEnd = 0,          // BoolTrue
    TxInPrevHash,       // sha256 (Bytearray)
    TxInPrevIndex,      // PositiveNumber
    TxInputStackItem,  // bytearray
    TxInputStackItemContinued, // bytearray
    TxOutValue,         // PositiveNumber (in satoshis)
    TxOutScript,        // bytearray
    TxRelativeBlockLock,// PositiveNumber
    TxRelativeTimeLock, // PositiveNumber
    CoinbaseMessage     // Bytearray. Max 100 bytes.
};
}

#endif
