# This file is part of the Flowee project
# Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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

project (hub-qt)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set_property(SOURCE qrc_bitcoin.cpp PROPERTY SKIP_AUTOGEN ON)
set_property(SOURCE paymentrequest.pb.cc PROPERTY SKIP_AUTOGEN ON)
set_property(SOURCE paymentrequest.pb.h PROPERTY SKIP_AUTOGEN ON)

include_directories(${LIBAPI_INCLUDES} ${CMAKE_BINARY_DIR}/include)

set (HUB_QT_LIBS
    flowee_api
    flowee_server
    flowee_networkmanager
    flowee_utils

    ${Boost_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${Event_LIBRARIES}
    ${BERKELEY_DB_LIBRARIES}
    ${MINIUPNP_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${QREncode_LIBRARIES}
)

set (HUB_QT_FILES
    bantablemodel.cpp
    bitcoinaddressvalidator.cpp
    bitcoinamountfield.cpp
    bitcoin.cpp
    bitcoingui.cpp
    bitcoinunits.cpp
    clientmodel.cpp
    csvmodelwriter.cpp
    guiutil.cpp
    intro.cpp
    networkstyle.cpp
    notificator.cpp
    optionsdialog.cpp
    optionsmodel.cpp
    peertablemodel.cpp
    platformstyle.cpp
    qvalidatedlineedit.cpp
    qvaluecombobox.cpp
    rpcconsole.cpp
    splashscreen.cpp
    trafficgraphwidget.cpp
    utilitydialog.cpp
    qrc_bitcoin.cpp
)

qt5_add_resources(qrc_bitcoin.cpp bitcoin.qrc)

if (WIN32)
    set (HUB_QT_WIN_FILES qt/winshutdownmonitor.cpp)
endif ()

if (enable_wallet)
    set (HUB_QT_WALLET_FILES
        addressbookpage.cpp
        addresstablemodel.cpp
        askpassphrasedialog.cpp
        coincontroldialog.cpp
        coincontroltreewidget.cpp
        editaddressdialog.cpp
        openuridialog.cpp
        overviewpage.cpp
        paymentrequestplus.cpp
        paymentserver.cpp
        receivecoinsdialog.cpp
        receiverequestdialog.cpp
        recentrequeststablemodel.cpp
        sendcoinsdialog.cpp
        sendcoinsentry.cpp
        signverifymessagedialog.cpp
        transactiondesc.cpp
        transactiondescdialog.cpp
        transactionfilterproxy.cpp
        transactionrecord.cpp
        transactiontablemodel.cpp
        transactionview.cpp
        walletframe.cpp
        walletmodel.cpp
        walletmodeltransaction.cpp
        walletview.cpp
    )
    set (AUTOMOC_MOC_OPTIONS -DENABLE_WALLET=1)
endif ()

find_package(Qt5Widgets)
if (NOT ${Qt5Widgets_FOUND})
    message("Missing qt5 (widgets) library, not building hub-qt")
endif ()
find_package(Qt5Network)
if (NOT ${Qt5Network_FOUND})
    message("Missing qt5 (network) library, not building hub-qt")
endif ()
find_package(Protobuf)
if (NOT ${Protobuf_FOUND})
    message("Missing protobuf, not building hub-qt")
endif ()

if (${Qt5DBus_FOUND})
    include_directories(${Qt5DBus_INCLUDE_DIRS})
endif ()

# find qrencode
# USE-QRCODE

#find_package(Qt5 COMPONENTS Core Gui Widgets Xml REQUIRED)
if ("${Qt5Widgets_FOUND}" AND "${Protobuf_FOUND}" AND "${Qt5Network_FOUND}")
    PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS paymentrequest.proto)

    #qt5_wrap_ui(forms/sendcoinsdialog.ui)


    add_executable(hub-qt ${HUB_QT_FILES} ${HUB_QT_WIN_FILES} ${HUB_QT_WALLET_FILES} ${PROTO_SRCS} ${PROTO_HDRS})
    include_directories(${PROTOBUF_INCLUDE_DIRS})
    target_link_libraries(hub-qt ${HUB_QT_LIBS}  ${PROTOBUF_LIBRARIES} Qt5::Widgets Qt5::Network ${Qt5DBus_LIBRARIES})
    add_dependencies(hub-qt univalue)
    add_dependencies(hub-qt leveldb)

    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/hub-qt DESTINATION bin)
endif ()
