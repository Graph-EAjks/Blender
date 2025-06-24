/* SPDX-FileCopyrightText: 2011-2025 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <complex>

#include "util/vector.h"

CCL_NAMESPACE_BEGIN

/* Computes the discrete Fourier transform of x in-place. Only works for power-of-2 sizes.
 * NOTE: This is a very straightforward/naive implementation. If performance is
 * important and/or the input is large, use a proper implementation (e.g. FFTW)!
 */
void util_fft_radix2(vector<std::complex<float>> &x);

/* Computes the discrete Fourier transform of real input values x.
 * Modifies x in-place to return the lower half of the resulting DFT (since the upper half
 * is just the mirrored complex conjugate), packed as x[0].real, x[0].imag, x[1].real and so on.
 * To keep the output the same size, x[N/2].real is packed into where x[0].imag would go,
 * which works since both x[0] and x[N/2] end up being real numbers for real inputs.
 */
void util_fft_r2c(vector<float> &x);

CCL_NAMESPACE_END
