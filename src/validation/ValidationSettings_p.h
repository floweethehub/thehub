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

#ifndef VALIDATIONSETTINGS_P_H
#define VALIDATIONSETTINGS_P_H

#include <uint256.h>
#include <mutex>
#include <condition_variable>

class CBlockIndex;
class BlockValidationState;

struct ValidationSettingsPrivate {
    ValidationSettingsPrivate();

    void startRun();

    // allows waitHeaderValidated to return.
    // the usage of the info and the hash is such that the user can't use it longer than the lifetime of the settings object.
    // pass in a hash and we'll set it on the info if it doesn't have one yet.
    void setBlockIndex(CBlockIndex *info, const uint256 &hash);
    void markFinished();

    std::shared_ptr<BlockValidationState> state;
    CBlockIndex *blockIndex;
    std::mutex lock;
    std::condition_variable waitVariable;
    std::string error;

    short ref;
    bool headerFinished;
    bool finished;
    uint256 blockHash;
};

#endif
