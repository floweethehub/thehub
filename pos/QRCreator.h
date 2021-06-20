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
#ifndef QRCREATOR_H
#define QRCREATOR_H

#include <QQuickImageProvider>

class PaymentDataProvider;

class QRCreator : public QQuickImageProvider
{
public:
    QRCreator(PaymentDataProvider *provider);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    PaymentDataProvider *m_provider;
};

#endif
