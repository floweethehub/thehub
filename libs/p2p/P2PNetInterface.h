#ifndef P2PNETINTERFACE_H
#define P2PNETINTERFACE_H

#include "PeerAddressDB.h"

class P2PNetInterface
{
public:
    P2PNetInterface() = default;
    virtual ~P2PNetInterface();

    virtual void newPeer(int peerId, const std::string &userAgent, int startHeight, PeerAddress address) {}
    virtual void lostPeer(int peerId) {}
    virtual void punishMentChanged(int peerId) {}

    virtual void blockchainHeightChanged(int newHeight) {}
};

#endif
