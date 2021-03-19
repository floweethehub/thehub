/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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
#include "BlockMetaData.h"

#include <deque>
#include <string.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

#include <primitives/script.h>

constexpr int TxRowWidth = 40;
constexpr int32_t FEE_INVALID = 0xFFFFFF;

// tags used to save our data file with.
enum Tags {
    BlockID = 0,
    BlockHeight,
    IsCTOR,
    TransactionDataBlob
};

bool BlockMetaData::hasFeesData() const
{
    return first()->fees != FEE_INVALID;
}

BlockMetaData::BlockMetaData(const Streaming::ConstBuffer &buffer)
    : m_data(buffer)
{
    Streaming::MessageParser parser(buffer);
    while (parser.next() != Streaming::EndOfDocument) {
        if (parser.tag() == BlockID)
            m_blockId = parser.uint256Data();
        else if (parser.tag() == BlockHeight)
            m_blockHeight = parser.intData();
        else if (parser.tag() == IsCTOR)
            m_ctorSorted = parser.boolData();
        else if (parser.tag() == TransactionDataBlob) {
            assert(parser.isByteArray());
            m_transactions = parser.bytesDataBuffer();
        }
    }
}

BlockMetaData::BlockMetaData()
{
}

BlockMetaData BlockMetaData::parseBlock(int blockHeight, const FastBlock &block,
                                        const std::vector<std::unique_ptr<std::deque<std::int32_t> > > &perTxFees,
                                        Streaming::BufferPool &pool)
{
    std::deque<TransactionData> txs;
    Tx::Iterator iter(block);
    bool endFound = false;
    bool isCTOR = true;
    bool coinbase = true;
    TransactionData currentTx;
    currentTx.scriptTags = 0;
    uint256 txidBeforeThis; // the txid of the transaction placed before the current in the block
    uint256 prevTxHash; // a copy from the input

    size_t chunkIndex = 0;
    size_t feeIndex = 0;
    std::deque<std::int32_t> *chunk = nullptr;
    if (!perTxFees.empty())
        chunk = perTxFees.at(chunkIndex).get();

    while (iter.next()) {
        if (iter.tag() == Tx::End) {
            if (endFound) // done
                break;
            Tx tx = iter.prevTx();
            uint256 txid = tx.createHash();
            memcpy(currentTx.txid, txid.begin(), 32);
            currentTx.offsetInBlock = tx.offsetInBlock(block);
            currentTx.fees = 0;

            if (!coinbase && chunk) {
                if (chunk->size() <= feeIndex) { // next chunk
                    feeIndex = 0;
                    chunk = nullptr;
                    if (perTxFees.size() > ++chunkIndex) {
                        chunk = perTxFees.at(chunkIndex).get();
                        assert(!chunk->empty());
                    }
                }

                if (chunk) {
                    int fees = chunk->at(feeIndex++);
                    if (fees >= 0 && fees < FEE_INVALID) // fits in our field
                        currentTx.fees = fees;
                    else
                        currentTx.fees = FEE_INVALID; // -1, fee too heigh for our cache
                }
            }
            else if (coinbase && chunk == nullptr) {
                currentTx.fees = FEE_INVALID; // -1, mark that we don't have fee info in this BMD
            }

            coinbase = false;
            txs.push_back(currentTx);
            currentTx.scriptTags = 0;

            if (txs.size() >= 2) {
                if (isCTOR && txs.size() > 2)
                    isCTOR = txid.Compare(txidBeforeThis) > 0;
                if (isCTOR)
                    txidBeforeThis = txid;
            }
            endFound = true;
            continue;
        }
        endFound = false;
        if (iter.tag() == Tx::PrevTxHash) {
            prevTxHash = iter.uint256Data();
        }
        else if (iter.tag() == Tx::PrevTxIndex) {

        }
        else if (iter.tag() == Tx::OutputScript) {
            const CScript script(iter.byteData());
            if (script.IsPayToScriptHash()) {
                currentTx.scriptTags |= Api::ScriptTag::P2SH;
                continue;
            }
            auto iter = script.begin();
            opcodetype type;
            while (script.GetOp(iter, type)) {
                switch (type) {
                case OP_RETURN:
                    currentTx.scriptTags |= Api::ScriptTag::OpReturn;
                    break;
                case OP_CHECKDATASIG:
                case OP_CHECKDATASIGVERIFY:
                    currentTx.scriptTags |= Api::ScriptTag::OpCheckDataSig;
                    break;
                case OP_CHECKSIG:
                case OP_CHECKSIGVERIFY:
                    currentTx.scriptTags |= Api::ScriptTag::OpChecksig;
                    break;
                case OP_CHECKMULTISIG:
                case OP_CHECKMULTISIGVERIFY:
                    currentTx.scriptTags |= Api::ScriptTag::OpCheckmultisig;
                    break;
                case OP_CHECKLOCKTIMEVERIFY:
                    currentTx.scriptTags |= Api::ScriptTag::OpCheckLockTimeverify;
                    break;
                default:
                    break;
                };
            }

        }
    }

    pool.reserve(txs.size() * TxRowWidth);
    for (size_t i = 0; i < txs.size(); ++i) {
        static_assert(sizeof(TransactionData) == TxRowWidth, "Jump table row size");
        memcpy(pool.begin() + i * TxRowWidth, &txs[i], TxRowWidth);
    }
    auto txData = pool.commit(txs.size() * TxRowWidth);

    pool.reserve(txData.size() + 55);
    Streaming::MessageBuilder builder(pool);
    builder.add(BlockID, block.createHash());
    builder.add(BlockHeight, blockHeight);
    builder.add(IsCTOR, isCTOR);
    builder.add(TransactionDataBlob, txData);

    return BlockMetaData(pool.commit());
}

const BlockMetaData::TransactionData *BlockMetaData::findTransaction(const uint256 &txid) const
{
    const char *currentTx = m_transactions.begin();
    bool first = true;
    while (currentTx < m_transactions.end()) {
        const uint256 *curTxId = reinterpret_cast<const uint256*>(currentTx);

        int comp = curTxId->Compare(txid);
        if (comp == 0) {
            return reinterpret_cast<const BlockMetaData::TransactionData*>(currentTx);
        } else if (!first && m_ctorSorted && comp > 0) {
            break; // CTOR, stop searching in sorted list
        }
        first = false; // only the first transaction does not follow the CTOR layout.
        currentTx += TxRowWidth;
    }
    return nullptr;
}

const BlockMetaData::TransactionData *BlockMetaData::findTransaction(int offsetInBlock) const
{
    assert(offsetInBlock > 0);
    uint32_t oib = static_cast<uint32_t>(offsetInBlock);
    const char *currentTx = m_transactions.begin();
    while (currentTx < m_transactions.end()) {
        auto tx = reinterpret_cast<const BlockMetaData::TransactionData*>(currentTx);
        if (tx->offsetInBlock == oib)
            return tx;
        if (tx->offsetInBlock > oib)
            break;
        currentTx += TxRowWidth;
    }
    return nullptr;
}


const BlockMetaData::TransactionData *BlockMetaData::first() const
{
    return reinterpret_cast<const BlockMetaData::TransactionData*>(m_transactions.begin());
}

int BlockMetaData::txCount() const
{
    return m_transactions.size() / TxRowWidth;
}

const BlockMetaData::TransactionData *BlockMetaData::tx(int index) const
{
    assert(index >= 0);
    auto *ptr = m_transactions.begin() + index * sizeof(TransactionData);
    if (ptr > m_transactions.end() - sizeof(TransactionData))
        throw std::runtime_error("Index out of bounds");
    return reinterpret_cast<const BlockMetaData::TransactionData*>(ptr);
}

Streaming::ConstBuffer BlockMetaData::data() const
{
    return m_data;
}

int BlockMetaData::blockHeight() const
{
    return m_blockHeight;
}

bool BlockMetaData::ctorSorted() const
{
    return m_ctorSorted;
}

uint256 BlockMetaData::blockId() const
{
    return m_blockId;
}
