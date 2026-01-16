/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup bke
 */

#include <optional>

#include "BKE_grease_pencil.hh"

namespace blender::bke::greasepencil {

std::optional<FillCache> fill_cache_from_fill_ids(const int num_curves,
                                                  const VArray<int> &fill_ids);

}  // namespace blender::bke::greasepencil
