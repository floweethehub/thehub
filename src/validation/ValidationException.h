/*
 * This file is part of the flowee project
 * Copyright (C) 2017-2018 Tom Zander <tomz@freedommail.ch>
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

#ifndef VALIDATIONEXCEPTION_H
#define VALIDATIONEXCEPTION_H

#include <stdexcept>
#include <string>

namespace Validation {

enum RejectCodes {
    NotRejected = 0,
    RejectMalformed = 0x01,
    RejectInvalid = 0x10,
    RejectObsolete = 0x11,
    RejectDuplicate = 0x12,
    RejectExceedsLimit = 0x13,
    RejectNonstandard = 0x40,
    RejectDust = 0x41,
    RejectInsufficientFee = 0x42,
    RejectCheckpoint = 0x43,
    /** Reject codes greater or equal to this can be returned by AcceptToMemPool
     * for transactions, to signal internal conditions. They cannot and should not
     * be sent over the P2P network.
     */
    RejectInternal = 0x100,
    /** Too high fee. Can not be triggered by P2P transactions */
    RejectHighfee = 0x100,
    /** Transaction is already known (either in mempool or blockchain) */
    RejectAlreadyKnown = 0x101,
    /** Transaction conflicts with a transaction already known */
    RejectConflict = 0x102
};

enum CorruptionPossible {
    InvalidNotFatal //< This marks a block as clearly invalid but that doesn't mean any other block with this blockheader is
};

class Exception : public std::runtime_error {
public:
    Exception(const std::string &error, int punishment = 100);
    Exception(const std::string &error, CorruptionPossible p);
    Exception(const std::string &error, Validation::RejectCodes rejectCode, int punishment = 100);

    inline int punishment() const {
        return m_punishment;
    }

    inline Validation::RejectCodes rejectCode() const {
        return m_rejectCode;
    }

    inline bool corruptionPossible() const {
        return m_corruptionPossible;
    }

private:
    int m_punishment;
    RejectCodes m_rejectCode;
    bool m_corruptionPossible = false;
};
}

#endif
