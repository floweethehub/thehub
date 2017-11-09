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
#include "datadirmigration.h"
#include "ui_interface.h"

#include "util.h"
#include "net.h"
#include "addrman.h"
#include "chainparams.h"

#include <iostream>
#include <fstream>

#include <boost/filesystem.hpp>

#define PLACEHOLDER_FILENAME "migrationInProgress"

#ifndef UAHF_CLIENT
# define UAHF_CLIENT 0
#endif

DatadirMigration::DatadirMigration()
    : m_migrationFinished(false)
{
    // only when user didn't override dir.
    m_needsMigration = GetArg("-datadir", "").empty();

    // if user wants to run a non-main chain today, no migration for you!
    m_needsMigration = m_needsMigration && ChainNameFromCommandLine() == CBaseChainParams::MAIN;

    // only when we are an UAHF (Bitcoin Cash) client.
    const bool fallback = GetArg("-uahfstarttime",  UAHF_CLIENT) > 0;
    m_needsMigration = m_needsMigration && GetBoolArg("-uahf", fallback);

    // only when we actually have an orig dir.
    if (m_needsMigration) {
        auto path = GetDefaultDataDir(false);
        if (!boost::filesystem::exists(path / "blocks/index"))
            m_needsMigration = false;
    }

    // only when we don't have a target dir yet.
    if (m_needsMigration) {
        auto path = GetDefaultDataDir(true);
        if (boost::filesystem::exists(path / "blocks/index") && !boost::filesystem::exists(path / PLACEHOLDER_FILENAME)) {
            // already done
            m_needsMigration = false;
            m_migrationFinished = true;
        }
    }
}

bool DatadirMigration::needsMigration() const
{
    return m_needsMigration;
}

/// if you want to redirect data to the dir, should it exist, call this.
void DatadirMigration::updateConfig()
{
    if (!m_migrationFinished)
        return;
    boost::filesystem::path path = GetDefaultDataDir(m_migrationFinished);
    mapArgs["-datadir"] = path.string();
}


static void clearAndCopyDir(const boost::filesystem::path &from, const boost::filesystem::path &to, const std::string &dirname) {
    boost::filesystem::remove_all(to / dirname);
    if (boost::filesystem::is_directory(from / dirname)) {
        boost::filesystem::copy_directory(from / dirname, to / dirname);
        for (boost::filesystem::recursive_directory_iterator end, iter(from / dirname);  iter != end; ++iter) {
            boost::filesystem::copy_file(*iter, to / dirname / iter->path().filename());
        }
    }
}

static void copyFile(const boost::filesystem::path &from, const boost::filesystem::path &to, const std::string &filename)
{
    if (boost::filesystem::is_regular_file(from / filename))
        boost::filesystem::copy_file(from  / filename, to / filename, boost::filesystem::copy_option::overwrite_if_exists);
}

void DatadirMigration::migrateToCashIfNeeded()
{
    if (!m_needsMigration)
        return;
    uiInterface.InitMessage(_("Migrating data-dir to CASH..."));
    auto from = GetDefaultDataDir(false);
    auto to = GetDefaultDataDir(true);

    boost::filesystem::create_directories(to / "blocks");
    std::ofstream inProgressMarker;
    inProgressMarker.open((to / PLACEHOLDER_FILENAME).string());
    inProgressMarker << "Bitcoin Classic data migration started. If this file is still here, then it was interrupted!\n\n";
    inProgressMarker.close();

    if (boost::filesystem::exists(from / "logs.conf")) {
        boost::filesystem::copy_file(from  / "logs.conf", to / "logs.conf",  boost::filesystem::copy_option::overwrite_if_exists);
    } else {
        std::ofstream logsConf;
        logsConf.open((to / "logs.conf").string());
        logsConf << "# Bitcoin Classic logging config." << std::endl;
        logsConf << "channel file" << std::endl;
        logsConf << "  # timestamp option takes [time, millisecond, date]. Any combination allowed. None of these 3 for no timestamps" << std::endl;
        logsConf << "  option timestamp time millisecond" << std::endl << std::endl;
        logsConf << "#channel console" << std::endl << std::endl;
        logsConf << "#####  Log sections from Log::Sections and verbosity" << std::endl;
        logsConf << "# Lookup is 1) direct match.  2) group (n mod 1000)  3) default to 'info'" << std::endl;
        logsConf << "# numbers come from file Logging.h, enum Log::Sections" << std::endl;
        logsConf << "0 silent\n1000 quiet\n2000 quiet\n3000 quiet\n4000 quiet\n5000 quiet\n6000 quiet" << std::endl;
        logsConf << "# silent only shows fatal" << std::endl;
        logsConf << "# quiet only shows critical and fatal" << std::endl;
        logsConf << "# info shows warning, info, critical and fatal" << std::endl;
        logsConf << "# debug shows everything." << std::endl << std::endl;
        logsConf.close();;
    }

    assert(GetArg("-datadir", "").empty());
    mapArgs["-datadir"] = to.string();

    // Need to do this so we can log...
    SelectParams(ChainNameFromCommandLine());
    Log::Manager::instance()->parseConfig();
    logInfo(42) << "Starting data migration process. From" << from.string() << "to" << to.string();

    // recursively copy
    logInfo(42) << "Copying chainstate dir";
    clearAndCopyDir(from, to, "chainstate");
    logInfo(42) << "Copying database dir";
    clearAndCopyDir(from, to, "database");
    logInfo(42) << "Copying blockindex dir";
    clearAndCopyDir(from, to, "blocks/index");
    logInfo(42) << "Copying misc-files";
    const std::string wallet("wallet.dat");
    { // I'm pretty sure this file is empty, but we are talking about money. So move it, don't delete it.
        std::string backupName(wallet);
        while (boost::filesystem::exists(to / backupName)) {
            backupName += '~';
        }
        if (backupName != wallet)
            boost::filesystem::rename(to / wallet , to / backupName);
    }

    if (boost::filesystem::is_regular_file(from / wallet))
        boost::filesystem::copy_file(from  / wallet, to / wallet, boost::filesystem::copy_option::fail_if_exists);
    copyFile(from, to, "fee_estimates.dat");

    // Copy the last block and rev file. We assume they are unfinished.
    std::string lastFile;
    for (boost::filesystem::recursive_directory_iterator end, iter(from / "blocks");  iter != end; ++iter) {
        const std::string filename = iter->path().filename().string();
        if (filename.size() == 12 && filename.substr(0, 3) == "blk" && filename > lastFile) {
            lastFile = filename;
        }
    }
    logInfo(42) << " last block file:" << lastFile;
    copyFile(from / "blocks", to / "blocks", lastFile);
    lastFile.replace(0, 3, "rev");
    logInfo(42) << " last rev file:" << lastFile;
    copyFile(from / "blocks", to / "blocks", lastFile);
    copyFile(from, to, "bitcoin.conf");

    std::ofstream config;
    config.open((to / "bitcoin.conf").string(), std::ios_base::app | std::ios_base::out | std::ios_base::ate);
    config << "\n\n## The follwing added by the Bitcoin Classic datamigration\n";
    config << "\nblockdatadir=" << from.string() << "\n";
    config.close();

    logInfo(42) << "Finished migration, removing placeholder";

    // remove PLACEHOLDER_FILENAME
    boost::filesystem::remove(to / PLACEHOLDER_FILENAME);
}

std::string DatadirMigration::from() const
{
    auto from = GetDefaultDataDir(false);
    return from.string();
}

std::string DatadirMigration::to() const
{
    auto to = GetDefaultDataDir(true);
    return to.string();
}
