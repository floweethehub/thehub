/*
 * This file is part of the Flowee project
 * Copyright (C) 2014 BitPay, Inc.
 * Copyright (C) 2014-2015 The Bitcoin Core developers
 * Copyright (C) 2020 Tom Zander <tomz@freedommail.ch>
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
#include "univalue_tests.h"

#include <univalue.h>

void TestUnivalue::testConstructor()
{
    UniValue v1;
    QVERIFY(v1.isNull());

    UniValue v2(UniValue::VSTR);
    QVERIFY(v2.isStr());

    UniValue v3(UniValue::VSTR, "foo");
    QVERIFY(v3.isStr());
    QCOMPARE(v3.getValStr(), "foo");

    UniValue numTest;
    QVERIFY(numTest.setNumStr("82"));
    QVERIFY(numTest.isNum());
    QCOMPARE(numTest.getValStr(), "82");

    uint64_t vu64 = 82;
    UniValue v4(vu64);
    QVERIFY(v4.isNum());
    QCOMPARE(v4.getValStr(), "82");

    int64_t vi64 = -82;
    UniValue v5(vi64);
    QVERIFY(v5.isNum());
    QCOMPARE(v5.getValStr(), "-82");

    int vi = -688;
    UniValue v6(vi);
    QVERIFY(v6.isNum());
    QCOMPARE(v6.getValStr(), "-688");

    double vd = -7.21;
    UniValue v7(vd);
    QVERIFY(v7.isNum());
    QCOMPARE(v7.getValStr(), "-7.21");

    std::string vs("yawn");
    UniValue v8(vs);
    QVERIFY(v8.isStr());
    QCOMPARE(v8.getValStr(), "yawn");

    const char *vcs = "zappa";
    UniValue v9(vcs);
    QVERIFY(v9.isStr());
    QCOMPARE(v9.getValStr(), "zappa");
}

void TestUnivalue::testTypecheck()
{
    UniValue v1;
    QVERIFY(v1.setNumStr("1"));
    QVERIFY(v1.isNum());
    try {
        v1.get_bool();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}

    UniValue v2;
    QVERIFY(v2.setBool(true));
    QCOMPARE(v2.get_bool(), true);
    try {
        v2.get_int();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}

    UniValue v3;
    QVERIFY(v3.setNumStr("32482348723847471234"));
    try {
        v3.get_int64();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    QVERIFY(v3.setNumStr("1000"));
    QCOMPARE(v3.get_int64(), 1000);

    UniValue v4;
    QVERIFY(v4.setNumStr("2147483648"));
    QCOMPARE(v4.get_int64(), 2147483648);
    try {
        v4.get_int();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    QVERIFY(v4.setNumStr("1000"));
    QCOMPARE(v4.get_int(), 1000);
    try {
        v4.get_str();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    QCOMPARE(v4.get_real(), 1000);
    try {
        v4.get_array();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    try {
        v4.getKeys();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    try {
        v4.getValues();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    try {
        v4.get_obj();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}

    UniValue v5;
    QVERIFY(v5.read("[true, 10]"));
    v5.get_array(); // should not throw
    std::vector<UniValue> vals = v5.getValues();
    try {
        vals[0].get_int();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
    QCOMPARE(vals[0].get_bool(), true);

    QCOMPARE(vals[1].get_int(), 10);
    try {
        vals[1].get_bool();
        QFAIL("Should throw");
    } catch (const std::runtime_error &) {}
}

void TestUnivalue::testSet()
{
    UniValue v(UniValue::VSTR, "foo");
    v.clear();
    QVERIFY(v.isNull());
    QCOMPARE(v.getValStr(), "");

    QVERIFY(v.setObject());
    QVERIFY(v.isObject());
    QCOMPARE(v.size(), 0);
    QCOMPARE(v.getType(), UniValue::VOBJ);
    QVERIFY(v.empty());

    QVERIFY(v.setArray());
    QVERIFY(v.isArray());
    QCOMPARE(v.size(), 0);

    QVERIFY(v.setStr("zum"));
    QVERIFY(v.isStr());
    QCOMPARE(v.getValStr(), "zum");

    QVERIFY(v.setFloat(-1.01));
    QVERIFY(v.isNum());
    QCOMPARE(v.getValStr(), "-1.01");

    QVERIFY(v.setInt((int)1023));
    QVERIFY(v.isNum());
    QCOMPARE(v.getValStr(), "1023");

    QVERIFY(v.setInt((int64_t)-1023LL));
    QVERIFY(v.isNum());
    QCOMPARE(v.getValStr(), "-1023");

    QVERIFY(v.setInt((uint64_t)1023ULL));
    QVERIFY(v.isNum());
    QCOMPARE(v.getValStr(), "1023");

    QVERIFY(v.setNumStr("-688"));
    QVERIFY(v.isNum());
    QCOMPARE(v.getValStr(), "-688");

    QVERIFY(v.setBool(false));
    QCOMPARE(v.isBool(), true);
    QCOMPARE(v.isTrue(), false);
    QCOMPARE(v.isFalse(), true);
    QCOMPARE(v.getBool(), false);

    QVERIFY(v.setBool(true));
    QCOMPARE(v.isBool(), true);
    QCOMPARE(v.isTrue(), true);
    QCOMPARE(v.isFalse(), false);
    QCOMPARE(v.getBool(), true);

    QVERIFY(!v.setNumStr("zombocom"));

    QVERIFY(v.setNull());
    QVERIFY(v.isNull());
}

void TestUnivalue::testArray()
{
    UniValue arr(UniValue::VARR);

    UniValue v((int64_t)1023LL);
    QVERIFY(arr.push_back(v));

    std::string vStr("zippy");
    QVERIFY(arr.push_back(vStr));

    const char *s = "pippy";
    QVERIFY(arr.push_back(s));

    std::vector<UniValue> vec;
    v.setStr("boing");
    vec.push_back(v);

    v.setStr("going");
    vec.push_back(v);

    QVERIFY(arr.push_backV(vec));

    QCOMPARE(arr.empty(), false);
    QCOMPARE(arr.size(), 5);

    QCOMPARE(arr[0].getValStr(), "1023");
    QCOMPARE(arr[1].getValStr(), "zippy");
    QCOMPARE(arr[2].getValStr(), "pippy");
    QCOMPARE(arr[3].getValStr(), "boing");
    QCOMPARE(arr[4].getValStr(), "going");

    QCOMPARE(arr[999].getValStr(), "");

    arr.clear();
    QVERIFY(arr.empty());
    QCOMPARE(arr.size(), 0);
}

void TestUnivalue::testObject()
{
    UniValue obj(UniValue::VOBJ);
    std::string strKey, strVal;
    UniValue v;

    strKey = "age";
    v.setInt(100);
    QVERIFY(obj.pushKV(strKey, v));

    strKey = "first";
    strVal = "John";
    QVERIFY(obj.pushKV(strKey, strVal));

    strKey = "last";
    const char *cVal = "Smith";
    QVERIFY(obj.pushKV(strKey, cVal));

    strKey = "distance";
    QVERIFY(obj.pushKV(strKey, (int64_t) 25));

    strKey = "time";
    QVERIFY(obj.pushKV(strKey, (uint64_t) 3600));

    strKey = "calories";
    QVERIFY(obj.pushKV(strKey, (int) 12));

    strKey = "temperature";
    QVERIFY(obj.pushKV(strKey, (double) 90.012));

    UniValue obj2(UniValue::VOBJ);
    QVERIFY(obj2.pushKV("cat1", 9000));
    QVERIFY(obj2.pushKV("cat2", 12345));

    QVERIFY(obj.pushKVs(obj2));

    QCOMPARE(obj.empty(), false);
    QCOMPARE(obj.size(), 9);

    QCOMPARE(obj["age"].getValStr(), "100");
    QCOMPARE(obj["first"].getValStr(), "John");
    QCOMPARE(obj["last"].getValStr(), "Smith");
    QCOMPARE(obj["distance"].getValStr(), "25");
    QCOMPARE(obj["time"].getValStr(), "3600");
    QCOMPARE(obj["calories"].getValStr(), "12");
    QCOMPARE(obj["temperature"].getValStr(), "90.012");
    QCOMPARE(obj["cat1"].getValStr(), "9000");
    QCOMPARE(obj["cat2"].getValStr(), "12345");

    QCOMPARE(obj["nyuknyuknyuk"].getValStr(), "");

    QVERIFY(obj.exists("age"));
    QVERIFY(obj.exists("first"));
    QVERIFY(obj.exists("last"));
    QVERIFY(obj.exists("distance"));
    QVERIFY(obj.exists("time"));
    QVERIFY(obj.exists("calories"));
    QVERIFY(obj.exists("temperature"));
    QVERIFY(obj.exists("cat1"));
    QVERIFY(obj.exists("cat2"));

    QVERIFY(!obj.exists("nyuknyuknyuk"));

    std::map<std::string, UniValue::VType> objTypes;
    objTypes["age"] = UniValue::VNUM;
    objTypes["first"] = UniValue::VSTR;
    objTypes["last"] = UniValue::VSTR;
    objTypes["distance"] = UniValue::VNUM;
    objTypes["time"] = UniValue::VNUM;
    objTypes["calories"] = UniValue::VNUM;
    objTypes["temperature"] = UniValue::VNUM;
    objTypes["cat1"] = UniValue::VNUM;
    objTypes["cat2"] = UniValue::VNUM;
    QVERIFY(obj.checkObject(objTypes));

    objTypes["cat2"] = UniValue::VSTR;
    QVERIFY(!obj.checkObject(objTypes));

    obj.clear();
    QVERIFY(obj.empty());
    QCOMPARE(obj.size(), 0);
}

static const char *json1 =
"[1.10000000,{\"key1\":\"str\\u0000\",\"key2\":800,\"key3\":{\"name\":\"martian http://test.com\"}}]";

void TestUnivalue::testReadwrite()
{
    UniValue v;
    QVERIFY(v.read(json1));

    std::string strJson1(json1);
    QVERIFY(v.read(strJson1));

    QVERIFY(v.isArray());
    QCOMPARE(v.size(), 2);

    QCOMPARE(v[0].getValStr(), "1.10000000");

    UniValue obj = v[1];
    QVERIFY(obj.isObject());
    QCOMPARE(obj.size(), 3);

    QVERIFY(obj["key1"].isStr());
    std::string correctValue("str");
    correctValue.push_back('\0');
    QCOMPARE(obj["key1"].getValStr(), correctValue);
    QVERIFY(obj["key2"].isNum());
    QCOMPARE(obj["key2"].getValStr(), "800");
    QVERIFY(obj["key3"].isObject());

    QCOMPARE(strJson1, v.write());

    /* Check for (correctly reporting) a parsing error if the initial
       JSON construct is followed by more stuff.  Note that whitespace
       is, of course, exempt.  */

    QVERIFY(v.read("  {}\n  "));
    QVERIFY(v.isObject());
    QVERIFY(v.read("  []\n  "));
    QVERIFY(v.isArray());

    QVERIFY(!v.read("@{}"));
    QVERIFY(!v.read("{} garbage"));
    QVERIFY(!v.read("[]{}"));
    QVERIFY(!v.read("{}[]"));
    QVERIFY(!v.read("{} 42"));
}

