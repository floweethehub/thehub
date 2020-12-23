/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2020 Tom Zander <tomz@freedommail.ch>
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
#ifndef APIPROTOCOL_H
#define APIPROTOCOL_H

namespace Api {
enum ServiceIds {
    APIService,
    BlockChainService,
    LiveTransactionService,
    UtilService,
    RegTestService,

    // MiningService,

    /* service IDs under 16 are reserved to be handled by the APIServer
       it will generate errors for any in this region it doesn't understand.
    */

    /// Hub control service.
    HubControlService = 16,
        // hub stopping, networking settings. Logging.

    /// Connections can subscribe to bitcoin-address usage notifications
    AddressMonitorService = 17,
    BlockNotificationService,

    /// The service ID reserved for the Flowee Indexer. Runs as a stand-alone cloud-server.
    IndexerService,
    // waits for specific TxIds
    TransactionMonitorService,

    // <--  new services go here -->


    LegacyP2P = 0x1000
};

enum ApiTags {
    // various common tags
    Separator = 0,
    GenericByteData,
    BitcoinP2PKHAddress, ///< A bytearray: the raw 160 bit hash.
    PrivateKey,
    TxId,
    BlockHash,
    Amount,
    BlockHeight,
    OffsetInBlock,
    BitcoinScriptHashed, ///< a (single) sha256 hash of a script (typically output) used as a unique ID for the payment 'address'.

    ASyncRequest = 10,   ///< Use only in headers.
    RequestId,           ///< Use only in headers.
    UserTag1,
    UserTag2,
    UserTag3
};


//  API (owned by APIServer)
namespace Meta {

enum MessageIds {
    Version,
    VersionReply,
    CommandFailed,
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,

    FailedReason = 20,
    FailedCommandServiceId,
    FailedCommandId,
};
}

// Utils service (owned by APIServer)
namespace Util {

enum MessageIds {
    CreateAddress,
    CreateAddressReply,
//  createmultisig nrequired ["key",...]
//  estimatefee nblocks
//  estimatepriority nblocks
//  estimatesmartfee nblocks
//  estimatesmartpriority nblocks
    ValidateAddress,
    ValidateAddressReply,
//  verifymessage "bitcoinaddress" "signature" "message"
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    BitcoinP2PKHAddress = Api::BitcoinP2PKHAddress,
    PrivateKey = Api::PrivateKey,

    ScriptPubKey = 20,
    IsValid,
};
}

namespace Mining { // Mining Service

enum MessageIds {
    // CreateNewBlock,
    // CreateNewBlockReply

//   getblocktemplate ( "jsonrequestobject" )
//   getmininginfo
//   getnetworkhashps ( blocks height )
//   prioritisetransaction <txid> <priority delta> <fee delta>
//   setcoinbase pubkey
//   submitblock "hexdata" ( "jsonparametersobject" )
//
};

enum Tags {
    Separator = 0,
    GenericByteData,
};
}

namespace LiveTransactions {

enum MessageIds {
    GetTransaction,
    GetTransactionReply,
    SendTransaction,
    SendTransactionReply,
    IsUnspent,
    IsUnspentReply,
    GetUnspentOutput,
    GetUnspentOutputReply,

    SearchMempool,
    SearchMempoolReply,

    GetMempoolInfo,
    GetMempoolInfoReply,
};
enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    Tx_Out_Address = Api::BitcoinP2PKHAddress, ///< a ripe160 based P2PKH address.
    TxId = Api::TxId,   // bytearray
    Amount = Api::Amount,  // value in satoshis
    BlockHeight = Api::BlockHeight,
    OffsetInBlock = Api::OffsetInBlock,
    BitcoinScriptHashed = Api::BitcoinScriptHashed,

    Tx_IN_TxId = 20,
    OutIndex,
    Tx_InputScript,
    OutputScript,
    Tx_Out_Index,

    Transaction,
    UnspentState, // bool, when true the utxo is unspent
    DSProofId,
    FirstSeenTime, // long-int with seconds since epoch (UTC)
    MatchingOutIndex, // int. Output index that matches the requested search.

    // for individual transaction you can select how they should be returned.
    Include_TxId = 43,      ///< bool.
    FullTransactionData = 45, ///< bool. When true, return full tx data even when interpeted data is sent.
    Include_Inputs,         ///< bool. Return all inputs for selected tx.
    Include_OutputAmounts,  ///< bool. Return the amounts field for selected transactions.
    Include_OutputScripts,  ///< bool. Return full output Scripts.
    Include_Outputs,        ///< bool. Return all parts of outputs, overriding the previous 2 options.
    Include_OutputAddresses,///< bool. If the output is a p2pkh, return the hash160 of the address paid to.
    Include_OutputScriptHash,///< bool. Include Tx_Out_ScriptHash
    FilterOutputIndex,  	///< integer of output. This filters to only return data for those.

    // GetMempoolInfo
    MempoolSize = 60, ///< long-int. Current tx count
    MempoolBytes,     ///< long-int. Sum of all tx sizes (bytes)
    MempoolUsage,     ///< long-int. Total memory usage for the mempool (bytes)
    MaxMempool,       ///< long-int. Maximum memory usage for the mempool (bytes)
    MempoolMinFee,    ///< double. Minimum fee for tx to be accepted (Satoshi-per-1000-bytes)
};
}

namespace BlockChain {
enum MessageIds {
    GetBlockChainInfo,
    GetBlockChainInfoReply,
    GetBestBlockHash,
    GetBestBlockHashReply,
    GetBlock,
    GetBlockReply,
    GetBlockVerbose,
    GetBlockVerboseReply,
    GetBlockHeader,
    GetBlockHeaderReply,
    GetBlockCount,
    GetBlockCountReply,
    GetTransaction,
    GetTransactionReply,
//   getchaintips
//   getdifficulty
//   gettxout "txid" n ( includemempool )
//   verifychain ( checklevel numblocks )
};

// BlockChain-tags
enum Tags {
    // GetBlockReply  / GetTransactionReply tags
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    TxId = Api::TxId,
    BlockHash = Api::BlockHash,
    Tx_Out_Amount = Api::Amount,
    BlockHeight = Api::BlockHeight,
    Tx_Out_Address = Api::BitcoinP2PKHAddress, ///< a ripe160 based P2PKH address.
    Tx_OffsetInBlock = Api::OffsetInBlock,
    Tx_Out_ScriptHash = Api::BitcoinScriptHashed, ///< A sha256 over the contents of the script-out.

    Tx_IN_TxId = 20,
    Tx_IN_OutIndex,
    Tx_InputScript,
    Tx_OutputScript,
    Tx_Out_Index,

    // GetBlock-Request-tags
    // GetBlock can filter a block to only return transactions that match a bitcoin-address filter
    // (list of addresses).
    ReuseAddressFilter = 40, ///< A getBlock call resuses a previously created address filter. bool
    SetFilterScriptHash,        ///< Followed with one bytearray script-hash. Clears entire filter and and sets one script-hash in filter.
    AddFilterScriptHash,        ///< Add one bytearray script-hash.

    // for individual transaction you can select how they should be returned.
    Include_TxId,           ///< bool.
    Include_OffsetInBlock,  ///< bool.
    FullTransactionData,     ///< bool. When true, return full tx data even when interpeted data is sent.
    Include_Inputs,         ///< bool. Return all inputs for selected tx.
    Include_OutputAmounts,  ///< bool. Return the amounts field for selected transactions.
    Include_OutputScripts,  ///< bool. Return full output Scripts.
    Include_Outputs,        ///< bool. Return all parts of outputs, overriding the previous 2 options.
    Include_OutputAddresses,///< bool. If the output is a p2pkh, return the hash160 of the address paid to.
    Include_OutputScriptHash,///< bool. Include Tx_Out_ScriptHash
    FilterOutputIndex,  	///< integer of output. This filters to only return data for those.

    Verbose = 60,   // bool
    Size,           // int
    Version,        // int
    Time,           // in seconds since epoch (int)
    Difficulty,     // double
    MedianTime,     // in seconds since epoch (int)
    ChainWork,      // a sha256
    Chain,          // string. "main", "testnet", "testnet4", "scalenet", "regtest"
    Blocks,         // number of blocks (int)
    Headers,        // number of headers (int)
    BestBlockHash,  // sha256
    VerificationProgress, // double

    // GetBlockVerbose-tags
    Confirmations,  // int
    MerkleRoot,     // sha256
    Nonce,          // int
    Bits,           // int
    PrevBlockHash,  // sha256
    NextBlockHash  // sha256

};

}

// Hub Control Service
namespace Hub {
enum MessageIds {
//   == Network ==
//   addnode "node" "add|remove|onetry"
//   clearbanned
//   disconnectnode "node"
//   getaddednodeinfo dns ( "node" )
//   getconnectioncount
//   getnettotals
//   getnetworkinfo
//   getpeerinfo
//   listbanned
//   setban "ip(/netmask)" "add|remove" (bantime) (absolute)
//   == blockchain ==
//   getmempoolinfo
//   getrawmempool ( verbose )
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,

};
}

namespace RegTest {

// RegTest Service
enum MessageIds {
    GenerateBlock,
    GenerateBlockReply
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    BitcoinP2PKHAddress = Api::BitcoinP2PKHAddress,
    BlockHash = Api::BlockHash,
    Amount = Api::Amount
};
}

namespace AddressMonitor {
enum MessageIds {
    /// Client sents a message to the hub to subscribe to a BitcoinAddress
    Subscribe,
    /// reply with Result and maybe ErrorMessage
    SubscribeReply,
    /// Client sents a message to the hub to unsubscribe a BitcoinAddress
    Unsubscribe,
    /**
     * When the Hub finds a match, it sends this message to the client.
     * We send BitcoinAddress, TransactionId, Amount and ConfirmationCount.
     */
    TransactionFound,
    /**
     * Notify of a douple spend on one of the subscribed addresses.
     */
    DoubleSpendFound
};

enum Tags {
    /// A bytearray for a full sha256 txid
    TxId = Api::TxId,
    /// An unsigned 64 bit number for the amount of satshi you received
    Amount = Api::Amount,
    BlockHeight = Api::BlockHeight,
    /// If a transaction is added in a block, this is the offset-in-block
    OffsetInBlock = Api::OffsetInBlock,
    BitcoinScriptHashed = Api::BitcoinScriptHashed,

    /// A string giving a human (or, at least, developer) readable error message
    ErrorMessage = 20,

    /// positive-number. the amount of addresses found in the subscribe/unsubscribe message
    Result,
    /// A bytearray with a double-spend-proof object
    DoubleSpendProofData,
    /// A bytearray with a Transaction
    TransactionData
};
}

namespace TransactionMonitor {
enum MessageIds {
    /// Client sents a message to the hub to subscribe to a BitcoinAddress
    Subscribe,
    /// reply with Result and maybe ErrorMessage
    SubscribeReply,
    /// Client sents a message to the hub to unsubscribe a BitcoinAddress
    Unsubscribe,
    /**
     * When the Hub finds a match, it sends this message to the client.
     * We send only the txid and the OffsetInBlock/BlockHeight if mined.
     */
    TransactionFound,
    /**
     * Notify of a douple spend on one of the subscribed transactions.
     */
    DoubleSpendFound
};

enum Tags {
    GenericByteData = Api::GenericByteData,
    /// A bytearray for a full sha256 txid
    TxId = Api::TxId,
    BlockHeight = Api::BlockHeight,
    /// If a transaction is added in a block, this is the offset-in-block
    OffsetInBlock = Api::OffsetInBlock,

    /// A string giving a human (or, at least, developer) readable error message
    ErrorMessage = 20,

    /// positive-number. the amount of TxIds found in the subscribe/unsubscribe message
    Result,
    /// A bytearray with a double-spend-proof object
    DoubleSpendProofData,
    /// A bytearray with a Transaction
    TransactionData
};
}


namespace BlockNotification {
enum MessageIds {
    Subscribe,
    Unsubscribe = 2,
    NewBlockOnChain = 4,
};

enum Tags {
    BlockHash = Api::BlockHash,
    BlockHeight = Api::BlockHeight
};
}

namespace Indexer {
enum MessageIds {
    GetAvailableIndexers,
    GetAvailableIndexersReply,
    FindTransaction,
    FindTransactionReply,
    FindAddress,
    FindAddressReply,
    FindSpentOutput,
    FindSpentOutputReply,
    GetIndexerLastBlock,
    GetIndexerLastBlockReply,
    Version,
    VersionReply
};

enum Tags {
    GenericByteData = Api::GenericByteData,
    Separator = Api::Separator,
    BitcoinP2PKHAddress = Api::BitcoinP2PKHAddress,
    TxId = Api::TxId,
    BlockHeight = Api::BlockHeight,
    OffsetInBlock = Api::OffsetInBlock,
    BitcoinScriptHashed = Api::BitcoinScriptHashed,

    OutIndex = 20,
    AddressIndexer,
    TxIdIndexer,
    SpentOutputIndexer
};
}

namespace P2P {
enum MessageIds {
    Version = 1,
    VersionAck,
    GetAddr,
    Addresses, // addr

    PreferHeaders, // sendheaders
    GetBlocks,
    GetHeaders,
    Headers,

    Inventory, // inv
    GetData,
    Data_MerkleBlock,
    Data_Transaction,
    Data_Block,
    Data_NotFound,
    Data_DSProof,

    RejectData,

    GetXThin,
    Data_ThinBlock,
    Data_XThinBlock,
    Get_XBlockTx,
    Data_XBlockTx,

    GetMempool,

    Ping,
    Pong,

    FilterLoad,
    FilterAdd,
    FilterClear
};
}
}
#endif
