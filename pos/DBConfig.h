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
#ifndef DBCONFIG_H
#define DBCONFIG_H

#include <QObject>
class QSqlDatabase;

class DBConfig : public QObject
{
    Q_OBJECT
    Q_ENUMS(DBType)
    Q_PROPERTY(QString dbFile READ dbFile WRITE setDbFile NOTIFY dbFileChanged)
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY usernameChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
    Q_PROPERTY(QString hostname READ hostname WRITE setHostname NOTIFY hostnameChanged)
    Q_PROPERTY(QString dbName READ dbName WRITE setDbName NOTIFY dbNameChanged)
    Q_PROPERTY(QString socketPath READ socketPath WRITE setSocketPath NOTIFY socketPathChanged)
    Q_PROPERTY(DBType dbType READ dbType WRITE setDbType NOTIFY dbTypeChanged)
public:
    explicit DBConfig(QObject *parent = nullptr);

    enum DBType {
        SQLite,
        MySQL
    };

    QString dbFile() const;
    void setDbFile(const QString &dbFile);

    QString username() const;
    void setUsername(const QString &username);

    QString password() const;
    void setPassword(const QString &password);

    QString hostname() const;
    void setHostname(const QString &hostname);

    QString dbName() const;
    void setDbName(const QString &dbName);

    QString socketPath() const;
    void setSocketPath(const QString &socketPath);

    DBType dbType() const;
    void setDbType(const DBType &dbType);

    static QSqlDatabase connectToDB(QString &dbType);

signals:
    void dbFileChanged();
    void usernameChanged();
    void passwordChanged();
    void hostnameChanged();
    void dbNameChanged();
    void socketPathChanged();
    void dbTypeChanged();

private:
    DBType m_dbType = SQLite;

    // SQLite
    QString m_dbFile;

    // MySQL
    QString m_username;
    QString m_password;
    QString m_hostname;
    QString m_dbname;
    QString m_socketPath;
};

#endif // DBCONFIG_H
