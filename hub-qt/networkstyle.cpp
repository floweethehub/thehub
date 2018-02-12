/*
 * This file is part of the Flowee project
 * Copyright (C) 2014-2015 The Bitcoin Core developers
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

#include "networkstyle.h"

#include "guiconstants.h"

#include <QApplication>

#include <stdexcept>

static const struct {
    const char *networkId;
    const char *appName;
    const int iconColorHueShift;
    const int iconColorSaturationReduction;
    const char *titleAddText;
} network_styles[] = {
    {"main", QAPP_APP_NAME_DEFAULT, 0, 0, ""},
    {"test", QAPP_APP_NAME_TESTNET, 160, 30, QT_TRANSLATE_NOOP("SplashScreen", "[testnet]")},
    {"regtest", QAPP_APP_NAME_TESTNET, 70, 30, "[regtest]"}
};
static const int network_styles_count = sizeof(network_styles)/sizeof(*network_styles);

static QImage fixIcon(const QImage &image, int iconColorHueShift, int iconColorSaturationReduction)
{
    if (iconColorSaturationReduction == 0 && iconColorHueShift == 0)
        return image;

    QImage copy(image);
    // traverse though lines
    for (int y=0; y < copy.height(); y++)
    {
        QRgb *scL = reinterpret_cast<QRgb*>(copy.scanLine(y));

        // loop through pixels
        for (int x=0; x < copy.width(); x++)
        {
            int h,s,l,a;
            // preserve alpha because QColor::getHsl doesen't return the alpha value
            a = qAlpha(scL[x]);
            QColor col(scL[x]);

            // get hue value
            col.getHsl(&h,&s,&l);

            // rotate color on RGB color circle
            // 70° should end up with the typical "testnet" green
            h+=iconColorHueShift;

            // change saturation value
            if (s>iconColorSaturationReduction)
            {
                s -= iconColorSaturationReduction;
            }
            col.setHsl(h,s,l,a);

            // set the pixel
            scL[x] = col.rgba();
        }
    }

    return copy;
}


NetworkStyle::NetworkStyle(const QString &networkId)
{
    int iconColorHueShift = 0;
    int iconColorSaturationReduction = 0;
    // char *titleAddText;

    for (int x=0; x<network_styles_count; ++x)
    {
        if (networkId == network_styles[x].networkId)
        {
            appName = network_styles[x].appName;
            titleAddText = qApp->translate("SplashScreen", network_styles[x].titleAddText);
            iconColorHueShift = network_styles[x].iconColorHueShift;
            iconColorSaturationReduction = network_styles[x].iconColorSaturationReduction;
            break;
        }
    }
    if (appName.isEmpty())
        throw std::runtime_error("Unknown networkId passed into NetworkStyle");

    appIcon = fixIcon(QImage(":/icons/hub"), iconColorHueShift, iconColorSaturationReduction);
    Q_ASSERT(appIcon.width() == 1000);
    Q_ASSERT(appIcon.height() == 655);
    trayAndWindowIcon = QIcon(QPixmap::fromImage(appIcon.scaled(256, 164)));
}
