/*
 * This file is part of the Flowee project
 * Copyright (C) 2011-2015 The Bitcoin Core developers
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

#include "splashscreen.h"

#include "networkstyle.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"
#include "UiInterface.h"
#include "version.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QRadialGradient>

SplashScreen::SplashScreen(const NetworkStyle &networkStyle, Qt::WindowFlags f) :
    QWidget(0, f), curAlignment(0)
{
    // set reference point, paddings
    int paddingRight = 30;
    int paddingTop = 38;

    // define text to place
    QString titleText       = "Flowee the Hub";
    QString versionText     = QString("%1").arg(QString::fromStdString(FormatFullVersion()));
    QString titleAddText    = networkStyle.getTitleAddText();
    QString font            = QApplication::font().toString();

    // create a bitmap according to device pixelratio
    float devicePixelRatio;
    bool useMorePixels = false;
#if QT_VERSION > 0x050100
    devicePixelRatio = qobject_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio();
    if (qFuzzyCompare(devicePixelRatio, (float) 1)) {
        useMorePixels = true;
        devicePixelRatio = logicalDpiX() / (float) 96;
    }
#else
    devicePixelRatio = 1.0;
#endif

    QSize splashSize(350 * devicePixelRatio, 250 * devicePixelRatio);
    pixmap = QPixmap(splashSize);

#if QT_VERSION > 0x050100
    if (!useMorePixels) // change to HiDPI if it makes sense
        pixmap.setDevicePixelRatio(devicePixelRatio);
#endif

    QPainter pixPaint(&pixmap);
    pixPaint.setPen(QColor(220,220,220));
    pixPaint.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    if (useMorePixels) // change to HiDPI if it makes sense
        pixPaint.scale(devicePixelRatio, devicePixelRatio);

    // draw a slightly radial gradient
    QRadialGradient gradient(QPoint(0,0), splashSize.width()/devicePixelRatio);
    gradient.setColorAt(0, Qt::white);
    gradient.setColorAt(1, QColor(247,247,247));
    QRect rGradient(QPoint(0,0), splashSize);
    pixPaint.fillRect(rGradient, gradient);

    // draw the bitcoin icon, expected size of PNG: 1000x655
    QRect rectIcon(QPoint(8, 5), QSize(333, 218));

    QImage icon = networkStyle.getAppIcon();
    Q_ASSERT(icon.width() == 1000);
    Q_ASSERT(icon.height() == 655);
    pixPaint.drawImage(rectIcon, icon);

    // check font size and drawing width
    float fontFactor = 1.0;
    if (useMorePixels) // fonts are set in Point, but we used painter::scale(), so we have to counter that.
        fontFactor /= devicePixelRatio;

    pixPaint.setFont(QFont(font, 15*fontFactor));
    QFontMetrics fm(pixPaint.fontMetrics());
    // if the version string is to long, reduce size
    int versionTextWidth  = fm.width(versionText);
    if (versionTextWidth > paddingRight-10) {
        pixPaint.setFont(QFont(font, 10*fontFactor));
        versionTextWidth  = pixPaint.fontMetrics().width(versionText);
    }
    pixPaint.drawText(pixmap.width() / devicePixelRatio - versionTextWidth - paddingRight, paddingTop, versionText);

    // draw additional text if special network
    if (!titleAddText.isEmpty()) {
        QFont boldFont = QFont(font, 10*fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth  = fm.width(titleAddText);
        pixPaint.drawText(pixmap.width()/devicePixelRatio-titleAddTextWidth-paddingRight, 22 ,titleAddText);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    if (useMorePixels) // if scaling uses more pixels, actually allow the windows to have larger pixelsize
        devicePixelRatio = 1;

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(pixmap.size().width()/devicePixelRatio,pixmap.size().height()/devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);
    hide();
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress)
{
    InitMessage(splash, title + strprintf("%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
static void ConnectWallet(SplashScreen *splash, CWallet* wallet)
{
    wallet->ShowProgress.connect(boost::bind(ShowProgress, splash, _1, _2));
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, boost::placeholders::_1));
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(ConnectWallet, this, boost::placeholders::_1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, boost::placeholders::_1));
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2));
#ifdef ENABLE_WALLET
    if(pwalletMain)
        pwalletMain->ShowProgress.disconnect(boost::bind(ShowProgress, this, boost::placeholders::_1, boost::placeholders::_2));
#endif
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    StartShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
