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

#ifndef TXVALIDATION_P_H
#define TXVALIDATION_P_H

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the validation component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */


#include "BlockValidation_p.h"
#include <primitives/FastTransaction.h>
#include <mutex>

class CTransaction;

namespace Validation {
    uint32_t countSigOps(const CTransaction &tx);
}

class TxValidationState  : public std::enable_shared_from_this<TxValidationState> {
public:
    enum InternalFlags {
        FromMempool = 0x1000000 ///< A transaction with this flag was confirmed before and now is re-added to the mempool
    };
    TxValidationState(const std::weak_ptr<ValidationEnginePrivate> &parent, const Tx &transaction, uint32_t onValidationFlags = 0);
    ~TxValidationState();
    std::weak_ptr<ValidationEnginePrivate> m_parent;
    Tx m_tx;
    std::uint32_t m_validationFlags;
    std::promise<std::string> m_promise;
    std::int32_t m_originatingNodeId;
    std::uint64_t m_originalInsertTime;

    // Data for double-spend-notifications (done in notifyDoubleSpend())
    Tx m_doubleSpendTx;
    int m_doubleSpendProofId = -1;

    void checkTransaction();
    /// Only called when fully successful, to be called in the strand.
    void sync();

    /// to be called in the strand
    void notifyDoubleSpend();
};


#endif
