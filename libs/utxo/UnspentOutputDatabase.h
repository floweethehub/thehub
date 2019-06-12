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
#ifndef UNSPENTOUTPUTDATABASE_H
#define UNSPENTOUTPUTDATABASE_H

#include "UTXOInteralError.h"

#include <uint256.h>

#include <streaming/ConstBuffer.h>
#include <streaming/BufferPool.h>

#include <boost/asio/io_service.hpp>
#include <set>

/**
 * @brief The UnspentOutput class is a mem-mappable "leaf" in the UnspentOutputDatabase.
 *
 * A single unspentOutput instance maps to a single UTXO entry.
 * With the information in this class you can open the relevant block and find the transaction
 * to iterate over in order to find the output that is being refered to.
 * In other words, this is mostly just an index onto the actual block-chain which actually stores
 * the real unspent output.
 *
 */
class UnspentOutput {
public:
    UnspentOutput() = default;
    UnspentOutput(Streaming::BufferPool &pool, const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock);
    UnspentOutput(uint64_t cheapHash, const Streaming::ConstBuffer &buffer);
    UnspentOutput(const UnspentOutput &other) = default;

    inline bool isValid() const {
        return m_data.size() >= 33;
    }

    uint256 prevTxId() const;
    inline int outIndex() const {
        return m_outIndex;
    }
    /// return the offset in the block. In bytes. Notice that offsetInBlock < 91 implies this is a coinbase.
    inline int offsetInBlock() const {
        return m_offsetInBlock;
    }
    inline int blockHeight() const {
        return m_blockHeight;
    }

    bool isCoinbase() const;

    inline const Streaming::ConstBuffer &data() const {
        return m_data;
    }

    /// return the UnspentOutputDatabase internal to make remove faster
    /// pass in to UnspentOutputDatabase::remove() if available
    uint64_t rmHint() const {
        return m_privData;
    }

    /// \internal
    inline void setRmHint(uint64_t hint) {
        m_privData = hint;
    }

private:
    friend class UnspentOutputDatabase;
    Streaming::ConstBuffer m_data;
    int m_outIndex = 0;
    int m_offsetInBlock = 0; // in bytes. 2GB blocks is enough for a while.
    int m_blockHeight = 0;
    uint64_t m_cheapHash = 0;
    uint64_t m_privData = 0; // used by the database for cache
};

class SpentOutput {
public:
    int blockHeight = -1;
    int offsetInBlock = -1;
    bool isValid() const { return blockHeight > 0; }
};

class UODBPrivate;
/// The unspent outputs database. Also known as the UTXO
class UnspentOutputDatabase
{
public:
    UnspentOutputDatabase(boost::asio::io_service &service, const boost::filesystem::path &basedir);
    UnspentOutputDatabase(UODBPrivate *priv);
    ~UnspentOutputDatabase();

    static UnspentOutputDatabase *createMemOnlyDB(const boost::filesystem::path &basedir);

    /// Change limits to be smaller, for instance for regtest setups
    static void setSmallLimits();

    /**
     * Set the amount of changes (inserts/deletes) that should trigger an save.
     * The UTXO gathers all changes in memory and has processes to push those slowly
     * to disk for permanent storage.
     * If we store too often the system slows down and we end up saving data that might
     * have been deleted in the next block.
     * If we store too little we may run out of memory.
     *
     * This method allows you to set when a save is to be started, based on the amount
     * of changes made. Note that the standard UTXO usage shows that we save around half
     * the amount of record vs changes.
     * But when this database is used as a TXID-DB we store about 120% of the records vs changes.
     *
     * This is much more about usecase than it is about how much memory you have.
     */
    static void setChangeCountCausesStore(int count);

    struct BlockData {
        struct TxOutputs { // can hold all the data for a single transaction
            TxOutputs(const uint256 &id, int offsetInBlock, int firstOutput, int lastOutput = -1)
                : txid(id),
                  offsetInBlock(offsetInBlock),
                  firstOutput(firstOutput),
                  lastOutput(lastOutput < firstOutput ? firstOutput : lastOutput) {
            }
            uint256 txid;
            int firstOutput = 0, lastOutput = 0;
            int offsetInBlock = 0;
        };
        int blockHeight = -1;
        std::vector<TxOutputs> outputs;
    };
    void insertAll(const BlockData &data);

    /**
     * @brief insert a new spendable output.
     * @param txid the (prev) transaction id.
     * @param output Index the index of the output.
     * @param blockHeight which block the output is in.
     * @param offsetInBlock the amount of bytes into the block the tx is positioned.
     */
    void insert(const uint256 &txid, int outIndex, int blockHeight, int offsetInBlock);

    /**
     * @brief find an output by (prev) txid and output-index.
     * @param txid the previous transaction index. The one that created the output.
     * @param index the output index inside the prev-tx.
     * @return A filled UnspentOutput if found, or an invalid one otherwise.
     */
    UnspentOutput find(const uint256 &txid, int index) const;

    /**
     * @brief remove an output from the unspend database.
     * This spends an output, forgetting it from the latest tree of the DB.
     * @param txid the previous transaction index. The one that created the output.
     * @param index the output index inside the prev-tx.
     * @return valid SpendOutput if something was found to remove
     */
    SpentOutput remove(const uint256 &txid, int index, uint64_t rmHint = 0);

    /**
     * The blockFinished should be called after every block to update the UnspentOutput DB
     * about which block we just finished.
     *
     * It is essential that this happens in a synchronous manner so we know that if we
     * need to restart from this point, we can start from the next block and the UTXO is
     * consistent with the full block passed in via the args.
     *
     * @see rollback
     */
    void blockFinished(int blockheight, const uint256 &blockId);

    /**
     * Changes made since the last blockFinished() call are reverted.
     */
    void rollback();

    /**
     * Save (some) caches to disk.
     * The DB triggers saving of caches to disk based on how many changes
     * have been made, which means that if nothing happens then we won't save.
     * This method can be called periodically to use a time-based saving.
     */
    void saveCaches();

    int blockheight() const;
    uint256 blockId() const;

private:
    UODBPrivate *d;
};

#endif
