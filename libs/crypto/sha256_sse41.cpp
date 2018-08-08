/*
 * This file is part of the Flowee project
 * Copyright (C) 2017-2018 The Bitcoin Core developers
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

#ifdef ENABLE_SSE41

#include <cstdint>
#include <immintrin.h>

#include "common.h"

namespace sha256_sse41 {

/*
* The implementation in this namespace is a conversion to intrinsics from a
* NASM implementation by Intel, found at
* https://github.com/intel/intel-ipsec-mb/blob/master/sse/sha256_one_block_sse.asm
*
* Its original copyright text:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Copyright (c) 2012, Intel Corporation
;
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are
; met:
;
; * Redistributions of source code must retain the above copyright
;   notice, this list of conditions and the following disclaimer.
;
; * Redistributions in binary form must reproduce the above copyright
;   notice, this list of conditions and the following disclaimer in the
;   documentation and/or other materials provided with the
;   distribution.
;
; * Neither the name of the Intel Corporation nor the names of its
;   contributors may be used to endorse or promote products derived from
;   this software without specific prior written permission.
;
; THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION "AS IS" AND ANY
; EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
; PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
; CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
; EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
; PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
; PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
; LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
; NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
; SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

namespace {

uint32_t inline __attribute__((always_inline)) Ror(uint32_t x, int val) { return ((x >> val) | (x << (32 - val))); }

/** Compute one round of SHA256. */
void inline __attribute__((always_inline)) Round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d, uint32_t& e, uint32_t& f, uint32_t& g, uint32_t& h, uint32_t w)
{
   uint32_t t0, t1, t2;
   t0 = Ror(e, 25 - 11) ^ e;
   t1 = Ror(a, 22 - 13) ^ a;
   t2 = (f ^ g) & e;
   t0 = Ror(t0, 11 - 6) ^ e;
   t1 = Ror(t1, 13 - 2) ^ a;
   t0 = Ror(t0, 6);
   t2 = (t2 ^ g) + t0 + w;
   t1 = Ror(t1, 2);
   h += t2;
   t0 = (a | c) & b;
   d += h;
   h += t1;
   t0 |= (a & c);
   h += t0;
}

/** Compute 4 rounds of SHA256, while simultaneously computing the expansion for
*  16 rounds later.
*
*  Input: a,b,c,d,e,f,g,h: The state variables to update with 4 rounds
*         x0,x1,x2,x3:     4 128-bit variables containing expansions.
*                          If the current round is r, x0,x1,x2,x3 contain the
*                          expansions for rounds r..r+15. x0 will be updated
*                          to have the expansions for round r+16..r+19.
*         W:               The round constants for r..r+3.
*/
void inline __attribute__((always_inline)) QuadRoundSched(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d, uint32_t& e, uint32_t& f, uint32_t& g, uint32_t& h, __m128i& x0, __m128i x1, __m128i x2, __m128i x3, __m128i w)
{
   alignas(__m128i) uint32_t w32[4];
   __m128i t0, t1, t2, t3, t4;

   w = _mm_add_epi32(w, x0);
   _mm_store_si128((__m128i*)w32, w);

   Round(a, b, c, d, e, f, g, h, w32[0]);
   t0 = _mm_add_epi32(_mm_alignr_epi8(x3, x2, 4), x0);
   t3 = t2 = t1 = _mm_alignr_epi8(x1, x0, 4);
   t2 = _mm_srli_epi32(t2, 7);
   t1 = _mm_or_si128(_mm_slli_epi32(t1, 32 - 7), t2);

   Round(h, a, b, c, d, e, f, g, w32[1]);
   t4 = t2 = t3;
   t3 = _mm_slli_epi32(t3, 32 - 18);
   t2 = _mm_srli_epi32(t2, 18);
   t1 = _mm_xor_si128(t1, t3);
   t4 = _mm_srli_epi32(t4, 3);
   t1 = _mm_xor_si128(_mm_xor_si128(t1, t2), t4);
   t2 = _mm_shuffle_epi32(x3, 0xFA);
   t0 = _mm_add_epi32(t0, t1);

   Round(g, h, a, b, c, d, e, f, w32[2]);
   t4 = t3 = t2;
   t2 = _mm_srli_epi64(t2, 17);
   t3 = _mm_srli_epi64(t3, 19);
   t4 = _mm_srli_epi32(t4, 10);
   t2 = _mm_xor_si128(t2, t3);
   t4 = _mm_shuffle_epi8(_mm_xor_si128(t4, t2), _mm_set_epi64x(0xFFFFFFFFFFFFFFFFULL, 0x0b0a090803020100ULL));
   t0 = _mm_add_epi32(t0, t4);
   t2 = _mm_shuffle_epi32(t0, 0x50);

   Round(f, g, h, a, b, c, d, e, w32[3]);
   x0 = t3 = t2;
   t2 = _mm_srli_epi64(t2, 17);
   t3 = _mm_srli_epi64(t3, 19);
   x0 = _mm_srli_epi32(x0, 10);
   t2 = _mm_xor_si128(t2, t3);
   x0 = _mm_add_epi32(_mm_shuffle_epi8(_mm_xor_si128(x0, t2), _mm_set_epi64x(0x0b0a090803020100ULL, 0xFFFFFFFFFFFFFFFFULL)), t0);
}

void inline __attribute__((always_inline)) QuadRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d, uint32_t& e, uint32_t& f, uint32_t& g, uint32_t& h, __m128i x0, __m128i w)
{
   alignas(__m128i) uint32_t w32[32];

   x0 = _mm_add_epi32(x0, w);
   _mm_store_si128((__m128i*)w32, x0);

   Round(a, b, c, d, e, f, g, h, w32[0]);
   Round(h, a, b, c, d, e, f, g, w32[1]);
   Round(g, h, a, b, c, d, e, f, w32[2]);
   Round(f, g, h, a, b, c, d, e, w32[3]);
}

__m128i inline __attribute__((always_inline)) KK(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
   return _mm_set_epi32(d, c, b, a);
}

}

void Transform(uint32_t* s, const unsigned char* chunk, size_t blocks)
{
   const unsigned char* end = chunk + blocks * 64;
   static const __m128i BYTE_FLIP_MASK = _mm_set_epi64x(0x0c0d0e0f08090a0b, 0x0405060700010203);

   static const __m128i TBL[16] = {
       KK(0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5),
       KK(0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5),
       KK(0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3),
       KK(0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174),
       KK(0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc),
       KK(0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da),
       KK(0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7),
       KK(0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967),
       KK(0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13),
       KK(0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85),
       KK(0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3),
       KK(0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070),
       KK(0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5),
       KK(0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3),
       KK(0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208),
       KK(0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2),
   };

   uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4], f = s[5], g = s[6], h = s[7];

   __m128i x0, x1, x2, x3;

   while (chunk != end) {
       x0 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)chunk), BYTE_FLIP_MASK);
       x1 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(chunk + 16)), BYTE_FLIP_MASK);
       x2 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(chunk + 32)), BYTE_FLIP_MASK);
       x3 = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(chunk + 48)), BYTE_FLIP_MASK);

       QuadRoundSched(a, b, c, d, e, f, g, h, x0, x1, x2, x3, TBL[0]);
       QuadRoundSched(e, f, g, h, a, b, c, d, x1, x2, x3, x0, TBL[1]);
       QuadRoundSched(a, b, c, d, e, f, g, h, x2, x3, x0, x1, TBL[2]);
       QuadRoundSched(e, f, g, h, a, b, c, d, x3, x0, x1, x2, TBL[3]);
       QuadRoundSched(a, b, c, d, e, f, g, h, x0, x1, x2, x3, TBL[4]);
       QuadRoundSched(e, f, g, h, a, b, c, d, x1, x2, x3, x0, TBL[5]);
       QuadRoundSched(a, b, c, d, e, f, g, h, x2, x3, x0, x1, TBL[6]);
       QuadRoundSched(e, f, g, h, a, b, c, d, x3, x0, x1, x2, TBL[7]);
       QuadRoundSched(a, b, c, d, e, f, g, h, x0, x1, x2, x3, TBL[8]);
       QuadRoundSched(e, f, g, h, a, b, c, d, x1, x2, x3, x0, TBL[9]);
       QuadRoundSched(a, b, c, d, e, f, g, h, x2, x3, x0, x1, TBL[10]);
       QuadRoundSched(e, f, g, h, a, b, c, d, x3, x0, x1, x2, TBL[11]);
       QuadRound(a, b, c, d, e, f, g, h, x0, TBL[12]);
       QuadRound(e, f, g, h, a, b, c, d, x1, TBL[13]);
       QuadRound(a, b, c, d, e, f, g, h, x2, TBL[14]);
       QuadRound(e, f, g, h, a, b, c, d, x3, TBL[15]);

       a += s[0]; s[0] = a;
       b += s[1]; s[1] = b;
       c += s[2]; s[2] = c;
       d += s[3]; s[3] = d;
       e += s[4]; s[4] = e;
       f += s[5]; s[5] = f;
       g += s[6]; s[6] = g;
       h += s[7]; s[7] = h;

       chunk += 64;
   }
}
}

#endif
