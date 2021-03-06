/*
 * This file is part of the Flowee project
 * Copyright (C) 2015 The Bitcoin Core developers
 * Copyright (c) 2020 The Bitcoin developers
 * Copyright (C) 2017-2020 Tom Zander <tom@flowee.org>
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

#include "pow_tests.h"

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "test/test_bitcoin.h"

/* Test calculation of next difficulty target with no constraints applying */
void POWTests::get_next_work()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1261130161; // Block #30240
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1262152739;  // Block #32255
    pindexLast.nBits = 0x1d00ffff;
    QCOMPARE(Calculate2016NextWorkRequired(&pindexLast, nLastRetargetTime, params), (uint) 0x1d00d86a);
}

/* Test the constraint on the upper bound for next work */
void POWTests::get_next_work_pow_limit()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1231006505; // Block #0
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1233061996;  // Block #2015
    pindexLast.nBits = 0x1d00ffff;
    QCOMPARE(Calculate2016NextWorkRequired(&pindexLast, nLastRetargetTime, params), (uint) 0x1d00ffff);
}

/* Test the constraint on the lower bound for actual time taken */
void POWTests::get_next_work_lower_limit_actual()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1279008237; // Block #66528
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1279297671;  // Block #68543
    pindexLast.nBits = 0x1c05a3f4;
    QCOMPARE(Calculate2016NextWorkRequired(&pindexLast, nLastRetargetTime, params), (uint) 0x1c0168fd);
}

/* Test the constraint on the upper bound for actual time taken */
void POWTests::get_next_work_upper_limit_actual()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1269211443;  // Block #46367
    pindexLast.nBits = 0x1c387f6f;
    QCOMPARE(Calculate2016NextWorkRequired(&pindexLast, nLastRetargetTime, params), (uint) 0x1d00e1fd);
}

void POWTests::GetBlockProofEquivalentTime_test()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        QCOMPARE(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

static CBlockIndex GetBlockIndex(CBlockIndex *pindexPrev, int64_t nTimeInterval, uint32_t nBits) {
    CBlockIndex block;
    block.pprev = pindexPrev;
    block.nHeight = pindexPrev->nHeight + 1;
    block.nTime = pindexPrev->nTime + nTimeInterval;
    block.nBits = nBits;

    block.nChainWork = pindexPrev->nChainWork + GetBlockProof(block);
    return block;
}

void POWTests::retargeting_test()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params &params = Params().GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 currentPow = powLimit >> 1;
    uint32_t initialBits = currentPow.GetCompact();

    std::vector<CBlockIndex> blocks(1013);

    // Genesis block?
    blocks[0].SetNull();
    blocks[0].nHeight = 0;
    blocks[0].nTime = 1269211443;
    blocks[0].nBits = initialBits;
    blocks[0].nChainWork = GetBlockProof(blocks[0]);

    // Pile up some blocks.
    for (size_t i = 1; i < 100; i++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], params.nPowTargetSpacing, initialBits);
    }

    CBlockHeader blkHeaderDummy;

    // We start getting 2h blocks time. For the first 10 blocks, it doesn't
    // matter as the MTP is not affected. For the next 10 block, MTP difference
    // increases but stays below 12h.
    for (size_t i = 100; i < 110; i++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 2 * 3600, initialBits);
        QCOMPARE(CalculateNextWorkRequired(&blocks[i], &blkHeaderDummy, params), initialBits);
    }

    // Now we expect the difficulty to decrease.
    blocks[110] = GetBlockIndex(&blocks[109], 2 * 3600, initialBits);
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    QCOMPARE(CalculateNextWorkRequired(&blocks[110], &blkHeaderDummy, params),
        currentPow.GetCompact());

    // As we continue with 2h blocks, difficulty continue to decrease.
    blocks[111] =
        GetBlockIndex(&blocks[110], 2 * 3600, currentPow.GetCompact());
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    QCOMPARE(CalculateNextWorkRequired(&blocks[111], &blkHeaderDummy, params),
        currentPow.GetCompact());

    // We decrease again.
    blocks[112] =
        GetBlockIndex(&blocks[111], 2 * 3600, currentPow.GetCompact());
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    QCOMPARE(CalculateNextWorkRequired(&blocks[112], &blkHeaderDummy, params),
        currentPow.GetCompact());

    // We check that we do not go below the minimal difficulty.
    blocks[113] =
        GetBlockIndex(&blocks[112], 2 * 3600, currentPow.GetCompact());
    currentPow.SetCompact(currentPow.GetCompact());
    currentPow += (currentPow >> 2);
    QVERIFY(powLimit.GetCompact() != currentPow.GetCompact());
    QCOMPARE(CalculateNextWorkRequired(&blocks[113], &blkHeaderDummy, params),
        powLimit.GetCompact());

    // Once we reached the minimal difficulty, we stick with it.
    blocks[114] = GetBlockIndex(&blocks[113], 2 * 3600, powLimit.GetCompact());
    QVERIFY(powLimit.GetCompact() != currentPow.GetCompact());
    QCOMPARE(CalculateNextWorkRequired(&blocks[114], &blkHeaderDummy, params),
        powLimit.GetCompact());
}

void POWTests::cash_difficulty_test()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params &params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(3000);

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    uint32_t powLimitBits = powLimit.GetCompact();
    arith_uint256 currentPow = powLimit >> 4;
    uint32_t initialBits = currentPow.GetCompact();

    // Genesis block.
    blocks[0] = CBlockIndex();
    blocks[0].nHeight = 0;
    blocks[0].nTime = 1269211443;
    blocks[0].nBits = initialBits;

    blocks[0].nChainWork = GetBlockProof(blocks[0]);

    // Block counter.
    size_t i;

    // Pile up some blocks every 10 mins to establish some history.
    for (i = 1; i < 2050; i++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, initialBits);
    }

    CBlockHeader blkHeaderDummy;
    uint32_t nBits = CalculateNextCW144WorkRequired(&blocks[2049], &blkHeaderDummy, params);

    // Difficulty stays the same as long as we produce a block every 10 mins.
    for (size_t j = 0; j < 10; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, nBits);
        QCOMPARE(CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params), nBits);
    }

    // Make sure we skip over blocks that are out of wack. To do so, we produce
    // a block that is far in the future, and then produce a block with the
    // expected timestamp.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
    QCOMPARE(CalculateNextCW144WorkRequired(&blocks[i++], &blkHeaderDummy, params), nBits);
    blocks[i] = GetBlockIndex(&blocks[i - 1], 2 * 600 - 6000, nBits);
    QCOMPARE(CalculateNextCW144WorkRequired(&blocks[i++], &blkHeaderDummy, params), nBits);

    // The system should continue unaffected by the block with a bogous
    // timestamps.
    for (size_t j = 0; j < 20; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, nBits);
        QCOMPARE(CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params), nBits);
    }

    // We start emitting blocks slightly faster. The first block has no impact.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 550, nBits);
    QCOMPARE(CalculateNextCW144WorkRequired(&blocks[i++], &blkHeaderDummy, params), nBits);

    // Now we should see difficulty increase slowly.
    for (size_t j = 0; j < 10; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 550, nBits);
        const uint32_t nextBits = CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that difficulty increases very slowly.
        QVERIFY(nextTarget < currentTarget);
        QVERIFY((currentTarget - nextTarget) < (currentTarget >> 10));

        nBits = nextBits;
    }

    // Check the actual value.
    QCOMPARE(nBits, (uint) 0x1c0fe7b1);

    // If we dramatically shorten block production, difficulty increases faster.
    for (size_t j = 0; j < 20; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 10, nBits);
        const uint32_t nextBits = CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that difficulty increases faster.
        QVERIFY(nextTarget < currentTarget);
        QVERIFY((currentTarget - nextTarget) < (currentTarget >> 4));

        nBits = nextBits;
    }

    // Check the actual value.
    QCOMPARE(nBits, (uint) 0x1c0db19f);

    // We start to emit blocks significantly slower. The first block has no
    // impact.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
    nBits = CalculateNextCW144WorkRequired(&blocks[i++], &blkHeaderDummy, params);

    // Check the actual value.
    QCOMPARE(nBits, (uint) 0x1c0d9222);

    // If we dramatically slow down block production, difficulty decreases.
    for (size_t j = 0; j < 93; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
        const uint32_t nextBits = CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Check the difficulty decreases.
        QVERIFY(nextTarget <= powLimit);
        QVERIFY(nextTarget > currentTarget);
        QVERIFY((nextTarget - currentTarget) < (currentTarget >> 3));

        nBits = nextBits;
    }

    // Check the actual value.
    QCOMPARE(nBits, (uint) 0x1c2f13b9);

    // Due to the window of time being bounded, next block's difficulty actually
    // gets harder.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
    nBits = CalculateNextCW144WorkRequired(&blocks[i++], &blkHeaderDummy, params);
    QCOMPARE(nBits, (uint) 0x1c2ee9bf);

    // And goes down again. It takes a while due to the window being bounded and
    // the skewed block causes 2 blocks to get out of the window.
    for (size_t j = 0; j < 192; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
        const uint32_t nextBits = CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params);

        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Check the difficulty decreases.
        QVERIFY(nextTarget <= powLimit);
        QVERIFY(nextTarget > currentTarget);
        QVERIFY((nextTarget - currentTarget) < (currentTarget >> 3));

        nBits = nextBits;
    }

    // Check the actual value.
    QCOMPARE(nBits, (uint) 0x1d00ffff);

    // Once the difficulty reached the minimum allowed level, it doesn't get any
    // easier.
    for (size_t j = 0; j < 5; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 6000, nBits);
        const uint32_t nextBits = CalculateNextCW144WorkRequired(&blocks[i], &blkHeaderDummy, params);

        // Check the difficulty stays constant.
        QCOMPARE(nextBits, powLimitBits);
        nBits = nextBits;
    }
}

double POWTests::TargetFromBits(const uint32_t nBits) const
{
    return (nBits & 0xffffff) * pow(256, (nBits >> 24)-3);
}

double POWTests::GetASERTApproximationError(const CBlockIndex *pindexPrev,
        const uint32_t finalBits, const CBlockIndex *pindexReferenceBlock) const
{
    const int64_t nHeightDiff = pindexPrev->nHeight - pindexReferenceBlock->nHeight;
    const int64_t nTimeDiff   = pindexPrev->nTime   - pindexReferenceBlock->nTime;
    const uint32_t initialBits = pindexReferenceBlock->nBits;

    assert(nHeightDiff >= 0);
    double dInitialPow = TargetFromBits(initialBits);
    double dFinalPow   = TargetFromBits(finalBits);

    double dExponent = double(nTimeDiff - nHeightDiff * 600) / double(2*24*3600);
    double dTarget = dInitialPow * pow(2, dExponent);

    return (dFinalPow - dTarget) / dTarget;
}

void POWTests::asert_difficulty_test()
{
    SelectParams(CBaseChainParams::MAIN);

    std::vector<CBlockIndex> blocks(3000 + 2*24*3600);

    const Consensus::Params& params = Params().GetConsensus();
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 currentPow = powLimit >> 3;
    uint32_t initialBits = currentPow.GetCompact();
    double dMaxErr = 0.0001166792656486;

    // Genesis block, also ASERT reference block in this test case.
    blocks[0] = CBlockIndex();
    blocks[0].nHeight = 0;
    blocks[0].nTime = 1269211443;
    blocks[0].nBits = initialBits;

    blocks[0].nChainWork = GetBlockProof(blocks[0]);

    // Block counter.
    size_t i;

    // Pile up some blocks every 10 mins to establish some history.
    for (i = 1; i < 150; i++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, initialBits);
        QCOMPARE(blocks[i].nBits, initialBits);
    }

    CBlockHeader blkHeaderDummy;
    uint32_t nBits =
        CalculateNextASERTWorkRequired(&blocks[i - 1], &blkHeaderDummy, params, &blocks[1]);

    QCOMPARE(nBits, initialBits);

    // Difficulty stays the same as long as we produce a block every 10 mins.
    for (size_t j = 0; j < 10; i++, j++) {
        blocks[i] = GetBlockIndex(&blocks[i - 1], 600, nBits);
        QCOMPARE(
            CalculateNextASERTWorkRequired(&blocks[i], &blkHeaderDummy, params, &blocks[1]),
            nBits);
    }

    // If we add a two blocks whose solvetimes together add up to 1200s,
    // then the next block's target should be the same as the one before these blocks
    // (at this point, equal to initialBits).
    blocks[i] = GetBlockIndex(&blocks[i - 1], 300, nBits);
    nBits = CalculateNextASERTWorkRequired(&blocks[i++], &blkHeaderDummy, params, &blocks[1]);
    QVERIFY(fabs(GetASERTApproximationError(&blocks[i-1], nBits, &blocks[0])) < dMaxErr);
    blocks[i] = GetBlockIndex(&blocks[i - 1], 900, nBits);
    nBits = CalculateNextASERTWorkRequired(&blocks[i++], &blkHeaderDummy, params, &blocks[1]);
    QVERIFY(fabs(GetASERTApproximationError(&blocks[i-1], nBits, &blocks[1])) < dMaxErr);
    QCOMPARE(nBits, initialBits);
    QVERIFY(nBits != blocks[i-1].nBits);

    // Same in reverse - this time slower block first, followed by faster block.
    blocks[i] = GetBlockIndex(&blocks[i - 1], 900, nBits);
    nBits = CalculateNextASERTWorkRequired(&blocks[i++], &blkHeaderDummy, params, &blocks[1]);
    QVERIFY(fabs(GetASERTApproximationError(&blocks[i-1], nBits, &blocks[1])) < dMaxErr);
    blocks[i] = GetBlockIndex(&blocks[i - 1], 300, nBits);
    nBits = CalculateNextASERTWorkRequired(&blocks[i++], &blkHeaderDummy, params, &blocks[1]);
    QVERIFY(fabs(GetASERTApproximationError(&blocks[i-1], nBits, &blocks[1])) < dMaxErr);
    QCOMPARE(nBits, initialBits);
    QVERIFY(nBits != blocks[i-1].nBits);

    // Jumping forward 2 days should double the target (halve the difficulty)
    blocks[i] = GetBlockIndex(&blocks[i - 1], 600 + 2*24*3600, nBits);
    nBits = CalculateNextASERTWorkRequired(&blocks[i++], &blkHeaderDummy, params, &blocks[1]);
    QVERIFY(fabs(GetASERTApproximationError(&blocks[i-1], nBits, &blocks[1])) < dMaxErr);
    currentPow = arith_uint256().SetCompact(nBits) / 2;
    QCOMPARE(currentPow.GetCompact(), initialBits);

    // Iterate over the entire -2*24*3600..+2*24*3600 range to check that our integer approximation:
    //   1. Should be monotonic
    //   2. Should change target at least once every 8 seconds (worst-case: 15-bit precision on nBits)
    //   3. Should never change target by more than XXXX per 1-second step
    //   4. Never exceeds dMaxError in absolute error vs a double float calculation
    //   5. Has almost exactly the dMax and dMin errors we expect for the formula
    double dMin = 0;
    double dMax = 0;
    double dErr;
    double dMaxStep = 0;
    uint32_t nBitsRingBuffer[8];
    double dStep = 0;
    blocks[i] = GetBlockIndex(&blocks[i - 1], -2*24*3600 - 30, nBits);
    for (size_t j = 0; j < 4*24*3600 + 660; j++) {
        blocks[i].nTime++;
        nBits = CalculateNextASERTWorkRequired(&blocks[i], &blkHeaderDummy, params, &blocks[1]);

        if (j > 8) {
            // 1: Monotonic
            QVERIFY(arith_uint256().SetCompact(nBits) >= arith_uint256().SetCompact(nBitsRingBuffer[(j-1)%8]));
            // 2: Changes at least once every 8 seconds (worst case: nBits = 1d008000 to 1d008001)
            QVERIFY(arith_uint256().SetCompact(nBits) > arith_uint256().SetCompact(nBitsRingBuffer[j%8]));
            // 3: Check 1-sec step size
            dStep = (TargetFromBits(nBits) - TargetFromBits(nBitsRingBuffer[(j-1)%8])) / TargetFromBits(nBits);
            if (dStep > dMaxStep) dMaxStep = dStep;
            QVERIFY(dStep < 0.0000314812106363); // from nBits = 1d008000 to 1d008001
        }
        nBitsRingBuffer[j%8] = nBits;

        // 4 and 5: check error vs double precision float calculation
        dErr = GetASERTApproximationError(&blocks[i], nBits, &blocks[1]);
        if (dErr < dMin) dMin = dErr;
        if (dErr > dMax) dMax = dErr;

        auto failMsg = strprintf("solveTime: %d\tStep size: %.8f%%\tdErr: %.8f%%\tnBits: %0x\n",
                int64_t(blocks[i].nTime) - blocks[i-1].nTime, dStep*100, dErr*100, nBits);
        QVERIFY2(fabs(dErr) < dMaxErr, failMsg.c_str());
    }
    const auto failMsg = strprintf("Min error: %16.14f%%\tMax error: %16.14f%%\tMax step: %16.14f%%\n", dMin*100, dMax*100, dMaxStep*100);
    QVERIFY2(   dMin < -0.0001013168981059
                        && dMin > -0.0001013168981060
                        && dMax >  0.0001166792656485
                        && dMax <  0.0001166792656486,
                        failMsg.c_str());

    // Difficulty increases as long as we produce fast blocks
    for (size_t j = 0; j < 100; i++, j++) {
        uint32_t nextBits;
        arith_uint256 currentTarget;
        currentTarget.SetCompact(nBits);

        blocks[i] = GetBlockIndex(&blocks[i - 1], 500, nBits);
        nextBits = CalculateNextASERTWorkRequired(&blocks[i], &blkHeaderDummy, params, &blocks[1]);
        arith_uint256 nextTarget;
        nextTarget.SetCompact(nextBits);

        // Make sure that target is decreased
        QVERIFY(nextTarget <= currentTarget);

        nBits = nextBits;
    }
}

// Tests of the CalculateASERT function.
void POWTests::calculate_asert_test()
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    const int64_t nHalfLife = params.nASERTHalfLife;

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 initialTarget = powLimit >> 4;
    int64_t height = 0;

    // The CalculateASERT function uses the absolute ASERT formulation
    // and adds +1 to the height difference that it receives.
    // The time difference passed to it must factor in the difference
    // to the *parent* of the reference block.
    // We assume the parent is ideally spaced in time before the reference block.
    static const int64_t parent_time_diff = 600;

    // Steady
    arith_uint256 nextTarget = CalculateASERT(initialTarget, params.nPowTargetSpacing, parent_time_diff + 600 /* nTimeDiff */, ++height, powLimit, nHalfLife);
    QVERIFY(nextTarget == initialTarget);

    // A block that arrives in half the expected time
    nextTarget = CalculateASERT(initialTarget, params.nPowTargetSpacing, parent_time_diff + 600 + 300, ++height, powLimit, nHalfLife);
    QVERIFY(nextTarget < initialTarget);

    // A block that makes up for the shortfall of the previous one, restores the target to initial
    arith_uint256 prevTarget = nextTarget;
    nextTarget = CalculateASERT(initialTarget, params.nPowTargetSpacing, parent_time_diff + 600 + 300 + 900, ++height, powLimit, nHalfLife);
    QVERIFY(nextTarget > prevTarget);
    QVERIFY(nextTarget == initialTarget);

    // Two days ahead of schedule should halve the target (double the difficulty)
    prevTarget = nextTarget;
    nextTarget = CalculateASERT(prevTarget, params.nPowTargetSpacing, parent_time_diff + 288*1200, 288, powLimit, nHalfLife);
    QVERIFY(nextTarget == prevTarget * 2);

    // Two days behind schedule should double the target (halve the difficulty)
    prevTarget = nextTarget;
    nextTarget = CalculateASERT(prevTarget, params.nPowTargetSpacing, parent_time_diff + 288*0, 288, powLimit, nHalfLife);
    QVERIFY(nextTarget == prevTarget / 2);
    QVERIFY(nextTarget == initialTarget);

    // Ramp up from initialTarget to PowLimit - should only take 4 doublings...
    uint32_t powLimit_nBits = powLimit.GetCompact();
    uint32_t next_nBits;
    for (size_t k = 0; k < 3; k++) {
        prevTarget = nextTarget;
        nextTarget = CalculateASERT(prevTarget, params.nPowTargetSpacing, parent_time_diff + 288*1200, 288, powLimit, nHalfLife);
        QVERIFY(nextTarget == prevTarget * 2);
        QVERIFY(nextTarget < powLimit);
        next_nBits = nextTarget.GetCompact();
        QVERIFY(next_nBits != powLimit_nBits);
    }

    prevTarget = nextTarget;
    nextTarget = CalculateASERT(prevTarget, params.nPowTargetSpacing, parent_time_diff + 288*1200, 288, powLimit, nHalfLife);
    next_nBits = nextTarget.GetCompact();
    QVERIFY(nextTarget == prevTarget * 2);
    QVERIFY(next_nBits == powLimit_nBits);

    // Fast periods now cannot increase target beyond POW limit, even if we try to overflow nextTarget.
    // prevTarget is a uint256, so 256*2 = 512 days would overflow nextTarget unless CalculateASERT
    // correctly detects this error
    nextTarget = CalculateASERT(prevTarget, params.nPowTargetSpacing, parent_time_diff + 512*144*600, 0, powLimit, nHalfLife);
    next_nBits = nextTarget.GetCompact();
    QVERIFY(next_nBits == powLimit_nBits);

    // We also need to watch for underflows on nextTarget. We need to withstand an extra ~446 days worth of blocks.
    // This should bring down a powLimit target to the a minimum target of 1.
    nextTarget = CalculateASERT(powLimit, params.nPowTargetSpacing, 0, 2*(256-33)*144, powLimit, nHalfLife);
    next_nBits = nextTarget.GetCompact();
    QCOMPARE(next_nBits, arith_uint256(1).GetCompact());

    // Define a structure holding parameters to pass to CalculateASERT.
    // We are going to check some expected results  against a vector of
    // possible arguments.
    struct calc_params {
        arith_uint256 refTarget;
        int64_t targetSpacing;
        int64_t timeDiff;
        int64_t heightDiff;
        arith_uint256 expectedTarget;
        uint32_t expectednBits;
    };

    // Define some named input argument values
    const arith_uint256 SINGLE_300_TARGET { "00000000ffb1ffffffffffffffffffffffffffffffffffffffffffffffffffff" };
    const arith_uint256 FUNNY_REF_TARGET { "000000008000000000000000000fffffffffffffffffffffffffffffffffffff" };

    // Define our expected input and output values.
    // The timeDiff entries exclude the `parent_time_diff` - this is
    // added in the call to CalculateASERT in the test loop.
    const std::vector<calc_params> calculate_args = {

        /* refTarget, targetSpacing, timeDiff, heightDiff, expectedTarget, expectednBits */

        { powLimit, 600, 0, 2*144, powLimit >> 1, 0x1c7fffff },
        { powLimit, 600, 0, 4*144, powLimit >> 2, 0x1c3fffff },
        { powLimit >> 1, 600, 0, 2*144, powLimit >> 2, 0x1c3fffff },
        { powLimit >> 2, 600, 0, 2*144, powLimit >> 3, 0x1c1fffff },
        { powLimit >> 3, 600, 0, 2*144, powLimit >> 4, 0x1c0fffff },
        { powLimit, 600, 0, 2*(256-34)*144, 3, 0x01030000 },
        { powLimit, 600, 0, 2*(256-34)*144 + 119, 3, 0x01030000 },
        { powLimit, 600, 0, 2*(256-34)*144 + 120, 2, 0x01020000 },
        { powLimit, 600, 0, 2*(256-33)*144-1, 2, 0x01020000 },
        { powLimit, 600, 0, 2*(256-33)*144, 1, 0x01010000 },  // 1 bit less since we do not need to shift to 0
        { powLimit, 600, 0, 2*(256-32)*144, 1, 0x01010000 },  // more will not decrease below 1
        { 1, 600, 0, 2*(256-32)*144, 1, 0x01010000 },
        { powLimit, 600, 2*(512-32)*144, 0, powLimit, powLimit_nBits },
        { 1, 600, (512-64)*144*600, 0, powLimit, powLimit_nBits },
        { powLimit, 600, 300, 1, SINGLE_300_TARGET, 0x1d00ffb1 },  // clamps to powLimit
        { FUNNY_REF_TARGET, 600, 600*2*33*144, 0, powLimit, powLimit_nBits }, // confuses any attempt to detect overflow by inspecting result
    };

    for (auto &v : calculate_args) {
        nextTarget = CalculateASERT(v.refTarget, v.targetSpacing, parent_time_diff + v.timeDiff, v.heightDiff, powLimit, nHalfLife);
        next_nBits = nextTarget.GetCompact();
        //  const auto failMsg =
        //      StrPrintCalcArgs(v.refTarget, v.targetSpacing, parent_time_diff + v.timeDiff, v.heightDiff, v.expectedTarget, v.expectednBits)
        //      + strprintf("nextTarget=  %s\nnext nBits=  0x%08x\n", nextTarget.ToString(), next_nBits);
        QVERIFY(nextTarget == v.expectedTarget && next_nBits == v.expectednBits);
    }
}
