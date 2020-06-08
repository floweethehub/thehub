#ifndef BROADCASTTXDATA_H
#define BROADCASTTXDATA_H

#include <primitives/FastTransaction.h>

#include <string>

class BroadcastTxData
{
public:
    BroadcastTxData(const Tx &tx);

    virtual ~BroadcastTxData();

    enum RejectReason {
        InvalidTx = 0x10,
        DoubleSpend = 0x12,
        NonStandard = 0x40,
        Dust = 0x41,
        LowFee = 0x42
    };

    /**
     * @brief txRejected is called with the remote peers reject message.
     * @param reason the reason for the reject, notice that this is foreign input from
     *         a random node on the Intenet. The value doesn't have to be included in
               the enum.
     * @param message
     */
    virtual void txRejected(RejectReason reason, const std::string &message) = 0;
    virtual void sentOne() = 0;

    /// the wallet, or privacy segment this transaction is associated with.
    virtual uint16_t privSegment() const = 0;

    inline Tx transaction() const {
        return m_tx;
    }
    inline const uint256 &hash() const {
        return m_hash;
    }

private:
    const Tx m_tx;
    const uint256 m_hash;
};

#endif
