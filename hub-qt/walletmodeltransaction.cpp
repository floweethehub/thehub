/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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

#include "walletmodeltransaction.h"

#include "wallet/wallet.h"

WalletModelTransaction::WalletModelTransaction(const QList<SendCoinsRecipient> &recipients) :
    recipients(recipients),
    walletTransaction(0),
    keyChange(0),
    fee(0)
{
    walletTransaction = new CWalletTx();
}

WalletModelTransaction::~WalletModelTransaction()
{
    delete keyChange;
    delete walletTransaction;
}

QList<SendCoinsRecipient> WalletModelTransaction::getRecipients()
{
    return recipients;
}

CWalletTx *WalletModelTransaction::getTransaction()
{
    return walletTransaction;
}

unsigned int WalletModelTransaction::getTransactionSize()
{
    return (!walletTransaction ? 0 : (::GetSerializeSize(*(CTransaction*)walletTransaction, SER_NETWORK, PROTOCOL_VERSION)));
}

int64_t WalletModelTransaction::getTransactionFee()
{
    return fee;
}

void WalletModelTransaction::setTransactionFee(const int64_t& newFee)
{
    fee = newFee;
}

void WalletModelTransaction::reassignAmounts(int nChangePosRet)
{
    int i = 0;
    for (QList<SendCoinsRecipient>::iterator it = recipients.begin(); it != recipients.end(); ++it)
    {
        SendCoinsRecipient& rcp = (*it);

        if (rcp.paymentRequest.IsInitialized())
        {
            int64_t subtotal = 0;
            const payments::PaymentDetails& details = rcp.paymentRequest.getDetails();
            for (int j = 0; j < details.outputs_size(); j++)
            {
                const payments::Output& out = details.outputs(j);
                if (out.amount() <= 0) continue;
                if (i == nChangePosRet)
                    i++;
                subtotal += walletTransaction->vout[i].nValue;
                i++;
            }
            rcp.amount = subtotal;
        }
        else // normal recipient (no payment request)
        {
            if (i == nChangePosRet)
                i++;
            rcp.amount = walletTransaction->vout[i].nValue;
            i++;
        }
    }
}

int64_t WalletModelTransaction::getTotalTransactionAmount()
{
    int64_t totalTransactionAmount = 0;
    Q_FOREACH(const SendCoinsRecipient &rcp, recipients)
    {
        totalTransactionAmount += rcp.amount;
    }
    return totalTransactionAmount;
}

void WalletModelTransaction::newPossibleKeyChange(CWallet *wallet)
{
    keyChange = new CReserveKey(wallet);
}

CReserveKey *WalletModelTransaction::getPossibleKeyChange()
{
    return keyChange;
}
