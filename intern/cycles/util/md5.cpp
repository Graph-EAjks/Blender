/* SPDX-FileCopyrightText: 1999, 2002 Aladdin Enterprises. All rights reserved.
 *
 * SPDX-License-Identifier: Zlib
 *
 * By `L. Peter Deutsch <ghost@aladdin.com>`. */

/* Minor modifications done to remove some code and change style. */

#include "util/md5.h"
#include "util/log.h"
#include "util/path.h"

#include <cstdio>
#include <cstring>

CCL_NAMESPACE_BEGIN

// NOLINTBEGIN
#define T_MASK ((uint32_t)~0)
#define T1 /* 0xd76aa478 */ (T_MASK ^ 0x28955b87)
#define T2 /* 0xe8c7b756 */ (T_MASK ^ 0x173848a9)
#define T3 0x242070db
#define T4 /* 0xc1bdceee */ (T_MASK ^ 0x3e423111)
#define T5 /* 0xf57c0faf */ (T_MASK ^ 0x0a83f050)
#define T6 0x4787c62a
#define T7 /* 0xa8304613 */ (T_MASK ^ 0x57cfb9ec)
#define T8 /* 0xfd469501 */ (T_MASK ^ 0x02b96afe)
#define T9 0x698098d8
#define T10 /* 0x8b44f7af */ (T_MASK ^ 0x74bb0850)
#define T11 /* 0xffff5bb1 */ (T_MASK ^ 0x0000a44e)
#define T12 /* 0x895cd7be */ (T_MASK ^ 0x76a32841)
#define T13 0x6b901122
#define T14 /* 0xfd987193 */ (T_MASK ^ 0x02678e6c)
#define T15 /* 0xa679438e */ (T_MASK ^ 0x5986bc71)
#define T16 0x49b40821
#define T17 /* 0xf61e2562 */ (T_MASK ^ 0x09e1da9d)
#define T18 /* 0xc040b340 */ (T_MASK ^ 0x3fbf4cbf)
#define T19 0x265e5a51
#define T20 /* 0xe9b6c7aa */ (T_MASK ^ 0x16493855)
#define T21 /* 0xd62f105d */ (T_MASK ^ 0x29d0efa2)
#define T22 0x02441453
#define T23 /* 0xd8a1e681 */ (T_MASK ^ 0x275e197e)
#define T24 /* 0xe7d3fbc8 */ (T_MASK ^ 0x182c0437)
#define T25 0x21e1cde6
#define T26 /* 0xc33707d6 */ (T_MASK ^ 0x3cc8f829)
#define T27 /* 0xf4d50d87 */ (T_MASK ^ 0x0b2af278)
#define T28 0x455a14ed
#define T29 /* 0xa9e3e905 */ (T_MASK ^ 0x561c16fa)
#define T30 /* 0xfcefa3f8 */ (T_MASK ^ 0x03105c07)
#define T31 0x676f02d9
#define T32 /* 0x8d2a4c8a */ (T_MASK ^ 0x72d5b375)
#define T33 /* 0xfffa3942 */ (T_MASK ^ 0x0005c6bd)
#define T34 /* 0x8771f681 */ (T_MASK ^ 0x788e097e)
#define T35 0x6d9d6122
#define T36 /* 0xfde5380c */ (T_MASK ^ 0x021ac7f3)
#define T37 /* 0xa4beea44 */ (T_MASK ^ 0x5b4115bb)
#define T38 0x4bdecfa9
#define T39 /* 0xf6bb4b60 */ (T_MASK ^ 0x0944b49f)
#define T40 /* 0xbebfbc70 */ (T_MASK ^ 0x4140438f)
#define T41 0x289b7ec6
#define T42 /* 0xeaa127fa */ (T_MASK ^ 0x155ed805)
#define T43 /* 0xd4ef3085 */ (T_MASK ^ 0x2b10cf7a)
#define T44 0x04881d05
#define T45 /* 0xd9d4d039 */ (T_MASK ^ 0x262b2fc6)
#define T46 /* 0xe6db99e5 */ (T_MASK ^ 0x1924661a)
#define T47 0x1fa27cf8
#define T48 /* 0xc4ac5665 */ (T_MASK ^ 0x3b53a99a)
#define T49 /* 0xf4292244 */ (T_MASK ^ 0x0bd6ddbb)
#define T50 0x432aff97
#define T51 /* 0xab9423a7 */ (T_MASK ^ 0x546bdc58)
#define T52 /* 0xfc93a039 */ (T_MASK ^ 0x036c5fc6)
#define T53 0x655b59c3
#define T54 /* 0x8f0ccc92 */ (T_MASK ^ 0x70f3336d)
#define T55 /* 0xffeff47d */ (T_MASK ^ 0x00100b82)
#define T56 /* 0x85845dd1 */ (T_MASK ^ 0x7a7ba22e)
#define T57 0x6fa87e4f
#define T58 /* 0xfe2ce6e0 */ (T_MASK ^ 0x01d3191f)
#define T59 /* 0xa3014314 */ (T_MASK ^ 0x5cfebceb)
#define T60 0x4e0811a1
#define T61 /* 0xf7537e82 */ (T_MASK ^ 0x08ac817d)
#define T62 /* 0xbd3af235 */ (T_MASK ^ 0x42c50dca)
#define T63 0x2ad7d2bb
#define T64 /* 0xeb86d391 */ (T_MASK ^ 0x14792c6e)
// NOLINTEND

void MD5Hash::process(const uint8_t *data /*[64]*/)
{
  uint32_t a = abcd[0];
  uint32_t b = abcd[1];
  uint32_t c = abcd[2];
  uint32_t d = abcd[3];
  uint32_t t;
  /* Define storage for little-endian or both types of CPUs. */
  uint32_t xbuf[16];
  const uint32_t *X;

  {
    /*
     * Determine dynamically whether this is a big-endian or
     * little-endian machine, since we can use a more efficient
     * algorithm on the latter.
     */
    static const int w = 1;

    if (*((const uint8_t *)&w)) /* dynamic little-endian */ {
      /*
       * On little-endian machines, we can process properly aligned
       * data without copying it.
       */
      if (!((data - (const uint8_t *)nullptr) & 3)) {
        /* data are properly aligned */
        X = (const uint32_t *)data;
      }
      else {
        /* not aligned */
        memcpy(xbuf, data, 64);
        X = xbuf;
      }
    }
    else { /* dynamic big-endian */
      /*
       * On big-endian machines, we must arrange the bytes in the
       * right order.
       */
      const uint8_t *xp = data;
      int i;

      X = xbuf; /* (dynamic only) */
      for (i = 0; i < 16; ++i, xp += 4) {
        xbuf[i] = xp[0] + (xp[1] << 8) + (xp[2] << 16) + (xp[3] << 24);
      }
    }
  }

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

  /* Round 1. */
  /* Let [abcd k s i] denote the operation
   * a = b + ((a + F(b,c,d) + X[k] + T[i]) <<< s). */
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define SET(a, b, c, d, k, s, Ti) \
  t = a + F(b, c, d) + X[k] + Ti; \
  a = ROTATE_LEFT(t, s) + b
  /* Do the following 16 operations. */
  SET(a, b, c, d, 0, 7, T1);
  SET(d, a, b, c, 1, 12, T2);
  SET(c, d, a, b, 2, 17, T3);
  SET(b, c, d, a, 3, 22, T4);
  SET(a, b, c, d, 4, 7, T5);
  SET(d, a, b, c, 5, 12, T6);
  SET(c, d, a, b, 6, 17, T7);
  SET(b, c, d, a, 7, 22, T8);
  SET(a, b, c, d, 8, 7, T9);
  SET(d, a, b, c, 9, 12, T10);
  SET(c, d, a, b, 10, 17, T11);
  SET(b, c, d, a, 11, 22, T12);
  SET(a, b, c, d, 12, 7, T13);
  SET(d, a, b, c, 13, 12, T14);
  SET(c, d, a, b, 14, 17, T15);
  SET(b, c, d, a, 15, 22, T16);
#undef SET

  /* Round 2. */
  /* Let [abcd k s i] denote the operation
   * a = b + ((a + G(b,c,d) + X[k] + T[i]) <<< s). */
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define SET(a, b, c, d, k, s, Ti) \
  t = a + G(b, c, d) + X[k] + Ti; \
  a = ROTATE_LEFT(t, s) + b
  /* Do the following 16 operations. */
  SET(a, b, c, d, 1, 5, T17);
  SET(d, a, b, c, 6, 9, T18);
  SET(c, d, a, b, 11, 14, T19);
  SET(b, c, d, a, 0, 20, T20);
  SET(a, b, c, d, 5, 5, T21);
  SET(d, a, b, c, 10, 9, T22);
  SET(c, d, a, b, 15, 14, T23);
  SET(b, c, d, a, 4, 20, T24);
  SET(a, b, c, d, 9, 5, T25);
  SET(d, a, b, c, 14, 9, T26);
  SET(c, d, a, b, 3, 14, T27);
  SET(b, c, d, a, 8, 20, T28);
  SET(a, b, c, d, 13, 5, T29);
  SET(d, a, b, c, 2, 9, T30);
  SET(c, d, a, b, 7, 14, T31);
  SET(b, c, d, a, 12, 20, T32);
#undef SET

  /* Round 3. */
  /* Let [abcd k s t] denote the operation
   * a = b + ((a + H(b,c,d) + X[k] + T[i]) <<< s). */
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define SET(a, b, c, d, k, s, Ti) \
  t = a + H(b, c, d) + X[k] + Ti; \
  a = ROTATE_LEFT(t, s) + b
  /* Do the following 16 operations. */
  SET(a, b, c, d, 5, 4, T33);
  SET(d, a, b, c, 8, 11, T34);
  SET(c, d, a, b, 11, 16, T35);
  SET(b, c, d, a, 14, 23, T36);
  SET(a, b, c, d, 1, 4, T37);
  SET(d, a, b, c, 4, 11, T38);
  SET(c, d, a, b, 7, 16, T39);
  SET(b, c, d, a, 10, 23, T40);
  SET(a, b, c, d, 13, 4, T41);
  SET(d, a, b, c, 0, 11, T42);
  SET(c, d, a, b, 3, 16, T43);
  SET(b, c, d, a, 6, 23, T44);
  SET(a, b, c, d, 9, 4, T45);
  SET(d, a, b, c, 12, 11, T46);
  SET(c, d, a, b, 15, 16, T47);
  SET(b, c, d, a, 2, 23, T48);
#undef SET

  /* Round 4. */
  /* Let [abcd k s t] denote the operation
   * a = b + ((a + I(b,c,d) + X[k] + T[i]) <<< s). */
#define I(x, y, z) ((y) ^ ((x) | ~(z)))
#define SET(a, b, c, d, k, s, Ti) \
  t = a + I(b, c, d) + X[k] + Ti; \
  a = ROTATE_LEFT(t, s) + b
  /* Do the following 16 operations. */
  SET(a, b, c, d, 0, 6, T49);
  SET(d, a, b, c, 7, 10, T50);
  SET(c, d, a, b, 14, 15, T51);
  SET(b, c, d, a, 5, 21, T52);
  SET(a, b, c, d, 12, 6, T53);
  SET(d, a, b, c, 3, 10, T54);
  SET(c, d, a, b, 10, 15, T55);
  SET(b, c, d, a, 1, 21, T56);
  SET(a, b, c, d, 8, 6, T57);
  SET(d, a, b, c, 15, 10, T58);
  SET(c, d, a, b, 6, 15, T59);
  SET(b, c, d, a, 13, 21, T60);
  SET(a, b, c, d, 4, 6, T61);
  SET(d, a, b, c, 11, 10, T62);
  SET(c, d, a, b, 2, 15, T63);
  SET(b, c, d, a, 9, 21, T64);
#undef SET

  /* Then perform the following additions. (That is increment each
   * of the four registers by the value it had before this block
   * was started.) */
  abcd[0] += a;
  abcd[1] += b;
  abcd[2] += c;
  abcd[3] += d;
}

MD5Hash::MD5Hash()
{
  count[0] = count[1] = 0;
  abcd[0] = 0x67452301;
  abcd[1] = /*0xefcdab89*/ T_MASK ^ 0x10325476;
  abcd[2] = /*0x98badcfe*/ T_MASK ^ 0x67452301;
  abcd[3] = 0x10325476;
}

MD5Hash::~MD5Hash() = default;

void MD5Hash::append(const uint8_t *data, const int nbytes)
{
  const uint8_t *p = data;
  int left = nbytes;
  const int offset = (count[0] >> 3) & 63;
  const uint32_t nbits = (uint32_t)(nbytes << 3);

  if (nbytes <= 0) {
    return;
  }

  /* Update the message length. */
  count[1] += nbytes >> 29;
  count[0] += nbits;
  if (count[0] < nbits) {
    count[1]++;
  }

  /* Process an initial partial block. */
  if (offset) {
    const int copy = (offset + nbytes > 64 ? 64 - offset : nbytes);

    memcpy(buf + offset, p, copy);
    if (offset + copy < 64) {
      return;
    }
    p += copy;
    left -= copy;
    process(buf);
  }

  /* Process full blocks. */
  for (; left >= 64; p += 64, left -= 64) {
    process(p);
  }

  /* Process a final partial block. */
  if (left) {
    memcpy(buf, p, left);
  }
}

void MD5Hash::append(const string &str)
{
  if (!str.empty()) {
    append((const uint8_t *)str.c_str(), str.size());
  }
}

bool MD5Hash::append_file(const string &filepath)
{
  FILE *f = path_fopen(filepath, "rb");

  if (!f) {
    LOG_ERROR << "MD5: failed to open file " << filepath;
    return false;
  }

  const size_t buffer_size = 1024;
  uint8_t buffer[buffer_size];
  size_t n;

  do {
    n = fread(buffer, 1, buffer_size, f);
    append(buffer, n);
  } while (n == buffer_size);

  const bool success = (ferror(f) == 0);

  fclose(f);

  return success;
}

void MD5Hash::finish(uint8_t digest[16])
{
  static const uint8_t pad[64] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  uint8_t data[8];
  int i;

  /* Save the length before padding. */
  for (i = 0; i < 8; ++i) {
    data[i] = (uint8_t)(count[i >> 2] >> ((i & 3) << 3));
  }

  /* Pad to 56 bytes mod 64. */
  append(pad, ((55 - (count[0] >> 3)) & 63) + 1);
  /* Append the length. */
  append(data, 8);

  for (i = 0; i < 16; ++i) {
    digest[i] = (uint8_t)(abcd[i >> 2] >> ((i & 3) << 3));
  }
}

string MD5Hash::get_hex()
{
  constexpr char kHexDigits[] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  uint8_t digest[16];
  char buf[16 * 2 + 1];

  finish(digest);

  for (int i = 0; i < 16; i++) {
    buf[i * 2 + 0] = kHexDigits[digest[i] / 0x10];
    buf[i * 2 + 1] = kHexDigits[digest[i] % 0x10];
  }
  buf[sizeof(buf) - 1] = '\0';

  return string(buf);
}

string util_md5_string(const string &str)
{
  MD5Hash md5;
  md5.append((uint8_t *)str.c_str(), str.size());
  return md5.get_hex();
}

CCL_NAMESPACE_END
