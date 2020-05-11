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
#include "Blockchain.h"
#include "DownloadManager.h"

#include <streaming/BufferPool.h>
#include <streaming/P2PBuilder.h>
#include <streaming/P2PParser.h>
#include <utils/utiltime.h>

#include <stdexcept>

Blockchain::Blockchain(DownloadManager *downloadManager, const boost::filesystem::path &basedir)
    : m_basedir(basedir), m_dlmanager(downloadManager)
{
    assert(m_dlmanager);
    m_longestChain.reserve(650000); // pre-allocate
    load();
    if (m_longestChain.empty()) {
        BlockHeader genesis;
        genesis.nBits = 0x1d00ffff;
        genesis.nTime = 1231006505;
        genesis.nNonce = 2083236893;
        genesis.nVersion = 1;
        genesis.hashMerkleRoot = uint256S("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");

        m_longestChain.push_back(genesis);

        const uint256 genesisHash = genesis.createHash();
        m_blockHeight.insert(std::make_pair(genesisHash, 0));
        m_tip.tip = genesisHash;
        m_tip.height = 0;
        m_tip.chainWork += genesis.blockProof();
    }

    checkpoints.insert(std::make_pair( 11111, uint256S("0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d")));
    checkpoints.insert(std::make_pair( 33333, uint256S("000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6")));
    checkpoints.insert(std::make_pair( 74000, uint256S("0000000000573993a3c9e41ce34471c079dcf5f52a0e824a81e7f953b8661a20")));
    checkpoints.insert(std::make_pair(105000, uint256S("00000000000291ce28027faea320c8d2b054b2e0fe44a773f3eefb151d6bdc97")));
    checkpoints.insert(std::make_pair(134444, uint256S("00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe")));
    checkpoints.insert(std::make_pair(168000, uint256S("000000000000099e61ea72015e79632f216fe6cb33d7899acb35b75c8303b763")));
    checkpoints.insert(std::make_pair(193000, uint256S("000000000000059f452a5f7340de6682a977387c17010ff6e6c3bd83ca8b1317")));
    checkpoints.insert(std::make_pair(210000, uint256S("000000000000048b95347e83192f69cf0366076336c639f9b7228e9ba171342e")));
    checkpoints.insert(std::make_pair(216116, uint256S("00000000000001b4f4b433e81ee46494af945cf96014816a4e2370f11b23df4e")));
    checkpoints.insert(std::make_pair(225430, uint256S("00000000000001c108384350f74090433e7fcf79a606b8e797f065b130575932")));
    checkpoints.insert(std::make_pair(250000, uint256S("000000000000003887df1f29024b06fc2200b55f8af8f35453d7be294df2d214")));
    checkpoints.insert(std::make_pair(279000, uint256S("0000000000000001ae8c72a0b0c301f67e3afca10e819efa9041e458e9bd7e40")));
    checkpoints.insert(std::make_pair(295000, uint256S("00000000000000004d9b4ef50f0f9d686fd69db2e03af35a100370c64632a983")));
    checkpoints.insert(std::make_pair(478559, uint256S("000000000000000000651ef99cb9fcbe0dadde1d424bd9f15ff20136191a5eec")));
    checkpoints.insert(std::make_pair(556767, uint256S("0000000000000000004626ff6e3b936941d341c5932ece4357eeccac44e6d56c")));
    checkpoints.insert(std::make_pair(582680, uint256S("000000000000000001b4b8e36aec7d4f9671a47872cb9a74dc16ca398c7dcc18")));
    checkpoints.insert(std::make_pair(609136, uint256S("000000000000000000b48bb207faac5ac655c313e41ac909322eaa694f5bc5b1")));
}

Message Blockchain::createGetHeadersRequest(Streaming::P2PBuilder &builder)
{
    std::unique_lock<std::mutex> lock(m_lock);
    uint256 tip;
    if (m_tip.height < 1000) {
        builder.writeCompactSize(1);
        builder.writeByteArray(tip.begin(), 32, Streaming::RawBytes);
    } else {
        builder.writeCompactSize(10);
        std::array<int, 10> offsets = {
            m_tip.height,
            m_tip.height - 3,
            m_tip.height - 20,
            m_tip.height - 60,
            m_tip.height - 100,
            m_tip.height - 200,
            m_tip.height - 400,
            m_tip.height - 600,
            m_tip.height - 800,
            m_tip.height - 1000
        };
        for (auto i : offsets) {
            uint256 hash = m_longestChain.at(i - 1).createHash();
            builder.writeByteArray(hash.begin(), 32, Streaming::RawBytes);
        }
    }
    builder.writeByteArray(tip.begin(), 32, Streaming::RawBytes);
    return builder.message(Api::P2P::GetHeaders);
}

void Blockchain::processBlockHeaders(Message message, int peerId)
{
    int newTip;

    try {
        std::unique_lock<std::mutex> lock(m_lock);
        Streaming::P2PParser parser(message);
        auto count = parser.readCompactInt();
        if (count > 2000) {
            logInfo() << "Peer" << peerId << "Sent too many headers" << count << "p2p protocol violation";
            m_dlmanager->reportDataFailure(peerId);
            return;
        }
        const uint32_t maxFuture = time(nullptr) + 7200; // headers can not be more than 2 hours in the future.

        uint256 prevHash;
        int startHeight = -1;
        int height = 0;
        int64_t previousTime = 0;
        arith_uint256 chainWork;
        for (size_t i = 0; i < count; ++i) {
            BlockHeader header = BlockHeader::fromMessage(parser);
            /*int txCount =*/ parser.readCompactInt(); // always zero

            // timestamp not more than 2h in the future.
            if (header.nTime > maxFuture) {
                logWarning() << "Peer" << peerId << "sent bogus headers. Too far in future";
                m_dlmanager->reportDataFailure(peerId);
                return;
            }

            if (startHeight == -1) { // first header in the sequence.
                auto iter = m_blockHeight.find(header.hashPrevBlock);
                if (iter == m_blockHeight.end())
                    throw std::runtime_error("is on a different chain, headers don't extend ours");
                height = startHeight = iter->second + 1;
                if (m_tip.height + 1 == startHeight) {
                    chainWork = m_tip.chainWork;
                    previousTime = m_longestChain.end()->nTime;
                } else if (m_tip.height - startHeight > (int) count) {
                    throw std::runtime_error("is on a different chain, headers don't extend ours");
                } else {
                    // rollback the chainWork to branch-point
                    assert(m_tip.height == (int) m_longestChain.size() - 1);
                    chainWork = m_tip.chainWork;
                    for (int height = m_tip.height; height >= startHeight; --height) {
                        chainWork -= m_longestChain.at(height).blockProof();
                    }
                    previousTime = m_longestChain.at(startHeight - 1).nTime;
                }
            }
            else if (prevHash != header.hashPrevBlock) { // check if we are really a sequence.
                throw std::runtime_error("sent bogus headers. Not in sequence");
            }
            uint256 hash = header.createHash();
            // check POW
            {
                bool fNegative;
                bool fOverflow;
                arith_uint256 bnTarget;
                bnTarget.SetCompact(header.nBits, &fNegative, &fOverflow);
                if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(powLimit)
                        || UintToArith256(hash) > bnTarget) {// Check proof of work matches claimed amount
                    throw std::runtime_error("sent bogus headers. POW failed");
                }
            }
            chainWork += header.blockProof();

            auto cpIter = checkpoints.find(height);
            if (cpIter != checkpoints.end()) {
                if (cpIter->second != hash)
                    throw std::runtime_error("is on a different chain, checkpoint failure");
            }
            prevHash = std::move(hash);
            ++height;
        }

        if (chainWork <= m_tip.chainWork)
            return;

        // The new chain has more PoW, apply it.
        parser = Streaming::P2PParser(message);
        count = parser.readCompactInt();
        height = startHeight;
        m_longestChain.resize(startHeight + count);
        for (size_t i = 0; i < count; ++i) {
            BlockHeader header = BlockHeader::fromMessage(parser);
            /*int txCount =*/ parser.readCompactInt(); // always zero
            m_blockHeight.insert(std::make_pair(header.createHash(), height));
            m_longestChain[height++] = header;
        }
        m_tip.height = height - 1;
        m_tip.tip = prevHash;
        m_tip.chainWork= chainWork;
        newTip = m_tip.height;
        logCritical() << "Headers now at" << newTip << m_tip.tip <<
                     DateTimeStrFormat("%Y-%m-%d %H:%M:%S", m_longestChain.back().nTime).c_str();
    } catch (const std::runtime_error &err) {
        logWarning() << "Peer" << peerId << "is" << err.what();
        m_dlmanager->reportDataFailure(peerId);
        return;
    }

    m_dlmanager->headersDownloadFinished(newTip, peerId);
}

int Blockchain::height() const
{
    return m_tip.height;
}

int Blockchain::expectedBlockHeight() const
{
    std::unique_lock<std::mutex> lock(m_lock);
    const BlockHeader &tip = m_longestChain.back();
    int secsSinceLastBlock = time(nullptr) - tip.nTime;
    return m_tip.height + (secsSinceLastBlock + 300) / 600; // add how many 10 minutes chunks fit in the time-span.
}

bool Blockchain::isKnown(const uint256 &blockId) const
{
    std::unique_lock<std::mutex> lock(m_lock);
    return m_blockHeight.find(blockId) != m_blockHeight.end();
}

int Blockchain::blockHeightFor(const uint256 &blockId) const
{
    std::unique_lock<std::mutex> lock(m_lock);
    auto iter = m_blockHeight.find(blockId);
    if (iter == m_blockHeight.end())
        return -1;
    if (int(m_longestChain.size()) <= iter->second)
        return -1;
    if (m_longestChain.at(iter->second).createHash() != blockId)
        return -1;
    return iter->second;
}

BlockHeader Blockchain::block(int height) const
{
    assert(height > 0);
    std::unique_lock<std::mutex> lock(m_lock);
    if (int(m_longestChain.size()) <= height)
        return BlockHeader();
    return m_longestChain.at(height);
}

void Blockchain::save()
{
    boost::filesystem::create_directories(m_basedir);
    std::unique_lock<std::mutex> lock(m_lock);

    std::ofstream out((m_basedir / "blockchain").string());
    Streaming::BufferPool pool;
    for (const auto &header : m_longestChain) {
        auto cd = header.write(pool);
        assert(cd.size() == 80);
        out.write(cd.begin(), cd.size());
    }
}

void Blockchain::load()
{
    std::unique_lock<std::mutex> lock(m_lock);

    std::ifstream in((m_basedir / "blockchain").string());
    if (!in.is_open())
        return;

    logInfo() << "Starting to load the blockchain";
    Streaming::BufferPool pool;
    while (true) {
        pool.reserve(80);
        size_t need = 80;
        while (need > 0) {
            auto readAmount = in.readsome(pool.begin() + (80 - need), need);
            if (readAmount <= 0)
                break;
            need -= readAmount;
        }
        if (need != 0)
            break;

        BlockHeader header = BlockHeader::fromMessage(pool.commit(80));
        m_blockHeight.insert(std::make_pair(header.createHash(), m_longestChain.size()));
        m_tip.chainWork += header.blockProof();
        m_longestChain.push_back(header);
    }

    m_tip.tip = m_longestChain.back().createHash();
    m_tip.height = m_longestChain.size() - 1;
    logInfo() << "  done. Tip:" << m_tip.height << m_tip.tip;
}
