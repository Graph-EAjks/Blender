/* SPDX-FileCopyrightText: 2021 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup eevee
 */

#include <algorithm>

#include "eevee_instance.hh"
#include "eevee_subsurface.hh"

namespace blender::eevee {

/* -------------------------------------------------------------------- */
/** \name Subsurface
 * \{ */

void SubsurfaceModule::end_sync()
{
  data_.sample_len = 16;

  if (!(inst_.pipelines.deferred.closure_bits_get() & CLOSURE_SSS)) {
    return;
  }

  {
    PassSimple &pass = setup_ps_;
    pass.init();
    pass.state_set(DRW_STATE_NO_DRAW);
    pass.shader_set(inst_.shaders.static_shader_get(SUBSURFACE_SETUP));
    pass.bind_resources(inst_.gbuffer);
    pass.bind_texture("depth_tx", &inst_.render_buffers.depth_tx);
    pass.bind_image("direct_light_img", &direct_light_tx_);
    pass.bind_image("indirect_light_img", &indirect_light_tx_);
    pass.bind_image("object_id_img", &object_id_tx_);
    pass.bind_image("radiance_img", &radiance_tx_);
    pass.bind_ssbo("convolve_tile_buf", &convolve_tile_buf_);
    pass.bind_ssbo("convolve_dispatch_buf", &convolve_dispatch_buf_);
    pass.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    pass.dispatch(&setup_dispatch_size_);
  }
  {
    /* Clamping to border color allows to always load ID 0 for out of view samples and discard
     * their influence. Also disable filtering to avoid light bleeding between different objects
     * and loading invalid interpolated IDs. */
    GPUSamplerState sampler = {GPU_SAMPLER_FILTERING_DEFAULT,
                               GPU_SAMPLER_EXTEND_MODE_CLAMP_TO_BORDER,
                               GPU_SAMPLER_EXTEND_MODE_CLAMP_TO_BORDER,
                               GPU_SAMPLER_CUSTOM_COMPARE,
                               GPU_SAMPLER_STATE_TYPE_PARAMETERS};

    PassSimple &pass = convolve_ps_;
    pass.init();
    pass.state_set(DRW_STATE_NO_DRAW);
    pass.shader_set(inst_.shaders.static_shader_get(SUBSURFACE_CONVOLVE));
    pass.bind_resources(inst_.uniform_data);
    pass.bind_resources(inst_.gbuffer);
    pass.bind_texture("radiance_tx", &radiance_tx_, sampler);
    pass.bind_texture("depth_tx", &inst_.render_buffers.depth_tx, sampler);
    pass.bind_texture("object_id_tx", &object_id_tx_, sampler);
    pass.bind_image("out_direct_light_img", &direct_light_tx_);
    pass.bind_image("out_indirect_light_img", &indirect_light_tx_);
    pass.bind_ssbo("tiles_coord_buf", &convolve_tile_buf_);
    pass.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_STORAGE);
    pass.dispatch(convolve_dispatch_buf_);
  }
}

void SubsurfaceModule::render(gpu::Texture *direct_diffuse_light_tx,
                              gpu::Texture *indirect_diffuse_light_tx,
                              eClosureBits active_closures,
                              View &view)
{
  if (!(active_closures & CLOSURE_SSS)) {
    return;
  }

  precompute_samples_location();

  int2 render_extent = inst_.film.render_extent_get();
  setup_dispatch_size_ = int3(math::divide_ceil(render_extent, int2(SUBSURFACE_GROUP_SIZE)), 1);

  const int convolve_tile_count = setup_dispatch_size_.x * setup_dispatch_size_.y;
  convolve_tile_buf_.resize(ceil_to_multiple_u(convolve_tile_count, 512));

  direct_light_tx_ = direct_diffuse_light_tx;
  indirect_light_tx_ = indirect_diffuse_light_tx;

  eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;
  object_id_tx_.acquire(render_extent, gpu::TextureFormat::SUBSURFACE_OBJECT_ID_FORMAT, usage);
  radiance_tx_.acquire(render_extent, gpu::TextureFormat::SUBSURFACE_RADIANCE_FORMAT, usage);

  convolve_dispatch_buf_.clear_to_zero();

  inst_.manager->submit(setup_ps_, view);
  inst_.manager->submit(convolve_ps_, view);

  object_id_tx_.release();
  radiance_tx_.release();
}

void SubsurfaceModule::precompute_samples_location()
{
  /* Precompute sample position with white albedo. */
  float d = burley_setup(float3(1.0f), float3(1.0f)).x;

  float rand_u = inst_.sampling.rng_get(SAMPLING_SSS_U);
  float rand_v = inst_.sampling.rng_get(SAMPLING_SSS_V);

  /* Find minimum radius that we can represent because we are only sampling the largest radius. */
  data_.min_radius = 1.0f;

  double golden_angle = M_PI * (3.0 - sqrt(5.0));
  for (auto i : IndexRange(data_.sample_len)) {
    float theta = golden_angle * i + M_PI * 2.0f * rand_u;
    float x = (rand_v + i) / data_.sample_len;
    float r = SubsurfaceModule::burley_sample(d, x);
    data_.min_radius = min_ff(data_.min_radius, r);
    data_.samples[i].x = cosf(theta) * r;
    data_.samples[i].y = sinf(theta) * r;
    data_.samples[i].z = 1.0f / burley_pdf(d, r);
  }
  /* Avoid float imprecision. */
  data_.min_radius = max_ff(data_.min_radius, 1e-4f);

  inst_.uniform_data.push_update();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Christensen-Burley SSS model
 *
 * Based on: "Approximate Reflectance Profiles for Efficient Subsurface Scattering"
 * by Per Christensen
 * https://graphics.pixar.com/library/ApproxBSSRDF/approxbssrdfslides.pdf
 * \{ */

float SubsurfaceModule::burley_sample(float d, float x_rand)
{
  x_rand *= SSS_BURLEY_TRUNCATE_CDF;

  const float tolerance = 1e-6;
  const int max_iteration_count = 10;
  /* Do initial guess based on manual curve fitting, this allows us to reduce
   * number of iterations to maximum 4 across the [0..1] range. We keep maximum
   * number of iteration higher just to be sure we didn't miss root in some
   * corner case.
   */
  float r;
  if (x_rand <= 0.9) {
    r = exp(x_rand * x_rand * 2.4) - 1.0;
  }
  else {
    /* TODO(sergey): Some nicer curve fit is possible here. */
    r = 15.0;
  }
  /* Solve against scaled radius. */
  for (int i = 0; i < max_iteration_count; i++) {
    float exp_r_3 = exp(-r / 3.0);
    float exp_r = exp_r_3 * exp_r_3 * exp_r_3;
    float f = 1.0 - 0.25 * exp_r - 0.75 * exp_r_3 - x_rand;
    float f_ = 0.25 * exp_r + 0.25 * exp_r_3;

    if (abs(f) < tolerance || f_ == 0.0) {
      break;
    }

    r = r - f / f_;
    r = std::max<double>(r, 0.0);
  }

  return r * d;
}

float SubsurfaceModule::burley_eval(float d, float r)
{
  if (r >= SSS_BURLEY_TRUNCATE * d) {
    return 0.0;
  }
  /* Slide 33. */
  float exp_r_3_d = expf(-r / (3.0f * d));
  float exp_r_d = exp_r_3_d * exp_r_3_d * exp_r_3_d;
  return (exp_r_d + exp_r_3_d) / (8.0f * float(M_PI) * d);
}

float SubsurfaceModule::burley_pdf(float d, float r)
{
  return burley_eval(d, r) / SSS_BURLEY_TRUNCATE_CDF;
}

/** \} */

}  // namespace blender::eevee
