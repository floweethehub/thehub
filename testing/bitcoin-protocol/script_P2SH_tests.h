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
#ifndef SCRIPT_P2SH_H
#define SCRIPT_P2SH_H

#include <common/TestFloweeSession.h>

class TestPaymentToScriptHash : public TestFloweeSession
{
    Q_OBJECT
private slots:
    void sign();
    void norecurse();
    void set();
    void is();
    void switchover();
    void AreInputsStandard();
};

#endif
