/*
 * This file is part of the Flowee project
 * Copyright (C) 2020 The Bitcoin developers
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

#include "TestReverseBytes.h"

#include <script/interpreter.h>
#include <utils/primitives/script.h>

#include <common/lcg.h>

typedef std::vector<uint8_t> valtype;
typedef std::vector<valtype> stacktype;

struct ReverseTestCase {
    const std::vector<uint8_t> item;
    const std::vector<uint8_t> reversed_item;
};

inline void CheckErrorWithFlags(const uint32_t flags, const stacktype &original_stack,
                                const CScript &script, const ScriptError expected) {
    BaseSignatureChecker sigchecker;
    Script::State state(flags);
    stacktype stack{original_stack};
    bool r = Script::eval(stack, script, sigchecker, state);
    QVERIFY(!r);
    QVERIFY(state.error == expected);
}

inline void CheckPassWithFlags(const uint32_t flags, const stacktype &original_stack,
                               const CScript &script, const stacktype &expected) {
    BaseSignatureChecker sigchecker;
    Script::State state(flags);
    stacktype stack{original_stack};
    bool r = Script::eval(stack, script, sigchecker, state);
    QVERIFY(r);
    QVERIFY(state.error == SCRIPT_ERR_OK);
    QVERIFY(stack == expected);
}

/**
 * Verifies that the given error occurs with OP_REVERSEBYTES enabled
 * and that BAD_OPCODE occurs if disabled.
 */
inline void CheckErrorIfEnabled(const uint32_t flags, const stacktype &original_stack,
                                const CScript &script, const ScriptError expected) {
    CheckErrorWithFlags(flags | SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack, script, expected);
    CheckErrorWithFlags(flags & ~SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack,
                        script, SCRIPT_ERR_BAD_OPCODE);
}

/**
 * Verifies that the given stack results with OP_REVERSEBYTES enabled
 * and that BAD_OPCODE occurs if disabled.
 */
inline void CheckPassIfEnabled(const uint32_t flags, const stacktype &original_stack,
                               const CScript &script, const stacktype &expected) {
    CheckPassWithFlags(flags | SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack, script, expected);
    CheckErrorWithFlags(flags & ~SCRIPT_ENABLE_OP_REVERSEBYTES, original_stack,
                        script, SCRIPT_ERR_BAD_OPCODE);
}

/**
 * Verifies a given reverse test case.
 * Checks both if <item> OP_REVERSEBYTES results in <reversed_item> and
 * whether double-reversing <item> is a no-op.
 */
inline void CheckPassReverse(const uint32_t flags, const ReverseTestCase &reverse_case) {
    CheckPassIfEnabled(flags, {reverse_case.item},
            CScript() << OP_REVERSEBYTES, {reverse_case.reversed_item});
    CheckPassIfEnabled(flags, {reverse_case.item},
            CScript() << OP_DUP << OP_REVERSEBYTES << OP_REVERSEBYTES << OP_EQUALVERIFY, {});
}

void TestReverseBytes::op_reversebytes_tests()
{
    // Manual tests.
    std::vector<ReverseTestCase> test_cases({
        {{}, {}},
        {{99}, {99}},
        {{0xde, 0xad}, {0xad, 0xde}},
        {{0xde, 0xad, 0xa1}, {0xa1, 0xad, 0xde}},
        {{0xde, 0xad, 0xbe, 0xef}, {0xef, 0xbe, 0xad, 0xde}},
        {{0x12, 0x34, 0x56}, {0x56, 0x34, 0x12}},
    });

    // Generate some tests with various length strings.
    MMIXLinearCongruentialGenerator lcg;
    for (size_t step = 0; step < 4; ++step) {
        size_t datasize = std::min(step * 5, (size_t) MAX_SCRIPT_ELEMENT_SIZE);
        std::vector<uint8_t> data;
        data.reserve(datasize);
        for (size_t item = 0; item < datasize; ++item) {
            data.emplace_back(lcg.next() % 256);
        }
        test_cases.push_back({data, {data.rbegin(), data.rend()}});
    }

    // test them with and without the feature on.
    for (int i = 0; i < 1; i++) {
        uint32_t flags = i == 1 ? SCRIPT_ENABLE_OP_REVERSEBYTES : 0;

        // Empty stack.
        CheckErrorIfEnabled(flags, {}, CScript() << OP_REVERSEBYTES, SCRIPT_ERR_INVALID_STACK_OPERATION);

        for (const ReverseTestCase &test_case : test_cases) {
            CheckPassReverse(flags, test_case);
        }

        // Verify non-palindrome fails.
        CheckErrorIfEnabled(flags, {{0x01, 0x02, 0x03, 0x02, 0x02}},
                            CScript() << OP_DUP << OP_REVERSEBYTES << OP_EQUALVERIFY, SCRIPT_ERR_EQUALVERIFY);
    }
}
