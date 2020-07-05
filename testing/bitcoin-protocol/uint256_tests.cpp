/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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
#include "uint256_tests.h"

#include "arith_uint256.h"
#include "uint256.h"
#include "version.h"

namespace {
const unsigned char R1Array[] =
    "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
    "\x22\x81\xaa\xb5\x33\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";
const char R1ArrayHex[] = "7D1DE5EAF9B156D53208F033B5AA8122D2d2355d5e12292b121156cfdb4a529c";
const uint256 R1L = uint256(std::vector<unsigned char>(R1Array,R1Array+32));
const uint160 R1S = uint160(std::vector<unsigned char>(R1Array,R1Array+20));

const unsigned char R2Array[] =
    "\x70\x32\x1d\x7c\x47\xa5\x6b\x40\x26\x7e\x0a\xc3\xa6\x9c\xb6\xbf"
    "\x13\x30\x47\xa3\x19\x2d\xda\x71\x49\x13\x72\xf0\xb4\xca\x81\xd7";
const uint256 R2L = uint256(std::vector<unsigned char>(R2Array,R2Array+32));
const uint160 R2S = uint160(std::vector<unsigned char>(R2Array,R2Array+20));

const unsigned char ZeroArray[] =
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const uint256 ZeroL = uint256(std::vector<unsigned char>(ZeroArray,ZeroArray+32));
const uint160 ZeroS = uint160(std::vector<unsigned char>(ZeroArray,ZeroArray+20));

const unsigned char OneArray[] =
    "\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
const uint256 OneL = uint256(std::vector<unsigned char>(OneArray,OneArray+32));
const uint160 OneS = uint160(std::vector<unsigned char>(OneArray,OneArray+20));

const unsigned char MaxArray[] =
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
const uint256 MaxL = uint256(std::vector<unsigned char>(MaxArray,MaxArray+32));
const uint160 MaxS = uint160(std::vector<unsigned char>(MaxArray,MaxArray+20));

std::string ArrayToString(const unsigned char A[], unsigned int width)
{
    std::stringstream Stream;
    Stream << std::hex;
    for (unsigned int i = 0; i < width; ++i)
    {
        Stream<<std::setw(2)<<std::setfill('0')<<(unsigned int)A[width-i-1];
    }
    return Stream.str();
}

inline uint160 uint160S(const char *str)
{
    uint160 rv;
    rv.SetHex(str);
    return rv;
}

inline uint160 uint160S(const std::string& str)
{
    uint160 rv;
    rv.SetHex(str);
    return rv;
}
}

void TestUint256::basics()
{
    QVERIFY(1 == 0+1);
    // constructor uint256(vector<char>):
    QVERIFY(R1L.ToString() == ArrayToString(R1Array,32));
    QVERIFY(R1S.ToString() == ArrayToString(R1Array,20));
    QVERIFY(R2L.ToString() == ArrayToString(R2Array,32));
    QVERIFY(R2S.ToString() == ArrayToString(R2Array,20));
    QVERIFY(ZeroL.ToString() == ArrayToString(ZeroArray,32));
    QVERIFY(ZeroS.ToString() == ArrayToString(ZeroArray,20));
    QVERIFY(OneL.ToString() == ArrayToString(OneArray,32));
    QVERIFY(OneS.ToString() == ArrayToString(OneArray,20));
    QVERIFY(MaxL.ToString() == ArrayToString(MaxArray,32));
    QVERIFY(MaxS.ToString() == ArrayToString(MaxArray,20));
    QVERIFY(OneL.ToString() != ArrayToString(ZeroArray,32));
    QVERIFY(OneS.ToString() != ArrayToString(ZeroArray,20));

    // == and !=
    QVERIFY(R1L != R2L && R1S != R2S);
    QVERIFY(ZeroL != OneL && ZeroS != OneS);
    QVERIFY(OneL != ZeroL && OneS != ZeroS);
    QVERIFY(MaxL != ZeroL && MaxS != ZeroS);

    // String Constructor and Copy Constructor
    QVERIFY(uint256S("0x"+R1L.ToString()) == R1L);
    QVERIFY(uint256S("0x"+R2L.ToString()) == R2L);
    QVERIFY(uint256S("0x"+ZeroL.ToString()) == ZeroL);
    QVERIFY(uint256S("0x"+OneL.ToString()) == OneL);
    QVERIFY(uint256S("0x"+MaxL.ToString()) == MaxL);
    QVERIFY(uint256S(R1L.ToString()) == R1L);
    QVERIFY(uint256S("   0x"+R1L.ToString()+"   ") == R1L);
    QVERIFY(uint256S("") == ZeroL);
    QVERIFY(R1L == uint256S(R1ArrayHex));
    QVERIFY(uint256(R1L) == R1L);
    QVERIFY(uint256(ZeroL) == ZeroL);
    QVERIFY(uint256(OneL) == OneL);

    QVERIFY(uint160S("0x"+R1S.ToString()) == R1S);
    QVERIFY(uint160S("0x"+R2S.ToString()) == R2S);
    QVERIFY(uint160S("0x"+ZeroS.ToString()) == ZeroS);
    QVERIFY(uint160S("0x"+OneS.ToString()) == OneS);
    QVERIFY(uint160S("0x"+MaxS.ToString()) == MaxS);
    QVERIFY(uint160S(R1S.ToString()) == R1S);
    QVERIFY(uint160S("   0x"+R1S.ToString()+"   ") == R1S);
    QVERIFY(uint160S("") == ZeroS);
    QVERIFY(R1S == uint160S(R1ArrayHex));

    QVERIFY(uint160(R1S) == R1S);
    QVERIFY(uint160(ZeroS) == ZeroS);
    QVERIFY(uint160(OneS) == OneS);
}

void TestUint256::comparison()
{
    uint256 LastL;
    for (int i = 255; i >= 0; --i) {
        uint256 TmpL;
        *(TmpL.begin() + (i>>3)) |= 1<<(7-(i&7));
        QVERIFY( LastL < TmpL );
        LastL = TmpL;
    }

    QVERIFY( ZeroL < R1L );
    QVERIFY( R2L < R1L );
    QVERIFY( ZeroL < OneL );
    QVERIFY( OneL < MaxL );
    QVERIFY( R1L < MaxL );
    QVERIFY( R2L < MaxL );

    uint160 LastS;
    for (int i = 159; i >= 0; --i) {
        uint160 TmpS;
        *(TmpS.begin() + (i>>3)) |= 1<<(7-(i&7));
        QVERIFY( LastS < TmpS );
        LastS = TmpS;
    }
    QVERIFY( ZeroS < R1S );
    QVERIFY( R2S < R1S );
    QVERIFY( ZeroS < OneS );
    QVERIFY( OneS < MaxS );
    QVERIFY( R1S < MaxS );
    QVERIFY( R2S < MaxS );

    // the new Compare method;

    QCOMPARE(ZeroL.Compare(OneL), -1);
    QCOMPARE(ZeroL.Compare(ZeroL), 0);
    QCOMPARE(OneL.Compare(OneL), 0);
    QCOMPARE(OneL.Compare(ZeroL), 1);

    // in contrary to the previous method, this compares from back to front.
    QCOMPARE(R1L.Compare(R2L), -1);
    QCOMPARE(R1L.Compare(R1L), 0);
    QCOMPARE(R2L.Compare(R2L), 0);
    QCOMPARE(R2L.Compare(R1L), 1);
}

void TestUint256::methods()
{
    QVERIFY(R1L.GetHex() == R1L.ToString());
    QVERIFY(R2L.GetHex() == R2L.ToString());
    QVERIFY(OneL.GetHex() == OneL.ToString());
    QVERIFY(MaxL.GetHex() == MaxL.ToString());
    uint256 TmpL(R1L);
    QVERIFY(TmpL == R1L);
    TmpL.SetHex(R2L.ToString());   QVERIFY(TmpL == R2L);
    TmpL.SetHex(ZeroL.ToString()); QVERIFY(TmpL == uint256());

    TmpL.SetHex(R1L.ToString());
    QVERIFY(memcmp(R1L.begin(), R1Array, 32)==0);
    QVERIFY(memcmp(TmpL.begin(), R1Array, 32)==0);
    QVERIFY(memcmp(R2L.begin(), R2Array, 32)==0);
    QVERIFY(memcmp(ZeroL.begin(), ZeroArray, 32)==0);
    QVERIFY(memcmp(OneL.begin(), OneArray, 32)==0);
    QVERIFY(R1L.size() == sizeof(R1L));
    QVERIFY(sizeof(R1L) == 32);
    QVERIFY(R1L.size() == 32);
    QVERIFY(R2L.size() == 32);
    QVERIFY(ZeroL.size() == 32);
    QVERIFY(MaxL.size() == 32);
    QVERIFY(R1L.begin() + 32 == R1L.end());
    QVERIFY(R2L.begin() + 32 == R2L.end());
    QVERIFY(OneL.begin() + 32 == OneL.end());
    QVERIFY(MaxL.begin() + 32 == MaxL.end());
    QVERIFY(TmpL.begin() + 32 == TmpL.end());
    QVERIFY(R1L.GetSerializeSize(0,PROTOCOL_VERSION) == 32);
    QVERIFY(ZeroL.GetSerializeSize(0,PROTOCOL_VERSION) == 32);

    std::stringstream ss;
    R1L.Serialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ss.str() == std::string(R1Array,R1Array+32));
    TmpL.Unserialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(R1L == TmpL);
    ss.str("");
    ZeroL.Serialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ss.str() == std::string(ZeroArray,ZeroArray+32));
    TmpL.Unserialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ZeroL == TmpL);
    ss.str("");
    MaxL.Serialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ss.str() == std::string(MaxArray,MaxArray+32));
    TmpL.Unserialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(MaxL == TmpL);
    ss.str("");

    QVERIFY(R1S.GetHex() == R1S.ToString());
    QVERIFY(R2S.GetHex() == R2S.ToString());
    QVERIFY(OneS.GetHex() == OneS.ToString());
    QVERIFY(MaxS.GetHex() == MaxS.ToString());
    uint160 TmpS(R1S);
    QVERIFY(TmpS == R1S);
    TmpS.SetHex(R2S.ToString());   QVERIFY(TmpS == R2S);
    TmpS.SetHex(ZeroS.ToString()); QVERIFY(TmpS == uint160());

    TmpS.SetHex(R1S.ToString());
    QVERIFY(memcmp(R1S.begin(), R1Array, 20)==0);
    QVERIFY(memcmp(TmpS.begin(), R1Array, 20)==0);
    QVERIFY(memcmp(R2S.begin(), R2Array, 20)==0);
    QVERIFY(memcmp(ZeroS.begin(), ZeroArray, 20)==0);
    QVERIFY(memcmp(OneS.begin(), OneArray, 20)==0);
    QVERIFY(R1S.size() == sizeof(R1S));
    QVERIFY(sizeof(R1S) == 20);
    QVERIFY(R1S.size() == 20);
    QVERIFY(R2S.size() == 20);
    QVERIFY(ZeroS.size() == 20);
    QVERIFY(MaxS.size() == 20);
    QVERIFY(R1S.begin() + 20 == R1S.end());
    QVERIFY(R2S.begin() + 20 == R2S.end());
    QVERIFY(OneS.begin() + 20 == OneS.end());
    QVERIFY(MaxS.begin() + 20 == MaxS.end());
    QVERIFY(TmpS.begin() + 20 == TmpS.end());
    QVERIFY(R1S.GetSerializeSize(0,PROTOCOL_VERSION) == 20);
    QVERIFY(ZeroS.GetSerializeSize(0,PROTOCOL_VERSION) == 20);

    R1S.Serialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ss.str() == std::string(R1Array,R1Array+20));
    TmpS.Unserialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(R1S == TmpS);
    ss.str("");
    ZeroS.Serialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ss.str() == std::string(ZeroArray,ZeroArray+20));
    TmpS.Unserialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ZeroS == TmpS);
    ss.str("");
    MaxS.Serialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(ss.str() == std::string(MaxArray,MaxArray+20));
    TmpS.Unserialize(ss,0,PROTOCOL_VERSION);
    QVERIFY(MaxS == TmpS);
    ss.str("");
}

void TestUint256::conversion()
{
    QVERIFY(ArithToUint256(UintToArith256(ZeroL)) == ZeroL);
    QVERIFY(ArithToUint256(UintToArith256(OneL)) == OneL);
    QVERIFY(ArithToUint256(UintToArith256(R1L)) == R1L);
    QVERIFY(ArithToUint256(UintToArith256(R2L)) == R2L);
    QVERIFY(UintToArith256(ZeroL) == 0);
    QVERIFY(UintToArith256(OneL) == 1);
    QVERIFY(ArithToUint256(0) == ZeroL);
    QVERIFY(ArithToUint256(1) == OneL);
    QVERIFY(arith_uint256(R1L.GetHex()) == UintToArith256(R1L));
    QVERIFY(arith_uint256(R2L.GetHex()) == UintToArith256(R2L));
    QVERIFY(R1L.GetHex() == UintToArith256(R1L).GetHex());
    QVERIFY(R2L.GetHex() == UintToArith256(R2L).GetHex());
}
