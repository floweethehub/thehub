/*
 * This file is part of the Flowee project
 * Copyright (c) 2012 Pieter Wuille
 * Copyright (c) 2012-2015 The Bitcoin Core developers
 * Copyright (c) 2017 Tom Zander <tomz@freedommail.ch>
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

#include "addrman.h"

#include "hash.h"

struct ValidateRAII {
    CAddrMan *parent;
    ValidateRAII(CAddrMan *parent) : parent(parent) {
#ifdef DEBUG_ADDRMAN
        int err = parent->validateInteral();
        if (err != 0)
            logCritical(Log::Addrman) << "ADDRMAN CONSISTENCY CHECK FAILED!!! err:" << err;
#endif
    }
    ~ValidateRAII() {
#ifdef DEBUG_ADDRMAN
        int err = parent->validateInteral();
        if (err != 0)
            logCritical(Log::Addrman) << "ADDRMAN CONSISTENCY CHECK FAILED!!! err:" << err;
#endif
    }
};

bool CAddrInfo::getKnowsXThin() const
{
    return fKnowsXThin;
}

void CAddrInfo::setKnowsXThin(bool value)
{
    fKnowsXThin = value;
}

int CAddrInfo::getUselessness() const
{
    return uselessness;
}

void CAddrInfo::setUselessness(int value)
{
    uselessness = std::max(0, value);
}

int64_t CAddrInfo::getLastSuccess() const
{
    return nLastSuccess;
}

void CAddrInfo::Init()
{
    nLastSuccess = 0;
    nLastTry = 0;
    nAttempts = 0;
    nRefCount = 0;
    fInTried = false;
    fKnowsXThin = false;
    uselessness = 0;
    nRandomPos = -1;
}

int CAddrInfo::GetTriedBucket(const uint256& nKey) const
{
    uint64_t hash1 = (CHashWriter(SER_GETHASH, 0) << nKey << GetKey()).GetHash().GetCheapHash();
    uint64_t hash2 = (CHashWriter(SER_GETHASH, 0) << nKey << GetGroup() << (hash1 % ADDRMAN_TRIED_BUCKETS_PER_GROUP)).GetHash().GetCheapHash();
    return static_cast<int>(hash2 % ADDRMAN_TRIED_BUCKET_COUNT);
}

int CAddrInfo::GetNewBucket(const uint256& nKey, const CNetAddr& src) const
{
    std::vector<unsigned char> vchSourceGroupKey = src.GetGroup();
    uint64_t hash1 = (CHashWriter(SER_GETHASH, 0) << nKey << GetGroup() << vchSourceGroupKey).GetHash().GetCheapHash();
    uint64_t hash2 = (CHashWriter(SER_GETHASH, 0) << nKey << vchSourceGroupKey << (hash1 % ADDRMAN_NEW_BUCKETS_PER_SOURCE_GROUP)).GetHash().GetCheapHash();
    return static_cast<int>(hash2 % ADDRMAN_NEW_BUCKET_COUNT);
}

int CAddrInfo::GetBucketPosition(const uint256 &nKey, bool fNew, int nBucket) const
{
    uint64_t hash1 = (CHashWriter(SER_GETHASH, 0) << nKey << (fNew ? 'N' : 'K') << nBucket << GetKey()).GetHash().GetCheapHash();
    return static_cast<int>(hash1 % ADDRMAN_BUCKET_SIZE);
}

bool CAddrInfo::IsTerrible(int64_t nNow) const
{
    if (nLastTry && nLastTry >= nNow - 60) // never remove things tried in the last minute
        return false;

    if (nTime > nNow + 10 * 60) // came in a flying DeLorean
        return true;

    if (nTime == 0 || nNow - nTime > ADDRMAN_HORIZON_DAYS * 24 * 60 * 60) // not seen in recent history
        return true;

    if (nLastSuccess == 0 && nAttempts >= ADDRMAN_RETRIES) // tried N times and never a success
        return true;

    if (nNow - nLastSuccess > ADDRMAN_MIN_FAIL_DAYS * 24 * 60 * 60 && nAttempts >= ADDRMAN_MAX_FAILURES) // N successive failures in the last week
        return true;

    return false;
}

double CAddrInfo::GetChance(int64_t nNow) const
{
    double fChance = 1.0;

    int64_t nSinceLastTry = nNow - nLastTry;

    if (nSinceLastTry < 0)
        nSinceLastTry = 0;

    // deprioritize very recent attempts away
    if (nSinceLastTry < 60 * 10)
        fChance *= 0.01;

    // deprioritize 66% after each failed attempt, but at most 1/28th to avoid the search taking forever or overly penalizing outages.
    fChance *= pow(0.66, std::min(nAttempts, 8));

    if (uselessness > 0)
        fChance /= uselessness * 10;
    else if (fKnowsXThin)
        fChance *= 2;

    return fChance;
}


CAddrInfo *CAddrMan::Find(const CNetAddr &addr)
{
    std::lock_guard<std::mutex> guard(lock);
    return Find_(addr);
}

CAddrInfo* CAddrMan::Find_(const CNetAddr& addr, int* pnId)
{
    std::map<CNetAddr, int>::iterator it = mapAddr.find(addr);
    if (it == mapAddr.end())
        return nullptr;
    if (pnId)
        *pnId = (*it).second;
    std::map<int, CAddrInfo>::iterator it2 = mapInfo.find((*it).second);
    if (it2 != mapInfo.end())
        return &(*it2).second;
    return nullptr;
}

CAddrInfo* CAddrMan::Create(const CAddress& addr, const CNetAddr& addrSource, int* pnId)
{
    int nId = nIdCount++;
    mapInfo[nId] = CAddrInfo(addr, addrSource);
    mapAddr[addr] = nId;
    mapInfo[nId].nRandomPos = static_cast<int>(vRandom.size());
    vRandom.push_back(nId);
    if (pnId)
        *pnId = nId;
    return &mapInfo[nId];
}

void CAddrMan::SwapRandom(unsigned int nRndPos1, unsigned int nRndPos2)
{
    if (nRndPos1 == nRndPos2)
        return;

    assert(nRndPos1 < vRandom.size() && nRndPos2 < vRandom.size());

    int nId1 = vRandom[nRndPos1];
    int nId2 = vRandom[nRndPos2];

    assert(mapInfo.count(nId1) == 1);
    assert(mapInfo.count(nId2) == 1);

    mapInfo[nId1].nRandomPos = nRndPos2;
    mapInfo[nId2].nRandomPos = nRndPos1;

    vRandom[nRndPos1] = nId2;
    vRandom[nRndPos2] = nId1;
}

void CAddrMan::Delete(int nId)
{
    assert(mapInfo.count(nId) != 0);
    CAddrInfo& info = mapInfo[nId];
    assert(!info.fInTried);
    assert(info.nRefCount == 0);

    SwapRandom(static_cast<unsigned int>(info.nRandomPos), static_cast<unsigned int>(vRandom.size() - 1));
    vRandom.pop_back();
    mapAddr.erase(info);
    mapInfo.erase(nId);
    nNew--;
}

void CAddrMan::ClearNew(int nUBucket, int nUBucketPos)
{
    // if there is an entry in the specified bucket, delete it.
    if (vvNew[nUBucket][nUBucketPos] != -1) {
        int nIdDelete = vvNew[nUBucket][nUBucketPos];
        CAddrInfo& infoDelete = mapInfo[nIdDelete];
        assert(infoDelete.nRefCount > 0);
        infoDelete.nRefCount--;
        vvNew[nUBucket][nUBucketPos] = -1;
        if (infoDelete.nRefCount == 0) {
            Delete(nIdDelete);
        }
    }
}

void CAddrMan::MarkTried(CAddrInfo& info, int nId)
{
    // remove the entry from all new buckets
    for (int bucket = 0; bucket < ADDRMAN_NEW_BUCKET_COUNT; bucket++) {
        int pos = info.GetBucketPosition(nKey, true, bucket);
        if (vvNew[bucket][pos] == nId) {
            vvNew[bucket][pos] = -1;
            info.nRefCount--;
        }
    }
    nNew--;

    assert(info.nRefCount == 0);

    // which tried bucket to move the entry to
    int nKBucket = info.GetTriedBucket(nKey);
    int nKBucketPos = info.GetBucketPosition(nKey, false, nKBucket);

    // first make space to add it (the existing tried entry there is moved to new, deleting whatever is there).
    if (vvTried[nKBucket][nKBucketPos] != -1) {
        // find an item to evict
        int nIdEvict = vvTried[nKBucket][nKBucketPos];
        assert(mapInfo.count(nIdEvict) == 1);
        CAddrInfo& infoOld = mapInfo[nIdEvict];

        // Remove the to-be-evicted item from the tried set.
        infoOld.fInTried = false;
        vvTried[nKBucket][nKBucketPos] = -1;
        nTried--;

        // find which new bucket it belongs to
        int nUBucket = infoOld.GetNewBucket(nKey);
        int nUBucketPos = infoOld.GetBucketPosition(nKey, true, nUBucket);
        ClearNew(nUBucket, nUBucketPos);
        assert(vvNew[nUBucket][nUBucketPos] == -1);

        // Enter it into the new set again.
        infoOld.nRefCount = 1;
        vvNew[nUBucket][nUBucketPos] = nIdEvict;
        nNew++;
    }
    assert(vvTried[nKBucket][nKBucketPos] == -1);

    vvTried[nKBucket][nKBucketPos] = nId;
    nTried++;
    info.fInTried = true;
}

void CAddrMan::Good(const CService& addr, int64_t nTime)
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);
    int nId;
    CAddrInfo* pinfo = Find_(addr, &nId);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    info.nLastSuccess = nTime;
    info.nLastTry = nTime;
    info.nAttempts = 0;
    // nTime is not updated here, to avoid leaking information about
    // currently-connected peers.

    // if it is already in the tried set, don't do anything else
    if (info.fInTried)
        return;

    // find a bucket it is in now
    int nRnd = GetRandInt(ADDRMAN_NEW_BUCKET_COUNT);
    int nUBucket = -1;
    for (unsigned int n = 0; n < ADDRMAN_NEW_BUCKET_COUNT; n++) {
        int nB = (n + nRnd) % ADDRMAN_NEW_BUCKET_COUNT;
        int nBpos = info.GetBucketPosition(nKey, true, nB);
        if (vvNew[nB][nBpos] == nId) {
            nUBucket = nB;
            break;
        }
    }

    // if no bucket is found, something bad happened;
    // TODO: maybe re-add the node, but for now, just bail out
    if (nUBucket == -1)
        return;

    logDebug(Log::Addrman) << "Moving" << addr << "to tried";

    // move nId to the tried tables
    MarkTried(info, nId);
}

bool CAddrMan::Add_(const CAddress& addr, const CNetAddr& source, int64_t nTimePenalty)
{
    if (!addr.IsRoutable())
        return false;

    bool fNew = false;
    int nId;
    CAddrInfo* pinfo = Find_(addr, &nId);

    if (pinfo) {
        // periodically update nTime
        bool fCurrentlyOnline = (GetAdjustedTime() - addr.nTime < 24 * 60 * 60);
        int64_t nUpdateInterval = (fCurrentlyOnline ? 60 * 60 : 24 * 60 * 60);
        if (addr.nTime && (!pinfo->nTime || pinfo->nTime < addr.nTime - nUpdateInterval - nTimePenalty))
            pinfo->nTime = (unsigned int) std::max((int64_t)0, addr.nTime - nTimePenalty);

        // add services
        pinfo->nServices |= addr.nServices;

        // do not update if no new information is present
        if (!addr.nTime || (pinfo->nTime && addr.nTime <= pinfo->nTime))
            return false;

        // do not update if the entry was already in the "tried" table
        if (pinfo->fInTried)
            return false;

        // do not update if the max reference count is reached
        if (pinfo->nRefCount == ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
            return false;

        // stochastic test: previous nRefCount == N: 2^N times harder to increase it
        int nFactor = 1;
        for (int n = 0; n < pinfo->nRefCount; n++)
            nFactor *= 2;
        if (nFactor > 1 && (GetRandInt(nFactor) != 0))
            return false;
    } else {
        pinfo = Create(addr, source, &nId);
        pinfo->nTime = (unsigned int) std::max((int64_t)0, (int64_t)pinfo->nTime - nTimePenalty);
        nNew++;
        fNew = true;
    }

    int nUBucket = pinfo->GetNewBucket(nKey, source);
    int nUBucketPos = pinfo->GetBucketPosition(nKey, true, nUBucket);
    if (vvNew[nUBucket][nUBucketPos] != nId) {
        bool fInsert = vvNew[nUBucket][nUBucketPos] == -1;
        if (!fInsert) {
            CAddrInfo& infoExisting = mapInfo[vvNew[nUBucket][nUBucketPos]];
            if (infoExisting.IsTerrible() || (infoExisting.nRefCount > 1 && pinfo->nRefCount == 0)) {
                // Overwrite the existing new table entry.
                fInsert = true;
            }
        }
        if (fInsert) {
            ClearNew(nUBucket, nUBucketPos);
            pinfo->nRefCount++;
            vvNew[nUBucket][nUBucketPos] = nId;
        } else {
            if (pinfo->nRefCount == 0) {
                Delete(nId);
            }
        }
    }
    return fNew;
}

void CAddrMan::Attempt(const CService& addr, int64_t nTime)
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);
    CAddrInfo* pinfo = Find_(addr);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    info.nLastTry = nTime;
    info.nAttempts++;
}

CAddrInfo CAddrMan::Select(bool newOnly)
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);

    if (size() == 0)
        return CAddrInfo();

    if (newOnly && nNew == 0)
        return CAddrInfo();

    // Use a 50% chance for choosing between tried and new table entries.
    if (!newOnly &&
       (nTried > 0 && (nNew == 0 || GetRandInt(2) == 0))) { 
        // use a tried node
        double fChanceFactor = 1.0;
        while (1) {
            int nKBucket = GetRandInt(ADDRMAN_TRIED_BUCKET_COUNT);
            int nKBucketPos = GetRandInt(ADDRMAN_BUCKET_SIZE);
            while (vvTried[nKBucket][nKBucketPos] == -1) {
                nKBucket = (nKBucket + insecure_rand()) % ADDRMAN_TRIED_BUCKET_COUNT;
                nKBucketPos = (nKBucketPos + insecure_rand()) % ADDRMAN_BUCKET_SIZE;
            }
            int nId = vvTried[nKBucket][nKBucketPos];
            assert(mapInfo.count(nId) == 1);
            CAddrInfo& info = mapInfo[nId];
            if (GetRandInt(1 << 30) < fChanceFactor * info.GetChance() * (1 << 30))
                return info;
            fChanceFactor *= 1.2;
        }
    } else {
        // use a new node
        double fChanceFactor = 1.0;
        while (1) {
            int nUBucket = GetRandInt(ADDRMAN_NEW_BUCKET_COUNT);
            int nUBucketPos = GetRandInt(ADDRMAN_BUCKET_SIZE);
            while (vvNew[nUBucket][nUBucketPos] == -1) {
                nUBucket = (nUBucket + insecure_rand()) % ADDRMAN_NEW_BUCKET_COUNT;
                nUBucketPos = (nUBucketPos + insecure_rand()) % ADDRMAN_BUCKET_SIZE;
            }
            int nId = vvNew[nUBucket][nUBucketPos];
            assert(mapInfo.count(nId) == 1);
            CAddrInfo& info = mapInfo[nId];
            if (GetRandInt(1 << 30) < fChanceFactor * info.GetChance() * (1 << 30))
                return info;
            fChanceFactor *= 1.2;
        }
    }
}

int CAddrMan::validateInteral()
{
#ifdef DEBUG_ADDRMAN
    std::set<int> setTried;
    std::map<int, int> mapNew;

    if (vRandom.size() != nTried + nNew)
        return -7;

    for (auto it = mapInfo.begin(); it != mapInfo.end(); it++) {
        int n = (*it).first;
        const CAddrInfo& info = (*it).second;
        if (info.fInTried) {
            if (!info.nLastSuccess)
                return -1;
            if (info.nRefCount)
                return -2;
            setTried.insert(n);
        } else {
            if (info.nRefCount < 0 || info.nRefCount > ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
                return -3;
            if (!info.nRefCount)
                return -4;
            mapNew[n] = info.nRefCount;
        }
        if (mapAddr[info] != n)
            return -5;
        if (info.nRandomPos < 0 || info.nRandomPos >= vRandom.size() || vRandom[info.nRandomPos] != n)
            return -14;
        if (info.nLastTry < 0)
            return -6;
        if (info.nLastSuccess < 0)
            return -8;
    }

    if (setTried.size() != nTried)
        return -9;
    if (mapNew.size() != nNew)
        return -10;

    for (int n = 0; n < ADDRMAN_TRIED_BUCKET_COUNT; n++) {
        for (int i = 0; i < ADDRMAN_BUCKET_SIZE; i++) {
             if (vvTried[n][i] != -1) {
                 if (!setTried.count(vvTried[n][i]))
                     return -11;
                 if (mapInfo[vvTried[n][i]].GetTriedBucket(nKey) != n)
                     return -17;
                 if (mapInfo[vvTried[n][i]].GetBucketPosition(nKey, false, n) != i)
                     return -18;
                 setTried.erase(vvTried[n][i]);
             }
        }
    }

    for (int n = 0; n < ADDRMAN_NEW_BUCKET_COUNT; n++) {
        for (int i = 0; i < ADDRMAN_BUCKET_SIZE; i++) {
            if (vvNew[n][i] != -1) {
                if (!mapNew.count(vvNew[n][i]))
                    return -12;
                if (mapInfo[vvNew[n][i]].GetBucketPosition(nKey, true, n) != i)
                    return -19;
                if (--mapNew[vvNew[n][i]] == 0)
                    mapNew.erase(vvNew[n][i]);
            }
        }
    }

    if (setTried.size())
        return -13;
    if (mapNew.size())
        return -15;
    if (nKey.IsNull())
        return -16;

#endif
    return 0;
}

std::vector<CAddress> CAddrMan::GetAddr()
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);
    std::vector<CAddress> vAddr;

    unsigned int nNodes = static_cast<unsigned int>(ADDRMAN_GETADDR_MAX_PCT * vRandom.size() / 100);
    if (nNodes > ADDRMAN_GETADDR_MAX)
        nNodes = ADDRMAN_GETADDR_MAX;

    // gather a list of random nodes, skipping those of low quality
    for (unsigned int n = 0; n < vRandom.size(); n++) {
        if (vAddr.size() >= nNodes)
            break;

        int nRndPos = GetRandInt(static_cast<int>(vRandom.size() - n)) + n;
        SwapRandom(n, static_cast<unsigned int>(nRndPos));
        assert(mapInfo.count(vRandom[n]) == 1);

        const CAddrInfo& ai = mapInfo[vRandom[n]];
        if (!ai.IsTerrible())
            vAddr.push_back(ai);
    }
    return vAddr;
}

void CAddrMan::Connected(const CService& addr, int64_t nTime)
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);
    CAddrInfo* pinfo = Find_(addr);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    int64_t nUpdateInterval = 20 * 60;
    if (nTime - info.nTime > nUpdateInterval)
        info.nTime = static_cast<unsigned int>(nTime);
}

void CAddrMan::Clear()
{
    std::vector<int>().swap(vRandom);
    nKey = GetRandHash();
    for (size_t bucket = 0; bucket < ADDRMAN_NEW_BUCKET_COUNT; bucket++) {
        for (size_t entry = 0; entry < ADDRMAN_BUCKET_SIZE; entry++) {
            vvNew[bucket][entry] = -1;
        }
    }
    for (size_t bucket = 0; bucket < ADDRMAN_TRIED_BUCKET_COUNT; bucket++) {
        for (size_t entry = 0; entry < ADDRMAN_BUCKET_SIZE; entry++) {
            vvTried[bucket][entry] = -1;
        }
    }

    nIdCount = 0;
    nTried = 0;
    nNew = 0;
}

CAddrMan::CAddrMan()
{
    Clear();
}

CAddrMan::~CAddrMan()
{
    nKey.SetNull();
}

bool CAddrMan::Add(const CAddress &addr, const CNetAddr &source, int64_t nTimePenalty)
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);
    bool fRet = Add_(addr, source, nTimePenalty);
    if (fRet)
        logInfo(Log::Addrman) <<  "Added" << addr << "from" << source <<  "{" << nTried << "tried" << nNew << "new }";

    return fRet;
}

bool CAddrMan::Add(const std::vector<CAddress> &vAddr, const CNetAddr &source, int64_t nTimePenalty)
{
    std::lock_guard<std::mutex> guard(lock);
    ValidateRAII raii(this);
    int nAdd = 0;
    for (std::vector<CAddress>::const_iterator it = vAddr.begin(); it != vAddr.end(); it++)
        nAdd += Add_(*it, source, nTimePenalty) ? 1 : 0;

    if (nAdd)
        logInfo(Log::Addrman) <<  "Added" << nAdd << "addresses from" << source <<  "{" << nTried << "tried" << nNew << "new }";
    return nAdd > 0;
}

void CAddrMan::increaseUselessness(const CNetAddr &addr, int count)
{
    std::lock_guard<std::mutex> guard(lock);
    auto info = Find_(addr);
    if (info)
        info->setUselessness(info->getUselessness() + count);
}

void CAddrMan::MakeDeterministic()
{
    nKey.SetNull();
}
