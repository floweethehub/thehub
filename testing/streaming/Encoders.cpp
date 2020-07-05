/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2016 The Bitcoin Core developers
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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

#include "Encoders.h"
#include "data/base58_encode_decode.json.h"

#include <utilstrencodings.h>
#include <base58.h>
#include <serialize.h> // for begin_ptr etc
#include <univalue.h>


static const unsigned char ParseHex_expected[65] = {
    0x04, 0x67, 0x8a, 0xfd, 0xb0, 0xfe, 0x55, 0x48,
    0x27, 0x19, 0x67, 0xf1, 0xa6, 0x71, 0x30, 0xb7,
    0x10, 0x5c, 0xd6, 0xa8, 0x28, 0xe0, 0x39, 0x09,
    0xa6, 0x79, 0x62, 0xe0, 0xea, 0x1f, 0x61, 0xde,
    0xb6, 0x49, 0xf6, 0xbc, 0x3f, 0x4c, 0xef, 0x38,
    0xc4, 0xf3, 0x55, 0x04, 0xe5, 0x1e, 0xc1, 0x12,
    0xde, 0x5c, 0x38, 0x4d, 0xf7, 0xba, 0x0b, 0x8d,
    0x57, 0x8a, 0x4c, 0x70, 0x2b, 0x6b, 0xf1, 0x1d,
    0x5f
};

void TestEncoders::testUtilParseHex()
{
    std::vector<unsigned char> result;
    std::vector<unsigned char> expected(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected));
    // Basic test vector
    result = ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f");
    compare(result, expected);

    // Spaces between bytes must be supported
    result = ParseHex("12 34 56 78");
    QVERIFY(result.size() == 4 && result[0] == 0x12 && result[1] == 0x34 && result[2] == 0x56 && result[3] == 0x78);

    // Stop parsing at invalid value
    result = ParseHex("1234 invalid 1234");
    QVERIFY(result.size() == 2 && result[0] == 0x12 && result[1] == 0x34);
}

void TestEncoders::testUtilHexString()
{
    QCOMPARE(
        HexStr(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected)),
        "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f");

    QCOMPARE(HexStr(ParseHex_expected, ParseHex_expected + 5, true), "04 67 8a fd b0");

    QCOMPARE(HexStr(ParseHex_expected, ParseHex_expected, true), "");

    std::vector<unsigned char> ParseHex_vec(ParseHex_expected, ParseHex_expected + 5);

    QCOMPARE(HexStr(ParseHex_vec, true), "04 67 8a fd b0");
}

void TestEncoders::testUtilIsHex()
{
    QVERIFY(IsHex("00"));
    QVERIFY(IsHex("00112233445566778899aabbccddeeffAABBCCDDEEFF"));
    QVERIFY(IsHex("ff"));
    QVERIFY(IsHex("FF"));

    QVERIFY(!IsHex(""));
    QVERIFY(!IsHex("0"));
    QVERIFY(!IsHex("a"));
    QVERIFY(!IsHex("eleven"));
    QVERIFY(!IsHex("00xx00"));
    QVERIFY(!IsHex("0x0000"));
}

void TestEncoders::base32TestVectors()
{
    static const std::string vstrIn[]  = {"","f","fo","foo","foob","fooba","foobar"};
    static const std::string vstrOut[] = {"","my======","mzxq====","mzxw6===","mzxw6yq=","mzxw6ytb","mzxw6ytboi======"};
    for (unsigned int i=0; i<sizeof(vstrIn)/sizeof(vstrIn[0]); i++)
    {
        std::string strEnc = EncodeBase32(vstrIn[i]);
        QVERIFY(strEnc == vstrOut[i]);
        std::string strDec = DecodeBase32(vstrOut[i]);
        QVERIFY(strDec == vstrIn[i]);
    }
}

UniValue read_json(const std::string& jsondata)
{
    UniValue v;

    if (!v.read(jsondata) || !v.isArray())
    {
        qWarning() << "Parse error";
        assert(false);
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}

void TestEncoders::base58Encode()
{
    UniValue tests = read_json(std::string(json_tests::base58_encode_decode, json_tests::base58_encode_decode + sizeof(json_tests::base58_encode_decode)));
    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        assert(test.size() >= 2); // Allow for extra stuff (useful for comments)
        std::vector<unsigned char> sourcedata = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        QVERIFY2(EncodeBase58(begin_ptr(sourcedata), end_ptr(sourcedata)) == base58string, strTest.c_str());
    }
}

void TestEncoders::base58Decode()
{
    UniValue tests = read_json(std::string(json_tests::base58_encode_decode, json_tests::base58_encode_decode + sizeof(json_tests::base58_encode_decode)));
    std::vector<unsigned char> result;

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        assert (test.size() >= 2); // Allow for extra stuff (useful for comments)
        std::vector<unsigned char> expected = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        QVERIFY2(DecodeBase58(base58string, result), strTest.c_str());
        QVERIFY2(result.size() == expected.size() && std::equal(result.begin(), result.end(), expected.begin()), strTest.c_str());
    }

    QVERIFY(!DecodeBase58("invalid", result));

    // check that DecodeBase58 skips whitespace, but still fails with unexpected non-whitespace at the end.
    QVERIFY(!DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t a", result));
    QVERIFY( DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t ", result));
    std::vector<unsigned char> expected = ParseHex("971a55");
    compare(result, expected);
}

void TestEncoders::base64TestVectors()
{
    static const std::string vstrIn[]  = {"","f","fo","foo","foob","fooba","foobar"};
    static const std::string vstrOut[] = {"","Zg==","Zm8=","Zm9v","Zm9vYg==","Zm9vYmE=","Zm9vYmFy"};
    for (unsigned int i=0; i<sizeof(vstrIn)/sizeof(vstrIn[0]); i++)
    {
        std::string strEnc = EncodeBase64(vstrIn[i]);
        QVERIFY(strEnc == vstrOut[i]);
        std::string strDec = DecodeBase64(strEnc);
        QVERIFY(strDec == vstrIn[i]);
    }
}

void TestEncoders::bswap()
{
    uint16_t u1 = 0x1234;
    uint32_t u2 = 0x56789abc;
    uint64_t u3 = 0xdef0123456789abc;
    uint16_t e1 = 0x3412;
    uint32_t e2 = 0xbc9a7856;
    uint64_t e3 = 0xbc9a78563412f0de;
    QCOMPARE(bswap_16(u1), e1);
    QCOMPARE(bswap_32(u2), e2);
    QCOMPARE(bswap_64(u3), e3);
}
