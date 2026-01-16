/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include "BLI_map.hh"
#include "BLI_vector.hh"
#include "BLI_virtual_array.hh"

#include "BKE_grease_pencil_fills.hh"

namespace blender::bke::greasepencil {

std::optional<FillCache> fill_cache_from_fill_ids(const int num_curves,
                                                  const VArray<int> &fill_ids)
{
  if (num_curves == 0 || fill_ids.is_empty()) {
    return std::nullopt;
  }

  /* The size of each fill. This includes zero-id fills (which always have a size of 1). */
  Vector<int> all_fill_sizes;
  /* Maps the non-zero fill id to the index of the fill. */
  Map<int, int> all_non_zero_fill_indexing;
  /* The fill id of each fill. The fill id zero can appear more than once, others may appear
   * at most once. */
  Vector<int> all_fill_ids;
  /* The index of the curve if the fill is a zero fill. Otherwise -1. */
  Vector<int> all_zero_fill_curve_indices;

  /* Contains the curve indices for each non-zero fill. */
  Vector<Vector<int>> curve_indices_by_non_zero_fill;
  /* Maps the fill id to the index in the #curve_indices_by_non_zero_fill vector. */
  Map<int, int> non_zero_fill_indexing;

  bool has_non_single_curve_fill = false;
  for (const int curve : IndexRange(num_curves)) {
    const int fill_id = fill_ids[curve];
    if (fill_id == 0) {
      all_fill_sizes.append(1);
      all_fill_ids.append(0);
      all_zero_fill_curve_indices.append(curve);
    }
    /* Try adding non zero fill id to the map. */
    else if (all_non_zero_fill_indexing.add(fill_id, all_fill_sizes.size())) {
      all_fill_sizes.append(1);
      all_fill_ids.append(fill_id);
      /* Not a zero fill. */
      all_zero_fill_curve_indices.append(-1);
    }
    else {
      all_fill_sizes[all_non_zero_fill_indexing.lookup(fill_id)]++;
      has_non_single_curve_fill = true;
    }

    /* Keep track of curve indices for non-zero fills. */
    if (fill_id != 0) {
      if (non_zero_fill_indexing.add(fill_id, curve_indices_by_non_zero_fill.size())) {
        curve_indices_by_non_zero_fill.append(Vector<int>({curve}));
      }
      else {
        curve_indices_by_non_zero_fill[non_zero_fill_indexing.lookup(fill_id)].append(curve);
      }
    }
  }

  if (!has_non_single_curve_fill) {
    /* All fills are a single curve. Cache is not needed. */
    return std::nullopt;
  }

  all_fill_sizes.append(0);
  OffsetIndices<int> fill_offsets = offset_indices::accumulate_counts_to_offsets(all_fill_sizes);

  Vector<int> fill_map(num_curves);
  MutableSpan<int> fill_map_span = fill_map.as_mutable_span();
  threading::parallel_for(fill_offsets.index_range(), 4096, [&](const IndexRange range) {
    for (const int fill_i : range) {
      const IndexRange fill_range = fill_offsets[fill_i];
      const bool is_zero_fill = all_fill_ids[fill_i] == 0;
      if (is_zero_fill) {
        const int curve_i = all_zero_fill_curve_indices[fill_i];
        BLI_assert(fill_range.size() == 1);
        fill_map_span[fill_range.first()] = curve_i;
      }
      else {
        const int fill_id = all_fill_ids[fill_i];
        const Span<int> curve_indices =
            curve_indices_by_non_zero_fill[non_zero_fill_indexing.lookup(fill_id)].as_span();
        fill_map_span.slice(fill_range).copy_from(curve_indices);
      }
    }
  });

  FillCache fill_cache;
  fill_cache.fill_map = std::move(fill_map);
  fill_cache.fill_offsets = std::move(all_fill_sizes);
  return fill_cache;
}

}  // namespace blender::bke::greasepencil
