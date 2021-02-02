/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tom@flowee.org>
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
#ifndef BROADCASTTXDATA_H
#define BROADCASTTXDATA_H

#include <primitives/FastTransaction.h>

#include <string>

class BroadcastTxData
{
public:
    BroadcastTxData(const Tx &tx);

    virtual ~BroadcastTxData();

    enum RejectReason {
        InvalidTx = 0x10,
        DoubleSpend = 0x12,
        NonStandard = 0x40,
        Dust = 0x41,
        LowFee = 0x42
    };

    /**
     * @brief txRejected is called with the remote peers reject message.
     * @param reason the reason for the reject, notice that this is foreign input from
     *         a random node on the Intenet. The value doesn't have to be included in
               the enum.
     * @param message
     */
    virtual void txRejected(RejectReason reason, const std::string &message) = 0;
    virtual void sentOne() = 0;

    /// the wallet, or privacy segment this transaction is associated with.
    virtual uint16_t privSegment() const = 0;

    inline Tx transaction() const {
        return m_tx;
    }
    inline const uint256 &hash() const {
        return m_hash;
    }

private:
    const Tx m_tx;
    const uint256 m_hash;
};

#endif
