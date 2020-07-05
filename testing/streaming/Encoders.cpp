/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
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
