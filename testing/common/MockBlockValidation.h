/*
 * This file is part of the Flowee project
 * Copyright (c) 2015 The Bitcoin Core developers
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

#ifndef MOCKBLOCKVALIDATION_H
#define MOCKBLOCKVALIDATION_H

#include <primitives/key.h>
#include <txmempool.h>
#include <validation/Engine.h>
#include <primitives/script.h>
#include <primitives/FastBlock.h>

class MockBlockValidation : public Validation::Engine {
public:
    MockBlockValidation();
    ~MockBlockValidation();

    void initSingletons();
    FastBlock createBlock(CBlockIndex *parent, const CScript& scriptPubKey, const std::vector<CTransaction>& txns = std::vector<CTransaction>()) const;
    /// short version of the above
    FastBlock createBlock(CBlockIndex *parent);

    /**
     * @brief appendGenesis creates the standard reg-test genesis and appends.
     * This will only succeed if the current chain (Params()) is REGTEST
     */
    void appendGenesis();


    enum OutputType {
        EmptyOutScript,
        StandardOutScript,
        FullOutScript // full p2pkh output script
    };

    /**
     * @brief Append a list of blocks to the block-validator and wait for them to be validated.
     * @param blocks the amount of blocks to add to the blockchain-tip.
     * @param coinbaseKey [out] an empty key we will initialize and use as coinbase.
     * @param out one of the OutputType members.
     */
    std::vector<FastBlock> appendChain(int blocks, CKey &coinbaseKey, OutputType out = StandardOutScript);

    inline std::vector<FastBlock> appendChain(int blocks, OutputType out = StandardOutScript) {
        CKey key;
        return appendChain(blocks, key, out);
    }

    uint32_t tipValidationFlags(bool requireStandard = false) const;

    /**
     * @brief This creates a chain of blocks on top of a random index.
     * @param parent the index that is to be extended
     * @param blocks the amount of blocks to build.
     * @return The full list of blocks.
     * This method doesn't add the blocks, use appendChain() for that.
     */
    std::vector<FastBlock> createChain(CBlockIndex *parent, int blocks) const;

    CTxMemPool mp;
};

#endif
