/*
 * This file is part of the Flowee project
 * Copyright (C) 2017 Tom Zander <tomz@freedommail.ch>
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
#ifndef DATADIRMIGRATION_H
#define DATADIRMIGRATION_H

#include <string>

/// This is a helper class to migrate blockchain data from one dir to a new one.
class DatadirMigration
{
public:
    DatadirMigration();

    bool needsMigration() const;

    /// if you want to redirect data to the dir, should it exist, call this.
    void updateConfig();

    /// do the heavy lifting.
    void migrateToCashIfNeeded();

    std::string from() const;
    std::string to() const;

private:
    bool m_needsMigration;
    bool m_migrationFinished;
};

#endif
