/*
 * This file is part of the Flowee project
 * Copyright (c) 2009-2010 Satoshi Nakamoto
 * Copyright (c) 2009-2015 The Bitcoin Core developers
 * Copyright (C) 2016, 2018 Tom Zander <tomz@freedommail.ch>
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

#include "Application.h"
#include <SettingsDefaults.h>
#include <validation/Engine.h>
#include <validation/BlockValidation_p.h>
#include "primitives/FastBlock.h"
#include "streaming/BufferPool.h"
#include "serialize.h"
#include "encodings_legacy.h"
#include "miner.h"
#include <primitives/pubkey.h>

#include "amount.h"
#include "Application.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include <merkle.h>
#include "consensus/validation.h"
#include "hash.h"
#include "main.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "utilstrencodings.h"

#include <boost/tuple/tuple.hpp>
#include <queue>
#include <script/standard.cpp>

#ifdef ENABLE_WALLET
# include <wallet/wallet.h>
# include <init.h>
# include <boost/algorithm/hex.hpp>
#endif

/** What bits to set in version for versionbits blocks */
static const int32_t VERSIONBITS_TOP_BITS = 0x20000000UL;

//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t Mining::UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = CalculateNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

CScript Mining::GetCoinbase() const
{
    std::lock_guard<std::mutex> lock(m_lock);
    return m_coinbase;
}

void Mining::SetCoinbase(const CScript &coinbase)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_coinbase = coinbase;
}


CBlockTemplate* Mining::CreateNewBlock(Validation::Engine &validationEngine) const
{
    assert(validationEngine.blockchain());
    assert(validationEngine.mempool());
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience
    pblock->nTime = GetAdjustedTime();

    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_coinbase.empty())
            throw std::runtime_error("Require coinbase to be set before mining");
        txNew.vout[0].scriptPubKey = m_coinbase;
    }

    // Add dummy coinbase tx as first transaction
    pblock->vtx.push_back(CTransaction());
    pblocktemplate->vTxFees.push_back(-1); // updated at end

    // Largest block you're willing to create (in bytes):
    uint32_t nBlockMaxSize = std::max<uint32_t>(1000, GetArg("-blockmaxsize", Settings::DefaultBlockMAxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    const uint32_t nBlockPrioritySize = std::min<uint32_t>(GetArg("-blockprioritysize", Settings::DefaultBlockPrioritySize), nBlockMaxSize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    uint32_t nBlockMinSize = std::min<uint32_t>(GetArg("-blockminsize", Settings::DefaultBlockMinSize), nBlockMaxSize);

    // Collect memory pool transactions into the block
    CTxMemPool::setEntries inBlock;
    CTxMemPool::setEntries waitSet;

    // This vector will be sorted into a priority queue:
    std::vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    std::priority_queue<CTxMemPool::txiter, std::vector<CTxMemPool::txiter>, ScoreCompare> clearedTxs;
    bool fPrintPriority = GetBoolArg("-printpriority", Settings::DefaultGeneratePriorityLogging);
    const uint32_t nCoinbaseReserveSize = 1000;
    uint64_t nBlockSize = nCoinbaseReserveSize;
    uint64_t nBlockTx = 0;
    int lastFewTxs = 0;
    CAmount nFees = 0;

    {
        CTxMemPool *mempool = validationEngine.mempool();
        LOCK2(cs_main, mempool->cs);
        CBlockIndex* pindexPrev = validationEngine.blockchain()->Tip();
        assert(pindexPrev); // genesis should be present.

        const int nHeight = pindexPrev->nHeight + 1;
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

        pblock->nVersion = VERSIONBITS_TOP_BITS;
        // -regtest only: allow overriding block.nVersion with
        // -blockversion=N to test forking scenarios
        if (Params().MineBlocksOnDemand())
            pblock->nVersion = GetArg("-blockversion", pblock->nVersion);

        UpdateTime(pblock, Params().GetConsensus(), pindexPrev);

        int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                ? nMedianTimePast
                                : pblock->GetBlockTime();

        bool fPriorityBlock = nBlockPrioritySize > 0;
        if (fPriorityBlock) {
            vecPriority.reserve(mempool->mapTx.size());
            for (CTxMemPool::indexed_transaction_set::iterator mi = mempool->mapTx.begin();
                 mi != mempool->mapTx.end(); ++mi)
            {
                double dPriority = mi->GetPriority(nHeight);
                CAmount dummy;
                mempool->ApplyDeltas(mi->GetTx().GetHash(), dPriority, dummy);
                vecPriority.push_back(TxCoinAgePriority(dPriority, mi));
            }
            std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        }

        CTxMemPool::indexed_transaction_set::nth_index<3>::type::iterator mi = mempool->mapTx.get<3>().begin();
        CTxMemPool::txiter iter;

        while (mi != mempool->mapTx.get<3>().end() || !clearedTxs.empty())
        {
            bool priorityTx = false;
            if (fPriorityBlock && !vecPriority.empty()) { // add a tx from priority queue to fill the blockprioritysize
                priorityTx = true;
                iter = vecPriority.front().second;
                actualPriority = vecPriority.front().first;
                std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                vecPriority.pop_back();
            }
            else if (clearedTxs.empty()) { // add tx with next highest score
                iter = mempool->mapTx.project<0>(mi);
                mi++;
            }
            else {  // try to add a previously postponed child tx
                iter = clearedTxs.top();
                clearedTxs.pop();
            }

            if (inBlock.count(iter))
                continue; // could have been added to the priorityBlock

            const CTransaction& tx = iter->GetTx();

            bool fOrphan = false;
            for (CTxMemPool::txiter parent : mempool->GetMemPoolParents(iter)) {
                if (!inBlock.count(parent)) {
                    fOrphan = true;
                    break;
                }
            }
            if (fOrphan) {
                if (priorityTx)
                    waitPriMap.insert(std::make_pair(iter,actualPriority));
                else
                    waitSet.insert(iter);
                continue;
            }

            unsigned int nTxSize = iter->GetTxSize();
            if (fPriorityBlock &&
                (nBlockSize + nTxSize >= nBlockPrioritySize || !AllowFree(actualPriority))) {
                fPriorityBlock = false;
                waitPriMap.clear();
            }
            if (!priorityTx &&
                (iter->GetModifiedFee() < ::minRelayTxFee.GetFee(nTxSize) && nBlockSize >= nBlockMinSize)) {
                break;
            }
            if (nBlockSize + nTxSize >= nBlockMaxSize) {
                if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
                    break;
                }
                // Once we're within 1000 bytes of a full block, only look at 50 more txs
                // to try to fill the remaining space.
                if (nBlockSize > nBlockMaxSize - 1000) {
                    lastFewTxs++;
                }
                continue;
            }

            if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
                continue;

            CAmount nTxFees = iter->GetFee();
            // Added
            pblock->vtx.push_back(tx);
            pblocktemplate->vTxFees.push_back(nTxFees);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nFees += nTxFees;

            if (fPrintPriority) {
                double dPriority = iter->GetPriority(nHeight);
                CAmount dummy;
                mempool->ApplyDeltas(tx.GetHash(), dPriority, dummy);
                logInfo(Log::Mining) << "priority" << dPriority << "fee" << CFeeRate(iter->GetModifiedFee(), nTxSize).ToString() << "txid" << tx.GetHash();
            }

            inBlock.insert(iter);

            // Add transactions that depend on this one to the priority queue
            for (CTxMemPool::txiter child : mempool->GetMemPoolChildren(iter)) {
                if (fPriorityBlock) {
                    waitPriIter wpiter = waitPriMap.find(child);
                    if (wpiter != waitPriMap.end()) {
                        vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                        std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                        waitPriMap.erase(wpiter);
                    }
                }
                else {
                    if (waitSet.count(child)) {
                        clearedTxs.push(child);
                        waitSet.erase(child);
                    }
                }
            }
        }
        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        logInfo(Log::Mining) << "CreateNewBlock(): total size:" <<nBlockSize << "txs:" << nBlockTx
                             << "fees:" << nFees;

        // Compute final coinbase transaction.
        txNew.vout[0].nValue = nFees + GetBlockSubsidy(nHeight, Params().GetConsensus());
        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0 << m_coinbaseComment;

        // Make sure the coinbase is big enough. (since 20181115 HF we require a min 100bytes tx size)
        const uint32_t coinbaseSize = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
        if (coinbaseSize < 100)
            txNew.vin[0].scriptSig << std::vector<uint8_t>(100 - coinbaseSize - 1);
        pblock->vtx[0] = txNew;
        pblocktemplate->vTxFees[0] = -nFees;

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        UpdateTime(pblock, Params().GetConsensus(), pindexPrev);
        pblock->nBits          = CalculateNextWorkRequired(pindexPrev, pblock, Params().GetConsensus());
        pblock->nNonce         = 0;
    }
    if (validationEngine.priv().lock()->tipFlags.hf201811Active) {
        // sort the to-be-mined block using CTOR rules
        std::sort(++pblock->vtx.begin(), pblock->vtx.end(), &CTransaction::sortTxByTxId);
    }
    auto conf = validationEngine.addBlock(FastBlock::fromOldBlock(*pblock), 0);
    conf.setCheckMerkleRoot(false);
    conf.setCheckPoW(false);
    conf.setOnlyCheckValidity(true);
    conf.start();
    conf.waitUntilFinished();
    if (!conf.error().empty()) {
        logFatal(Log::Mining) << "CreateNewBlock managed to mine an invalid block:" << conf.error();
        if (pblock->vtx.size() == 1) // avoid user passing in bad block number or somesuch create an infinite recursion.
            return nullptr;
        // This should also never happen... but if an invalid transaction somehow entered
        // the mempool due to a bug, remove all the transactions in the block
        // and try again (it is not worth trying to figure out which transaction(s)
        // are causing the block to be invalid).
        logCritical(Log::Mining) << "Retrying with smaller mempool";
        std::list<CTransaction> unused;
        CTxMemPool *mempool = validationEngine.mempool();
        BOOST_REVERSE_FOREACH(const CTransaction& tx, pblock->vtx) {
            mempool->remove(tx, unused, true);
        }
        pblocktemplate.reset();
        return CreateNewBlock(validationEngine); // recurse with smaller mempool
    }

    return pblocktemplate.release();
}

void Mining::IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    if (m_hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        m_hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) << m_coinbaseComment;
    const uint32_t coinbaseSize = ::GetSerializeSize(txCoinbase, SER_NETWORK, PROTOCOL_VERSION);
    if (coinbaseSize < 100)
        txCoinbase.vin[0].scriptSig << std::vector<uint8_t>(100 - coinbaseSize - 1);
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

//
// ScanHash scans nonces looking for a hash with at least some zero bits.
// The nonce is usually preserved between calls, but periodically or if the
// nonce is 0xffff0000 or above, the block is rebuilt and nNonce starts over at
// zero.
//
bool static ScanHash(const CBlockHeader *pblock, uint32_t& nNonce, uint256 *phash)
{
    // Write the first 76 bytes of the block header to a double-SHA256 state.
    CHash256 hasher;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *pblock;
    assert(ss.size() == 80);
    hasher.Write((unsigned char*)&ss[0], 76);

    while (true) {
        nNonce++;

        // Write the last 4 bytes of the block header (the nonce) to a copy of
        // the double-SHA256 state, and compute the result.
        CHash256(hasher).Write((unsigned char*)&nNonce, 4).Finalize((unsigned char*)phash);

        // Return the nonce if the hash has at least some zero bits,
        // caller will check if it has enough to reach the target
        if (((uint16_t*)phash)[15] == 0)
            return true;

        // If nothing found after trying for a while, return -1
        if ((nNonce & 0xfff) == 0)
            return false;
    }
}

static void ProcessBlockFound(const CBlock* pblock)
{
    logInfo(Log::Mining) << pblock->ToString();
    logInfo(Log::Mining) << "generated" << FormatMoney(pblock->vtx[0].vout[0].nValue);

    auto validation = Application::instance()->validation();
    // Process this block the same as if we had received it from another node
    auto future = validation->addBlock(FastBlock::fromOldBlock(*pblock),
            Validation::ForwardGoodToPeers | Validation::SaveGoodToDisk).start();
    future.waitUntilFinished();
}

void static BitcoinMiner(const CChainParams& chainparams)
{
    logCritical(Log::Mining) << "BitcoinMiner started";
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("bitcoin-miner");

    unsigned int nExtraNonce = 0;
    Mining *mining = Mining::instance();

    try {
        while (true) {
            if (chainparams.MiningRequiresPeers()) {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                do {
                    bool fvNodesEmpty;
                    {
                        LOCK(cs_vNodes);
                        fvNodesEmpty = vNodes.empty();
                    }
                    if (!fvNodesEmpty && !IsInitialBlockDownload())
                        break;
                    MilliSleep(1000);
                } while (true);
            }

            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            CBlockIndex* pindexPrev = chainActive.Tip();

            std::unique_ptr<CBlockTemplate> pblocktemplate(mining->CreateNewBlock());
            if (!pblocktemplate.get()) {
                logCritical(Log::Mining) << "Error in BitcoinMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread";
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            mining->IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            logInfo(Log::Mining) << "Running BitcoinMiner with" << pblock->vtx.size() << "transactions in block."
                << ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION) << "bytes.";

            //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
            uint256 hash;
            uint32_t nNonce = 0;
            while (true) {
                // Check if something found
                if (ScanHash(pblock, nNonce, &hash))
                {
                    if (UintToArith256(hash) <= hashTarget)
                    {
                        // Found a solution
                        pblock->nNonce = nNonce;
                        assert(hash == pblock->GetHash());

                        SetThreadPriority(THREAD_PRIORITY_NORMAL);
                        logCritical(Log::Mining) << "BitcoinMiner:";
                        logCritical(Log::Mining) << "proof-of-work found\n  hash:" << hash.GetHex()
                                                 << "\n  target:" <<  hashTarget.GetHex();
                        ProcessBlockFound(pblock);
                        SetThreadPriority(THREAD_PRIORITY_LOWEST);

                        // In regression test mode, stop mining after a block is found.
                        if (chainparams.MineBlocksOnDemand())
                            throw boost::thread_interrupted();

                        break;
                    }
                }

                // Check for stop or if block needs to be rebuilt
                boost::this_thread::interruption_point();
                // Regtest mode doesn't require peers
                if (vNodes.empty() && chainparams.MiningRequiresPeers())
                    break;
                if (nNonce >= 0xffff0000)
                    break;
                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                    break;
                if (pindexPrev != chainActive.Tip())
                    break;

                // Update nTime every few seconds
                if (mining->UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev) < 0)
                    break; // Recreate the block if the clock has run backwards,
                           // so that we can use the correct time.
                if (chainparams.GetConsensus().fPowAllowMinDifficultyBlocks)
                {
                    // Changing pblock->nTime can change work required on testnet:
                    hashTarget.SetCompact(pblock->nBits);
                }
            }
        }
    }
    catch (const boost::thread_interrupted&) {
        logCritical(Log::Mining) << "BitcoinMiner terminated";
        throw;
    }
    catch (const std::runtime_error &e) {
        logCritical(Log::Mining) << "BitcoinMiner runtime error:" << e;
        return;
    }
}

CScript Mining::ScriptForCoinbase(const std::string &coinbase)
{
    if (coinbase.empty())
        throw std::runtime_error("Please pass in a coinbase");

    if (IsHex(coinbase)) {
        std::vector<unsigned char> data(ParseHex(coinbase));
        if (data.size() != 20)
            throw std::runtime_error("Invalid hash160");
        CScript answer;
        answer << OP_DUP << OP_HASH160 << ToByteVector(data) << OP_EQUALVERIFY << OP_CHECKSIG;
        return answer;
    }
    CBitcoinAddress ad(coinbase);
    if (ad.IsValid()) {
        CKeyID id;
        if (ad.GetKeyID(id)) {
            std::vector<unsigned char> data(id.begin(), id.end());
            CScript answer;
            answer << OP_DUP << OP_HASH160 << data << OP_EQUALVERIFY << OP_CHECKSIG;
            return answer;
        }
    }
    throw std::runtime_error("address not in recognized format");
}

void Mining::GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams, const std::string &coinbase_)
{
    if (nThreads < 0)
        nThreads = boost::thread::physical_concurrency();

    Mining *miningInstance = instance();

    if (miningInstance->m_minerThreads != 0) // delete old
    {
        miningInstance->m_minerThreads->interrupt_all();
        delete miningInstance->m_minerThreads;
        miningInstance->m_minerThreads = 0;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    std::string coinbase(coinbase_);
#ifdef ENABLE_WALLET
    if (coinbase.empty()) {
        // try to get it from the wallet
        boost::shared_ptr<CReserveScript> coinbaseScript;
        ValidationNotifier().GetScriptForMining(coinbaseScript);

        if (pwalletMain) {
            boost::shared_ptr<CReserveKey> rKey(new CReserveKey(pwalletMain));
            CPubKey pubkey;
            if (rKey->GetReservedKey(pubkey)) {
                std::vector<unsigned char> v = ToByteVector(pubkey);
                boost::algorithm::hex(v.begin(), v.end(), back_inserter(coinbase));
                rKey->KeepKey();
            }
        }
    }
#endif

    miningInstance->SetCoinbase(ScriptForCoinbase(coinbase));
    miningInstance->m_minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        miningInstance->m_minerThreads->create_thread(std::bind(&BitcoinMiner, boost::cref(chainparams)));
}

Mining* Mining::s_instance = 0;

void Mining::Stop()
{
    delete s_instance;
    s_instance = 0;
}

Mining *Mining::instance()
{
    if (s_instance == 0)
        s_instance = new Mining();
    return s_instance;
}

Mining::~Mining()
{
    if (m_minerThreads) {
        m_minerThreads->interrupt_all();
        delete m_minerThreads;
    }
}

CBlockTemplate *Mining::CreateNewBlock() const
{
    return CreateNewBlock(*Application::instance()->validation());
}

Mining::Mining()
    : m_minerThreads(0)
{
    // read args to create m_coinbaseComment
    std::int32_t sizeLimit = Policy::blockSizeAcceptLimit();

    std::stringstream ss;
    ss << std::fixed;
    if ((sizeLimit % 1000000) != 0)
        ss << std::setprecision(1) << sizeLimit / 1E6;
    else
        ss << (int) (sizeLimit / 1E6);
    std::string comment = "EB" + ss.str();
    m_coinbaseComment =  std::vector<unsigned char>(comment.begin(), comment.end());
}
