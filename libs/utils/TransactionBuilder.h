/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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
#ifndef TRANSACTIONBUILDER_H
#define TRANSACTIONBUILDER_H

#include "primitives/transaction.h"
#include "primitives/FastTransaction.h"
#include "primitives/key.h"

class CPubKey;

/**
 * This class allows anyone to create or extend Bitcoin (BCH) transactions.
 *
 * A transaction can be started from nothing, or a partially constructed transaction
 * can be imported and build further.
 *
 * Bitcoin transactions are only final once all inputs are signed, which may take
 * multiple people signing parts of that transaction. This builder allows building
 * new but also importing and extending existing transactions.
 *
 * TODO when an action destroys an existing signature I should have a way to communicate
 * this to the user of this class.
 */
class TransactionBuilder
{
public:
    TransactionBuilder();
    TransactionBuilder(const Tx &existingTx);
    TransactionBuilder(const CTransaction &existingTx);

    /**
     * Add a new input and select it.
     * This uses the default SignAllOutputs SignAllInputs sighash.
     */
    int appendInput(const uint256 &txid, int outputIndex);
    /**
     * Select an input based on index.
     * Please note that the first input is numbered as zero (0)
     * @returns the input number we selected.
     */
    int selectInput(int index);

    /// SigHash type, the inputs part.
    enum SignInputs {
        /**
         * This option signs all inputs, making it impossible to combine with other inputs after signing.
         *
         * This is not directly a signhash type as this is the default behaviour. When in doubt, use this.
         */
        SignAllInputs = 0,
        /**
         * This option allows for full combining of this input with any other inputs, even after signing.
         *
         * This sighash flag, also called SIGHASH_ANYONECANPAY, is useful to combine different inputs
         * from different parties and combine that into a valid transaction.
         *
         * Be careful about picking an good SignOutputs enum as picking NoOutput will essentially give your
         * money to anyone that manages to mine the input first.
         */
        SignOnlyThisInput = 0x80,
    };

    /// SighHash type, the outputs part.
    enum SignOutputs {
        /**
         * An input signed with this will disallow any change in all outputs.
         * This sighash type, also called SIGHASH_ALL, signs all outputs, preventing any modification.
         */
        SignAllOuputs = 1,
        /**
         * Put no requirement at all on the outputs to stay the same after signing.
         *
         * This sighash type, also called SIGHASH_NONE, signs only this input. Best used in
         * combination with SignAllInputs because otherwise this input can be combined in another
         * transaction and losses can occur.
         */
        SignNoOutputs = 2,
        /**
         * Requires the input to be combined with one specific output.
         * This sighash type, also called SIGHASH_SINGLE, signs the output from this transaction
         * that has the same index as this input.
         *
         * Please be aware that if there is no output of the same number that this silently turns
         * into a SignNoOutputs.
         *
         * It allows modifications of other outputs and the sequence number of other inputs.
         */
        SignSingleOutput = 3,
    };

    /**
     * Pushes the data needed for the current input to receive its signatures.
     *
     * Inputs use a signature to prove you own the money that this transaction spends. To make the
     * signing secure it doesn't just take the private key, it also takes the prevOutScript and the amount
     * properties, which your wallet should supply you with.
     *
     * The SignInputs / SignOutputs options determine how flexible the signature is with regards to a
     * changing transaction after final signing time.
     *
     * For instance a fundraiser may want to combine inputs from a lot of people into one transaction.
     * Those inputs can then be signed individually using SignOnlyThisInput and later combined without
     * breaking the signature.
     *
     * A common rule is that outputs or inputs not included in the transaction may be changed after signing
     * and before the transaction is mined.
     *
     * In most cases you should be very careful to pick at least one output you care about that you
     * will sign because that guarentees your money can only be spent with those outputs getting paid.
     *
     * The default is to sign all inputs and all outputs, which implies that the entire transaction
     * is fully constructed before signatures are collected.
     *
     * Notice that actual signing only happens when calling createTransaction()
     */
    void pushInputSignature(const CKey &privKey, const CScript &prevOutScript, int64_t amount, SignInputs inputs = SignAllInputs, SignOutputs outputs = SignAllOuputs);

    /// locking options.
    enum LockingOptions {
        /**
         * No locking applied, transaction can be mined immediately and spent immediately after.
         */
        NoLocking,
        /**
         * A transaction can be banned from mining till a certain block height.
         *
         * The value passed is the last block height the transaction is not allowed
         * to be mined in.
         *
         * Please be aware that this allows the transaction to be double spend quite easy.
         */
        LockMiningOnBlock,
        /**
         * A transaction can be banned from mining in a block until a certain time.
         * The time is set in seconds since unix epoch, the time should only be in the future.
         *
         * Please be aware that this allows the transaction to be double spend quite easy.
         */
        LockMiningOnTime,
        /**
         * A transaction *input* can be locked from being mined till a certain block height.
         *
         * This is only really useful in case the output you are spending used OP_CHECKSEQUENCEVERIFY
         */
        RelativeSpendingLockOnBlocks,

        /**
         * A transaction *input* can be locked from being mined untill a certain time.
         *
         * This is only really useful in case the output you are spending used OP_CHECKSEQUENCEVERIFY
         */
        RelativeSpendingLockOnTime
    };

    /**
     * Set the locking option on the current input.
     *
     * Please be aware that usage of the LockFromMiningBlock or LockFromMiningTime options
     * are transaction-global options and will effect all outputs in one go.
     */
    void setLocking(LockingOptions option, uint32_t value);
    /// delete an input based on index. Updates current input index.
    void deleteInput(int index);

    /// Appends and selects an output.
    int appendOutput(int64_t amount);
    /// selects an output
    int selectOutput(int index);

    /**
     * For the selected output a standard output script will be generated
     * that sends the funds to the public-key-hash (aka bitcoin-address) passed.
     */
    void pushOutputPay2Address(const CKeyID &address);
    // void pushOutputPay2Address(const std::string &address);

    /// delete an output based on index. Updates current output index.
    void deleteOutput(int index);

#if 0
    /// helper enum for createTransaction
    enum Signatures {
        /// signatures that depended on changed parts of the transaction will be cleared.
        ClearBrokenSignatures,
        /// Signatures that depended on changed parts of the transaction will be exported in the newly created Tx.
        LeaveBrokenSignatures
    };
#endif

    /**
     * Render the state of the transaction, signing any inputs that we have signing data for.
     * @see pushInputSignature
     *
     * @param optional pool to use for memory allocation.
     */
    Tx createTransaction(Streaming::BufferPool *pool = nullptr);

    /// Signatures imported may break because we removed/added or altered parts that signature relied on.
    /// This method returns which inputs used to have signatures that likely stopped working.
    // std::set<int> brokenSignatures() const;

    /**
     * Find equivalent inputs with signatures and copy those signatures to the current transaction
     */
    // mergeTransaction(const Tx &tx);

private:
    void checkCurInput();
    void checkCurOutput();
    CMutableTransaction m_transaction;

    LockingOptions m_defaultLocking = NoLocking;
    int m_curInput = -1, m_curOutput = -1;

    struct SignInfo {
        uint8_t hashType = 0;
        int64_t amount = 0;
        CKey privKey;
        CScript prevOutScript;
    };
    std::vector<SignInfo> m_signInfo;
};

#endif
