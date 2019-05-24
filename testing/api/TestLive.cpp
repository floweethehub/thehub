/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
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

#include "TestLive.h"

#include <Message.h>

#include <streaming/MessageBuilder.h>
#include <streaming/MessageParser.h>

void TestApiLive::testBasic()
{
    startHubs();
    QCOMPARE((int) con.size(), 1);

    con[0].send(Message(Api::APIService, Api::Meta::Version));
    Message m = waitForMessage(0, Api::APIService, Api::Meta::VersionReply);
    QCOMPARE((int) m.serviceId(), (int) Api::APIService);
    QCOMPARE((int) m.messageId(), (int) Api::Meta::VersionReply);
    Streaming::MessageParser parser(m.body());
    while (parser.next() == Streaming::FoundTag) {
        if (parser.tag() == Api::GenericByteData) {
            QVERIFY(parser.isString());
            QVERIFY(QString::fromStdString(parser.stringData()).startsWith("Flowee:"));
            return;
        }
    }
    QVERIFY(false); // version not included in reply
}
