/*
 * This file is part of the Flowee project
 * Copyright (C) 2018 Tom Zander <tomz@freedommail.ch>
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
#include "DBConfig.h"

#include <Logger.h>

#include <qsettings.h>
#include <QSqlDatabase>
#include <QSqlError>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

static const char *GROUP = "database";
static const char *KEY_DBTYPE = "type";
static const char *KEY_DBFILE = "dbfile";
static const char *KEY_DBNAME = "dbname";
static const char *KEY_USERNAME = "username";
static const char *KEY_PASSWORD = "password";
static const char *KEY_HOSTNAME = "hostname";
static const char *KEY_SOCKET = "socket";

static void set(const char *key, const QVariant &value)
{
    QSettings settings;
    settings.beginGroup(GROUP);
    settings.setValue(key, value);
}

DBConfig::DBConfig(QObject *parent) : QObject(parent)
{
}

QString DBConfig::dbFile() const
{
    return m_dbFile;
}

void DBConfig::setDbFile(const QString &dbFile)
{
    if (m_dbFile == dbFile)
        return;
    m_dbFile = dbFile;
    set(KEY_DBFILE, dbFile);
    emit dbFileChanged();
}

QString DBConfig::username() const
{
    return m_username;
}

void DBConfig::setUsername(const QString &username)
{
    if (m_username == username)
        return;
    m_username = username;
    set(KEY_USERNAME, username);
    emit usernameChanged();
}

QString DBConfig::password() const
{
    return m_password;
}

void DBConfig::setPassword(const QString &password)
{
    if (password == m_password)
        return;
    m_password = password;
    set(KEY_PASSWORD, password);
    emit passwordChanged();
}

QString DBConfig::hostname() const
{
    return m_hostname;
}

void DBConfig::setHostname(const QString &hostname)
{
    if (m_hostname == hostname)
        return;
    m_hostname = hostname;
    set(KEY_HOSTNAME, hostname);
    emit hostnameChanged();
}

QString DBConfig::dbName() const
{
    return m_dbname;
}

void DBConfig::setDbName(const QString &dbname)
{
    if (dbname == m_dbname)
        return;
    m_dbname = dbname;
    set(KEY_DBNAME, dbname);
    emit dbNameChanged();
}

QString DBConfig::socketPath() const
{
    return m_socketPath;
}

void DBConfig::setSocketPath(const QString &socketPath)
{
    if (m_socketPath == socketPath)
        return;
    m_socketPath = socketPath;
    set(KEY_SOCKET, socketPath);
    emit socketPathChanged();
}

DBConfig::DBType DBConfig::dbType() const
{
    return m_dbType;
}

void DBConfig::setDbType(const DBType &dbType)
{
    if (m_dbType == dbType)
        return;
    m_dbType = dbType;
    set(KEY_DBTYPE, dbType);
    emit dbTypeChanged();
}

QSqlDatabase DBConfig::connectToDB(QString &dbType)
{
    QSettings settings;
    settings.beginGroup(GROUP);
    dbType = settings.value(KEY_DBTYPE).toString();
    if (dbType.isEmpty()) {
        settings.setValue(KEY_DBTYPE, "QSQLITE");
        settings.setValue(KEY_DBFILE, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tx.db");
        dbType = "QSQLITE";
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(dbType);
    if (!db.isValid()) {
        logFatal(Log::POS) << "Unknown database-type." << dbType << "Try another or install QSql plugins";
        logCritical(Log::POS) << db.lastError().text();
        return db;
    }
    if (dbType == "QSQLITE") {
        QString dbFile = settings.value(KEY_DBFILE).toString();
        Q_ASSERT(!dbFile.isEmpty());
        QDir dir(QFileInfo(dbFile).absoluteDir());
        dir.mkpath(".");
        db.setDatabaseName(dbFile);
    } else if (dbType == "QMYSQL") {
        db.setConnectOptions(QString("UNIX_SOCKET=%1").arg(settings.value(KEY_SOCKET).toString()));
        db.setDatabaseName(settings.value(KEY_DBNAME).toString());
        db.setUserName(settings.value(KEY_USERNAME).toString());
        db.setPassword(settings.value(KEY_PASSWORD).toString());
        db.setHostName(settings.value(KEY_HOSTNAME).toString());
    }
}
