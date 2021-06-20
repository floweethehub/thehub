/*
 * This file is part of the flowee project
 * Copyright (C) 2017-2018 Tom Zander <tom@flowee.org>
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

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the validation component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

#include <uint256.h>
#include <mutex>
#include <condition_variable>

class CBlockIndex;
class BlockValidationState;

struct ValidationSettingsPrivate {
    ValidationSettingsPrivate();

    void startRun();

    // allows waitHeaderFinished to return.
    // the usage of the info and the hash is such that the user can't use it longer than the lifetime of the settings object.
    void setBlockIndex(CBlockIndex *info);
    void markFinished();

    std::shared_ptr<BlockValidationState> state;
    CBlockIndex *blockIndex = nullptr;
    std::mutex lock;
    std::condition_variable waitVariable;
    std::string error;

    uint256 blockHash;
    /// ref-count of the Settings main object, when it hits 0 we start the validation.
    short ref;
    bool headerFinished = false;
    bool finished = false;
    bool started = false;
};

#endif
