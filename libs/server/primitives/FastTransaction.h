/*
 * This file is part of the Flowee project
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


#ifndef FLOWEE_PRIMITIVES_FASTTRANSACTION_H
#define FLOWEE_PRIMITIVES_FASTTRANSACTION_H

#include <streaming/ConstBuffer.h>

#include <uint256.h>

class CTransaction;
class FastBlock;
class TxTokenizer;

namespace Streaming {
    class BufferPool;
}

/**
 * @brief The Tx class is a Bitcoin transaction in canonical form.
 * The Tx object is a thin wrapper around a buffer of data which is known to be a Bitcoin transaction.
 *
 * @see CTransaction, FastBlock
 */
class Tx
{
public:
    enum Component {
        TxVersion = 1, ///< int
        PrevTxHash = 2 , ///< 32-bytes hash (uint256)
        PrevTxIndex = 4, ///< int or uint64_t
        TxInScript = 8, ///< var-length const-buffer
        Sequence = 0x10, /// uint32_t
        OutputValue = 0x20, ///< uint64_t
        OutputScript = 0x40, ///< var-length const-buffer
        LockTime = 0x80, ///< uint32_t
        End = 0x100
    };

    /// creates invalid transaction.
    Tx();

    Tx(const Streaming::ConstBuffer &rawTransaction);
    // Tx(const Tx &other); // mark as having default implementation.
    // TODO assignment and copy constructor

    /**
     * @brief isValid returns true if it has a known backing memory store.
     * Notice that this method doesn't do validation of the transaction data.
     */
    inline bool isValid() const {
        return m_data.isValid();
    }

    /**
     * Returns the version number of a transaction.
     */
    uint32_t txVersion() const;

    /**
     * Hashes the transaction content and returns the sha256 double hash.
     * The hash is often also called the transaction-ID.
     */
    uint256 createHash() const;

    /**
     * for backwards compatibility with existing code this loads the transaction into a CTransaction class.
     */
    CTransaction createOldTransaction() const;

    /**
     * @brief offsetInBlock returns the amount of bytes into the block this transaction is positioned.
     * @param block the block it is supposed to be part of.
     * @return a simple subtraction of pointers, if the argument block doesn't contain the
     *      tx, the result is unspecified (but typically bad)
     */
    int64_t offsetInBlock(const FastBlock &block) const;

    static Tx fromOldTransaction(const CTransaction &transaction, Streaming::BufferPool *pool = 0);

    /**
     * @return the bytecount of this transaction.
     */
    inline int size() const {
        return m_data.size();
    }

    /**
     * @brief The Iterator class allows one to iterate over a ConstBuffer-backed transaction or block.
     * The Tx class doesn't have a random-access API for its contents because the class doesn't read
     * all the data into memory. This makes it significantly faster for many usecases and easier on
     * memory consumtion which is why Flowee followed this trade-off.
     * The correct way to find certain transaction data is to start an iterator and find it by 'walking'
     * over the transaction explicitly.
     *
     * Notice that little to no checks are done in the API for correct usage, which means that you could
     * request the LockTime variable as a uint256Data(), which is a bad idea (possible crash). So be
     * careful about the data-types you read actually matching the tag().
     */
    class Iterator {
    public:
        /// Constructor
        Iterator(const Tx &tx);
        /// This iterator skips the block-header and reads the first transaction. After a Tx::End
        /// it continues to the next transaction. At the end of the block Tx::Ends will continue
        /// repeatedly.
        Iterator(const FastBlock &block);
        Iterator(const Iterator &other) = delete;
        Iterator(const Iterator && other);
        ~Iterator();
        /**
         * @brief next seeks to find the next tag.
         * @param filter allows you to filter which tags you want to find. You can pass in multiple
         *         enum values OR-ed together. Notice that Tx::End will always implicitly be included
         *         in the filter.
         * @return The output of 'tag()'
         * Please be aware that this method can throw a runtime_error should the transaction encounter
         * partial or missing data.
         */
        Component next(int filter = 0);
        /// Return the current tag found.
        Component tag() const;

        /**
         * @brief prevTx creates a transaction object should you have gotten to the Tx::End tag.
         * Its very important to realize this method returns the content from the start of the transaction
         * to the current location, as such the only way to get a proper full transaction is just after
         * next() returned Tx::End
         *
         * Notice that the returned Tx is a zero-copy instance pointing to the same ConstData as backed by
         * the original Block.
         */
        Tx prevTx() const;

        /// Return the value of the current tag as a ConstBuffer.
        Streaming::ConstBuffer byteData() const;
        // Return the value of the current tag as a 32-bit signed int
        int32_t intData() const;
        // Return the value of the current tag as a 32-bit unsigned int
        uint32_t uintData() const;
        // Return the value of the current tag as a 64-bit unsigned int
        uint64_t longData() const;
        // Return the value of the current tag as a 256-bit unsigned 'int'
        uint256 uint256Data() const;

        void operator=(const Iterator &other) = delete;
    private:
        TxTokenizer *d;
    };

private:
    Streaming::ConstBuffer m_data;
};

#endif
