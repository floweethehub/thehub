/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2019 Tom Zander <tomz@freedommail.ch>
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
    FailuresService,
    BlockChainService,
    RawTransactionService,
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
};

enum ApiTags {
    // various common tags
    Separator = 0,
    GenericByteData,
    BitcoinAddress, ///< If a bytearray then we expect the raw 160 bit hash.
    PrivateKey,
    TxId,
    BlockHash,
    Amount,
    BlockHeight,
    RequestId = 11 ///< Use only in headers.
};


//  FailuresService (owned by APIServer)
namespace Failures {

enum MessageIds {
    CommandFailed,
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    FailedReason,
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
    BitcoinAddress = Api::BitcoinAddress,
    PrivateKey = Api::PrivateKey,
    ScriptPubKey,
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

namespace RawTransactions {

enum MessageIds {
    GetRawTransaction,
    GetRawTransactionReply,
    SendRawTransaction,
    SendRawTransactionReply,
    SignRawTransaction,
    SignRawTransactionReply
};
enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    BitcoinAddress = Api::BitcoinAddress, // Unused at this time.
    PrivateKey = Api::PrivateKey,     // string TODO stop bieng a string // TODO do we really use this??
    TxId = Api::TxId,   // bytearray

    RawTransaction,
    Completed,       // boolean
    OutputIndex,
    InputScript,      // bytearray.   This is also called the ScriptSig
    OutputScript,   // bytearray.   This is also called the ScriptPubKey
    Sequence,       // Number
    ErrorMessage,   // string
    SigHashType,    // Number
    OutputAmount    // value in satoshis
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
//   getblockhash index // maybe not needed as we add a height to GetBlock and GetBlockHeader?
//   getchaintips
//   getdifficulty
//   getmempoolinfo
//   getrawmempool ( verbose )
//   gettxout "txid" n ( includemempool )
//   verifychain ( checklevel numblocks )
};

// BlockChain-tags
enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
    Tx_Out_Address = Api::BitcoinAddress,
    PrivateKey,
    TxId = Api::TxId,
    BlockHash = Api::BlockHash,
    Tx_Out_Amount = Api::Amount,
    BlockHeight = Api::BlockHeight,

    // GetBlockReply tags
    Tx_OffsetInBlock,
    Tx_IN_TxId,
    Tx_IN_OutIndex,
    Tx_InputScript,
    Tx_OutputScript,
    Tx_Out_Index,

    // GetBlock-Request-tags
    // GetBlock can filter a block to only return transactions that match a bitcoin-address filter
    // (list of addresses).
    ReuseAddressFilter = 50, ///< A getBlock call resuses a previously created address filter. bool
    SetFilterAddress,        ///< Followed with one bytearray address. Clears and sets one address in filter.
    AddFilterAddress,        ///< Add one bytearray address.
    // for each individual transaction you can select how they should be returned.
    GetBlock_TxId,           ///< bool.
    GetBlock_OffsetInBlock,  ///< bool.
    FullTransactionData,     ///< bool. When true, return full tx data even when interpeted data is sent.
    GetBlock_Inputs,         ///< bool. Return all inputs for selected tx.
    GetBlock_OutputAmounts,  ///< bool. Return the amounts field for selected transactions.
    GetBlock_OutputScripts,  ///< bool. Return full output Scripts.
    GetBlock_Outputs,        ///< bool. Return all parts of outputs, overriding the previous 2 options.
    GetBlock_OutputAddresses,///< bool. If the output is a p2pkh, return the hash160 of the address paid to.

    Verbose,    // bool
    Size,       // int
    Version,    // int
    Time,       // in seconds since epoch
    Difficulty, // double
    MedianTime, // in seconds since epoch
    ChainWork,  // a sha256

    Chain,
    Blocks,
    Headers,
    BestBlockHash,
    VerificationProgress,
    Pruned,
    Bip9ForkId,
    Bip9ForkStatus,

    // GetBlockVerbose-tags
    Confirmations,
    MerkleRoot,
    Nonce,      //int
    Bits,       // integer
    PrevBlockHash,
    NextBlockHash
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
    BitcoinAddress = Api::BitcoinAddress, // Unused at this time.
    // skipped 2 numbers here.
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
    /// same as SubscribeReply
    UnsubscribeReply,
    /**
     * When the Hub finds a match, it sends this message to the client.
     * We send BitcoinAddress, TransactionId, Amount and ConfirmationCount.
     */
    TransactionFound,
    /**
     * When a block is mined that ends up rejecting a transaction to us,
     * the hub sends this message to the client.
     * Same content as TransactionFound
     */
    TransactionRejected
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,

    /// A string representing an address.
    BitcoinAddress = Api::BitcoinAddress, // Unused at this time.
    /// A bytearray for a full sha256 txid
    TxId = Api::TxId,
    /// An unsigned 64 bit number for the amount of satshi you received
    Amount = Api::Amount,
    /// True if it was mined in a block
    Mined,
    /// boolean. Success equals 'true'
    Result,
    /// A string giving a human (well, developer) readable error message
    ErrorMessage
};
}

namespace BlockNotification {
enum MessageIds {
    Subscribe,
    SubscribeReply,
    Unsubscribe,
    UnsubscribeReply,
    NewBlockOnChain,
};

enum Tags {
    Separator = Api::Separator,
    GenericByteData = Api::GenericByteData,
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
    FindAddressReply
};

enum Tags {
    Separator = Api::Separator,
    BitcoinAddress = Api::BitcoinAddress,
    TxId = Api::TxId,
    BlockHeight = Api::BlockHeight,
    OffsetInBlock,
    OutIndex,

    AddressIndexer,
    TxIdIndexer
};
}
}
#endif
