/* This file is part of Flowee
 *
 * Copyright (C) 2017 Nathan Osman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * For the full copy of the License see <http://www.gnu.org/licenses/>
 */

#ifndef HTTPENGINE_HANDLER_P_H
#define HTTPENGINE_HANDLER_P_H

#include <QList>
#include <QObject>
#include <QPair>
#include <QRegExp>

#include "handler.h"

namespace HttpEngine
{

typedef QPair<QRegExp, QString> Redirect;
typedef QPair<QRegExp, Handler*> SubHandler;

class HandlerPrivate : public QObject
{
    Q_OBJECT

public:

    explicit HandlerPrivate(Handler *handler);

    QList<Redirect> redirects;
    QList<SubHandler> subHandlers;
    QList<Middleware*> middleware;

private:

    Handler *const q;
};

}

#endif
