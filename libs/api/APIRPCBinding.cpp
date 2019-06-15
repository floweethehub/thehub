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

#include <APIProtocol.h>
#include <streaming/MessageBuilder.h>
#include <BlocksDB.h>
#include <main.h>
#include <rpcserver.h>
#include <encodings_legacy.h>
#include <univalue.h>

#include <boost/algorithm/hex.hpp>

#include <streaming/MessageParser.h>

#include <UnspentOutputData.h>
#include <list>

#include <primitives/FastBlock.h>
#include <primitives/FastTransaction.h>
#include <utxo/UnspentOutputDatabase.h>

namespace {

void addHash256ToBuilder(Streaming::MessageBuilder &builder, uint32_t tag, const UniValue &univalue) {
    assert(univalue.isStr());
    assert(univalue.getValStr().size() == 64);
    uint8_t buf[32];
    const char *input = univalue.getValStr().c_str();
    for (int i = 0; i < 32; ++i) {
        signed char c = HexDigit(*input++);
        assert(c != -1);
        unsigned char n = c << 4;
        c = HexDigit(*input++);
        assert(c != -1);
        n |= c;
        buf[31 - i] = n;
    }
    builder.addByteArray(tag, buf, 32);
}

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
        addHash256ToBuilder(builder, Api::BlockChain::BestBlockHash, find_value(result, "bestblockhash"));
        const UniValue &difficulty = find_value(result, "difficulty");
        builder.add(Api::BlockChain::Difficulty, difficulty.get_real());
        const UniValue &time = find_value(result, "mediantime");
        builder.add(Api::BlockChain::MedianTime, (uint64_t) time.get_int64());
        const UniValue &progress = find_value(result, "verificationprogress");
        builder.add(Api::BlockChain::VerificationProgress, progress.get_real());
        addHash256ToBuilder(builder, Api::BlockChain::ChainWork, find_value(result, "chainwork"));
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
                    || parser.tag() == Api::LiveTransactions::GenericByteData) {
                blockId = parser.uint256Data().ToString();
            } else if (parser.tag() == Api::BlockChain::Verbose) {
                m_verbose = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::BlockHeight) {
                auto index = chainActive[parser.intData()];
                if (index)
                    blockId = index->GetBlockHash().ToString();
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

        addHash256ToBuilder(builder, Api::BlockChain::BlockHash, find_value(result, "hash"));
        const UniValue &confirmations = find_value(result, "confirmations");
        builder.add(Api::BlockChain::Confirmations, confirmations.get_int());
        const UniValue &size = find_value(result, "size");
        builder.add(Api::BlockChain::Size, size.get_int());
        const UniValue &height = find_value(result, "height");
        builder.add(Api::BlockChain::BlockHeight, height.get_int());
        const UniValue &version = find_value(result, "version");
        builder.add(Api::BlockChain::Version, version.get_int());
        addHash256ToBuilder(builder, Api::BlockChain::MerkleRoot, find_value(result, "merkleroot"));
        const UniValue &tx = find_value(result, "tx");
        bool first = true;
        for (const UniValue &transaction: tx.getValues()) {
            if (first) first = false;
            else builder.add(Api::Separator, true);
            addHash256ToBuilder(builder, Api::BlockChain::TxId,  transaction);
        }
        const UniValue &time = find_value(result, "time");
        builder.add(Api::BlockChain::Time, (uint64_t) time.get_int64());
        const UniValue &mediantime = find_value(result, "mediantime");
        builder.add(Api::BlockChain::MedianTime, (uint64_t) mediantime.get_int64());
        const UniValue &nonce = find_value(result, "nonce");
        builder.add(Api::BlockChain::Nonce, (uint64_t) nonce.get_int64());
        // 'bits' is an 4 bytes hex-encoded string.
        const std::string &input = find_value(result, "bits").getValStr();
        assert(input.size() == 8);
        uint32_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            signed char c = HexDigit(input.at(i));
            assert(c != -1);
            bits = (bits << 4) + c;
        }
        builder.add(Api::BlockChain::Bits, (uint64_t) bits);
        const UniValue &difficulty = find_value(result, "difficulty");
        builder.add(Api::BlockChain::Difficulty, difficulty.get_real());
        addHash256ToBuilder(builder, Api::BlockChain::ChainWork, find_value(result, "chainwork"));
        addHash256ToBuilder(builder, Api::BlockChain::PrevBlockHash, find_value(result, "previousblockhash"));
        const UniValue &nextblock = find_value(result, "nextblockhash");
        if (nextblock.isStr())
            addHash256ToBuilder(builder, Api::BlockChain::NextBlockHash, nextblock);
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
        builder.add(Api::BlockChain::Confirmations, chainActive.Contains(index) ? chainActive.Height() - index->nHeight + 1: -1);
        builder.add(Api::BlockChain::BlockHeight, index->nHeight);
        builder.add(Api::BlockChain::Version, index->nVersion);
        builder.add(Api::BlockChain::MerkleRoot, index->hashMerkleRoot);
        builder.add(Api::BlockChain::Time, (uint64_t) index->nTime);
        builder.add(Api::BlockChain::MedianTime, (uint64_t) index->GetMedianTimePast());
        builder.add(Api::BlockChain::Nonce, (uint64_t) index->nNonce);
        builder.add(Api::BlockChain::Bits, (uint64_t) index->nBits);
        builder.add(Api::BlockChain::Difficulty, GetDifficulty(index));

        if (index->pprev)
            builder.add(Api::BlockChain::PrevBlockHash, index->pprev->GetBlockHash());
        auto next = chainActive.Next(index);
        if (next)
            builder.add(Api::BlockChain::NextBlockHash, next->GetBlockHash());
    }

    void buildReply(const Message &request, Streaming::MessageBuilder &builder) {
        Streaming::MessageParser parser(request.body());

        bool first = true;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHash) {
                if (!first) builder.add(Api::Separator, true);
                uint256 hash = parser.uint256Data();
                CBlockIndex *bi = Blocks::Index::get(hash);
                if (bi)
                    return buildReply(builder, bi);
                first = false;
            }
            else if (parser.tag() == Api::BlockChain::BlockHeight) {
                if (!first) builder.add(Api::Separator, true);
                int height = parser.intData();
                auto index = chainActive[height];
                if (index)
                    return buildReply(builder, index);
                first = false;
            }
        }
    }
};

struct TransactionSerializationOptions
{
    void serialize(Streaming::MessageBuilder &builder, Tx::Iterator &iter)
    {
        int outIndex = 0;
        auto type = iter.next();
        while (type != Tx::End) {
            if (returnInputs && type == Tx::PrevTxHash) {
                builder.add(Api::BlockChain::Tx_IN_TxId, iter.uint256Data());
            }
            else if (returnInputs && type == Tx::TxInScript) {
                builder.add(Api::BlockChain::Tx_InputScript, iter.byteData());
            }
            else if (returnInputs && type == Tx::PrevTxIndex) {
                builder.add(Api::BlockChain::Tx_IN_OutIndex, iter.intData());
            }
            else if (type == Tx::OutputValue) {
                if ((returnOutputs || returnOutputAmounts) &&
                            (filterOutputs.empty() || filterOutputs.find(outIndex) != filterOutputs.end())) {
                    builder.add(Api::BlockChain::Tx_Out_Index, outIndex);
                    builder.add(Api::BlockChain::Tx_Out_Amount, iter.longData());
                }
            }
            else if ((returnOutputs || returnOutputScripts || returnOutputAddresses) && type == Tx::OutputScript) {
                if (filterOutputs.empty() || filterOutputs.find(outIndex) != filterOutputs.end()) {
                    if (!returnOutputs && !returnOutputAmounts) // if not added before in OutputValue
                        builder.add(Api::BlockChain::Tx_Out_Index, outIndex);
                    if (returnOutputs || returnOutputScripts)
                        builder.add(Api::BlockChain::Tx_OutputScript, iter.byteData());
                    if (returnOutputAddresses) {
                        CScript scriptPubKey(iter.byteData());

                        std::vector<std::vector<unsigned char> > vSolutions;
                        Script::TxnOutType whichType;
                        bool recognizedTx = Script::solver(scriptPubKey, whichType, vSolutions);
                        if (recognizedTx && (whichType == Script::TX_PUBKEY || whichType == Script::TX_PUBKEYHASH)) {
                            if (whichType == Script::TX_PUBKEYHASH) {
                                assert(vSolutions[0].size() == 20);
                                builder.addByteArray(Api::BlockChain::Tx_Out_Address, vSolutions[0].data(), 20);
                            } else if (whichType == Script::TX_PUBKEY) {
                                CPubKey pubKey(vSolutions[0]);
                                assert (pubKey.IsValid());
                                CKeyID address = pubKey.GetID();
                                builder.addByteArray(Api::BlockChain::Tx_Out_Address, address.begin(), 20);
                            }
                        }
                    }
                }
                outIndex++;
            }

            type = iter.next();
        }
    }

    // return true only if serialize would actually export anything
    bool shouldRun() const {
        const bool partialTxData = returnInputs || returnOutputs || returnOutputAmounts
                || returnOutputScripts || returnOutputAddresses;
        return partialTxData;
    }
    bool returnInputs = false;
    bool returnOutputs = false;
    bool returnOutputAmounts = false;
    bool returnOutputScripts = false;
    bool returnOutputAddresses = false;
    std::set<int> filterOutputs;
};

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

        bool filterOnKeys = false;
        bool requestOk = false;
        bool fullTxData = false;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHash
                    || parser.tag() == Api::LiveTransactions::GenericByteData) {
                if (parser.dataLength() != 32)
                    throw Api::ParserException("BlockHash should be a 32 byte-bytearray");
                index = Blocks::Index::get(uint256(&parser.bytesData()[0]));
                requestOk = true;
            } else if (parser.tag() == Api::BlockChain::BlockHeight) {
                index = chainActive[parser.intData()];
                requestOk = true;
            } else if (parser.tag() == Api::BlockChain::ReuseAddressFilter) {
                filterOnKeys = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::SetFilterAddress
                       ||  parser.tag() == Api::BlockChain::AddFilterAddress) {
                if (parser.dataLength() != 20)
                    throw Api::ParserException("GetBlock: filter-address should be a 20bytes bytearray");
                if (parser.tag() == Api::BlockChain::SetFilterAddress)
                    session->keys.clear();
                session->keys.insert(CKeyID(uint160(parser.unsignedBytesData())));
                filterOnKeys = true;
            } else if (parser.tag() == Api::BlockChain::FullTransactionData) {
                fullTxData = parser.boolData();
                if (!fullTxData)
                    m_fullTxData = false;
            } else if (parser.tag() == Api::BlockChain::Include_TxId) {
                m_returnTxId = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OffsetInBlock) {
                m_returnOffsetInBlock = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_Inputs) {
                opt.returnInputs = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_Outputs) {
                opt.returnOutputs = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OutputAmounts) {
                opt.returnOutputAmounts = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OutputScripts) {
                opt.returnOutputScripts = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OutputAddresses) {
                opt.returnOutputAddresses = parser.boolData();
            }
        }
        if (fullTxData) // if explicitly asked.
            m_fullTxData = true;
        else if (m_returnTxId || opt.shouldRun()) // we imply false if they want a subset.
            m_fullTxData = false;

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

        Tx::Iterator iter(m_block);
        auto type = iter.next();
        bool oneEnd = false, txMatched = !filterOnKeys;
        int size = 0, matchedOutputs = 0, matchedInputsSize = 0;
        int txOutputCount = 0, txInputSize = 0, txOutputScriptSizes = 0;
        int matchedOutputScriptSizes = 0;
        while (true) {
            if (type == Tx::End) {
                if (oneEnd) // then the second end means end of block
                    break;
                if (txMatched) {
                    Tx prevTx = iter.prevTx();
                    size += prevTx.size();
                    matchedInputsSize += txInputSize;
                    matchedOutputs += txOutputCount;
                    matchedOutputScriptSizes += txOutputScriptSizes;
                    m_transactions.push_back(std::make_pair(prevTx.offsetInBlock(m_block), prevTx.size()));
                    txMatched = !filterOnKeys;
                }
                oneEnd = true;

                txInputSize = 0;
                txOutputCount = 0;
                txOutputScriptSizes = 0;
            } else {
                oneEnd = false;
            }

            if (opt.returnInputs && type == Tx::PrevTxHash) {
                txInputSize += 42; // prevhash: 32 + 3 +  prevIndex; 6 + 1
            }
            else if (opt.returnInputs && type == Tx::TxInScript) {
                txInputSize += iter.dataLength() + 3;
            }
            else if (type == Tx::OutputValue) {
                ++txOutputCount;
            }
            else if (type == Tx::OutputScript) {
                txOutputScriptSizes += iter.dataLength(); // the 6 for the outindex
                if (!txMatched) {
                    CScript scriptPubKey(iter.byteData());

                    std::vector<std::vector<unsigned char> > vSolutions;
                    Script::TxnOutType whichType;
                    bool recognizedTx = Script::solver(scriptPubKey, whichType, vSolutions);
                    if (recognizedTx && (whichType == Script::TX_PUBKEY || whichType == Script::TX_PUBKEYHASH)) {
                        CKeyID keyID;
                        if (whichType == Script::TX_PUBKEYHASH)
                            keyID = CKeyID(uint160(vSolutions[0]));
                        else if (whichType == Script::TX_PUBKEY)
                            keyID = CPubKey(vSolutions[0]).GetID();
                        if (session->keys.find(keyID) != session->keys.end())
                            txMatched = true;
                    }
                }
            }
            type = iter.next();
        }

        int bytesPerTx = 1;
        if (m_returnTxId) bytesPerTx += 35;
        if (m_returnOffsetInBlock) bytesPerTx += 6;
        if (m_fullTxData)  bytesPerTx += 5; // actual tx-data is in 'size'

        int bytesPerOutput = 5;
        if (opt.returnOutputAmounts || opt.returnOutputs) bytesPerOutput += 10;
        if (opt.returnOutputAddresses || opt.returnOutputs) bytesPerOutput += 23;

        int total = 45 + m_transactions.size() * bytesPerTx;
        if (m_fullTxData) total += size;
        if (opt.returnOutputs || opt.returnOutputScripts)
            total += matchedOutputScriptSizes;
        if (opt.returnInputs)
            total += matchedInputsSize;
        if (opt.returnOutputs || opt.returnOutputAddresses || opt.returnOutputAmounts)
            total += matchedOutputs * bytesPerOutput;

        logDebug(Log::ApiServer) << "GetBlock calculated to need at most" << total << "bytes";
        logDebug(Log::ApiServer) << "  tx" << bytesPerTx << "*" << m_transactions.size() << "(=num tx). Plus"
                                 << bytesPerOutput << "bytes per output (" << matchedOutputs << ")";
        logDebug(Log::ApiServer) << "  matched Script Output sizes:" << matchedOutputScriptSizes;
        return total;
    }

    void buildReply(const Message&, Streaming::MessageBuilder &builder) {
        assert(m_height >= 0);
        builder.add(Api::BlockChain::BlockHeight, m_height);
        builder.add(Api::BlockChain::BlockHash, m_block.createHash());

        for (auto posAndSize : m_transactions) {
            if (m_returnOffsetInBlock)
                builder.add(Api::BlockChain::Tx_OffsetInBlock, posAndSize.first);
            if (m_returnTxId) {
                Tx tx(m_block.data().mid(posAndSize.first, posAndSize.second));
                builder.add(Api::BlockChain::TxId, tx.createHash());
            }
            if (opt.shouldRun()) {
                Tx::Iterator iter(m_block, posAndSize.first);
                iter.next(Tx::PrevTxIndex); // skip version, prevTxId and prevTxIndex for coinbase
                assert(iter.tag() == Tx::PrevTxIndex);
                opt.serialize(builder, iter);
            }
            if (m_fullTxData)
                builder.add(Api::BlockChain::GenericByteData,
                    m_block.data().mid(posAndSize.first, posAndSize.second));

            builder.add(Api::BlockChain::Separator, true);
        }
    }

    FastBlock m_block;
    std::vector<std::pair<int, int>> m_transactions; // list of offset-in-block and length of tx to include
    bool m_fullTxData = true;
    bool m_returnTxId = false;
    bool m_returnOffsetInBlock = true;
    int m_height = -1;
    TransactionSerializationOptions opt;
};
class GetBlockCount : public Api::DirectParser
{
public:
    GetBlockCount() : DirectParser(Api::BlockChain::GetBlockCountReply, 20) {}

    void buildReply(const Message&, Streaming::MessageBuilder &builder) {
        builder.add(Api::BlockChain::BlockHeight, chainActive.Height());
    }
};

// Live transactions

class GetLiveTransaction : public Api::RpcParser
{
public:
    GetLiveTransaction() : RpcParser("getrawtransaction", Api::LiveTransactions::GetTransactionReply) {}

    virtual void createRequest(const Message &message, UniValue &output) {
        std::string txid;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::LiveTransactions::TxId
                    || parser.tag() == Api::LiveTransactions::GenericByteData)
                txid = parser.uint256Data().ToString();
        }
        output.push_back(std::make_pair("parameter 1", UniValue(UniValue::VSTR, txid)));
    }

    virtual int calculateMessageSize(const UniValue &result) const {
        return result.get_str().size() / 2 + 20;
    }
};

class SendLiveTransaction : public Api::RpcParser
{
public:
    SendLiveTransaction() : RpcParser("sendrawtransaction", Api::LiveTransactions::SendTransactionReply, 34) {}

    virtual void createRequest(const Message &message, UniValue &output) {
        std::string tx;
        Streaming::MessageParser parser(message.body());
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::LiveTransactions::Transaction
                    || parser.tag() == Api::LiveTransactions::GenericByteData)
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
        builder.addByteArray(Api::Util::PrivateKey, key.begin(), key.size());
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
        output.push_back(std::make_pair("item1", UniValue(UniValue::VSTR, hex)));
        m_messageSize = amount * 35;
    }

    void buildReply(Streaming::MessageBuilder &builder, const UniValue &result) {
        assert(result.getType() == UniValue::VARR);
        for (int i = 0; i < result.size(); ++i) {
            assert(result[i].get_str().size() == 64);
            addHash256ToBuilder(builder, Api::RegTest::BlockHash, result[i]);
        }
    }
};

class GetTransaction : public Api::DirectParser {
public:
    GetTransaction() : DirectParser(Api::BlockChain::GetTransactionReply) {}
    int calculateMessageSize(const Message &request) override {
        Streaming::MessageParser parser(request);
        CBlockIndex *index = nullptr;
        bool fullTxData = false;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::BlockChain::BlockHeight) {
                index = chainActive[parser.intData()];
                if (!index)
                    throw Api::ParserException("Unknown blockheight");
            } else if (parser.tag() == Api::BlockChain::BlockHash) {
                index = Blocks::Index::get(parser.uint256Data());
                if (!index)
                    throw Api::ParserException("Unknown block hash");
            } else if (parser.tag() == Api::BlockChain::Tx_OffsetInBlock) {
                m_offsetInBlock = parser.intData();
                if (m_offsetInBlock < 91)
                    throw Api::ParserException("OffsetInBlock should be a positive number");
            } else if (parser.tag() == Api::BlockChain::FullTransactionData) {
                fullTxData = parser.boolData();
                if (!fullTxData)
                    m_fullTxData = false;
            } else if (parser.tag() == Api::BlockChain::Include_TxId) {
                m_returnTxId = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OffsetInBlock) {
                m_returnOffsetInBlock = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_Inputs) {
                opt.returnInputs = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_Outputs) {
                opt.returnOutputs = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OutputAmounts) {
                opt.returnOutputAmounts = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OutputScripts) {
                opt.returnOutputScripts = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::Include_OutputAddresses) {
                opt.returnOutputAddresses = parser.boolData();
            } else if (parser.tag() == Api::BlockChain::FilterOutputIndex) {
                if (!parser.isInt() || parser.intData() < 0)
                    throw Api::ParserException("FilterOutputIndex should be a positive number");
                opt.filterOutputs.insert(parser.intData());
            }
        }
        if (fullTxData) // if explicitly asked.
            m_fullTxData = true;
        else if (m_returnTxId || opt.shouldRun()) // we imply false if they want a subset.
            m_fullTxData = false;

        if (!index || m_offsetInBlock < 91)
            throw Api::ParserException("Incomplete request.");
        if (index->nDataPos < 4 || (index->nStatus & BLOCK_HAVE_DATA) == 0)
            throw Api::ParserException("Block known but data not available");

        FastBlock block;
        try {
            block = Blocks::DB::instance()->loadBlock(index->GetBlockPos());
            assert(block.isFullBlock());
        } catch (...) {
            throw Api::ParserException("Blockdata not present on this Hub");
        }
        if (m_offsetInBlock > block.size())
            throw Api::ParserException("OffsetInBlock larger than block");
        Tx::Iterator iter(block, m_offsetInBlock);
        if (iter.next(Tx::End) == Tx::End)
            m_tx = iter.prevTx();

        int amount = m_fullTxData ? m_tx.size() + 10 : 0;
        if (m_returnTxId) amount += 40;
        if (m_returnOffsetInBlock) amount += 10;
        if (opt.shouldRun()) amount += m_tx.size();
        return amount;
    }
    void buildReply(const Message &, Streaming::MessageBuilder &builder) override {
        if (m_returnTxId)
            builder.add(Api::BlockChain::TxId, m_tx.createHash());
        if (m_returnOffsetInBlock)
            builder.add(Api::BlockChain::Tx_OffsetInBlock, m_offsetInBlock);
        if (opt.shouldRun()) {
            Tx::Iterator iter(m_tx);
            opt.serialize(builder, iter);
        }
        if (m_fullTxData)
            builder.add(Api::BlockChain::GenericByteData, m_tx.data());
    }

private:
    bool m_fullTxData = true;
    bool m_returnTxId = false;
    bool m_returnOffsetInBlock = false;
    int m_offsetInBlock = 0;
    Tx m_tx;
    TransactionSerializationOptions opt;
};

class UtxoFetcher: public Api::DirectParser
{
public:
    UtxoFetcher(int replyId)
      : DirectParser(replyId)
    {
    }

    int calculateMessageSize(const Message &request) override {
        int validCount = 0;
        Streaming::MessageParser parser(request.body());
        uint256 txid;
        int output = 0;
        while (parser.next() == Streaming::FoundTag) {
            if (parser.tag() == Api::LiveTransactions::TxId)
                txid = parser.uint256Data();
            else if (parser.tag() == Api::LiveTransactions::OutIndex) {
                if (!parser.isInt())
                    throw Api::ParserException("index wasn't number");
                output = parser.intData();
            }
            else if (parser.tag() == Api::Separator) {
                if (txid.IsNull())
                    throw Api::ParserException("Invalid or missing txid");
                auto out = g_utxo->find(txid, output);
                m_utxos.push_back(out);
                if (out.isValid())
                    validCount++;
                output = 0;
            }
        }

        if (txid.IsNull())
            throw Api::ParserException("Invalid or missing txid");
        auto out = g_utxo->find(txid, output);
        m_utxos.push_back(out);
        if (out.isValid())
            validCount++;

        const bool isVerbose = replyMessageId() == Api::LiveTransactions::GetUnspentOutputReply;
        int size = (m_utxos.size() - validCount) * 21;
        if (isVerbose) {
            // since I can't assume the max-size of the output-script, I need to actually fetch them here.
            for (auto unspent : m_utxos) {
                if (unspent.isValid()) {
                    size += 10; // for amount
                    UnspentOutputData uod(unspent);
                    size += uod.outputScript().size() + 3;
                }
            }
        }
        return size;
    }
    void buildReply(const Message&, Streaming::MessageBuilder &builder) override {
        const bool verbose = replyMessageId() == Api::LiveTransactions::GetUnspentOutputReply;
        bool first = true;
        for (auto unspent : m_utxos) {
            if (first)
                first = false;
            else
                builder.add(Api::LiveTransactions::Separator, true);
            const bool isValid = unspent.isValid();
            builder.add(Api::LiveTransactions::UnspentState, isValid);
            if (isValid) {
                builder.add(Api::LiveTransactions::BlockHeight, unspent.blockHeight());
                builder.add(Api::LiveTransactions::OffsetInBlock, unspent.offsetInBlock());
                if (verbose) {
                    UnspentOutputData uod(unspent);
                    builder.add(Api::LiveTransactions::Amount, (uint64_t) uod.outputValue());
                    builder.add(Api::LiveTransactions::OutputScript, uod.outputScript());
                }
            }
        }
    }
private:
    std::vector<UnspentOutput> m_utxos;
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
        case Api::BlockChain::GetTransaction:
            return new GetTransaction();
        }
        break;
    case Api::LiveTransactionService:
        switch (message.messageId()) {
        case Api::LiveTransactions::GetTransaction:
            return new GetLiveTransaction();
        case Api::LiveTransactions::SendTransaction:
            return new SendLiveTransaction();
        case Api::LiveTransactions::IsUnspent:
            return new UtxoFetcher(Api::LiveTransactions::IsUnspentReply);
        case Api::LiveTransactions::GetUnspentOutput:
            return new UtxoFetcher(Api::LiveTransactions::GetUnspentOutputReply);
        }
        break;
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
    assert(result.isStr());
    if (result.get_str().size() == 64) // assume sha256 which for some reason gets reversed in text
        addHash256ToBuilder(builder, 1, result);

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
