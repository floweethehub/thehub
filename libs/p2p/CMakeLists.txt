# This file is part of the Flowee project
# Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

include_directories(${LIBP2P_INCLUDES} ${CMAKE_BINARY_DIR}/include)

add_library(flowee_p2p STATIC
    Action.cpp
    BlockHeader.cpp
    Blockchain.cpp
    ConnectionManager.cpp
    DataListenerInterface.cpp
    DownloadManager.cpp
    FillAddressDBAction.cpp
    InventoryItem.cpp
    P2PNetInterface.cpp
    Peer.cpp
    PeerAddressDB.cpp
    PrivacySegment.cpp
    PrivacySegmentListener.cpp
    SyncChainAction.cpp
    SyncSPVAction.cpp
    BroadcastTxData.cpp
)

target_link_libraries(flowee_p2p flowee_networkmanager)
add_definitions(-DLOG_DEFAULT_SECTION=2009) # aka Logger::P2PNet

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libflowee_p2p.a DESTINATION lib)
install(FILES 
    Action.h
    BlockHeader.h
    Blockchain.h
    ConnectionManager.h
    DataListenerInterface.h
    DownloadManager.h
    FillAddressDBAction.h
    InventoryItem.h
    P2PNetInterface.h
    Peer.h
    PeerAddressDB.h
    PrivacySegment.h
    PrivacySegmentListener.h
    SyncChainAction.h
    SyncSPVAction.h
    BroadcastTxData.h
DESTINATION include/flowee/p2p)
