/*
 * This file is part of the Flowee project
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
#ifndef CRYPTO_TESTS_H
#define CRYPTO_TESTS_H

#include <common/TestFloweeBase.h>

#include <random.h>
#include <utilstrencodings.h>

class CryptoTests : public TestFloweeBase
{
    Q_OBJECT
public:
    enum Algo {
        SHA1Algo,
        SHA256Algo,
        SHA512Algo,
        Ripe160Algo,
        HMACSHA256Algo,
        HMACSHA512Algo
    };


private slots:
    void test_aes128();
    void test_aes128_data();
    void test_aes256();
    void test_aes256_data();

    void testVectors();
    void testVectors_data();

    void testHMACSHA2Vectors();
    void testHMACSHA2Vectors_data();

    void testAES128CBC();
    void testAES128CBC_data();

    void testAES256CBC();
    void testAES256CBC_data();

private:
    template<typename Hasher, typename In, typename Out>
    void TestVector(const Hasher &h, const In &in, const Out &out) {
        Out hash;
        QVERIFY(out.size() == h.OUTPUT_SIZE);
        hash.resize(out.size());
        {
            // Test that writing the whole input string at once works.
            Hasher(h).Write((unsigned char*)&in[0], in.size()).Finalize(&hash[0]);
            QVERIFY(hash == out);
        }
        for (int i = 0; i < 32; ++i) {
            // Test that writing the string broken up in random pieces works.
            Hasher hasher(h);
            size_t pos = 0;
            while (pos < in.size()) {
                size_t len = insecure_rand() % ((in.size() - pos + 1) / 2 + 1);
                hasher.Write((unsigned char*)&in[pos], len);
                pos += len;
                if (pos > 0 && pos + 2 * out.size() > in.size() && pos < in.size()) {
                    // Test that writing the rest at once to a copy of a hasher works.
                    Hasher(hasher).Write((unsigned char*)&in[pos], in.size() - pos).Finalize(&hash[0]);
                    QVERIFY(hash == out);
                }
            }
            hasher.Finalize(&hash[0]);
            QVERIFY(hash == out);
        }
    }
};

Q_DECLARE_METATYPE(CryptoTests::Algo)

#endif
