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
#include "QRCreator.h"
#include "PaymentDataProvider.h"
#include "Payment.h"

#if defined(HAVE_CONFIG_H)
#include "config/flowee-config.h" /* for USE_QRCODE */
#endif

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

QRCreator::QRCreator(PaymentDataProvider *provider)
    : QQuickImageProvider(QQmlImageProviderBase::Image),
      m_provider(provider)
{
}

QImage QRCreator::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(size)
    Q_UNUSED(requestedSize)
    const Payment *payment = m_provider->payment();
    if (!payment)
        return QImage();

#ifdef USE_QRCODE
    QString text("bitcoincash:%1?amount=%2.%3");
    text = text.arg(payment->pubAddress());
    qint64 amount = payment->amountBch();
    const int major = amount / 100000000;
    int minor = amount % 100000000;
    text = text.arg(major).arg(minor, 8, 10, QChar('0'));
    while (text.endsWith('0'))
        text = text.left(text.size() - 1);
    QRcode *code = QRcode_encodeString(text.toLatin1().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
    QImage result = QImage(code->width, code->width, QImage::Format_RGB32);
    result.fill(0xffffff);
    unsigned char *p = code->data;
    for (int y = 0; y < code->width; y++) {
        for (int x = 0; x < code->width; x++) {
            result.setPixel(x, y, ((*p & 1) ? 0x0 : 0xffffff));
            ++p;
        }
    }
    QRcode_free(code);
#else
    QImage result = QImage(8,8, QImage::Format_RGB32);
    result.fill(0xffffff);
#endif
    return result;
}
