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

project (point-of-sale)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# set the default log section (for logDebug() with no args) to the POS value
add_definitions(-DLOG_DEFAULT_SECTION=7000)

find_package(Qt5Quick)
find_package(Qt5Sql)

include_directories(${LIBAPPUTILS_INCLUDES})

set (POS_LIBS
    flowee_apputils
    flowee_networkmanager
    flowee_utils

    ${OPENSSL_LIBRARIES}
    ${Boost_LIBRARIES}
    ${BOOST_THREAD_LIBRARY}
)

set (CASHIER_SOURCES
    cashier.cpp
    Calculator.cpp
    DBConfig.cpp
    ExchangeRateResolver.cpp
    HubConfig.cpp
    NetworkPaymentProcessor.cpp
    Payment.cpp
    PaymentDataProvider.cpp
    qrc_qml.cpp
)

if (${QREncode_FOUND})
    set (CASHIER_SOURCES_QR QRCreator.cpp)
endif()

if (${Qt5Sql_FOUND} AND ${Qt5Quick_FOUND})
    qt5_add_resources(qrc_qml.cpp qml.qrc)
    set_property(SOURCE qrc_qml.cpp PROPERTY SKIP_AUTOGEN ON)

    add_executable(cashier ${CASHIER_SOURCES} ${CASHIER_SOURCES_QR})

    target_link_libraries(cashier ${POS_LIBS} Qt5::Quick Qt5::Sql ${QREncode_LIBRARIES})
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/cashier DESTINATION bin)
endif ()


###########################################################

add_executable(pos
    main.cpp
    NetworkPaymentProcessor.cpp
)
target_link_libraries(pos ${POS_LIBS} Qt5::Core)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/pos DESTINATION bin)
