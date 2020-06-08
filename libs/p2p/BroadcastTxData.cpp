#include "BroadcastTxData.h"

BroadcastTxData::BroadcastTxData(const Tx &tx)
    : m_tx(tx),
      m_hash(tx.createHash())
{
}

BroadcastTxData::~BroadcastTxData()
{
}
