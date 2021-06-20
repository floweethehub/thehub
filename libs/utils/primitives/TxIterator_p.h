/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tom@flowee.org>
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
#ifndef TX_ITERATOR_P_H
#define TX_ITERATOR_P_H

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the Tx component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */


#include "FastTransaction.h"

class FastBlock;

uint64_t readCompactSize(const char **in, const char *end)
{
    uint8_t b = static_cast<uint8_t>(*in[0]);
    if (b < 253) {
        *in += 1;
        return b;
    }
    if (b == 253) {
        if (*in + 3 > end)
            throw std::runtime_error("readCompactSize not enough bytes");
        *in += 3;
        return le16toh(*((uint16_t*)(*in - 2)));
    }
    if (b == 254) {
        if (*in + 5 > end)
            throw std::runtime_error("readCompactSize not enough bytes");
        *in += 5;
        return le32toh(*((uint32_t*)(*in - 4)));
    }
    if (*in + 9 > end)
        throw std::runtime_error("readCompactSize not enough bytes");
    *in += 9;
    return le64toh(*((uint64_t*)(*in - 8)));
}

int readCompactSizeSize(const char *in)
{
    uint8_t b = static_cast<uint8_t>(in[0]);
    if (b < 253) return 1;
    if (b == 253) return 3;
    if (b == 254) return 5;
    return 9;
}

class TxTokenizer {
public:
    TxTokenizer(const Streaming::ConstBuffer &buffer);
    TxTokenizer(const FastBlock &block, int offsetInBlock);

    Tx::Component next();
    inline Tx::Component tag() const {
        return static_cast<Tx::Component>(m_tag);
    }

    Tx::Component checkSpaceForTag();

    const Streaming::ConstBuffer m_data;
    const char *m_txStart;
    const char *m_privData;
    const char *m_end;
    const char *m_currentTokenStart;
    const char *m_currentTokenEnd;

    int m_numInputsLeft = -1;
    int m_numOutputsLeft = -1;

    int32_t m_tag = -1;
};

#endif
