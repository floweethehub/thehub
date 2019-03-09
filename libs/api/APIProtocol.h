/*
 * This file is part of the Flowee project
 * Copyright (C) 2016-2017 Tom Zander <tomz@freedommail.ch>
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
    ControlService,
    LoginService,
    UtilService,
    MiningService,
    RawTransactionService,
    BlockChainService,
    WalletService,
    RegTestService,

    /// Connections can subscribe to bitcoin-address usage notifications
    AddressMonitorService = 40,
};

enum ApiTags {
    RequestId = 11
};


namespace Control {

// Control Service
enum MessageIds {
    CommandFailed,
//   getinfo
    Stop,
    StopReply
    // Maybe 'version' ? To allow a client to see if the server is a different version.
    // and then at the same time a "supports" method that returns true if a certain
    // command is supported.
};

enum Tags {
    Separator = 0,
    GenericByteData,
    FailedReason,
    FailedCommandServiceId,
    FailedCommandId,
};

}


namespace Login { // Login Service

enum MessageIds {
    LoginMessage
};

enum Tags {
    Separator = 0,
    GenericByteData,
    CookieData
};

}


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
    Separator = 0,
    GenericByteData,
    BitcoinAddress,
    ScriptPubKey,
    PrivateAddress,
    IsValid,
};
}

namespace Mining { // Mining Service

enum MessageIds {
    CreateNewBlock,
    CreateNewBlockReply

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
    Separator = 0,
    GenericByteData,
    RawTransaction,
    TransactionId,   // bytearray
    Completed,       // boolean
    OutputIndex,
    ScriptPubKey,   // bytearray.   This is the output-script.
    ScriptSig,      // bytearray.   This is the input-script.
    Sequence,       // Number
    ErrorMessage,   // string
    PrivateKey,     // string
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

enum Tags {
    Separator = 0,
    GenericByteData,
    Verbose,    // bool
    Size,       // int
    Version,    // int
    Time,       // in seconds since epoch
    Difficulty, // double
    MedianTime, // in seconds since epoch
    ChainWork,  // a sha256

    // BlockChain-tags
    Chain = 30,
    Blocks,
    Headers,
    BestBlockHash,
    VerificationProgress,
    Pruned,
    Bip9ForkId,
    Bip9ForkStatus,

    // GetBlockReply tags
// TODO
    // txid
    // height
    // prevTxId
    // input
    // script  (in or out)
    // amount
    // address
    //

    // GetBlock-Request-tags
    // GetBlock can filter a block to only return transactions that match a bitcoin-address filter
    // (list of addresses).
    ReuseAddressFilter = 50, ///< A getBlock call resuses a previously created address filter. bool
    SetFilterAddress,        ///< Followed with one bytearray address. Clears and sets one address in filter.
    AddFilterAddress,        ///< Add one bytearray address.
    // for each individual transaction you can select how they should be returned.
// TODO
//   FullTransactionData,     ///< bool. Selects between original Tx data (when true) or tagged interpreted data otherwise.
//   GetBlock_TxId,           ///< bool.
//   GetBlock_OffsetInBlock,  ///< bool.
//   GetBlock_Inputs,         ///< bool.
//   GetBlock_Outputs,        ///< bool.
//   GetBlock_OutputAmounts,  ///< bool.
//   GetBlock_OutputScripts,  ///< bool. Full output Scripts.
//   GetBlock_OutputAddresses,///< bool. If OutputScripts if false, return addresses for default p2pkh scripts
//   GetBlock_SkipOpReturns,  ///< bool. if OutputScripts if false, return OP_RETURN based scripts.


    // GetBlockVerbose-tags
    BlockHash = 70,
    Confirmations,
    Height,
    MerkleRoot,
    TxId,
    Nonce,      //int
    Bits,       // integer
    PrevBlockHash,
    NextBlockHash
};

}


namespace Network {
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
    Separator = 0,
    GenericByteData,

};
}

namespace Wallet {
enum MessageIds {
    ListUnspent,
    ListUnspentReply,
    GetNewAddress,
    GetNewAddressReply,
};

enum Tags {
    Separator = 0,
    GenericByteData,
    MinimalConfirmations,
    MaximumConfirmations,
    TransactionId,
    TXOutputIndex,
    BitcoinAddress,
    ScriptPubKey,
    Amount,
    ConfirmationCount
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
    /// A string representing an address.
    BitcoinAddress,
    /// A bytearray for a full sha256 txid
    TransactionId,
    /// An unsigned 64 bit number for the amount of satshi you received
    Amount,
    /// True if it was mined in a block
    Mined,
    /// boolean. Success equals 'true'
    Result,
    /// A string giving a human (well, developer) readable error message
    ErrorMessage
};
}
namespace RegTest {

// RegTest Service
enum MessageIds {
    GenerateBlock,
    GenerateBlockReply
};

enum Tags {
    Separator = 0,
    GenericByteData,
    BitcoinAddress,
    Amount,
    BlockHash
};

}
}
#endif
