/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#include "FillAddressDBAction.h"

#include "DownloadManager.h"
#include "ConnectionManager.h"
#include "PeerAddressDB.h"
#include "Peer.h"

#include <time.h>
#include <functional>

static int PORTNR = 8333;

static std::vector<std::string> fillSeeders(P2PNet::Chain chain)
{
    std::vector<std::string> answer;
    switch (chain) {
    case P2PNet::MainChain:
        answer.push_back("seed.flowee.cash");
        answer.push_back("seed.bchd.cash");
        break;
    case P2PNet::Testnet4Chain:
        answer.push_back("testnet4-seed.flowee.cash");
        answer.push_back("testnet4-seed-bch.toom.im");
        answer.push_back("testnet4-seed-bch.bitcoinforks.org");
        answer.push_back("seed.tbch4.loping.net");
        PORTNR = 28333;
        break;
    }
    return answer;
}

FillAddressDBAction::FillAddressDBAction(DownloadManager *parent)
    : Action(parent),
    m_resolver(parent->service()),
    m_seeders(fillSeeders(parent->chain()))
{
    assert(!m_seeders.empty());
}

void FillAddressDBAction::execute(const boost::system::error_code &error)
{
    if (error)
        return;

    if (m_dlm->connectionManager().peerAddressDb().peerCount() < 50
            && (m_dnsLookupState == -1 || (m_dnsLookupState % 2) != 0)) { // skip if request in progress

        // start a new DNS lookup
        const auto state = ++m_dnsLookupState;
        const auto index = state / 2;
        if (index >= int(m_seeders.size())) {
            logInfo() << "Asked all DNS seeds";
        }
        else {
            logDebug() << "Start to resolve DNS entry" << m_seeders.at(index);
            const std::string port = strprintf("%d", PORTNR);
            boost::asio::ip::tcp::resolver::query query(m_seeders.at(index), port);
            m_resolver.async_resolve(query, std::bind(&FillAddressDBAction::onAddressResolveComplete,
                                this, std::placeholders::_1, std::placeholders::_2));
        }
    }

    if (m_dlm->connectionManager().peerAddressDb().peerCount() > 2000) {
        logDebug() << "FillAddressDb done";
        m_dlm->done(this);
        return;
    }

    for (auto peer : m_dlm->connectionManager().connectedPeers()) {
        auto address = peer->peerAddress();
        if (address.isValid()) {
            if (!address.askedAddresses()) {
                address.setAskedAddresses(true);
                logInfo() << "Sending GetAddr msg to" << address.peerAddress();
                peer->sendMessage(Message(Api::LegacyP2P, Api::P2P::GetAddr));
                m_lastRequestStarted = time(nullptr);
                again();
                return;
            }
        }
    }

    if (m_lastRequestStarted > 0 && static_cast<int64_t>(time(nullptr)) - m_lastRequestStarted < 10) {
        again();
        return;
    }

    // lets connect to some new peers then.
    auto address = m_dlm->connectionManager().peerAddressDb().findBest();
    if (address.isValid()) {
        logInfo() << "AddressDB still needs more data: creating a new connection";
        m_dlm->connectionManager().connect(address);
    }

    again();
}

void FillAddressDBAction::onAddressResolveComplete(const boost::system::error_code &error, boost::asio::ip::tcp::resolver::iterator iterator)
{
    if (error.value() == boost::asio::error::operation_aborted)
        // this means the app is shutting down.
        return;

    if (!error) {
        boost::asio::ip::tcp::resolver::iterator it_end;
        while (iterator != it_end) {
            EndPoint ep(iterator->endpoint().address(), PORTNR);
            m_dlm->connectionManager().peerAddressDb().addOne(ep);
            iterator++;
        }
    }

    m_dnsLookupState++;
}
