/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup bli
 *
 * \section mathabbrev Abbreviations
 *
 * - `fl` = `float`.
 * - `db` = `double`.
 * - `v2` = `vec2` = vector 2.
 * - `v3` = `vec3` = vector 3.
 * - `v4` = `vec4` = vector 4.
 * - `vn` = `vec4q` = vector N dimensions, *passed as an arg, after the vector*..
 * - `qt` = `quat` = quaternion.
 * - `dq` = `dquat` = dual quaternion.
 * - `m2` = `mat2` = matrix 2x2.
 * - `m3` = `mat3` = matrix 3x3.
 * - `m4` = `mat4` = matrix 4x4.
 * - `eul` = `euler` rotation.
 * - `eulO` = `euler` with order.
 * - `plane` = `plane 4`, (vec3, distance).
 * - `plane3` = `plane 3`, (same as a `plane` with a zero 4th component).
 *
 * \subsection mathabbrev_all Function Type Abbreviations
 *
 * For non float versions of functions (which typically operate on floats),
 * use single suffix abbreviations.
 *
 * - `_d` = double
 * - `_i` = int
 * - `_u` = unsigned int
 * - `_char` = char
 * - `_uchar` = unsigned char
 *
 * \section mathvarnames Variable Names
 *
 * - f = single value
 * - a, b, c = vectors
 * - r = result vector
 * - A, B, C = matrices
 * - R = result matrix
 */

#include "BLI_assert.h"
#include "BLI_math_constants.h"  // IWYU pragma: export
#include "BLI_math_inline.h"     // IWYU pragma: export
#include "BLI_sys_types.h"

#if defined(__GNUC__)
#  define NAN_FLT __builtin_nanf("")
#else /* evil quiet NaN definition */
static const int NAN_INT = 0x7FC00000;
#  define NAN_FLT (*((float *)(&NAN_INT)))
#endif

#if BLI_MATH_DO_INLINE
#  include "intern/math_base_inline.cc"  // IWYU pragma: export
#endif

#ifdef BLI_MATH_GCC_WARN_PRAGMA
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wredundant-decls"
#endif

/******************************* Float ******************************/

/* `powf` is really slow for raising to integer powers. */

MINLINE float pow2f(float x);
MINLINE float pow3f(float x);
MINLINE float pow4f(float x);
MINLINE float pow7f(float x);

MINLINE float sqrt3f(float f);
MINLINE double sqrt3d(double d);

MINLINE float sqrtf_signed(float f);

/* Compute linear interpolation (lerp) between origin and target. */
MINLINE float interpf(float target, float origin, float t);
MINLINE double interpd(double target, double origin, double t);

MINLINE float ratiof(float min, float max, float pos);
MINLINE double ratiod(double min, double max, double pos);

/* NOTE: Compilers will up-cast all types smaller than int to int when performing arithmetic
 * operation. */

MINLINE int square_s(short a);

MINLINE int square_i(int a);
MINLINE unsigned int square_uint(unsigned int a);
MINLINE float square_f(float a);

MINLINE int cube_i(int a);
MINLINE float cube_f(float a);

MINLINE float min_ff(float a, float b);
MINLINE float max_ff(float a, float b);
MINLINE float min_fff(float a, float b, float c);
MINLINE float max_fff(float a, float b, float c);
MINLINE float min_ffff(float a, float b, float c, float d);
MINLINE float max_ffff(float a, float b, float c, float d);

MINLINE double min_dd(double a, double b);
MINLINE double max_dd(double a, double b);
MINLINE double max_ddd(double a, double b, double c);

MINLINE int min_ii(int a, int b);
MINLINE int max_ii(int a, int b);
MINLINE int min_iii(int a, int b, int c);
MINLINE int max_iii(int a, int b, int c);
MINLINE int min_iiii(int a, int b, int c, int d);
MINLINE int max_iiii(int a, int b, int c, int d);

MINLINE uint min_uu(uint a, uint b);
MINLINE uint max_uu(uint a, uint b);

MINLINE int clamp_i(int value, int min, int max);
MINLINE float clamp_f(float value, float min, float max);

/**
 * Almost-equal for IEEE floats, using absolute difference method.
 *
 * \param max_diff: the maximum absolute difference.
 */
MINLINE int compare_ff(float a, float b, float max_diff);
/**
 * Computes the distance between two floats in ulps.
 *
 * In other words, returns zero if the floats are exactly equal, and
 * otherwise returns 1 plus the number of (unique) representable floats
 * between `a` and `b` on the number line.
 *
 * Notes:
 * - The order of `a` and `b` doesn't matter.  The returned value is the
 *   absolute difference.
 * - Unlike many ulp difference functions, this function handles the
 *   difference between positive and negative floats in a meaningful way.
 *   It returns the number (plus 1) of representable floats between those
 *   two values as they would be arranged on a number line.
 * - Zero and negative zero are *not* considered unique from each other.
 *   They are counted together as a single float in the difference.
 * - NaNs are not handled meaningfully.  If either number is NaN, this
 *   function returns uint max (0xffffffff).
 */
MINLINE uint ulp_diff_ff(float a, float b);
/**
 * Almost-equal for IEEE floats, using their integer representation
 * (mixing ULP and absolute difference methods).
 *
 * \param max_diff: is the maximum absolute difference (allows to take care of the near-zero area,
 * where relative difference methods cannot really work).
 * \param max_ulps: is the 'maximum number of floats + 1'
 * allowed between \a a and \a b to consider them equal.
 *
 * \see https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
 */
MINLINE int compare_ff_relative(float a, float b, float max_diff, int max_ulps);
MINLINE bool compare_threshold_relative(float value1, float value2, float thresh);

/**
 * Increment the given float to the next representable floating point value in
 * the positive direction.
 *
 * Infinities and NaNs are left untouched. Subnormal numbers are handled
 * correctly, as is crossing zero (i.e. 0 and -0 are considered a single value,
 * and progressing past zero continues on to the positive numbers).
 */
MINLINE float increment_ulp(float value);

/**
 * Decrement the given float to the next representable floating point value in
 * the negative direction.
 *
 * Infinities and NaNs are left untouched. Subnormal numbers are handled
 * correctly, as is zero (i.e. 0 and -0 are considered a single value, and
 * progressing past zero continues on to the negative numbers).
 */
MINLINE float decrement_ulp(float value);

MINLINE float signf(float f);
MINLINE int signum_i_ex(float a, float eps);
MINLINE int signum_i(float a);

/**
 * Used for zoom values.
 */
MINLINE float power_of_2(float f);

/**
 * Returns number of (base ten) *significant* digits of integer part of given float
 * (negative in case of decimal-only floats, 0.01 returns -1 e.g.).
 */
MINLINE int integer_digits_f(float f);
/**
 * Returns number of (base ten) *significant* digits of integer part of given double
 * (negative in case of decimal-only floats, 0.01 returns -1 e.g.).
 */
MINLINE int integer_digits_d(double d);
MINLINE int integer_digits_i(int i);

/* These don't really fit anywhere but were being copied about a lot. */

MINLINE int is_power_of_2_i(int n);

MINLINE unsigned int log2_floor_u(unsigned int x);
MINLINE unsigned int log2_ceil_u(unsigned int x);

/**
 * Returns next (or previous) power of 2 or the input number if it is already a power of 2.
 */
MINLINE int power_of_2_max_i(int n);
MINLINE int power_of_2_min_i(int n);
MINLINE unsigned int power_of_2_max_u(unsigned int x);

/**
 * Integer division that rounds 0.5 up, particularly useful for color blending
 * with integers, to avoid gradual darkening when rounding down.
 */
MINLINE int divide_round_i(int a, int b);

/**
 * Integer division that returns the ceiling, instead of flooring like normal C division.
 */
MINLINE uint divide_ceil_u(uint a, uint b);
MINLINE uint64_t divide_ceil_ul(uint64_t a, uint64_t b);

/**
 * Returns \a a if it is a multiple of \a b or the next multiple or \a b after \b a.
 */
MINLINE uint ceil_to_multiple_u(uint a, uint b);
MINLINE uint64_t ceil_to_multiple_ul(uint64_t a, uint64_t b);

/**
 * Floored modulo that is useful for wrapping numbers over \a n,
 * including when \a i is negative.
 *
 * This is the same as Python % or GLSL mod(): `mod_i(-5, 3) = 1`.
 *
 * \return an integer in the interval [0, n), same sign as n.
 */
MINLINE int mod_i(int i, int n);

/**
 * Floored modulo that is useful for wrapping numbers over \a n,
 * including when \a f is negative.
 *
 * This is the same as Python % or GLSL mod(): `floored_fmod(-0.2, 1.0) = 0.8`.
 *
 * \return a float in the interval [0, n), same sign as n.
 */
MINLINE float floored_fmod(float f, float n);

/**
 * Round to closest even number, halfway cases are rounded away from zero.
 */
MINLINE float round_to_even(float f);

MINLINE unsigned char round_fl_to_uchar(float a);
MINLINE short round_fl_to_short(float a);
MINLINE int round_fl_to_int(float a);
MINLINE unsigned int round_fl_to_uint(float a);

MINLINE int round_db_to_int(double a);

MINLINE unsigned char round_fl_to_uchar_clamp(float a);
MINLINE int round_fl_to_int_clamp(float a);

MINLINE unsigned char round_db_to_uchar_clamp(double a);
MINLINE short round_db_to_short_clamp(double a);
MINLINE int round_db_to_int_clamp(double a);

int pow_i(int base, int exp);

/**
 * \param ndigits: must be between 0 and 21.
 */
double double_round(double x, int ndigits);

/**
 * Floor to the nearest power of 10, e.g.:
 * - 15.0 -> 10.0
 * - 0.015 -> 0.01
 * - 1.0 -> 1.0
 *
 * \param f: Value to floor, must be over 0.0.
 * \note If we wanted to support signed values we could if this becomes necessary.
 */
float floor_power_of_10(float f);
/**
 * Ceiling to the nearest power of 10, e.g.:
 * - 15.0 -> 100.0
 * - 0.015 -> 0.1
 * - 1.0 -> 1.0
 *
 * \param f: Value to ceiling, must be over 0.0.
 * \note If we wanted to support signed values we could if this becomes necessary.
 */
float ceil_power_of_10(float f);

#ifdef BLI_MATH_GCC_WARN_PRAGMA
#  pragma GCC diagnostic pop
#endif

/* Asserts, some math functions expect normalized inputs
 * check the vector is unit length, or zero length (which can't be helped in some cases). */

#ifndef NDEBUG
/** \note 0.0001 is too small because normals may be converted from short's: see #34322. */
#  define BLI_ASSERT_UNIT_EPSILON 0.0002f
#  define BLI_ASSERT_UNIT_EPSILON_DB 0.0002
/**
 * \note Checks are flipped so NAN doesn't assert.
 * This is done because we're making sure the value was normalized and in the case we
 * don't want NAN to be raising asserts since there is nothing to be done in that case.
 */
#  define BLI_ASSERT_UNIT_V3(v) \
    { \
      const float _test_unit = len_squared_v3(v); \
      BLI_assert(!(fabsf(_test_unit - 1.0f) >= BLI_ASSERT_UNIT_EPSILON) || \
                 !(fabsf(_test_unit) >= BLI_ASSERT_UNIT_EPSILON)); \
    } \
    (void)0

#  define BLI_ASSERT_UNIT_V2(v) \
    { \
      const float _test_unit = len_squared_v2(v); \
      BLI_assert(!(fabsf(_test_unit - 1.0f) >= BLI_ASSERT_UNIT_EPSILON) || \
                 !(fabsf(_test_unit) >= BLI_ASSERT_UNIT_EPSILON)); \
    } \
    (void)0

#  define BLI_ASSERT_UNIT_QUAT(q) \
    { \
      const float _test_unit = dot_qtqt(q, q); \
      BLI_assert(!(fabsf(_test_unit - 1.0f) >= BLI_ASSERT_UNIT_EPSILON * 10) || \
                 !(fabsf(_test_unit) >= BLI_ASSERT_UNIT_EPSILON * 10)); \
    } \
    (void)0

#  define BLI_ASSERT_ZERO_M3(m) \
    { \
      BLI_assert(dot_vn_vn((const float *)m, (const float *)m, 9) != 0.0); \
    } \
    (void)0

#  define BLI_ASSERT_ZERO_M4(m) \
    { \
      BLI_assert(dot_vn_vn((const float *)m, (const float *)m, 16) != 0.0); \
    } \
    (void)0
#  define BLI_ASSERT_UNIT_M3(m) \
    { \
      BLI_ASSERT_UNIT_V3((m)[0]); \
      BLI_ASSERT_UNIT_V3((m)[1]); \
      BLI_ASSERT_UNIT_V3((m)[2]); \
    } \
    (void)0
#else
#  define BLI_ASSERT_UNIT_V2(v) (void)(v)
#  define BLI_ASSERT_UNIT_V3(v) (void)(v)
#  define BLI_ASSERT_UNIT_QUAT(v) (void)(v)
#  define BLI_ASSERT_ZERO_M3(m) (void)(m)
#  define BLI_ASSERT_ZERO_M4(m) (void)(m)
#  define BLI_ASSERT_UNIT_M3(m) (void)(m)
#endif
