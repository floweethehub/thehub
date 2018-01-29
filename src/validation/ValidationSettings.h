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

#ifndef VALIDATIONSETTINGS_H
#define VALIDATIONSETTINGS_H

#include <string>

class CBlockIndex;
class ValidationSettingsPrivate;

namespace Blocks {
class DB;
}
#include <memory>

namespace Validation {
class Engine;

/**
 * @brief The Validation::Settings can be used to set settings and also is a Future for a block being validated.
 *
 * The Block validation code is entirely a-synchronous. Adding a block will return immediately and processing
 * will happen in a separate worker thread.
 *
 * The method Validation::Engine:addBlock() returns an instance of Settings. This instance allows you
 * to set more settings on the block before the actual validation starts and also it allows you to introspect
 * information about the validation as it happens in the background.
 *
 * Notice that this means that should you keep a copy of the Settings, you delay validation until either
 * the settings object has its destructor run, or you explicitly call start();
 */
class Settings {
public:
    Settings();
    Settings(const Validation::Settings &other);
    ~Settings();

    /// Starts the validation of the block.
    Validation::Settings start();

    /**
     * After the header validation has succeded and no errors have been found, a block index will be created.
     * A BlockIndex in various cases will not have its ownership moved to the Blocks::DB, for instance
     * if no direct line to the genesis was found.
     * In such cases the blockIndex is owned by the ValidationSettings instance and the pointer will
     * become dangling as soon as the Settings object is deleted!
     * Please note that the index may not have a height or parent yet, it is guarenteed to have a blockhash.
     */
    CBlockIndex *blockIndex() const;

    /// After block validation is finished any validation errors will be stored here.
    const std::string &error() const;

    /**
     * @brief turning off the Proof-of-Work check will skip this check and avoid failing on an incorrect one.
     * @see setOnlyCheckValidity()
     * @see setCheckMerkleRoot()
     */
    void setCheckPoW(bool on);
    /**
     * @brief turning off the merkle root check will skip this check and avoid failing on an incorrect one.
     * @see setOnlyCheckValidity()
     * @see setCheckPoW()
     */
    void setCheckMerkleRoot(bool on);

    /**
     * @brief If false, skip general transaction validity checks before reporting header validated.
     * Defaults to true.
     */
    void setCheckTransactionValidity(bool on);

    /**
     * @brief only check validity, when enabled, will avoid adding the block to the chain and the index, just do validation.
     * Validation includes headers, basic block / transaction well-formed-ness, fees, utxo. But not signatures and mempool updates etc are skipped too.
     * Default off.
     * @see setCheckPoW()
     * @see setCheckMerkleRoot()
     */
    void setOnlyCheckValidity(bool on);

    /**
     * @brief waitUntilFinished returns when the block has finished validation.
     * A block that isn't very close to the tip may be finished after only inspecting its header.
     * Blocks that are expected to become the new tip will cause this method to block until the
     * main chain and mempool etc are updated.
     */
    void waitUntilFinished() const;

    /**
     * @brief waitHeaderFinshed won't return until the header has been validated and a blockIndex() assigned.
     * On valid blocks the blockIndex() getter will return an actual index.
     * @see blockIndex()
     */
    void waitHeaderFinished() const;

    Validation::Settings operator=(const Validation::Settings&);

private:
    friend class Validation::Engine;
    std::shared_ptr<ValidationSettingsPrivate> d;
};
}

#endif
