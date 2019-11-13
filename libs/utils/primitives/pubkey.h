/*
 * This file is part of the Flowee project
 * Copyright (C) 2009-2010 Satoshi Nakamoto
 * Copyright (C) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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

#ifndef FLOWEE_PUBKEY_H
#define FLOWEE_PUBKEY_H

#include "pubkey_utils.h"
#include "hash.h"
#include "serialize.h"
#include "uint256.h"

#include <stdexcept>
#include <vector>

/** 
 * secp256k1:
 * const unsigned int PRIVATE_KEY_SIZE = 279;
 * const unsigned int PUBLIC_KEY_SIZE  = 65;
 * const unsigned int SIGNATURE_SIZE   = 72;
 *
 * see www.keylength.com
 * script supports up to 75 for single byte push
 */

/** A reference to a CKey: the Hash160 of its serialized public key */
class CKeyID : public uint160
{
public:
    CKeyID() : uint160() {}
    CKeyID(const uint160& in) : uint160(in) {}
    explicit CKeyID(const char *d) : uint160(d) {}
};

typedef uint256 ChainCode;

/** An encapsulated public key. */
class CPubKey
{
public:
    /**
     * secp256k1:
     */
    static constexpr unsigned int PUBLIC_KEY_SIZE = 65;
    static constexpr unsigned int COMPRESSED_PUBLIC_KEY_SIZE = 33;
    static constexpr unsigned int SIGNATURE_SIZE = 72;
    static constexpr unsigned int COMPACT_SIGNATURE_SIZE = 65;
    /**
     * see www.keylength.com
     * script supports up to 75 for single byte push
     */
    static_assert(PUBLIC_KEY_SIZE >= COMPRESSED_PUBLIC_KEY_SIZE,
                  "COMPRESSED_PUBLIC_KEY_SIZE is larger than PUBLIC_KEY_SIZE");

    //! Construct an invalid public key.
    CPubKey() {
        Invalidate();
    }

    //! Initialize a public key using begin/end iterators to byte data.
    template <typename T>
    void Set(const T pbegin, const T pend)
    {
        int len = pend == pbegin ? 0 : PubKey::keyLength(pbegin[0]);
        if (len && len == (pend - pbegin))
            memcpy(vch, (unsigned char*)&pbegin[0], len);
        else
            Invalidate();
    }

    //! Construct a public key using begin/end iterators to byte data.
    template <typename T>
    CPubKey(const T pbegin, const T pend)
    {
        Set(pbegin, pend);
    }

    //! Construct a public key from a byte vector.
    CPubKey(const std::vector<unsigned char>& vch)
    {
        Set(vch.begin(), vch.end());
    }

    //! Simple read-only vector-like interface to the pubkey data.
    unsigned int size() const { return PubKey::keyLength(vch[0]); }
    const unsigned char* begin() const { return vch; }
    const unsigned char* end() const { return vch + size(); }
    const unsigned char& operator[](unsigned int pos) const { return vch[pos]; }

    //! Comparator implementation.
    friend bool operator==(const CPubKey& a, const CPubKey& b) {
        return a.vch[0] == b.vch[0] && memcmp(a.vch, b.vch, a.size()) == 0;
    }

    friend bool operator!=(const CPubKey& a, const CPubKey& b) {
        return !(a == b);
    }

    friend bool operator<(const CPubKey& a, const CPubKey& b) {
        return a.vch[0] < b.vch[0] ||
               (a.vch[0] == b.vch[0] && memcmp(a.vch, b.vch, a.size()) < 0);
    }

    //! Implement serialization, as if this was a byte vector.
    unsigned int GetSerializeSize(int nType, int nVersion) const {
        return size() + 1;
    }
    template <typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        unsigned int len = size();
        ::WriteCompactSize(s, len);
        s.write((char*)vch, len);
    }

    template <typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        unsigned int len = ::ReadCompactSize(s);
        if (len <= 65) {
            s.read((char*)vch, len);
        } else {
            // invalid pubkey, skip available data
            char dummy;
            while (len--)
                s.read(&dummy, 1);
            Invalidate();
        }
    }

    //! Get the KeyID of this public key (hash of its serialization)
    CKeyID GetID() const;

    //! Get the 256-bit hash of this public key.
    uint256 GetHash() const;

    /*
     * Check syntactic correctness.
     * 
     * Note that this is consensus critical as CheckSig() calls it!
     */
    bool IsValid() const;

    //! fully validate whether this is a valid public key (more expensive than IsValid())
    bool IsFullyValid() const;

    //! Check whether this is a compressed public key.
    bool IsCompressed() const;

    /**
     * Verify a DER signature (~72 bytes).
     * If this public key is not fully valid, the return value will be false.
     */
    bool verifyECDSA(const uint256& hash, const std::vector<unsigned char>& vchSig) const;

    /**
     * Verify a Schnorr signature (=64 bytes).
     * If this public key is not fully valid, the return value will be false.
     */
    bool verifySchnorr(const uint256 &hash, const std::vector<uint8_t> &vchSig) const;

    /**
     * Check whether a signature is normalized (lower-S).
     */
    static bool CheckLowS(const std::vector<unsigned char>& vchSig);

    //! Recover a public key from a compact signature.
    bool RecoverCompact(const uint256& hash, const std::vector<unsigned char>& vchSig);

    //! Turn this public key into an uncompressed public key.
    bool Decompress();

    //! Derive BIP32 child pubkey.
    bool Derive(CPubKey& pubkeyChild, ChainCode &ccChild, unsigned int nChild, const ChainCode& cc) const;

private:
    /**
     * Just store the serialized data.
     * Its length can very cheaply be computed from the first byte.
     */
    unsigned char vch[65];

    //! Set this key data to be invalid
    inline void Invalidate() {
        vch[0] = 0xFF;
    }
};

struct CExtPubKey {
    unsigned char nDepth;
    unsigned char vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CPubKey pubkey;

    friend bool operator==(const CExtPubKey &a, const CExtPubKey &b)
    {
        return a.nDepth == b.nDepth && memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], 4) == 0 && a.nChild == b.nChild &&
               a.chaincode == b.chaincode && a.pubkey == b.pubkey;
    }

    void Encode(unsigned char code[74]) const;
    void Decode(const unsigned char code[74]);
    bool Derive(CExtPubKey& out, unsigned int nChild) const;
};

/** Users of this module must hold an ECCVerifyHandle. The constructor and
 *  destructor of these are not allowed to run in parallel, though. */
class ECCVerifyHandle
{
    static int refcount;

public:
    ECCVerifyHandle();
    ~ECCVerifyHandle();
};

#endif
