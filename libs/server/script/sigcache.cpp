/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
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

#include "sigcache.h"

#include <hash.h>
#include "cuckoocache.h"
#include "memusage.h"
#include <SettingsDefaults.h>
#include <primitives/pubkey.h>
#include "random.h"
#include "uint256.h"
#include "util.h"

#include <boost/thread.hpp>

namespace {

/**
 * Valid signature cache, to avoid doing expensive ECDSA signature checking
 * twice for every transaction (once when accepted into memory pool, and
 * again when accepted into the block chain)
 */
class CSignatureCache
{
private:
     //! Entries are SHA256(nonce || signature hash || public key || signature):
    uint256 nonce;
    typedef CuckooCache::cache<uint256, SignatureCacheHasher> map_type;
    map_type setValid;
    boost::shared_mutex cs_sigcache;

public:
    CSignatureCache()
    {
        GetRandBytes(nonce.begin(), 32);
    }

    void
    ComputeEntry(uint256& entry, const uint256 &hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubkey)
    {
        CSHA256().Write(nonce.begin(), 32).Write(hash.begin(), 32).Write(&pubkey[0], pubkey.size()).Write(&vchSig[0], vchSig.size()).Finalize(entry.begin());
    }

    bool
    Get(const uint256& entry, bool erase)
    {
        boost::shared_lock<boost::shared_mutex> lock(cs_sigcache);
        return setValid.contains(entry, erase);
    }

    void Set(uint256& entry)
    {
        boost::unique_lock<boost::shared_mutex> lock(cs_sigcache);
        setValid.insert(entry);
    }

    uint32_t setup_bytes(size_t n)
    {
        return setValid.setup_bytes(n);
    }
};

/* In previous versions of this code, signatureCache was a local static variable
 * in CachingTransactionSignatureChecker::VerifySignature.  We initialize
 * signatureCache outside of VerifySignature to avoid the atomic operation per
 * call overhead associated with local static variables even though
 * signatureCache could be made local to VerifySignature.
*/
static CSignatureCache signatureCache;
}

// To be called once in AppInit2/TestingSetup to initialize the signatureCache
void InitSignatureCache() {
    size_t nMaxCacheSize = GetArg("-maxsigcachesize", Settings::DefaultMaxSigCacheSize) * ((size_t) 1 << 20);
    if (nMaxCacheSize <= 0) return;
    size_t nElems = signatureCache.setup_bytes(nMaxCacheSize);
    logInfo(Log::Bitcoin) << "Using" << (nElems * sizeof(uint256) >> 20) << "MiB out of" <<
        (nMaxCacheSize >> 20) << "requested for signature cache, able to store" << nElems << "elements";
}

bool CachingTransactionSignatureChecker::VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& pubkey, const uint256& sighash, uint32_t flags) const
{
    uint256 entry;
    signatureCache.ComputeEntry(entry, sighash, vchSig, pubkey);
    if (signatureCache.Get(entry, !store))
        return true;
    if (!TransactionSignatureChecker::VerifySignature(vchSig, pubkey, sighash, flags))
        return false;
    if (store)
        signatureCache.Set(entry);
    return true;
}
