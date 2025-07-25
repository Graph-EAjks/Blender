/* SPDX-FileCopyrightText: 2023 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "BLI_math_vector_types.hh"

#include "COM_context.hh"
#include "COM_result.hh"

namespace blender::compositor {

/* --------------------------------------------------------------------
 * Sum Reductions.
 */

/* Computes the sum of the red channel of all pixels in the given result. */
float sum_red(Context &context, const Result &result);

/* Computes the sum of the green channel of all pixels in the given result. */
float sum_green(Context &context, const Result &result);

/* Computes the sum of the blue channel of all pixels in the given result. */
float sum_blue(Context &context, const Result &result);

/* Computes the sum of the luminance of all pixels in the given result, using the given luminance
 * coefficients to compute the luminance. */
float sum_luminance(Context &context, const Result &result, const float3 &luminance_coefficients);

/* Computes the sum of the logarithm of the luminance of all pixels in the given result, using the
 * given luminance coefficients to compute the luminance. */
float sum_log_luminance(Context &context,
                        const Result &result,
                        const float3 &luminance_coefficients);

/* Computes the sum of the colors of all pixels in the given result. */
float4 sum_color(Context &context, const Result &result);

/* --------------------------------------------------------------------
 * Sum Of Squared Difference Reductions.
 */

/* Computes the sum of the squared difference between the red channel of all pixels in the given
 * result and the given subtrahend. This can be used to compute the standard deviation if the
 * given subtrahend is the mean. */
float sum_red_squared_difference(Context &context, const Result &result, const float subtrahend);

/* Computes the sum of the squared difference between the green channel of all pixels in the given
 * result and the given subtrahend. This can be used to compute the standard deviation if the
 * given subtrahend is the mean. */
float sum_green_squared_difference(Context &context, const Result &result, const float subtrahend);

/* Computes the sum of the squared difference between the blue channel of all pixels in the given
 * result and the given subtrahend. This can be used to compute the standard deviation if the
 * given subtrahend is the mean. */
float sum_blue_squared_difference(Context &context, const Result &result, const float subtrahend);

/* Computes the sum of the squared difference between the luminance of all pixels in the given
 * result and the given subtrahend, using the given luminance coefficients to compute the
 * luminance. This can be used to compute the standard deviation if the given subtrahend is the
 * mean. */
float sum_luminance_squared_difference(Context &context,
                                       const Result &result,
                                       const float3 &luminance_coefficients,
                                       const float subtrahend);

/* --------------------------------------------------------------------
 * Maximum Reductions.
 */

/* Computes the maximum luminance of all pixels in the given result, using the given luminance
 * coefficients to compute the luminance. */
float maximum_luminance(Context &context,
                        const Result &result,
                        const float3 &luminance_coefficients);

/* Computes the maximum float value of all pixels in the given result. */
float maximum_float(Context &context, const Result &result);

/* Computes the maximum float2 value of all pixels in the given result. */
float2 maximum_float2(Context &context, const Result &result);

/* Computes the maximum float of all pixels in the given float result, limited to the given range.
 * Values outside of the given range are ignored. If non of the pixel values are in the range, the
 * lower bound of the range is returned. For instance, if the given range is [-10, 10] and the
 * image contains the values {2, 5, 11}, the maximum will be 5, since 11 is outside of the range.
 * This is particularly useful for Z Depth normalization, since Z Depth can contain near infinite
 * values, so enforcing an upper bound is beneficial. */
float maximum_float_in_range(Context &context,
                             const Result &result,
                             const float lower_bound,
                             const float upper_bound);

/* --------------------------------------------------------------------
 * Minimum Reductions.
 */

/* Computes the minimum luminance of all pixels in the given result, using the given luminance
 * coefficients to compute the luminance. */
float minimum_luminance(Context &context,
                        const Result &result,
                        const float3 &luminance_coefficients);

/* Computes the minimum float value of all pixels in the given result. */
float minimum_float(Context &context, const Result &result);

/* Computes the minimum float of all pixels in the given float result, limited to the given range.
 * Values outside of the given range are ignored. If non of the pixel values are in the range, the
 * upper bound of the range is returned. For instance, if the given range is [-10, 10] and the
 * image contains the values {-11, 2, 5}, the minimum will be 2, since -11 is outside of the range.
 * This is particularly useful for Z Depth normalization, since Z Depth can contain near infinite
 * values, so enforcing a lower bound is beneficial. */
float minimum_float_in_range(Context &context,
                             const Result &result,
                             const float lower_bound,
                             const float upper_bound);

}  // namespace blender::compositor
