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

#include "Engine.h"
#include "ValidationSettings.h"
#include "BlockValidation_p.h"
#include <Application.h>

Validation::Settings::Settings()
    : d(new ValidationSettingsPrivate())
{
}

Validation::Settings::Settings(const Settings &other)
    : d(other.d)
{
    ++d->ref;
}

Validation::Settings::~Settings()
{
    if (!--d->ref) {
        d->startRun();
    }
}

Validation::Settings Validation::Settings::start()
{
    d->startRun();
    return *this;
}

CBlockIndex *Validation::Settings::blockIndex() const
{
    return d->blockIndex;
}

const std::string &Validation::Settings::error() const
{
    return d->error;
}

void Validation::Settings::setCheckPoW(bool on)
{
    assert(d->state.get());
    d->state->m_checkPow = on;
}

void Validation::Settings::setCheckMerkleRoot(bool on)
{
    assert(d->state.get());
    d->state->m_checkMerkleRoot = on;
}

void Validation::Settings::setCheckTransactionValidity(bool on)
{
    assert(d->state.get());
    d->state->m_checkTransactionValidity = on;
}

void Validation::Settings::setOnlyCheckValidity(bool on)
{
    assert(d->state.get());
    d->state->m_checkValidityOnly = on;
}

void Validation::Settings::waitHeaderFinished()
{
    std::unique_lock<decltype(d->lock)> lock(d->lock);
    if (d->state.get())
        logDebug(Log::BlockValidation) << "Doing a waitHeaderFinished() before start(), possible deadlock";
    while (!d->headerFinished)
        d->waitVariable.wait(lock);
}

Validation::Settings Validation::Settings::operator=(const Validation::Settings &other)
{
    ++other.d->ref;
    if (!--d->ref)
        d->startRun();
    d = other.d;
    return *this;
}

void Validation::Settings::waitUntilFinished()
{
    std::unique_lock<decltype(d->lock)> lock(d->lock);
    if (d->state.get())
        logDebug(Log::BlockValidation) << "Doing a waitUntilFinished() before start(), possible deadlock";
    while (!d->finished)
        d->waitVariable.wait(lock);
}

ValidationSettingsPrivate::ValidationSettingsPrivate()
    : blockIndex(nullptr),
    ref(1),
    headerFinished(false),
    finished(false)
{
}

void ValidationSettingsPrivate::setBlockIndex(CBlockIndex *index, const uint256 &hash)
{
    std::unique_lock<decltype(lock)> waitLock(lock);
    assert(index);
    blockIndex = index;
    if (blockIndex->phashBlock == nullptr) {
        // yes this feels less than grand, lets schedule a rewrite of the blockindex database as this is as good as it gets.
        blockIndex->phashBlock = &blockHash;
    }
    blockHash = hash;
    headerFinished = true;
    waitVariable.notify_all();
}


void ValidationSettingsPrivate::startRun()
{
    if (state.get()) {
        Application::instance()->ioService().post(std::bind(&BlockValidationState::checks1NoContext, state));
        state.reset();
    }
}

void ValidationSettingsPrivate::markFinished()
{
    std::unique_lock<decltype(lock)> waitLock(lock);
    finished = true;
    headerFinished = true;
    waitVariable.notify_all();
}
