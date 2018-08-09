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

include_directories(${LIBCRYPTO_INCLUDES})

add_library(flowee_crypto STATIC
    aes.cpp
    hmac_sha256.cpp
    hmac_sha512.cpp
    ripemd160.cpp
    sha1.cpp
    sha256_shani.cpp
    sha256_sse41.cpp
    sha256.cpp
    sha512.cpp
)
