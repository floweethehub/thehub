/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2017,2019 Tom Zander <tomz@freedommail.ch>
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
#include "APIRPCBinding.h"
#include "APIProtocol.h"

#include "streaming/MessageBuilder.h"
#include "BlocksDB.h"
#include "main.h"
#include "rpcserver.h"
#include "base58.h"
#include <univalue.h>

#include <boost/algorithm/hex.hpp>

#include <streaming/MessageParser.h>

#include <list>

#include <primitives/FastBlock.h>

namespace {

// blockchain

class GetBlockChainInfo : public Api::RpcParser
{
public:
    GetBlockChainInfo() : RpcParser("getblockchaininfo", Api::BlockChain::GetBlockChainInfoReply, 500) {}
    virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        const UniValue &chain = find_value(result, "chain");
        builder.add(Api::BlockChain::Chain, chain.get_str());
        const UniValue &blocks = find_value(result, "blocks");
        builder.add(Api::BlockChain::Blocks, blocks.get_int());
        const UniValue &headers = find_value(result, "headers");
        builder.add(Api::BlockChain::Headers, headers.get_int());
        const UniValue &best = find_value(result, "bestblockhash");
        uint256 sha256;
        sha256.SetHex(best.get_str());
        builder.add(Api::BlockChain::BestBlockHash, sha256);
        const UniValue &difficulty = find_value(result, "difficulty");
        builder.add(Api::BlockChain::Difficulty, difficulty.get_real());
        const UniValue &time = find_value(result, "mediantime");
        builder.add(Api::BlockChain::MedianTime, (uint64_t) time.get_int64());
        const UniValue &progress = find_value(result, "verificationprogress");
        builder.add(Api::BlockChain::VerificationProgress, progress.get_real());
        const UniValue &chainwork = find_value(result, "chainwork");
        sha256.SetHex(chainwork.get_str());
        builder.add(Api::BlockChain::ChainWork, sha256);
        const UniValue &pruned = find_value(result, "pruned");
        if (pruned.get_bool())
            builder.add(Api::BlockChain::Pruned, true);

        const UniValue &bip9 = find_value(result, "bip9_softforks");
        bool first = true;
        for (auto fork : bip9.getValues()) {
            if (first) first = false;
            else builder.add(Api::Wallet::Separator, true);
            const UniValue &id = find_value(fork, "id");
            builder.add(Api::BlockChain::Bip9ForkId, id.get_str());
            const UniValue &status = find_value(fork, "status");
            // TODO change status to an enum?
            builder.add(Api::BlockChain::Bip9ForkStatus, status.get_str());
        }
    }
};

class GetBestBlockHash : public Api::RpcParser
{
public:
    GetBestBlockHash() : RpcParser("getbestblockhash", Api::BlockChain::GetBestBlockHashReply, 50) {}
};

class GetBlockLegacy : public Api::RpcParser
{
public:
    GetBlockLegacy() : RpcParser("getblock", Api::BlockChain::GetBlockVerboseReply), m_verbose(true) {}
    virtual void createRequest(const Message &message, UniValue &output) {
        std::string blockId;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHash
                    || parser.tag() == Api::RawTransactions::GenericByteData) {
                boost::algorithm::hex(parser.bytesData(), back_inserter(blockId));
            } else if (parser.tag() == Api::BlockChain::Verbose) {
                m_verbose = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Height) {
                auto index = Blocks::DB::instance()->headerChain()[parser.intData()];
                if (index) {
                    const uint256 blockHash = index->GetBlockHash();
                    boost::algorithm::hex(blockHash.begin(), blockHash.end(), back_inserter(blockId));
                }
            }
        }
        output.push_back(std::make_pair("block", UniValue(UniValue::VSTR, blockId)));
        output.push_back(std::make_pair("verbose", UniValue(UniValue::VBOOL, m_verbose ? "1": "0")));
    }

    virtual int calculateMessageSize(const UniValue &result) const {
        if (m_verbose) {
            const UniValue &tx = find_value(result, "tx");
            return tx.size() * 70 + 200;
        }
        return result.get_str().size() / 2 + 20;
    }

    virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        if (!m_verbose) {
            std::vector<char> answer;
            boost::algorithm::unhex(result.get_str(), back_inserter(answer));
            builder.add(1, answer);
            return;
        }

        const UniValue &hash = find_value(result, "hash");
        std::vector<char> bytearray;
        boost::algorithm::unhex(hash.get_str(), back_inserter(bytearray));
        builder.add(Api::BlockChain::BlockHash, bytearray);
        bytearray.clear();
        const UniValue &confirmations = find_value(result, "confirmations");
        builder.add(Api::BlockChain::Confirmations, confirmations.get_int());
        const UniValue &size = find_value(result, "size");
        builder.add(Api::BlockChain::Size, size.get_int());
        const UniValue &height = find_value(result, "height");
        builder.add(Api::BlockChain::Height, height.get_int());
        const UniValue &version = find_value(result, "version");
        builder.add(Api::BlockChain::Version, version.get_int());
        const UniValue &merkleroot = find_value(result, "merkleroot");
        boost::algorithm::unhex(merkleroot.get_str(), back_inserter(bytearray));
        builder.add(Api::BlockChain::MerkleRoot, bytearray);
        bytearray.clear();
        const UniValue &tx = find_value(result, "tx");
        bool first = true;
        for (const UniValue &transaction: tx.getValues()) {
            if (first) first = false;
            else builder.add(Api::Wallet::Separator, true);
            boost::algorithm::unhex(transaction.get_str(), back_inserter(bytearray));
            builder.add(Api::BlockChain::TxId, bytearray);
            bytearray.clear();
        }
        const UniValue &time = find_value(result, "time");
        builder.add(Api::BlockChain::Time, (uint64_t) time.get_int64());
        const UniValue &mediantime = find_value(result, "mediantime");
        builder.add(Api::BlockChain::MedianTime, (uint64_t) mediantime.get_int64());
        const UniValue &nonce = find_value(result, "nonce");
        builder.add(Api::BlockChain::Nonce, (uint64_t) nonce.get_int64());
        const UniValue &bits = find_value(result, "bits");
        boost::algorithm::unhex(bits.get_str(), back_inserter(bytearray));
        builder.add(Api::BlockChain::Bits, bytearray);
        bytearray.clear();
        const UniValue &difficulty = find_value(result, "difficulty");
        builder.add(Api::BlockChain::Difficulty, difficulty.get_real());
        const UniValue &chainwork = find_value(result, "chainwork");
        boost::algorithm::unhex(chainwork.get_str(), back_inserter(bytearray));
        builder.add(Api::BlockChain::ChainWork, bytearray);
        bytearray.clear();
        const UniValue &prevhash = find_value(result, "previousblockhash");
        boost::algorithm::unhex(prevhash.get_str(), back_inserter(bytearray));
        builder.add(Api::BlockChain::PrevBlockHash, bytearray);
        const UniValue &nextblock = find_value(result, "nextblockhash");
        if (nextblock.isStr()) {
            boost::algorithm::unhex(nextblock.get_str(), back_inserter(bytearray));
            builder.add(Api::BlockChain::NextBlockHash, bytearray);
        }
    }

private:
    bool m_verbose;
};

class GetBlockHeader : public Api::DirectParser
{
public:
    GetBlockHeader() : DirectParser(Api::BlockChain::GetBlockHeaderReply, 190) {}

    void buildReply(Streaming::MessageBuilder &builder, CBlockIndex *index) {
        assert(index);
        builder.add(Api::BlockChain::BlockHash, index->GetBlockHash());
        builder.add(Api::BlockChain::Confirmations, chainActive.Contains(index) ? chainActive.Height() - index->nHeight : -1);
        builder.add(Api::BlockChain::Height, index->nHeight);
        builder.add(Api::BlockChain::Version, index->nVersion);
        builder.add(Api::BlockChain::MerkleRoot, index->hashMerkleRoot);
        builder.add(Api::BlockChain::Time, (uint64_t) index->nTime);
        builder.add(Api::BlockChain::MedianTime, (uint64_t) index->GetMedianTimePast());
        builder.add(Api::BlockChain::Nonce, (uint64_t) index->nNonce);
        builder.add(Api::BlockChain::Bits, (uint64_t) index->nBits);
        builder.add(Api::BlockChain::Difficulty, GetDifficulty(index));
        builder.add(Api::BlockChain::PrevBlockHash, index->phashBlock);
        auto next = chainActive.Next(index);
        if (next)
            builder.add(Api::BlockChain::NextBlockHash, next->GetBlockHash());
    }

    void buildReply(const Message &request, Streaming::MessageBuilder &builder) {
        Streaming::MessageParser parser(request.body());

        Streaming::ParsedType type = parser.next();
        while (type == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHash) {
                uint256 hash = parser.uint256Data();
                CBlockIndex *bi = Blocks::Index::get(hash);
                if (bi)
                    return buildReply(builder, bi);
            }
            if (parser.tag() == Api::BlockChain::Height) {
                int height = parser.intData();
                auto index = chainActive[height];
                if (index)
                    return buildReply(builder, index);
            }

            type = parser.next();
        }
    }
};

/*
 * To receive a block should be hyper configurable.
 * The requestor can want the raw block
 * They should also be able to just get txids, offset-in-block,
 * only inputs or outputs, maybe outputs that are op_return removed,
 * and last we should allow a filter on addresses so we only ship
 * the transactions with outputs matching the filter and filter on
 * 'from' transaction-ids for same.
 */
class GetBlock : public Api::DirectParser
{
public:
    class BlockSessionData : public Api::SessionData
    {
    public:
        std::set<CKeyID> keys; // keys to filter on
    };

    GetBlock() : DirectParser(Api::BlockChain::GetBlockReply) {}

    int calculateMessageSize(const Message &request) {
        CBlockIndex *index = nullptr;
        Streaming::MessageParser parser(request.body());
        BlockSessionData *session = dynamic_cast<BlockSessionData*>(*data);
        if (session == nullptr) {
            session = new BlockSessionData();
            *data = session;
        }
        bool requestOk = false;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHash
                    || parser.tag() == Api::RawTransactions::GenericByteData) {
                if (parser.dataLength() != 32)
                    throw Api::ParserException("BlockHash should be a 32 byte-bytearray");
                index = Blocks::Index::get(uint256(&parser.bytesData()[0]));
                requestOk = true;
            } else if (parser.tag() == Api::BlockChain::Height) {
                index = Blocks::DB::instance()->headerChain()[parser.intData()];
                requestOk = true;
            } else if (parser.tag() == Api::BlockChain::ReuseAddressFilter) {
                m_filterOnKeys = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::SetFilterAddress
                       ||  parser.tag() == Api::BlockChain::AddFilterAddress) {
                if (parser.dataLength() != 20)
                    throw Api::ParserException("GetBlock: filter-address should be a 20bytes bytearray");
                if (parser.tag() == Api::BlockChain::SetFilterAddress)
                    session->keys.clear();
                session->keys.insert(CKeyID(uint160(parser.unsignedBytesData())));
                m_filterOnKeys = true;
            }
        }

        if (index == nullptr)
            throw Api::ParserException(requestOk ? "Requested block not found" :
                                                   "Request needs to contain either height or blockhash");
        m_height = index->nHeight;
        try {
            m_block = Blocks::DB::instance()->loadBlock(index->GetBlockPos());
            assert(m_block.isFullBlock());
        } catch (...) {
            throw Api::ParserException("Blockdata not present on this Hub");
        }

        if (!m_filterOnKeys)
            return m_block.size() + 45;

        Tx::Iterator iter(m_block);
        auto type = iter.next();
        bool oneEnd = false, txMatched = false;
        int size = 0;
        while (true) {
            if (type == Tx::End) {
                if (txMatched) {
                    Tx prevTx = iter.prevTx();
                    size += prevTx.size();
                    m_transactions.push_back(std::make_pair(prevTx.offsetInBlock(m_block), prevTx.size()));
                    txMatched = false;
                }
                if (oneEnd) // then the second end means end of block
                    break;
                oneEnd = true;
            }
            else if (!txMatched && type == Tx::OutputScript) {
                CScript scriptPubKey(iter.byteData());

                std::vector<std::vector<unsigned char> > vSolutions;
                txnouttype whichType;
                bool recognizedTx = Solver(scriptPubKey, whichType, vSolutions);
                if (recognizedTx && (whichType == TX_PUBKEY || whichType == TX_PUBKEYHASH)) {
                    CKeyID keyID;
                    if (whichType == TX_PUBKEYHASH)
                        keyID = CKeyID(uint160(vSolutions[0]));
                    else if (whichType == TX_PUBKEY)
                        keyID = CPubKey(vSolutions[0]).GetID();
                    if (session->keys.find(keyID) != session->keys.end())
                        txMatched = true;
                }
            }
            type = iter.next();
        }
        return size + 45;
    }

    void buildReply(const Message&, Streaming::MessageBuilder &builder) {
        assert(m_height >= 0);
        builder.add(Api::BlockChain::Height, m_height);
        builder.add(Api::BlockChain::BlockHash, m_block.createHash());

        if (m_filterOnKeys) {
            for (auto posAndSize : m_transactions) {
                builder.add(Api::BlockChain::GenericByteData,
                    m_block.data().mid(posAndSize.first, posAndSize.second));
            }
        }
        else if (m_block.isFullBlock())
            builder.add(Api::BlockChain::GenericByteData, m_block.data());
    }

    FastBlock m_block;
    std::vector<std::pair<int, int>> m_transactions; // list of offset-in-block and length of tx to include
    bool m_filterOnKeys = false;
    int m_height = -1;
};
class GetBlockCount : public Api::DirectParser
{
public:
    GetBlockCount() : DirectParser(Api::BlockChain::GetBlockCountReply, 20) {}

    void buildReply(const Message&, Streaming::MessageBuilder &builder) {
        builder.add(Api::BlockChain::Height, chainActive.Height());
    }
};

// raw transactions

class GetRawTransaction : public Api::RpcParser
{
public:
    GetRawTransaction() : RpcParser("getrawtransaction", Api::RawTransactions::GetRawTransactionReply) {}

    virtual void createRequest(const Message &message, UniValue &output) {
        std::string txid;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::RawTransactions::TransactionId
                    || parser.tag() == Api::RawTransactions::GenericByteData)
                boost::algorithm::hex(parser.bytesData(), back_inserter(txid));
        }
        output.push_back(std::make_pair("parameter 1", UniValue(UniValue::VSTR, txid)));
    }

    virtual int calculateMessageSize(const UniValue &result) const {
        return result.get_str().size() / 2 + 20;
    }
};

class SendRawTransaction : public Api::RpcParser
{
public:
    SendRawTransaction() : RpcParser("sendrawtransaction", Api::RawTransactions::SendRawTransactionReply, 34) {}

    virtual void createRequest(const Message &message, UniValue &output) {
        std::string tx;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::RawTransactions::RawTransaction
                    || parser.tag() == Api::RawTransactions::GenericByteData)
                boost::algorithm::hex(parser.bytesData(), back_inserter(tx));
        }
        output.push_back(std::make_pair("", UniValue(UniValue::VSTR, tx)));
    }
};

struct PrevTransaction {
    PrevTransaction() : vout(-1), amount(-1) {}
    std::string txid, scriptPubKey;
    int vout;
    int64_t amount;
    bool isValid() const {
        return vout >= 0 && !txid.empty() && !scriptPubKey.empty();
    }
};

class SignRawTransaction : public Api::RpcParser
{
public:
    SignRawTransaction() : Api::RpcParser("signrawtransaction", Api::RawTransactions::SignRawTransactionReply) {}

    virtual void createRequest(const Message &message, UniValue &output) {
        output = UniValue(UniValue::VARR);
        std::list<std::string> privateKeys;
        std::list<PrevTransaction> prevTxs;
        PrevTransaction prevTx;
        int sigHashType = -1;
        std::string rawTx;

        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            std::string string;
            switch (parser.tag()) {
            case Api::RawTransactions::PrivateKey:
                privateKeys.push_back(parser.stringData());
                break;
            case Api::RawTransactions::Separator:
                if (prevTx.isValid())
                    prevTxs.push_back(prevTx);
                prevTx = PrevTransaction();
                break;
            case Api::RawTransactions::SigHashType:
                sigHashType = parser.intData();
                break;
            case Api::RawTransactions::TransactionId:
                boost::algorithm::hex(parser.bytesData(), back_inserter(string));
                prevTx.txid = string;
                break;
            case Api::RawTransactions::OutputIndex:
                prevTx.vout = parser.intData();
                break;
            case Api::RawTransactions::ScriptPubKey:
                boost::algorithm::hex(parser.bytesData(), back_inserter(string));
                prevTx.scriptPubKey = string;
                break;
            case Api::RawTransactions::OutputAmount:
                prevTx.amount = parser.longData();
                break;
            case Api::RawTransactions::GenericByteData:
            case Api::RawTransactions::RawTransaction:
                boost::algorithm::hex(parser.bytesData(), back_inserter(rawTx));
                break;
            }
        }
        if (prevTx.isValid())
            prevTxs.push_back(prevTx);

        output.push_back(UniValue(UniValue::VSTR, rawTx));

        // send previous tx
        UniValue list1(UniValue::VARR);
        for (const auto &tx : prevTxs) {
            UniValue item(UniValue::VOBJ);
            item.push_back(std::make_pair("txid", UniValue(UniValue::VSTR, tx.txid)));
            item.push_back(std::make_pair("scriptPubKey", UniValue(UniValue::VSTR, tx.scriptPubKey)));
            item.push_back(std::make_pair("vout", UniValue(tx.vout)));
            if (tx.amount != -1)
                item.push_back(std::make_pair("amount", UniValue(ValueFromAmount(tx.amount))));
            list1.push_back(item);
        }
        output.push_back(list1);

        // send private keys.
        UniValue list2(UniValue::VNULL);
        if (!privateKeys.empty()) {
            list2 = UniValue(UniValue::VARR);
            for (const std::string &str : privateKeys) {
                list2.push_back(UniValue(UniValue::VSTR, str));
            }
        }
        output.push_back(list2);

        if (sigHashType >= 0)
            output.push_back(UniValue(sigHashType));
    }

    virtual int calculateMessageSize(const UniValue &result) const {
        const UniValue &hex = find_value(result, "hex");
        const UniValue &errors = find_value(result, "errors");
        return errors.size() * 300 + hex.get_str().size() / 2 + 10;
    }

    virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        const UniValue &hex = find_value(result, "hex");
        std::vector<char> bytearray;
        boost::algorithm::unhex(hex.get_str(), back_inserter(bytearray));
        builder.add(Api::RawTransactions::RawTransaction, bytearray);
        bytearray.clear();
        const UniValue &complete = find_value(result, "complete");
        builder.add(Api::RawTransactions::Completed, complete.getBool());
        const UniValue &errors = find_value(result, "errors");
        if (!errors.isNull()) {
            bool first = true;
            for (const UniValue &error : errors.getValues()) {
                if (first) first = false;
                else builder.add(Api::Wallet::Separator, true);
                const UniValue &txid = find_value(error, "txid");
                boost::algorithm::unhex(txid.get_str(), back_inserter(bytearray));
                builder.add(Api::RawTransactions::TransactionId, bytearray);
                bytearray.clear();
                const UniValue &vout = find_value(error, "vout");
                builder.add(Api::RawTransactions::OutputIndex, vout.get_int());
                const UniValue &scriptSig = find_value(error, "scriptSig");
                boost::algorithm::unhex(scriptSig.get_str(), back_inserter(bytearray));
                builder.add(Api::RawTransactions::ScriptSig, bytearray);
                bytearray.clear();
                const UniValue &sequence = find_value(error, "sequence");
                builder.add(Api::RawTransactions::Sequence, (uint64_t) sequence.get_int64());
                const UniValue &errorText = find_value(error, "error");
                builder.add(Api::RawTransactions::ErrorMessage, errorText.get_str());
            }
        }
    }
};

#ifdef ENABLE_WALLET
// wallet

class ListUnspent : public Api::RpcParser
{
public:
    ListUnspent() : RpcParser("listunspent", Api::Wallet::ListUnspentReply) {}

    virtual int calculateMessageSize(const UniValue &result) const {
        return result.size() * 300;
    }

    virtual void createRequest(const Message &message, UniValue &output) {
        int minConf = -1;
        int maxConf = -1;
        std::list<std::vector<char>> addresses;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Wallet::TransactionId
                    || parser.tag() == Api::Wallet::GenericByteData) {
                addresses.push_back(parser.bytesData());
            }
            else if (parser.tag() == Api::Wallet::MinimalConfirmations)
                minConf = boost::get<int>(parser.data());
            else if (parser.tag() == Api::Wallet::MaximumConfirmations)
                maxConf = boost::get<int>(parser.data());
        }

        if (!addresses.empty()) { // ensure we have useful min/max conf
            minConf = std::max(minConf, 0);
            if (maxConf == -1)
                maxConf = 1E6;
        }

        if (minConf != -1)
            output.push_back(std::make_pair("parameter 1", UniValue(minConf)));
        if (maxConf != -1)
            output.push_back(std::make_pair("parameter 2", UniValue(maxConf)));

        UniValue list(UniValue::VOBJ);
        for (const std::vector<char> &address : addresses) {
            std::string addressString;
            boost::algorithm::hex(address, back_inserter(addressString));
            list.push_back(UniValue(UniValue::VSTR, addressString));
        }
        if (list.size() > 0)
            output.push_back(std::make_pair("addresses", list));
    }

    virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        bool first = true;
        for (const UniValue &item : result.getValues()) {
            if (first) first = false;
            else builder.add(Api::Wallet::Separator, true);
            const UniValue &txid = find_value(item, "txid");
            std::vector<char> bytearray;
            boost::algorithm::unhex(txid.get_str(), back_inserter(bytearray));
            builder.add(Api::Wallet::TransactionId, bytearray);
            bytearray.clear();
            const UniValue &vout = find_value(item, "vout");
            builder.add(Api::Wallet::TXOutputIndex, vout.get_int());
            const UniValue &address = find_value(item, "address");
            builder.add(Api::Wallet::BitcoinAddress, address.get_str());
            const UniValue &scriptPubKey = find_value(item, "scriptPubKey");
            boost::algorithm::unhex(scriptPubKey.get_str(), back_inserter(bytearray));
            builder.add(Api::Wallet::ScriptPubKey, bytearray);
            bytearray.clear();
            const UniValue &amount = find_value(item, "amount");
            builder.add(Api::Wallet::Amount, (uint64_t) AmountFromValue(amount));
            const UniValue &confirmations = find_value(item, "confirmations");
            builder.add(Api::Wallet::ConfirmationCount, confirmations.get_int());
        }
    }
};

class GetNewAddress : public Api::RpcParser
{
public:
    GetNewAddress() : RpcParser("getnewaddress", Api::Wallet::GetNewAddressReply, 50) {}
    virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        builder.add(Api::Wallet::BitcoinAddress, result.get_str());
    }
};
#endif

// Util

class CreateAddress : public Api::DirectParser
{
public:
    CreateAddress() : DirectParser(Api::Util::CreateAddressReply, 150) {}

    virtual void buildReply(const Message &request, Streaming::MessageBuilder &builder) {
        CKey key;
        key.MakeNewKey();
        assert(key.IsCompressed());
        const CKeyID pkh = key.GetPubKey().GetID();
        builder.addByteArray(Api::Util::BitcoinAddress, pkh.begin(), pkh.size());
        builder.addByteArray(Api::Util::PrivateAddress, key.begin(), key.size());
    }
};

class ValidateAddress : public Api::RpcParser {
public:
    ValidateAddress() : RpcParser("validateaddress", Api::Util::ValidateAddressReply, 300) {}

    virtual void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        const UniValue &isValid = find_value(result, "isvalid");
        builder.add(Api::Util::IsValid, isValid.getBool());
        const UniValue &address = find_value(result, "address");
        builder.add(Api::Util::BitcoinAddress, address.get_str());
        const UniValue &scriptPubKey = find_value(result, "scriptPubKey");
        std::vector<char> bytearray;
        boost::algorithm::unhex(scriptPubKey.get_str(), back_inserter(bytearray));
        builder.add(Api::Util::ScriptPubKey, bytearray);
        bytearray.clear();
    }
    virtual void createRequest(const Message &message, UniValue &output) {
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::Util::BitcoinAddress) {
                output.push_back(std::make_pair("param 1", UniValue(UniValue::VSTR, parser.stringData())));
                return;
            }
        }
    }
};

class RegTestGenerateBlock : public Api::RpcParser {
public:
    RegTestGenerateBlock() : RpcParser("generate", Api::RegTest::GenerateBlockReply) {}

    void createRequest(const Message &message, UniValue &output) {
        Streaming::MessageParser parser(message.body());
        int amount = 1;
        std::vector<uint8_t> outAddress;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::RegTest::Amount)
                amount = parser.intData();
            else if (parser.tag() == Api::RegTest::BitcoinAddress)
                outAddress = parser.unsignedBytesData();
        }
        if (amount <= 0 || amount > 150)
            throw Api::ParserException("Invalid Amount argument");
        if (outAddress.size() != 20)
            throw Api::ParserException("Invalid BitcoinAddress (need 20 byte array)");

        std::string hex;
        boost::algorithm::hex(outAddress, back_inserter(hex));
        output.push_back(std::make_pair("item0", UniValue(amount)));
        logFatal() << "address: " << hex;
        output.push_back(std::make_pair("item1", UniValue(UniValue::VSTR, hex)));
        m_messageSize = amount * 35;
    }

    void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        assert(result.getType() == UniValue::VARR);
        for (int i = 0; i < result.size(); ++i) {
            assert(result[i].get_str().size() == 64);
            std::vector<char> hex;
            boost::algorithm::unhex(result[i].get_str(), back_inserter(hex));
            builder.add(Api::RegTest::BlockHash, hex);
        }
    }
};

}


Api::Parser *Api::createParser(const Message &message)
{
    switch (message.serviceId()) {
    case Api::BlockChainService:
        switch (message.messageId()) {
        case Api::BlockChain::GetBlockChainInfo:
            return new GetBlockChainInfo();
        case Api::BlockChain::GetBestBlockHash:
            return new GetBestBlockHash();
        case Api::BlockChain::GetBlock:
            return new GetBlock();
        case Api::BlockChain::GetBlockVerbose:
            return new GetBlockLegacy();
        case Api::BlockChain::GetBlockHeader:
            return new GetBlockHeader();
        case Api::BlockChain::GetBlockCount:
            return new GetBlockCount();
        }
        break;
    case Api::ControlService:
        switch (message.messageId()) {
        case Api::Control::Stop:
            return new RpcParser("stop", Api::Control::StopReply);
            break;
        }
        break;
    case Api::RawTransactionService:
        switch (message.messageId()) {
        case Api::RawTransactions::GetRawTransaction:
            return new GetRawTransaction();
        case Api::RawTransactions::SendRawTransaction:
            return new SendRawTransaction();
        case Api::RawTransactions::SignRawTransaction:
            return new SignRawTransaction();
        }
        break;
#ifdef ENABLE_WALLET
    case Api::WalletService:
        switch (message.messageId()) {
        case Api::Wallet::ListUnspent:
            return new ListUnspent();
        case Api::Wallet::GetNewAddress:
            return new GetNewAddress();
        }
        break;
#endif
    case Api::UtilService:
        switch (message.messageId()) {
        case Api::Util::CreateAddress:
            return new CreateAddress();
        case Api::Util::ValidateAddress:
            return new ValidateAddress();
        }
        break;
    case Api::RegTestService:
        switch (message.messageId()) {
        case Api::RegTest::GenerateBlock:
            return new RegTestGenerateBlock();
        }
        break;
    }
    throw std::runtime_error("Unsupported command");
}


Api::Parser::Parser(ParserType type, int answerMessageId, int messageSize)
    : m_messageSize(messageSize),
      m_replyMessageId(answerMessageId),
      m_type(type)
{
}

void Api::Parser::setSessionData(Api::SessionData **value)
{
    data = value;
}

Api::RpcParser::RpcParser(const std::string &method, int replyMessageId, int messageSize)
    : Parser(WrapsRPCCall, replyMessageId, messageSize),
      m_method(method)
{
}

void Api::RpcParser::buildReply(Streaming::MessageBuilder &builder, const UniValue &result)
{
    std::vector<char> answer;
    boost::algorithm::unhex(result.get_str(), back_inserter(answer));
    builder.add(1, answer);
}

void Api::RpcParser::createRequest(const Message &, UniValue &)
{
}

int Api::RpcParser::calculateMessageSize(const UniValue &result) const
{
    return result.get_str().size() + 20;
}

Api::DirectParser::DirectParser(int replyMessageId, int messageSize)
    : Parser(IncludesHandler, replyMessageId, messageSize)
{
}
