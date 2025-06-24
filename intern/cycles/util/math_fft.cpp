/* SPDX-FileCopyrightText: 2011-2025 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#include "util/math_fft.h"
#include "util/math_base.h"

CCL_NAMESPACE_BEGIN

void util_fft_radix2(vector<std::complex<float>> &x)
{
  const uint N = x.size();
  /* This naive implementation only supports power-of-2 sizes. */
  assert(N >= 2 && ((N & (N - 1)) == 0));

  /* Shuffle by reverse bit order. */
  const uint shift = count_leading_zeros(N) + 1;
  for (uint i = 0; i < N; i++) {
    uint j = reverse_integer_bits(i << shift);
    if (j > i) {
      std::swap(x[i], x[j]);
    }
  }

  /* Precompute sin/cos table. */
  vector<std::complex<float>> sincos_table;
  sincos_table.reserve(N);
  const float fac = M_2PI_F / N;
  for (uint i = 0; i < N; i++) {
    sincos_table.emplace_back(cosf(i * fac), -sinf(i * fac));
  }

  /* Perform FFT. */
  uint offset = N / 2;
  for (uint stride = 2, half_stride = 1; stride <= N; stride <<= 1, half_stride <<= 1) {
    for (uint i = 0; i < N; i += stride) {
      uint k = 0;
      for (uint j = i, l = i + half_stride; j < i + half_stride; j++, l++) {
        std::complex<float> diff = x[l] * sincos_table[k];
        x[l] = x[j] - diff;
        x[j] += diff;
        k += offset;
      }
    }
    offset >>= 1;
  }
}

void util_fft_r2c(vector<float> &x)
{
  const uint N = x.size();

  /* Pack inputs into complex vector. */
  vector<std::complex<float>> g;
  g.reserve(N / 2);
  for (uint i = 0; i < N; i += 2) {
    g.emplace_back(x[i], x[i + 1]);
  }

  /* Perform c2c FFT. */
  util_fft_radix2(g);

  /* Convert output. */
  x[0] = g[0].real() + g[0].imag(); /* DC component. */
  const float fac = M_2PI_F / N;
  for (int i = 1, j = N / 2 - 1; i < N / 2; i++, j--) {
    std::complex<float> W(cosf(fac * i), -sinf(fac * i));
    std::complex<float> A = std::complex<float>(1.0f + W.imag(), -W.real());
    std::complex<float> B = std::complex<float>(1.0f - W.imag(), W.real());
    std::complex<float> val = 0.5f * (g[i] * A + std::conj(g[j]) * B);
    x[2 * i] = val.real();
    x[2 * i + 1] = val.imag();
  }
  x[1] = g[0].real() - g[0].imag(); /* Nyquist component. */
}

CCL_NAMESPACE_END
