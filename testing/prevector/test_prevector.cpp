/*
 * This file is part of the Flowee project
 * Copyright (C) 2015 The Bitcoin Core developers
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

#include "test_prevector.h"
#include <vector>
#include <utils/prevector.h>
#include <Logger.h>
#include <random.h>

#include "serialize.h"
#include "streaming/streams.h"
#include <boost/foreach.hpp>
#include <utils/Logger.h>
#include <server/chainparams.h>

template<unsigned int N, typename T>
class prevector_tester {
    typedef std::vector<T> realtype;
    realtype real_vector;

    typedef prevector<N, T> pretype;
    pretype pre_vector;

    typedef typename pretype::size_type Size;

    void test() {
        const pretype& const_pre_vector = pre_vector;
        QCOMPARE(real_vector.size(), (size_t) pre_vector.size());
        QCOMPARE(real_vector.empty(), pre_vector.empty());
        for (Size s = 0; s < real_vector.size(); s++) {
             QVERIFY(real_vector[s] == pre_vector[s]);
             QVERIFY(&(pre_vector[s]) == &(pre_vector.begin()[s]));
             QVERIFY(&(pre_vector[s]) == &*(pre_vector.begin() + s));
             QVERIFY(&(pre_vector[s]) == &*((pre_vector.end() + s) - real_vector.size()));
        }
        // QVERIFY(realtype(pre_vector) == real_vector);
        QVERIFY(pretype(real_vector.begin(), real_vector.end()) == pre_vector);
        QVERIFY(pretype(pre_vector.begin(), pre_vector.end()) == pre_vector);
        size_t pos = 0;
        for (const T& v : pre_vector) {
             QVERIFY(v == real_vector[pos++]);
        }
        BOOST_REVERSE_FOREACH(const T& v, pre_vector) {
             QVERIFY(v == real_vector[--pos]);
        }
        for (const T& v : const_pre_vector) {
             QVERIFY(v == real_vector[pos++]);
        }
        BOOST_REVERSE_FOREACH(const T& v, const_pre_vector) {
             QVERIFY(v == real_vector[--pos]);
        }
        CDataStream ss1(SER_DISK, 0);
        CDataStream ss2(SER_DISK, 0);
        ss1 << real_vector;
        ss2 << pre_vector;
        QCOMPARE(ss1.size(), ss2.size());
        for (Size s = 0; s < ss1.size(); s++) {
            QCOMPARE(ss1[s], ss2[s]);
        }
    }

public:
    void resize(Size s) {
        real_vector.resize(s);
        QCOMPARE(real_vector.size(), (size_t) s);
        pre_vector.resize(s);
        QCOMPARE(pre_vector.size(), s);
        test();
    }

    void reserve(Size s) {
        real_vector.reserve(s);
        QVERIFY(real_vector.capacity() >= s);
        pre_vector.reserve(s);
        QVERIFY(pre_vector.capacity() >= s);
        test();
    }

    void insert(Size position, const T& value) {
        real_vector.insert(real_vector.begin() + position, value);
        pre_vector.insert(pre_vector.begin() + position, value);
        test();
    }

    void insert(Size position, Size count, const T& value) {
        real_vector.insert(real_vector.begin() + position, count, value);
        pre_vector.insert(pre_vector.begin() + position, count, value);
        test();
    }

    template<typename I>
    void insert_range(Size position, I first, I last) {
        real_vector.insert(real_vector.begin() + position, first, last);
        pre_vector.insert(pre_vector.begin() + position, first, last);
        test();
    }

    void erase(Size position) {
        real_vector.erase(real_vector.begin() + position);
        pre_vector.erase(pre_vector.begin() + position);
        test();
    }

    void erase(Size first, Size last) {
        real_vector.erase(real_vector.begin() + first, real_vector.begin() + last);
        pre_vector.erase(pre_vector.begin() + first, pre_vector.begin() + last);
        test();
    }

    void update(Size pos, const T& value) {
        real_vector[pos] = value;
        pre_vector[pos] = value;
        test();
    }

    void push_back(const T& value) {
        real_vector.push_back(value);
        pre_vector.push_back(value);
        test();
    }

    void pop_back() {
        real_vector.pop_back();
        pre_vector.pop_back();
        test();
    }

    void clear() {
        real_vector.clear();
        pre_vector.clear();
    }

    void assign(Size n, const T& value) {
        real_vector.assign(n, value);
        pre_vector.assign(n, value);
    }

    Size size() {
        return real_vector.size();
    }

    Size capacity() {
        return pre_vector.capacity();
    }

    void shrink_to_fit() {
        pre_vector.shrink_to_fit();
        test();
    }
};


void TestPrevector::runTests()
{
    /*
     * Likely the best chance I have to make this work is to create a method
     * that is called from the logger itself. The method should be one that I
     * implement in a separate header file and in each app I include that header file.
     * In unit tests instead I link to my own version of that call.
     *
     * Ah, I can set a bool on the Log::Manager that enables the call of this method :)
     * Or, even better, set a std::function there!  When unset we just use the old one.
     */
    for (int j = 0; j < 64; j++) {
        prevector_tester<8, int> test;
        for (int i = 0; i < 2048; i++) {
            int r = insecure_rand();
            if ((r % 4) == 0) {
                test.insert(insecure_rand() % (test.size() + 1), insecure_rand());
            }
            if (test.size() > 0 && ((r >> 2) % 4) == 1) {
                test.erase(insecure_rand() % test.size());
            }
            if (((r >> 4) % 8) == 2) {
                int new_size = std::max<int>(0, std::min<int>(30, test.size() + (insecure_rand() % 5) - 2));
                test.resize(new_size);
            }
            if (((r >> 7) % 8) == 3) {
                test.insert(insecure_rand() % (test.size() + 1), 1 + (insecure_rand() % 2), insecure_rand());
            }
            if (((r >> 10) % 8) == 4) {
                int del = std::min<int>(test.size(), 1 + (insecure_rand() % 2));
                int beg = insecure_rand() % (test.size() + 1 - del);
                test.erase(beg, beg + del);
            }
            if (((r >> 13) % 16) == 5) {
                test.push_back(insecure_rand());
            }
            if (test.size() > 0 && ((r >> 17) % 16) == 6) {
                test.pop_back();
            }
            if (((r >> 21) % 32) == 7) {
                int values[4];
                int num = 1 + (insecure_rand() % 4);
                for (int i = 0; i < num; i++) {
                    values[i] = insecure_rand();
                }
                test.insert_range(insecure_rand() % (test.size() + 1), values, values + num);
            }
            if (((r >> 26) % 32) == 8) {
                int del = std::min<int>(test.size(), 1 + (insecure_rand() % 4));
                int beg = insecure_rand() % (test.size() + 1 - del);
                test.erase(beg, beg + del);
            }
            r = insecure_rand();
            if (r % 32 == 9) {
                test.reserve(insecure_rand() % 32);
            }
            if ((r >> 5) % 64 == 10) {
                test.shrink_to_fit();
            }
            if (test.size() > 0) {
                test.update(insecure_rand() % test.size(), insecure_rand());
            }
            if (((r >> 11) & 1024) == 11) {
                test.clear();
            }
            if (((r >> 21) & 512) == 12) {
                test.assign(insecure_rand() % 32, insecure_rand());
            }
        }
    }
}

QTEST_MAIN(TestPrevector)

