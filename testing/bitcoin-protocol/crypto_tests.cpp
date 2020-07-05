/*
 * This file is part of the Flowee project
 * Copyright (C) 2014-2015 The Bitcoin Core developers
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
#include "crypto_tests.h"

#include "crypto/aes.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"
#include "crypto/hmac_sha256.h"
#include "crypto/hmac_sha512.h"

void CryptoTests::test_aes128()
{
    QFETCH(QString, hexkey);
    QFETCH(QString, hexin);
    QFETCH(QString, hexout);

    std::vector<unsigned char> key = ParseHex(hexkey.toStdString());
    std::vector<unsigned char> in = ParseHex(hexin.toStdString());
    std::vector<unsigned char> correctout = ParseHex(hexout.toStdString());
    std::vector<unsigned char> buf, buf2;

    QVERIFY(key.size() == 16);
    QVERIFY(in.size() == 16);
    QVERIFY(correctout.size() == 16);
    AES128Encrypt enc(&key[0]);
    buf.resize(correctout.size());
    buf2.resize(correctout.size());
    enc.Encrypt(&buf[0], &in[0]);
    QCOMPARE(HexStr(buf), HexStr(correctout));
    AES128Decrypt dec(&key[0]);
    dec.Decrypt(&buf2[0], &buf[0]);
    QCOMPARE(HexStr(buf2), HexStr(in));
}

void CryptoTests::test_aes128_data()
{
    QTest::addColumn<QString>("hexkey");
    QTest::addColumn<QString>("hexin");
    QTest::addColumn<QString>("hexout");

    // AES test vectors from FIPS 197.
    QTest::newRow("FIPS") <<
        "000102030405060708090a0b0c0d0e0f" << "00112233445566778899aabbccddeeff" << "69c4e0d86a7b0430d8cdb78070b4c55a";

    // AES-ECB test vectors from NIST sp800-38a.
    QTest::newRow("ECB1") <<
        "2b7e151628aed2a6abf7158809cf4f3c" << "6bc1bee22e409f96e93d7e117393172a" << "3ad77bb40d7a3660a89ecaf32466ef97";
    QTest::newRow("ECB2") <<
        "2b7e151628aed2a6abf7158809cf4f3c" << "ae2d8a571e03ac9c9eb76fac45af8e51" << "f5d3d58503b9699de785895a96fdbaaf";
    QTest::newRow("ECB3") <<
        "2b7e151628aed2a6abf7158809cf4f3c" << "30c81c46a35ce411e5fbc1191a0a52ef" << "43b1cd7f598ece23881b00e3ed030688";
    QTest::newRow("ECB4") <<
        "2b7e151628aed2a6abf7158809cf4f3c" << "f69f2445df4f9b17ad2b417be66c3710" << "7b0c785e27e8ad3f8223207104725dd4";
}

void CryptoTests::test_aes256()
{
    QFETCH(QString, hexkey);
    QFETCH(QString, hexin);
    QFETCH(QString, hexout);

    std::vector<unsigned char> key = ParseHex(hexkey.toStdString());
    std::vector<unsigned char> in = ParseHex(hexin.toStdString());
    std::vector<unsigned char> correctout = ParseHex(hexout.toStdString());
    std::vector<unsigned char> buf;

    QVERIFY(key.size() == 32);
    QVERIFY(in.size() == 16);
    QVERIFY(correctout.size() == 16);
    AES256Encrypt enc(&key[0]);
    buf.resize(correctout.size());
    enc.Encrypt(&buf[0], &in[0]);
    QVERIFY(buf == correctout);
    AES256Decrypt dec(&key[0]);
    dec.Decrypt(&buf[0], &buf[0]);
    QVERIFY(buf == in);
}

void CryptoTests::test_aes256_data()
{
    QTest::addColumn<QString>("hexkey");
    QTest::addColumn<QString>("hexin");
    QTest::addColumn<QString>("hexout");

    // AES test vectors from FIPS 197.
    QTest::newRow("FIPS") <<
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f" << "00112233445566778899aabbccddeeff" << "8ea2b7ca516745bfeafc49904b496089";
    // AES-ECB test vectors from NIST sp800-38a.
    QTest::newRow("ECB-1") <<
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4" << "6bc1bee22e409f96e93d7e117393172a" << "f3eed1bdb5d2a03c064b5a7e3db181f8";
    QTest::newRow("ECB-2") <<
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4" << "ae2d8a571e03ac9c9eb76fac45af8e51" << "591ccb10d410ed26dc5ba74a31362870";
    QTest::newRow("ECB-3") <<
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4" << "30c81c46a35ce411e5fbc1191a0a52ef" << "b6ed21b99ca6f4f9f153e7b1beafed1d";
    QTest::newRow("ECB-4") <<
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4" << "f69f2445df4f9b17ad2b417be66c3710" << "23304b7a39f9f3ff067d8d8f9e24ecc7";
}

void CryptoTests::testVectors()
{
    QFETCH(Algo, algo);
    QFETCH(QString, in);
    QFETCH(QString, hexout);

    switch (algo) {
    case SHA1Algo:
        assert(hexout.size() == 40);
        TestVector(CSHA1(), in.toStdString(), ParseHex(hexout.toStdString()));
        break;
    case SHA256Algo:
        assert(hexout.size() == 64);
        TestVector(CSHA256(), in.toStdString(), ParseHex(hexout.toStdString()));
        break;
    case SHA512Algo:
        TestVector(CSHA512(), in.toStdString(), ParseHex(hexout.toStdString()));
        break;
    case Ripe160Algo:
        TestVector(CRIPEMD160(), in.toStdString(), ParseHex(hexout.toStdString()));
        break;
    default:
        assert(false);
    }
}

void CryptoTests::testVectors_data()
{
    QTest::addColumn<Algo>("algo");
    QTest::addColumn<QString>("in");
    QTest::addColumn<QString>("hexout");

    auto longStr = QString::fromStdString(std::string(1000000, 'a'));

    QTest::newRow("sha1empty") << SHA1Algo << "" << "da39a3ee5e6b4b0d3255bfef95601890afd80709";
    QTest::newRow("sha1-2") << SHA1Algo << "abc" << "a9993e364706816aba3e25717850c26c9cd0d89d";
    QTest::newRow("sha1-3") << SHA1Algo << "message digest" << "c12252ceda8be8994d5fa0290a47231c1d16aae3";
    QTest::newRow("sha1-4") << SHA1Algo << "secure hash algorithm" << "d4d6d2f0ebe317513bbd8d967d89bac5819c2f60";
    QTest::newRow("sha1-5") << SHA1Algo << "SHA1 is considered to be safe" << "f2b6650569ad3a8720348dd6ea6c497dee3a842a";
    QTest::newRow("sha1-6") << SHA1Algo << "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
             << "84983e441c3bd26ebaae4aa1f95129e5e54670f1";
    QTest::newRow("sha1-7") << SHA1Algo << "For this sample, this 63-byte string will be used as input data"
             << "4f0ea5cd0585a23d028abdc1a6684e5a8094dc49";
    QTest::newRow("sha1-8") << SHA1Algo << "This is exactly 64 bytes long, not counting the terminating byte"
             << "fb679f23e7d1ce053313e66e127ab1b444397057";
    QTest::newRow("sha1-9") << SHA1Algo << longStr << "34aa973cd4c4daa4f61eeb2bdbad27316534016f";

    QTest::newRow("sha256-empty") << SHA256Algo << "" << "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    QTest::newRow("sha256-1") << SHA256Algo << "abc" << "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    QTest::newRow("sha256-2") << SHA256Algo << "message digest"
               << "f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650";
    QTest::newRow("sha256-3") << SHA256Algo << "secure hash algorithm"
               << "f30ceb2bb2829e79e4ca9753d35a8ecc00262d164cc077080295381cbd643f0d";
    QTest::newRow("sha256-4") << SHA256Algo << "SHA256 is considered to be safe"
               << "6819d915c73f4d1e77e4e1b52d1fa0f9cf9beaead3939f15874bd988e2a23630";
    QTest::newRow("sha256-5") << SHA256Algo << "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
               << "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
    QTest::newRow("sha256-6") << SHA256Algo << "For this sample, this 63-byte string will be used as input data"
               << "f08a78cbbaee082b052ae0708f32fa1e50c5c421aa772ba5dbb406a2ea6be342";
    QTest::newRow("sha256-7") << SHA256Algo << "This is exactly 64 bytes long, not counting the terminating byte"
               << "ab64eff7e88e2e46165e29f2bce41826bd4c7b3552f6b382a9e7d3af47c245f8";
    QTest::newRow("sha256-8") << SHA256Algo << "As Bitcoin relies on 80 byte header hashes, we want to have an example for that."
               << "7406e8de7d6e4fffc573daef05aefb8806e7790f55eab5576f31349743cca743";
    QTest::newRow("sha256-9") << SHA256Algo << longStr << "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";


    QTest::newRow("sha512-empty") << SHA512Algo << ""
        << "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
            "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e";
    QTest::newRow("sha512-1") << SHA512Algo << "abc"
        << "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f";
    QTest::newRow("sha512-2") << SHA512Algo << "message digest"
        << "107dbf389d9e9f71a3a95f6c055b9251bc5268c2be16d6c13492ea45b0199f33"
            "09e16455ab1e96118e8a905d5597b72038ddb372a89826046de66687bb420e7c";
    QTest::newRow("sha512-3") << SHA512Algo << "secure hash algorithm"
        << "7746d91f3de30c68cec0dd693120a7e8b04d8073cb699bdce1a3f64127bca7a3"
            "d5db502e814bb63c063a7a5043b2df87c61133395f4ad1edca7fcf4b30c3236e";
    QTest::newRow("sha512-4") << SHA512Algo << "SHA512 is considered to be safe"
        << "099e6468d889e1c79092a89ae925a9499b5408e01b66cb5b0a3bd0dfa51a9964"
            "6b4a3901caab1318189f74cd8cf2e941829012f2449df52067d3dd5b978456c2";
    QTest::newRow("sha512-5") << SHA512Algo << "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        << "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c335"
            "96fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445";
    QTest::newRow("sha512-6") << SHA512Algo << "For this sample, this 63-byte string will be used as input data"
        << "b3de4afbc516d2478fe9b518d063bda6c8dd65fc38402dd81d1eb7364e72fb6e"
            "6663cf6d2771c8f5a6da09601712fb3d2a36c6ffea3e28b0818b05b0a8660766";
    QTest::newRow("sha512-7") << SHA512Algo << "This is exactly 64 bytes long, not counting the terminating byte"
        << "70aefeaa0e7ac4f8fe17532d7185a289bee3b428d950c14fa8b713ca09814a38"
            "7d245870e007a80ad97c369d193e41701aa07f3221d15f0e65a1ff970cedf030";
    QTest::newRow("sha512-8") << SHA512Algo << "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
            "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"
        << "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
            "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909";
    QTest::newRow("sha512-9") << SHA512Algo << longStr << "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
            "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b";

     QTest::newRow("ripe160-empty") << Ripe160Algo << "" << "9c1185a5c5e9fc54612808977ee8f548b2258d31";
     QTest::newRow("ripe160-1") << Ripe160Algo << "abc" << "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc";
     QTest::newRow("ripe160-2") << Ripe160Algo << "message digest" << "5d0689ef49d2fae572b881b123a85ffa21595f36";
     QTest::newRow("ripe160-3") << Ripe160Algo << "secure hash algorithm" << "20397528223b6a5f4cbc2808aba0464e645544f9";
     QTest::newRow("ripe160-4") << Ripe160Algo << "RIPEMD160 is considered to be safe" << "a7d78608c7af8a8e728778e81576870734122b66";
     QTest::newRow("ripe160-5") << Ripe160Algo << "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
                  << "12a053384a9c0c88e405a06c27dcf49ada62eb2b";
     QTest::newRow("ripe160-6") << Ripe160Algo << "For this sample, this 63-byte string will be used as input data"
                  << "de90dbfee14b63fb5abf27c2ad4a82aaa5f27a11";
     QTest::newRow("ripe160-7") << Ripe160Algo << "This is exactly 64 bytes long, not counting the terminating byte"
                  << "eda31d51d3a623b81e19eb02e24ff65d27d67b37";
     QTest::newRow("ripe160-8") << Ripe160Algo << longStr << "52783243c1697bdbe16d37f97f68f08325dc1528";
}

void CryptoTests::testHMACSHA2Vectors()
{
    QFETCH(Algo, algo);
    QFETCH(QString, hexkey);
    QFETCH(QString, hexin);
    QFETCH(QString, hexout);

    std::vector<unsigned char> key = ParseHex(hexkey.toStdString());
    switch (algo) {
    case HMACSHA256Algo:
        TestVector(CHMAC_SHA256(&key[0],
            key.size()), ParseHex(hexin.toStdString()), ParseHex(hexout.toStdString()));
        break;
    case HMACSHA512Algo:
        TestVector(CHMAC_SHA512(&key[0],
            key.size()), ParseHex(hexin.toStdString()), ParseHex(hexout.toStdString()));
        break;
    default:
        assert(false);
    }
}

void CryptoTests::testHMACSHA2Vectors_data()
{
    QTest::addColumn<Algo>("algo");
    QTest::addColumn<QString>("hexkey");
    QTest::addColumn<QString>("hexin");
    QTest::addColumn<QString>("hexout");

    // test cases 1, 2, 3, 4, 6 and 7 of RFC 4231
    QTest::newRow("256-1") << HMACSHA256Algo << "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"
        << "4869205468657265"
        << "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7";
    QTest::newRow("256-1") << HMACSHA256Algo << "4a656665"
        << "7768617420646f2079612077616e7420666f72206e6f7468696e673f"
        << "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843";
    QTest::newRow("256-1") << HMACSHA256Algo << "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        << "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
           "dddddddddddddddddddddddddddddddddddd"
        << "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe";
    QTest::newRow("256-1") << HMACSHA256Algo << "0102030405060708090a0b0c0d0e0f10111213141516171819"
        << "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
           "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        << "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b";
    QTest::newRow("256-1") << HMACSHA256Algo << "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaa"
        << "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
           "65204b6579202d2048617368204b6579204669727374"
        << "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54";
    QTest::newRow("256-1") << HMACSHA256Algo
        <<"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaa"
        << "5468697320697320612074657374207573696e672061206c6172676572207468"
           "616e20626c6f636b2d73697a65206b657920616e642061206c61726765722074"
           "68616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565"
           "647320746f20626520686173686564206265666f7265206265696e6720757365"
           "642062792074686520484d414320616c676f726974686d2e"
        << "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2";


    // test cases 1, 2, 3, 4, 6 and 7 of RFC 4231
    QTest::newRow("512-1") << HMACSHA512Algo
        << "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"
        << "4869205468657265"
        << "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
           "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854";
    QTest::newRow("512-1") << HMACSHA512Algo << "4a656665"
        << "7768617420646f2079612077616e7420666f72206e6f7468696e673f"
        << "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
           "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737";
    QTest::newRow("512-1") << HMACSHA512Algo
        << "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        << "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
           "dddddddddddddddddddddddddddddddddddd"
        << "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
           "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb";
    QTest::newRow("512-1") << HMACSHA512Algo
        << "0102030405060708090a0b0c0d0e0f10111213141516171819"
        << "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
           "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
        << "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
           "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd";
    QTest::newRow("512-1") << HMACSHA512Algo
        << "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaa"
        << "54657374205573696e67204c6172676572205468616e20426c6f636b2d53697a"
           "65204b6579202d2048617368204b6579204669727374"
        << "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
           "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598";
    QTest::newRow("512-1") << HMACSHA512Algo
        << "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
           "aaaaaa"
        << "5468697320697320612074657374207573696e672061206c6172676572207468"
           "616e20626c6f636b2d73697a65206b657920616e642061206c61726765722074"
           "68616e20626c6f636b2d73697a6520646174612e20546865206b6579206e6565"
           "647320746f20626520686173686564206265666f7265206265696e6720757365"
           "642062792074686520484d414320616c676f726974686d2e"
        << "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
           "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58";
}

void CryptoTests::testAES128CBC()
{
    QFETCH(QString, hexkey);
    QFETCH(QString, hexiv);
    QFETCH(bool, pad);
    QFETCH(QString, hexin);
    QFETCH(QString, hexout);

    std::vector<unsigned char> key = ParseHex(hexkey.toStdString());
    std::vector<unsigned char> iv = ParseHex(hexiv.toStdString());
    std::vector<unsigned char> in = ParseHex(hexin.toStdString());
    std::vector<unsigned char> correctout = ParseHex(hexout.toStdString());
    std::vector<unsigned char> realout(in.size() + AES_BLOCKSIZE);

    // Encrypt the plaintext and verify that it equals the cipher
    AES128CBCEncrypt enc(&key[0], &iv[0], pad);
    int size = enc.Encrypt(&in[0], in.size(), &realout[0]);
    realout.resize(size);
    QVERIFY(realout.size() == correctout.size());
    QVERIFY2(realout == correctout, (HexStr(realout) + std::string(" != ") + hexout.toStdString()).c_str());

    // Decrypt the cipher and verify that it equals the plaintext
    std::vector<unsigned char> decrypted(correctout.size());
    AES128CBCDecrypt dec(&key[0], &iv[0], pad);
    size = dec.Decrypt(&correctout[0], correctout.size(), &decrypted[0]);
    decrypted.resize(size);
    QVERIFY(decrypted.size() == in.size());
    QVERIFY2(decrypted == in, (HexStr(decrypted) + std::string(" != ") + hexin.toStdString()).c_str());

    // Encrypt and re-decrypt substrings of the plaintext and verify that they equal each-other
    for(std::vector<unsigned char>::iterator i(in.begin()); i != in.end(); ++i) {
        std::vector<unsigned char> sub(i, in.end());
        std::vector<unsigned char> subout(sub.size() + AES_BLOCKSIZE);
        int size = enc.Encrypt(&sub[0], sub.size(), &subout[0]);
        if (size != 0) {
            subout.resize(size);
            std::vector<unsigned char> subdecrypted(subout.size());
            size = dec.Decrypt(&subout[0], subout.size(), &subdecrypted[0]);
            subdecrypted.resize(size);
            QVERIFY(decrypted.size() == in.size());
            QVERIFY2(subdecrypted == sub, (HexStr(subdecrypted) + std::string(" != ") + HexStr(sub)).c_str());
        }
    }
}

void CryptoTests::testAES128CBC_data()
{
    QTest::addColumn<QString>("hexkey");
    QTest::addColumn<QString>("hexiv");
    QTest::addColumn<bool>("pad");
    QTest::addColumn<QString>("hexin");
    QTest::addColumn<QString>("hexout");

    // NIST AES CBC 128-bit encryption test-vectors
    QTest::newRow("1") << "2b7e151628aed2a6abf7158809cf4f3c" << "000102030405060708090A0B0C0D0E0F" << false
                  << "6bc1bee22e409f96e93d7e117393172a" << "7649abac8119b246cee98e9b12e9197d";
    QTest::newRow("2") << "2b7e151628aed2a6abf7158809cf4f3c" << "7649ABAC8119B246CEE98E9B12E9197D" << false
                  << "ae2d8a571e03ac9c9eb76fac45af8e51" << "5086cb9b507219ee95db113a917678b2";
    QTest::newRow("3") << "2b7e151628aed2a6abf7158809cf4f3c" << "5086cb9b507219ee95db113a917678b2" << false
                  << "30c81c46a35ce411e5fbc1191a0a52ef" << "73bed6b8e3c1743b7116e69e22229516";
    QTest::newRow("4") << "2b7e151628aed2a6abf7158809cf4f3c" << "73bed6b8e3c1743b7116e69e22229516" << false
                  << "f69f2445df4f9b17ad2b417be66c3710" << "3ff1caa1681fac09120eca307586e1a7";

    // The same vectors with padding enabled
    QTest::newRow("a") << "2b7e151628aed2a6abf7158809cf4f3c" << "000102030405060708090A0B0C0D0E0F" << true
                  << "6bc1bee22e409f96e93d7e117393172a" << "7649abac8119b246cee98e9b12e9197d8964e0b149c10b7b682e6e39aaeb731c";
    QTest::newRow("b") << "2b7e151628aed2a6abf7158809cf4f3c" << "7649ABAC8119B246CEE98E9B12E9197D" << true
                  << "ae2d8a571e03ac9c9eb76fac45af8e51" << "5086cb9b507219ee95db113a917678b255e21d7100b988ffec32feeafaf23538";
    QTest::newRow("c") << "2b7e151628aed2a6abf7158809cf4f3c" << "5086cb9b507219ee95db113a917678b2" << true
                  << "30c81c46a35ce411e5fbc1191a0a52ef" << "73bed6b8e3c1743b7116e69e22229516f6eccda327bf8e5ec43718b0039adceb";
    QTest::newRow("d") << "2b7e151628aed2a6abf7158809cf4f3c" << "73bed6b8e3c1743b7116e69e22229516" << true
                       << "f69f2445df4f9b17ad2b417be66c3710" << "3ff1caa1681fac09120eca307586e1a78cb82807230e1321d3fae00d18cc2012";
}

void CryptoTests::testAES256CBC()
{
    QFETCH(QString, hexkey);
    QFETCH(QString, hexiv);
    QFETCH(bool, pad);
    QFETCH(QString, hexin);
    QFETCH(QString, hexout);

    std::vector<unsigned char> key = ParseHex(hexkey.toStdString());
    std::vector<unsigned char> iv = ParseHex(hexiv.toStdString());
    std::vector<unsigned char> in = ParseHex(hexin.toStdString());
    std::vector<unsigned char> correctout = ParseHex(hexout.toStdString());
    std::vector<unsigned char> realout(in.size() + AES_BLOCKSIZE);

    // Encrypt the plaintext and verify that it equals the cipher
    AES256CBCEncrypt enc(&key[0], &iv[0], pad);
    int size = enc.Encrypt(&in[0], in.size(), &realout[0]);
    realout.resize(size);
    QVERIFY(realout.size() == correctout.size());
    QVERIFY2(realout == correctout, (HexStr(realout) + std::string(" != ") + hexout.toStdString()).c_str());

    // Decrypt the cipher and verify that it equals the plaintext
    std::vector<unsigned char> decrypted(correctout.size());
    AES256CBCDecrypt dec(&key[0], &iv[0], pad);
    size = dec.Decrypt(&correctout[0], correctout.size(), &decrypted[0]);
    decrypted.resize(size);
    QVERIFY(decrypted.size() == in.size());
    QVERIFY2(decrypted == in, (HexStr(decrypted) + std::string(" != ") + hexin.toStdString()).c_str());

    // Encrypt and re-decrypt substrings of the plaintext and verify that they equal each-other
    for(std::vector<unsigned char>::iterator i(in.begin()); i != in.end(); ++i)
    {
        std::vector<unsigned char> sub(i, in.end());
        std::vector<unsigned char> subout(sub.size() + AES_BLOCKSIZE);
        int size = enc.Encrypt(&sub[0], sub.size(), &subout[0]);
        if (size != 0)
        {
            subout.resize(size);
            std::vector<unsigned char> subdecrypted(subout.size());
            size = dec.Decrypt(&subout[0], subout.size(), &subdecrypted[0]);
            subdecrypted.resize(size);
            QVERIFY(decrypted.size() == in.size());
            QVERIFY2(subdecrypted == sub, (HexStr(subdecrypted) + std::string(" != ") + HexStr(sub)).c_str());
        }
    }

}

void CryptoTests::testAES256CBC_data()
{
    QTest::addColumn<QString>("hexkey");
    QTest::addColumn<QString>("hexiv");
    QTest::addColumn<bool>("pad");
    QTest::addColumn<QString>("hexin");
    QTest::addColumn<QString>("hexout");

    // NIST AES CBC 256-bit encryption test-vectors
    QTest::newRow("1") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "000102030405060708090A0B0C0D0E0F" << false << "6bc1bee22e409f96e93d7e117393172a"
                  << "f58c4c04d6e5f1ba779eabfb5f7bfbd6";
    QTest::newRow("2") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "F58C4C04D6E5F1BA779EABFB5F7BFBD6" << false << "ae2d8a571e03ac9c9eb76fac45af8e51"
                  << "9cfc4e967edb808d679f777bc6702c7d";
    QTest::newRow("3") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "9CFC4E967EDB808D679F777BC6702C7D" << false << "30c81c46a35ce411e5fbc1191a0a52ef"
                  << "39f23369a9d9bacfa530e26304231461";
    QTest::newRow("4") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "39F23369A9D9BACFA530E26304231461" << false << "f69f2445df4f9b17ad2b417be66c3710"
                  << "b2eb05e2c39be9fcda6c19078c6a9d1b";

    // The same vectors with padding enabled
    QTest::newRow("a") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "000102030405060708090A0B0C0D0E0F" << true << "6bc1bee22e409f96e93d7e117393172a"
                  << "f58c4c04d6e5f1ba779eabfb5f7bfbd6485a5c81519cf378fa36d42b8547edc0";
    QTest::newRow("b") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "F58C4C04D6E5F1BA779EABFB5F7BFBD6" << true << "ae2d8a571e03ac9c9eb76fac45af8e51"
                  << "9cfc4e967edb808d679f777bc6702c7d3a3aa5e0213db1a9901f9036cf5102d2";
    QTest::newRow("c") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "9CFC4E967EDB808D679F777BC6702C7D" << true << "30c81c46a35ce411e5fbc1191a0a52ef"
                  << "39f23369a9d9bacfa530e263042314612f8da707643c90a6f732b3de1d3f5cee";
    QTest::newRow("d") << "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"
                  << "39F23369A9D9BACFA530E26304231461" << true << "f69f2445df4f9b17ad2b417be66c3710"
                  << "b2eb05e2c39be9fcda6c19078c6a9d1b3f461796d6b0d6b2e0c2a72b4d80e644";
}
