/*
 * This file is part of the Flowee project
 * Copyright (C) 2021 Tom Zander <tom@flowee.org>
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

#include "test_blockchain.h"

#include <p2p/Blockchain.h>
#include <p2p/DownloadManager.h>
#include <streaming/BufferPool.h>
#include <streaming/P2PBuilder.h>
#include <utiltime.h>

constexpr const char *genesisHash = "0x000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";

void TestP2PBlockchain::init()
{
    m_tmpPath += QDir::tempPath() + QString("/flowee-test-%1").arg(getpid());
}

void TestP2PBlockchain::cleanup()
{
     // delete tmp dir.
    if (!m_tmpPath.isEmpty())
        QDir(m_tmpPath).removeRecursively();
    m_tmpPath.clear();
    Blockchain::setStaticChain(nullptr, 0);
    SetMockTime(0);
}

void TestP2PBlockchain::basics()
{
    boost::asio::io_service ioService;
    boost::filesystem::path basedir(m_tmpPath.toStdString());
    DownloadManager dlm(ioService, basedir, P2PNet::MainChain);
    Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);

    // empty, no blocks other than genesis
    QCOMPARE(blockchain.height(), 0);
    const auto genesis = uint256S(genesisHash);
    QCOMPARE(blockchain.block(0).createHash(), genesis);
    QVERIFY(blockchain.isKnown(genesis));
    QCOMPARE(blockchain.blockHeightFor(genesis), 0);
}

void TestP2PBlockchain::staticChain()
{
    auto bytes = prepareStaticFile();
    // support
    SetMockTime(1232000000);
    boost::asio::io_service ioService;
    boost::filesystem::path basedir(m_tmpPath.toStdString());
    DownloadManager dlm(ioService, basedir, P2PNet::MainChain);

    // First just load only the static stuff.
    {
        Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);
        QCOMPARE(blockchain.height(), 99);
        QCOMPARE(blockchain.block(99).createHash(), uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995")); // block at height 99 on mainchain
        QCOMPARE(blockchain.blockHeightFor(uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995")), 99);
        QCOMPARE(blockchain.expectedBlockHeight(), 665);
    }

    const QString dest(m_tmpPath + "/blockchain");
    QFile::remove(dest);
    // Now follow the static load with an identical useless blockchain.
    QVERIFY(QFile::copy(QString("%1/headers0-99").arg(SRCDIR), dest));
    {
        Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);
        QCOMPARE(blockchain.height(), 99);
        QCOMPARE(blockchain.block(99).createHash(), uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995")); // block at height 99 on mainchain
        QCOMPARE(blockchain.blockHeightFor(uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995")), 99);
        QCOMPARE(blockchain.expectedBlockHeight(), 665);
    }

    // Now follow the static load with a nicely fitting one
    QFile::remove(dest);
    QVERIFY(QFile::copy(QString("%1/headers100-111").arg(SRCDIR), dest));
    {
        Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);
        QCOMPARE(blockchain.height(), 111);
        QCOMPARE(blockchain.block(99).createHash(), uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995"));
        QCOMPARE(blockchain.blockHeightFor(uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995")), 99);
        QCOMPARE(blockchain.block(111).createHash(), uint256S("000000004d6a6dd8b882deec7b54421949dddd2c166bd51ee7f62a52091a6c35"));
        QCOMPARE(blockchain.blockHeightFor(uint256S("000000004d6a6dd8b882deec7b54421949dddd2c166bd51ee7f62a52091a6c35")), 111);
        QCOMPARE(blockchain.expectedBlockHeight(), 662);
    }

    // Now follow the static load with one that slightly overlaps
    QFile::remove(dest);
    QVERIFY(QFile::copy(QString("%1/headers91-104").arg(SRCDIR), dest));
    {
        Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);
        QCOMPARE(blockchain.height(), 104);
        QCOMPARE(blockchain.block(99).createHash(), uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995"));
        QCOMPARE(blockchain.blockHeightFor(uint256S("00000000cd9b12643e6854cb25939b39cd7a1ad0af31a9bd8b2efe67854b1995")), 99);
        QCOMPARE(blockchain.block(104).createHash(), uint256S("00000000fb11ef25014e02b315285a22f80c8f97689d7e36d723317defaabe5b"));
        QCOMPARE(blockchain.blockHeightFor(uint256S("00000000fb11ef25014e02b315285a22f80c8f97689d7e36d723317defaabe5b")), 104);
        QCOMPARE(blockchain.expectedBlockHeight(), 664);
    }

#if 0
    {
        QFile out("headers0-99");
        if (!out.open(QIODevice::WriteOnly))
            logFatal() << "failed to write file";
        for (int i = 0; i <= 99; ++i) {
            BlockHeader bh = blockchain.block(i);
            out.write(reinterpret_cast<const char*>(&bh), 80);
        }
    }
#endif
}

void TestP2PBlockchain::blockHeightAtTime()
{
    boost::asio::io_service ioService;
    boost::filesystem::path basedir(m_tmpPath.toStdString());
    DownloadManager dlm(ioService, basedir, P2PNet::MainChain);
    auto bytes = prepareStaticFile();

    // block 80 is mined at: 1231646077
    // asking for time + 3 sec should give us the block after (81).
    {
        Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);
        QCOMPARE(blockchain.blockHeightAtTime(1231646080), 81);
    }

    // block 101 is mined at 1231661741
    // block 102 is mined at 1231662670
    QVERIFY(QFile::copy(QString("%1/headers100-111").arg(SRCDIR), m_tmpPath + "/blockchain"));
    {
        Blockchain blockchain(&dlm, basedir, P2PNet::MainChain);
        QCOMPARE(blockchain.height(), 111);
        QCOMPARE(blockchain.blockHeightAtTime(1231646080), 81);
        QCOMPARE(blockchain.blockHeightAtTime(1231662000), 102);
        QCOMPARE(blockchain.blockHeightAtTime(1800000000), 112);
    }
}

QByteArray TestP2PBlockchain::prepareStaticFile()
{
    // we copy our static data from a file, more flexible.
    // Suggestion for real apps is to use something like QResource.
    QFile staticHeaders(QString("%1/headers0-99").arg(SRCDIR));
    assert(staticHeaders.open(QIODevice::ReadOnly));
    QByteArray data = staticHeaders.readAll();
    Blockchain::setStaticChain(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
    return data;
}

QTEST_MAIN(TestP2PBlockchain)
