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
#ifndef UNSPENT_PRUNER_H
#define UNSPENT_PRUNER_H

#include <streaming/BufferPool.h>
#include <streaming/MessageBuilder.h>

/*
 * WARNING USAGE OF THIS HEADER IS RESTRICTED.
 * This Header file is part of the private API and is meant to be used solely by the UTXO component.
 *
 * Usage of this API will likely mean your code will break in interesting ways in the future,
 * or even stop to compile.
 *
 * YOU HAVE BEEN WARNED!!
 */

class Pruner {
public:
    enum DBType {
        MostActiveDB,
        OlderDB
    };
    Pruner(const std::string &dbFile, const std::string &infoFile, DBType dbType = OlderDB);

    // copies pruned data into new files.
    void prune();
    // if all went well, rename new files over originals
    void commit();

    // remove tmp files
    void cleanup();

    /// Post-prune this is set to the amount of bytes used for the jumptables.
    int bucketsSize() const;

private:
    const std::string m_dbFile;
    const std::string m_infoFile;
    std::string m_tmpExtension;
    DBType m_dbType;
    int m_bucketsSize = 0;
};

#endif
