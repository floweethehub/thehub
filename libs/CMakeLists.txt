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

add_subdirectory(3rdparty)
add_subdirectory(interfaces)
add_subdirectory(networkmanager)
add_subdirectory(utils)
add_subdirectory(server)
add_subdirectory(crypto)
#add_subdirectory(console)
add_subdirectory(api)
add_subdirectory(utxo)

if (${Qt5Core_FOUND})
    add_subdirectory(apputils)
    if (${Qt5Network_FOUND})
        add_subdirectory(httpengine)
    endif ()
endif ()
