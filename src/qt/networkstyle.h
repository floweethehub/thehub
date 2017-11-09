/*
 * This file is part of the Flowee project
 * Copyright (C) 2014 The Bitcoin Core developers
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

#ifndef BITCOIN_QT_NETWORKSTYLE_H
#define BITCOIN_QT_NETWORKSTYLE_H

#include <QIcon>
#include <QPixmap>
#include <QString>

/* Coin network-specific GUI style information */
class NetworkStyle
{
public:
    /**
     * Create style associated with provided BIP70 network id
     * The constructor throws an runtime error if the network id is unknown.
     */
    NetworkStyle(const QString &networkId);

    QString getAppName() const { return appName; }
    QString getTitleAddText() const { return titleAddText; }
    QImage getAppIcon() const { return appIcon; }
    QIcon getTrayAndWindowIcon() const { return trayAndWindowIcon; }



private:
    QString appName;
    QImage appIcon;
    QIcon trayAndWindowIcon;
    QString titleAddText;
};

#endif // BITCOIN_QT_NETWORKSTYLE_H
