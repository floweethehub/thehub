/*
 * This file is part of the Flowee project
 * Copyright (c) 2015 The Bitcoin Core developers
 * Copyright (C) 2017 Tom Zander <tom@flowee.org>
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

#ifndef MOCKAPPLICATION_H
#define MOCKAPPLICATION_H

#include <Application.h>

#include <validation/Engine.h>

class MockApplication : public Application
{
public:
    MockApplication() = delete;

    inline static void doInit() {
        static_cast<MockApplication*>(Application::instance())->pub_init();
    }
    inline static void doStartThreads() {
        static_cast<MockApplication*>(Application::instance())->pub_startThreads();
    }
    inline static void setValidationEngine(Validation::Engine *bv) {
        static_cast<MockApplication*>(Application::instance())->replaceValidationEngine(bv);
    }

protected:
    inline void pub_init() {
        init();
    }
    inline void pub_startThreads() {
        startThreads();
    }
    inline void replaceValidationEngine(Validation::Engine *bv) {
        m_validationEngine.release();
        m_validationEngine.reset(bv);
    }
};

#endif
