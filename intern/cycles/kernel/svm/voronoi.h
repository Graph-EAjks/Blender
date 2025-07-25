/* SPDX-FileCopyrightText: 2011-2022 Blender Foundation
 *
 * SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "kernel/svm/util.h"
#include "util/hash.h"

CCL_NAMESPACE_BEGIN

/*
 * SPDX-License-Identifier: MIT
 * Original code is copyright (c) 2013 Inigo Quilez.
 *
 * Smooth Voronoi:
 *
 * - https://wiki.blender.org/wiki/User:OmarSquircleArt/GSoC2019/Documentation/Smooth_Voronoi
 *
 * Distance To Edge based on:
 *
 * - https://www.iquilezles.org/www/articles/voronoilines/voronoilines.htm
 * - https://www.shadertoy.com/view/ldl3W8
 *
 * With optimization to change -2..2 scan window to -1..1 for better performance,
 * as explained in https://www.shadertoy.com/view/llG3zy.
 */

struct VoronoiParams {
  float scale;
  float detail;
  float roughness;
  float lacunarity;
  float smoothness;
  float exponent;
  float randomness;
  float max_distance;
  bool normalize;
  NodeVoronoiFeature feature;
  NodeVoronoiDistanceMetric metric;
};

struct VoronoiOutput {
  float distance = 0.0f;
  float3 color = zero_float3();
  float4 position = zero_float4();
};

/* ***** Distances ***** */

ccl_device float voronoi_distance(const float a, const float b)
{
  return fabsf(b - a);
}

template<typename T>
ccl_device float voronoi_distance(const T a, const T b, const ccl_private VoronoiParams &params)
{
  if (params.metric == NODE_VORONOI_EUCLIDEAN) {
    return distance(a, b);
  }
  if (params.metric == NODE_VORONOI_MANHATTAN) {
    return reduce_add(fabs(a - b));
  }
  if (params.metric == NODE_VORONOI_CHEBYCHEV) {
    return reduce_max(fabs(a - b));
  }
  if (params.metric == NODE_VORONOI_MINKOWSKI) {
    return powf(reduce_add(power(fabs(a - b), params.exponent)), 1.0f / params.exponent);
  }
  return 0.0f;
}

/* Possibly cheaper/faster version of Voronoi distance, in a way that does not change
 * logic of "which distance is the closest?". */
template<typename T>
ccl_device float voronoi_distance_bound(const T a,
                                        const T b,
                                        const ccl_private VoronoiParams &params)
{
  if (params.metric == NODE_VORONOI_EUCLIDEAN) {
    return len_squared(a - b);
  }
  if (params.metric == NODE_VORONOI_MANHATTAN) {
    return reduce_add(fabs(a - b));
  }
  if (params.metric == NODE_VORONOI_CHEBYCHEV) {
    return reduce_max(fabs(a - b));
  }
  if (params.metric == NODE_VORONOI_MINKOWSKI) {
    return reduce_add(power(fabs(a - b), params.exponent));
  }
  return 0.0f;
}

/* **** 1D Voronoi **** */

ccl_device float4 voronoi_position(const float coord)
{
  return make_float4(0.0f, 0.0f, 0.0f, coord);
}

ccl_device VoronoiOutput voronoi_f1(const ccl_private VoronoiParams &params, const float coord)
{
  const float cellPosition = floorf(coord);
  const float localPosition = coord - cellPosition;

  float minDistance = FLT_MAX;
  float targetOffset = 0.0f;
  float targetPosition = 0.0f;
  for (int i = -1; i <= 1; i++) {
    const float cellOffset = i;
    const float pointPosition = cellOffset +
                                hash_float_to_float(cellPosition + cellOffset) * params.randomness;
    const float distanceToPoint = voronoi_distance(pointPosition, localPosition);
    if (distanceToPoint < minDistance) {
      targetOffset = cellOffset;
      minDistance = distanceToPoint;
      targetPosition = pointPosition;
    }
  }

  VoronoiOutput octave;
  octave.distance = minDistance;
  octave.color = hash_float_to_float3(cellPosition + targetOffset);
  octave.position = voronoi_position(targetPosition + cellPosition);
  return octave;
}

ccl_device VoronoiOutput voronoi_smooth_f1(const ccl_private VoronoiParams &params,
                                           const float coord)
{
  const float cellPosition = floorf(coord);
  const float localPosition = coord - cellPosition;

  float smoothDistance = 0.0f;
  float smoothPosition = 0.0f;
  float3 smoothColor = make_float3(0.0f, 0.0f, 0.0f);
  float h = -1.0f;
  for (int i = -2; i <= 2; i++) {
    const float cellOffset = i;
    const float pointPosition = cellOffset +
                                hash_float_to_float(cellPosition + cellOffset) * params.randomness;
    const float distanceToPoint = voronoi_distance(pointPosition, localPosition);
    h = h == -1.0f ?
            1.0f :
            smoothstep(
                0.0f, 1.0f, 0.5f + 0.5f * (smoothDistance - distanceToPoint) / params.smoothness);
    float correctionFactor = params.smoothness * h * (1.0f - h);
    smoothDistance = mix(smoothDistance, distanceToPoint, h) - correctionFactor;
    correctionFactor /= 1.0f + 3.0f * params.smoothness;
    const float3 cellColor = hash_float_to_float3(cellPosition + cellOffset);
    smoothColor = mix(smoothColor, cellColor, h) - correctionFactor;
    smoothPosition = mix(smoothPosition, pointPosition, h) - correctionFactor;
  }

  VoronoiOutput octave;
  octave.distance = smoothDistance;
  octave.color = smoothColor;
  octave.position = voronoi_position(cellPosition + smoothPosition);
  return octave;
}

ccl_device VoronoiOutput voronoi_f2(const ccl_private VoronoiParams &params, const float coord)
{
  const float cellPosition = floorf(coord);
  const float localPosition = coord - cellPosition;

  float distanceF1 = FLT_MAX;
  float distanceF2 = FLT_MAX;
  float offsetF1 = 0.0f;
  float positionF1 = 0.0f;
  float offsetF2 = 0.0f;
  float positionF2 = 0.0f;
  for (int i = -1; i <= 1; i++) {
    const float cellOffset = i;
    const float pointPosition = cellOffset +
                                hash_float_to_float(cellPosition + cellOffset) * params.randomness;
    const float distanceToPoint = voronoi_distance(pointPosition, localPosition);
    if (distanceToPoint < distanceF1) {
      distanceF2 = distanceF1;
      distanceF1 = distanceToPoint;
      offsetF2 = offsetF1;
      offsetF1 = cellOffset;
      positionF2 = positionF1;
      positionF1 = pointPosition;
    }
    else if (distanceToPoint < distanceF2) {
      distanceF2 = distanceToPoint;
      offsetF2 = cellOffset;
      positionF2 = pointPosition;
    }
  }

  VoronoiOutput octave;
  octave.distance = distanceF2;
  octave.color = hash_float_to_float3(cellPosition + offsetF2);
  octave.position = voronoi_position(positionF2 + cellPosition);
  return octave;
}

ccl_device float voronoi_distance_to_edge(const ccl_private VoronoiParams &params,
                                          const float coord)
{
  const float cellPosition = floorf(coord);
  const float localPosition = coord - cellPosition;

  const float midPointPosition = hash_float_to_float(cellPosition) * params.randomness;
  const float leftPointPosition = -1.0f +
                                  hash_float_to_float(cellPosition - 1.0f) * params.randomness;
  const float rightPointPosition = 1.0f +
                                   hash_float_to_float(cellPosition + 1.0f) * params.randomness;
  const float distanceToMidLeft = fabsf((midPointPosition + leftPointPosition) / 2.0f -
                                        localPosition);
  const float distanceToMidRight = fabsf((midPointPosition + rightPointPosition) / 2.0f -
                                         localPosition);

  return min(distanceToMidLeft, distanceToMidRight);
}

ccl_device float voronoi_n_sphere_radius(const ccl_private VoronoiParams &params,
                                         const float coord)
{
  const float cellPosition = floorf(coord);
  const float localPosition = coord - cellPosition;

  float closestPoint = 0.0f;
  float closestPointOffset = 0.0f;
  float minDistance = FLT_MAX;
  for (int i = -1; i <= 1; i++) {
    const float cellOffset = i;
    const float pointPosition = cellOffset +
                                hash_float_to_float(cellPosition + cellOffset) * params.randomness;
    const float distanceToPoint = fabsf(pointPosition - localPosition);
    if (distanceToPoint < minDistance) {
      minDistance = distanceToPoint;
      closestPoint = pointPosition;
      closestPointOffset = cellOffset;
    }
  }

  minDistance = FLT_MAX;
  float closestPointToClosestPoint = 0.0f;
  for (int i = -1; i <= 1; i++) {
    if (i == 0) {
      continue;
    }
    const float cellOffset = i + closestPointOffset;
    const float pointPosition = cellOffset +
                                hash_float_to_float(cellPosition + cellOffset) * params.randomness;
    const float distanceToPoint = fabsf(closestPoint - pointPosition);
    if (distanceToPoint < minDistance) {
      minDistance = distanceToPoint;
      closestPointToClosestPoint = pointPosition;
    }
  }

  return fabsf(closestPointToClosestPoint - closestPoint) / 2.0f;
}

/* **** 2D Voronoi **** */

ccl_device float4 voronoi_position(const float2 coord)
{
  return make_float4(coord.x, coord.y, 0.0f, 0.0f);
}

ccl_device VoronoiOutput voronoi_f1(const ccl_private VoronoiParams &params, const float2 coord)
{
  const float2 cellPosition_f = floor(coord);
  const float2 localPosition = coord - cellPosition_f;
  const int2 cellPosition = make_int2(cellPosition_f);

  float minDistance = FLT_MAX;
  int2 targetOffset = make_int2(0);
  float2 targetPosition = make_float2(0.0f, 0.0f);
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      const int2 cellOffset = make_int2(i, j);
      const float2 pointPosition = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness;
      const float distanceToPoint = voronoi_distance_bound(pointPosition, localPosition, params);
      if (distanceToPoint < minDistance) {
        targetOffset = cellOffset;
        minDistance = distanceToPoint;
        targetPosition = pointPosition;
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = voronoi_distance(targetPosition, localPosition, params);
  octave.color = hash_int2_to_float3(cellPosition + targetOffset);
  octave.position = voronoi_position(targetPosition + cellPosition_f);
  return octave;
}

ccl_device VoronoiOutput voronoi_smooth_f1(const ccl_private VoronoiParams &params,
                                           const float2 coord)
{
  const float2 cellPosition_f = floor(coord);
  const float2 localPosition = coord - cellPosition_f;
  const int2 cellPosition = make_int2(cellPosition_f);

  float smoothDistance = 0.0f;
  float3 smoothColor = make_float3(0.0f, 0.0f, 0.0f);
  float2 smoothPosition = make_float2(0.0f, 0.0f);
  float h = -1.0f;
  for (int j = -2; j <= 2; j++) {
    for (int i = -2; i <= 2; i++) {
      const int2 cellOffset = make_int2(i, j);
      const float2 pointPosition = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness;
      const float distanceToPoint = voronoi_distance(pointPosition, localPosition, params);
      h = h == -1.0f ?
              1.0f :
              smoothstep(0.0f,
                         1.0f,
                         0.5f + 0.5f * (smoothDistance - distanceToPoint) / params.smoothness);
      float correctionFactor = params.smoothness * h * (1.0f - h);
      smoothDistance = mix(smoothDistance, distanceToPoint, h) - correctionFactor;
      correctionFactor /= 1.0f + 3.0f * params.smoothness;
      const float3 cellColor = hash_int2_to_float3(cellPosition + cellOffset);
      smoothColor = mix(smoothColor, cellColor, h) - correctionFactor;
      smoothPosition = mix(smoothPosition, pointPosition, h) - correctionFactor;
    }
  }

  VoronoiOutput octave;
  octave.distance = smoothDistance;
  octave.color = smoothColor;
  octave.position = voronoi_position(cellPosition_f + smoothPosition);
  return octave;
}

ccl_device VoronoiOutput voronoi_f2(const ccl_private VoronoiParams &params, const float2 coord)
{
  const float2 cellPosition_f = floor(coord);
  const float2 localPosition = coord - cellPosition_f;
  const int2 cellPosition = make_int2(cellPosition_f);

  float distanceF1 = FLT_MAX;
  float distanceF2 = FLT_MAX;
  int2 offsetF1 = make_int2(0);
  float2 positionF1 = make_float2(0.0f, 0.0f);
  int2 offsetF2 = make_int2(0);
  float2 positionF2 = make_float2(0.0f, 0.0f);
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      const int2 cellOffset = make_int2(i, j);
      const float2 pointPosition = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness;
      const float distanceToPoint = voronoi_distance(pointPosition, localPosition, params);
      if (distanceToPoint < distanceF1) {
        distanceF2 = distanceF1;
        distanceF1 = distanceToPoint;
        offsetF2 = offsetF1;
        offsetF1 = cellOffset;
        positionF2 = positionF1;
        positionF1 = pointPosition;
      }
      else if (distanceToPoint < distanceF2) {
        distanceF2 = distanceToPoint;
        offsetF2 = cellOffset;
        positionF2 = pointPosition;
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = distanceF2;
  octave.color = hash_int2_to_float3(cellPosition + offsetF2);
  octave.position = voronoi_position(positionF2 + cellPosition_f);
  return octave;
}

ccl_device float voronoi_distance_to_edge(const ccl_private VoronoiParams &params,
                                          const float2 coord)
{
  const float2 cellPosition_f = floor(coord);
  const float2 localPosition = coord - cellPosition_f;
  const int2 cellPosition = make_int2(cellPosition_f);

  float2 vectorToClosest = make_float2(0.0f, 0.0f);
  float minDistance = FLT_MAX;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      const int2 cellOffset = make_int2(i, j);
      const float2 vectorToPoint = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness -
                                   localPosition;
      const float distanceToPoint = dot(vectorToPoint, vectorToPoint);
      if (distanceToPoint < minDistance) {
        minDistance = distanceToPoint;
        vectorToClosest = vectorToPoint;
      }
    }
  }

  minDistance = FLT_MAX;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      const int2 cellOffset = make_int2(i, j);
      const float2 vectorToPoint = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness -
                                   localPosition;
      const float2 perpendicularToEdge = vectorToPoint - vectorToClosest;
      if (dot(perpendicularToEdge, perpendicularToEdge) > 0.0001f) {
        const float distanceToEdge = dot((vectorToClosest + vectorToPoint) / 2.0f,
                                         normalize(perpendicularToEdge));
        minDistance = min(minDistance, distanceToEdge);
      }
    }
  }

  return minDistance;
}

ccl_device float voronoi_n_sphere_radius(const ccl_private VoronoiParams &params,
                                         const float2 coord)
{
  const float2 cellPosition_f = floor(coord);
  const float2 localPosition = coord - cellPosition_f;
  const int2 cellPosition = make_int2(cellPosition_f);

  float2 closestPoint = make_float2(0.0f, 0.0f);
  int2 closestPointOffset = make_int2(0);
  float minDistanceSq = FLT_MAX;
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      const int2 cellOffset = make_int2(i, j);
      const float2 pointPosition = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness;
      const float distanceToPointSq = len_squared(pointPosition - localPosition);
      if (distanceToPointSq < minDistanceSq) {
        minDistanceSq = distanceToPointSq;
        closestPoint = pointPosition;
        closestPointOffset = cellOffset;
      }
    }
  }

  minDistanceSq = FLT_MAX;
  float2 closestPointToClosestPoint = make_float2(0.0f, 0.0f);
  for (int j = -1; j <= 1; j++) {
    for (int i = -1; i <= 1; i++) {
      if (i == 0 && j == 0) {
        continue;
      }
      const int2 cellOffset = make_int2(i, j) + closestPointOffset;
      const float2 pointPosition = make_float2(cellOffset) +
                                   hash_int2_to_float2(cellPosition + cellOffset) *
                                       params.randomness;
      const float distanceToPointSq = len_squared(closestPoint - pointPosition);
      if (distanceToPointSq < minDistanceSq) {
        minDistanceSq = distanceToPointSq;
        closestPointToClosestPoint = pointPosition;
      }
    }
  }

  return distance(closestPointToClosestPoint, closestPoint) / 2.0f;
}

/* **** 3D Voronoi **** */

ccl_device float4 voronoi_position(const float3 coord)
{
  return make_float4(coord);
}

ccl_device VoronoiOutput voronoi_f1(const ccl_private VoronoiParams &params, const float3 coord)
{
  const float3 cellPosition_f = floor(coord);
  const float3 localPosition = coord - cellPosition_f;
  const int3 cellPosition = make_int3(cellPosition_f);

  float minDistance = FLT_MAX;
  int3 targetOffset = make_int3(0);
  float3 targetPosition = make_float3(0.0f, 0.0f, 0.0f);
  for (int k = -1; k <= 1; k++) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        const int3 cellOffset = make_int3(i, j, k);
        const float3 pointPosition = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness;
        const float distanceToPoint = voronoi_distance_bound(pointPosition, localPosition, params);
        if (distanceToPoint < minDistance) {
          targetOffset = cellOffset;
          minDistance = distanceToPoint;
          targetPosition = pointPosition;
        }
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = voronoi_distance(targetPosition, localPosition, params);
  octave.color = hash_int3_to_float3(cellPosition + targetOffset);
  octave.position = voronoi_position(targetPosition + cellPosition_f);
  return octave;
}

ccl_device VoronoiOutput voronoi_smooth_f1(const ccl_private VoronoiParams &params,
                                           const float3 coord)
{
  const float3 cellPosition_f = floor(coord);
  const float3 localPosition = coord - cellPosition_f;
  const int3 cellPosition = make_int3(cellPosition_f);

  float smoothDistance = 0.0f;
  float3 smoothColor = make_float3(0.0f, 0.0f, 0.0f);
  float3 smoothPosition = make_float3(0.0f, 0.0f, 0.0f);
  float h = -1.0f;
  for (int k = -2; k <= 2; k++) {
    for (int j = -2; j <= 2; j++) {
      for (int i = -2; i <= 2; i++) {
        const int3 cellOffset = make_int3(i, j, k);
        const float3 pointPosition = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness;
        const float distanceToPoint = voronoi_distance(pointPosition, localPosition, params);
        h = h == -1.0f ?
                1.0f :
                smoothstep(0.0f,
                           1.0f,
                           0.5f + 0.5f * (smoothDistance - distanceToPoint) / params.smoothness);
        float correctionFactor = params.smoothness * h * (1.0f - h);
        smoothDistance = mix(smoothDistance, distanceToPoint, h) - correctionFactor;
        correctionFactor /= 1.0f + 3.0f * params.smoothness;
        const float3 cellColor = hash_int3_to_float3(cellPosition + cellOffset);
        smoothColor = mix(smoothColor, cellColor, h) - correctionFactor;
        smoothPosition = mix(smoothPosition, pointPosition, h) - correctionFactor;
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = smoothDistance;
  octave.color = smoothColor;
  octave.position = voronoi_position(cellPosition_f + smoothPosition);
  return octave;
}

ccl_device VoronoiOutput voronoi_f2(const ccl_private VoronoiParams &params, const float3 coord)
{
  const float3 cellPosition_f = floor(coord);
  const float3 localPosition = coord - cellPosition_f;
  const int3 cellPosition = make_int3(cellPosition_f);

  float distanceF1 = FLT_MAX;
  float distanceF2 = FLT_MAX;
  int3 offsetF1 = make_int3(0);
  float3 positionF1 = make_float3(0.0f, 0.0f, 0.0f);
  int3 offsetF2 = make_int3(0);
  float3 positionF2 = make_float3(0.0f, 0.0f, 0.0f);
  for (int k = -1; k <= 1; k++) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        const int3 cellOffset = make_int3(i, j, k);
        const float3 pointPosition = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness;
        const float distanceToPoint = voronoi_distance(pointPosition, localPosition, params);
        if (distanceToPoint < distanceF1) {
          distanceF2 = distanceF1;
          distanceF1 = distanceToPoint;
          offsetF2 = offsetF1;
          offsetF1 = cellOffset;
          positionF2 = positionF1;
          positionF1 = pointPosition;
        }
        else if (distanceToPoint < distanceF2) {
          distanceF2 = distanceToPoint;
          offsetF2 = cellOffset;
          positionF2 = pointPosition;
        }
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = distanceF2;
  octave.color = hash_int3_to_float3(cellPosition + offsetF2);
  octave.position = voronoi_position(positionF2 + cellPosition_f);
  return octave;
}

ccl_device float voronoi_distance_to_edge(const ccl_private VoronoiParams &params,
                                          const float3 coord)
{
  const float3 cellPosition_f = floor(coord);
  const float3 localPosition = coord - cellPosition_f;
  const int3 cellPosition = make_int3(cellPosition_f);

  float3 vectorToClosest = make_float3(0.0f, 0.0f, 0.0f);
  float minDistance = FLT_MAX;
  for (int k = -1; k <= 1; k++) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        const int3 cellOffset = make_int3(i, j, k);
        const float3 vectorToPoint = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness -
                                     localPosition;
        const float distanceToPoint = dot(vectorToPoint, vectorToPoint);
        if (distanceToPoint < minDistance) {
          minDistance = distanceToPoint;
          vectorToClosest = vectorToPoint;
        }
      }
    }
  }

  minDistance = FLT_MAX;
  for (int k = -1; k <= 1; k++) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        const int3 cellOffset = make_int3(i, j, k);
        const float3 vectorToPoint = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness -
                                     localPosition;
        const float3 perpendicularToEdge = vectorToPoint - vectorToClosest;
        if (dot(perpendicularToEdge, perpendicularToEdge) > 0.0001f) {
          const float distanceToEdge = dot((vectorToClosest + vectorToPoint) / 2.0f,
                                           normalize(perpendicularToEdge));
          minDistance = min(minDistance, distanceToEdge);
        }
      }
    }
  }

  return minDistance;
}

ccl_device float voronoi_n_sphere_radius(const ccl_private VoronoiParams &params,
                                         const float3 coord)
{
  const float3 cellPosition_f = floor(coord);
  const float3 localPosition = coord - cellPosition_f;
  const int3 cellPosition = make_int3(cellPosition_f);

  float3 closestPoint = make_float3(0.0f, 0.0f, 0.0f);
  int3 closestPointOffset = make_int3(0);
  float minDistanceSq = FLT_MAX;
  for (int k = -1; k <= 1; k++) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        const int3 cellOffset = make_int3(i, j, k);
        const float3 pointPosition = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness;
        const float distanceToPointSq = len_squared(pointPosition - localPosition);
        if (distanceToPointSq < minDistanceSq) {
          minDistanceSq = distanceToPointSq;
          closestPoint = pointPosition;
          closestPointOffset = cellOffset;
        }
      }
    }
  }

  minDistanceSq = FLT_MAX;
  float3 closestPointToClosestPoint = make_float3(0.0f, 0.0f, 0.0f);
  for (int k = -1; k <= 1; k++) {
    for (int j = -1; j <= 1; j++) {
      for (int i = -1; i <= 1; i++) {
        if (i == 0 && j == 0 && k == 0) {
          continue;
        }
        const int3 cellOffset = make_int3(i, j, k) + closestPointOffset;
        const float3 pointPosition = make_float3(cellOffset) +
                                     hash_int3_to_float3(cellPosition + cellOffset) *
                                         params.randomness;
        const float distanceToPointSq = len_squared(closestPoint - pointPosition);
        if (distanceToPointSq < minDistanceSq) {
          minDistanceSq = distanceToPointSq;
          closestPointToClosestPoint = pointPosition;
        }
      }
    }
  }

  return distance(closestPointToClosestPoint, closestPoint) / 2.0f;
}

/* **** 4D Voronoi **** */

ccl_device float4 voronoi_position(const float4 coord)
{
  return coord;
}

ccl_device VoronoiOutput voronoi_f1(const ccl_private VoronoiParams &params, const float4 coord)
{
  const float4 cellPosition_f = floor(coord);
  const float4 localPosition = coord - cellPosition_f;
  const int4 cellPosition = make_int4(cellPosition_f);

  float minDistance = FLT_MAX;
  int4 targetOffset = zero_int4();
  float4 targetPosition = zero_float4();
  for (int u = -1; u <= 1; u++) {
    for (int k = -1; k <= 1; k++) {
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          const int4 cellOffset = make_int4(i, j, k, u);
          const float4 pointPosition = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness;
          const float distanceToPoint = voronoi_distance_bound(
              pointPosition, localPosition, params);
          if (distanceToPoint < minDistance) {
            targetOffset = cellOffset;
            minDistance = distanceToPoint;
            targetPosition = pointPosition;
          }
        }
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = voronoi_distance(targetPosition, localPosition, params);
  octave.color = hash_int4_to_float3(cellPosition + targetOffset);
  octave.position = voronoi_position(targetPosition + cellPosition_f);
  return octave;
}

ccl_device VoronoiOutput voronoi_smooth_f1(const ccl_private VoronoiParams &params,
                                           const float4 coord)
{
  const float4 cellPosition_f = floor(coord);
  const float4 localPosition = coord - cellPosition_f;
  const int4 cellPosition = make_int4(cellPosition_f);

  float smoothDistance = 0.0f;
  float3 smoothColor = make_float3(0.0f, 0.0f, 0.0f);
  float4 smoothPosition = zero_float4();
  float h = -1.0f;
  for (int u = -2; u <= 2; u++) {
    for (int k = -2; k <= 2; k++) {
      for (int j = -2; j <= 2; j++) {
        for (int i = -2; i <= 2; i++) {
          const int4 cellOffset = make_int4(i, j, k, u);
          const float4 pointPosition = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness;
          const float distanceToPoint = voronoi_distance(pointPosition, localPosition, params);
          h = h == -1.0f ?
                  1.0f :
                  smoothstep(0.0f,
                             1.0f,
                             0.5f + 0.5f * (smoothDistance - distanceToPoint) / params.smoothness);
          float correctionFactor = params.smoothness * h * (1.0f - h);
          smoothDistance = mix(smoothDistance, distanceToPoint, h) - correctionFactor;
          correctionFactor /= 1.0f + 3.0f * params.smoothness;
          const float3 cellColor = hash_int4_to_float3(cellPosition + cellOffset);
          smoothColor = mix(smoothColor, cellColor, h) - correctionFactor;
          smoothPosition = mix(smoothPosition, pointPosition, h) - correctionFactor;
        }
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = smoothDistance;
  octave.color = smoothColor;
  octave.position = voronoi_position(cellPosition_f + smoothPosition);
  return octave;
}

ccl_device VoronoiOutput voronoi_f2(const ccl_private VoronoiParams &params, const float4 coord)
{
  const float4 cellPosition_f = floor(coord);
  const float4 localPosition = coord - cellPosition_f;
  const int4 cellPosition = make_int4(cellPosition_f);

  float distanceF1 = FLT_MAX;
  float distanceF2 = FLT_MAX;
  int4 offsetF1 = zero_int4();
  float4 positionF1 = zero_float4();
  int4 offsetF2 = zero_int4();
  float4 positionF2 = zero_float4();
  for (int u = -1; u <= 1; u++) {
    for (int k = -1; k <= 1; k++) {
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          const int4 cellOffset = make_int4(i, j, k, u);
          const float4 pointPosition = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness;
          const float distanceToPoint = voronoi_distance(pointPosition, localPosition, params);
          if (distanceToPoint < distanceF1) {
            distanceF2 = distanceF1;
            distanceF1 = distanceToPoint;
            offsetF2 = offsetF1;
            offsetF1 = cellOffset;
            positionF2 = positionF1;
            positionF1 = pointPosition;
          }
          else if (distanceToPoint < distanceF2) {
            distanceF2 = distanceToPoint;
            offsetF2 = cellOffset;
            positionF2 = pointPosition;
          }
        }
      }
    }
  }

  VoronoiOutput octave;
  octave.distance = distanceF2;
  octave.color = hash_int4_to_float3(cellPosition + offsetF2);
  octave.position = voronoi_position(positionF2 + cellPosition_f);
  return octave;
}

ccl_device float voronoi_distance_to_edge(const ccl_private VoronoiParams &params,
                                          const float4 coord)
{
  const float4 cellPosition_f = floor(coord);
  const float4 localPosition = coord - cellPosition_f;
  const int4 cellPosition = make_int4(cellPosition_f);

  float4 vectorToClosest = zero_float4();
  float minDistance = FLT_MAX;
  for (int u = -1; u <= 1; u++) {
    for (int k = -1; k <= 1; k++) {
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          const int4 cellOffset = make_int4(i, j, k, u);
          const float4 vectorToPoint = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness -
                                       localPosition;
          const float distanceToPoint = dot(vectorToPoint, vectorToPoint);
          if (distanceToPoint < minDistance) {
            minDistance = distanceToPoint;
            vectorToClosest = vectorToPoint;
          }
        }
      }
    }
  }

  minDistance = FLT_MAX;
  for (int u = -1; u <= 1; u++) {
    for (int k = -1; k <= 1; k++) {
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          const int4 cellOffset = make_int4(i, j, k, u);
          const float4 vectorToPoint = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness -
                                       localPosition;
          const float4 perpendicularToEdge = vectorToPoint - vectorToClosest;
          if (dot(perpendicularToEdge, perpendicularToEdge) > 0.0001f) {
            const float distanceToEdge = dot((vectorToClosest + vectorToPoint) / 2.0f,
                                             normalize(perpendicularToEdge));
            minDistance = min(minDistance, distanceToEdge);
          }
        }
      }
    }
  }

  return minDistance;
}

ccl_device float voronoi_n_sphere_radius(const ccl_private VoronoiParams &params,
                                         const float4 coord)
{
  const float4 cellPosition_f = floor(coord);
  const float4 localPosition = coord - cellPosition_f;
  const int4 cellPosition = make_int4(cellPosition_f);

  float4 closestPoint = zero_float4();
  int4 closestPointOffset = zero_int4();
  float minDistanceSq = FLT_MAX;
  for (int u = -1; u <= 1; u++) {
    for (int k = -1; k <= 1; k++) {
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          const int4 cellOffset = make_int4(i, j, k, u);
          const float4 pointPosition = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness;
          const float distanceToPointSq = len_squared(pointPosition - localPosition);
          if (distanceToPointSq < minDistanceSq) {
            minDistanceSq = distanceToPointSq;
            closestPoint = pointPosition;
            closestPointOffset = cellOffset;
          }
        }
      }
    }
  }

  minDistanceSq = FLT_MAX;
  float4 closestPointToClosestPoint = zero_float4();
  for (int u = -1; u <= 1; u++) {
    for (int k = -1; k <= 1; k++) {
      for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
          if (i == 0 && j == 0 && k == 0 && u == 0) {
            continue;
          }
          const int4 cellOffset = make_int4(i, j, k, u) + closestPointOffset;
          const float4 pointPosition = make_float4(cellOffset) +
                                       hash_int4_to_float4(cellPosition + cellOffset) *
                                           params.randomness;
          const float distanceToPointSq = len_squared(closestPoint - pointPosition);
          if (distanceToPointSq < minDistanceSq) {
            minDistanceSq = distanceToPointSq;
            closestPointToClosestPoint = pointPosition;
          }
        }
      }
    }
  }

  return distance(closestPointToClosestPoint, closestPoint) / 2.0f;
}

/* **** Fractal Voronoi **** */

/* The fractalization logic is the same as for fBM Noise, except that some additions are replaced
 * by lerps. */
template<typename T>
ccl_device VoronoiOutput fractal_voronoi_x_fx(const ccl_private VoronoiParams &params,
                                              const T coord)
{
  float amplitude = 1.0f;
  float max_amplitude = 0.0f;
  float scale = 1.0f;

  VoronoiOutput output;
  const bool zero_input = params.detail == 0.0f || params.roughness == 0.0f;

  for (int i = 0; i <= ceilf(params.detail); ++i) {
    VoronoiOutput octave = (params.feature == NODE_VORONOI_F2) ?
                               voronoi_f2(params, coord * scale) :
                           (params.feature == NODE_VORONOI_SMOOTH_F1 &&
                            params.smoothness != 0.0f) ?
                               voronoi_smooth_f1(params, coord * scale) :
                               voronoi_f1(params, coord * scale);

    if (zero_input) {
      max_amplitude = 1.0f;
      output = octave;
      break;
    }
    if (i <= params.detail) {
      max_amplitude += amplitude;
      output.distance += octave.distance * amplitude;
      output.color += octave.color * amplitude;
      output.position = mix(output.position, octave.position / scale, amplitude);
      scale *= params.lacunarity;
      amplitude *= params.roughness;
    }
    else {
      const float remainder = params.detail - floorf(params.detail);
      if (remainder != 0.0f) {
        max_amplitude = mix(max_amplitude, max_amplitude + amplitude, remainder);
        output.distance = mix(
            output.distance, output.distance + octave.distance * amplitude, remainder);
        output.color = mix(output.color, output.color + octave.color * amplitude, remainder);
        output.position = mix(
            output.position, mix(output.position, octave.position / scale, amplitude), remainder);
      }
    }
  }

  if (params.normalize) {
    output.distance /= max_amplitude * params.max_distance;
    output.color /= max_amplitude;
  }

  output.position = safe_divide(output.position, params.scale);

  return output;
}

/* The fractalization logic is the same as for fBM Noise, except that some additions are replaced
 * by lerps. */
template<typename T>
ccl_device float fractal_voronoi_distance_to_edge(const ccl_private VoronoiParams &params,
                                                  const T coord)
{
  float amplitude = 1.0f;
  float max_amplitude = params.max_distance;
  float scale = 1.0f;
  float distance = 8.0f;

  const bool zero_input = params.detail == 0.0f || params.roughness == 0.0f;

  for (int i = 0; i <= ceilf(params.detail); ++i) {
    const float octave_distance = voronoi_distance_to_edge(params, coord * scale);

    if (zero_input) {
      distance = octave_distance;
      break;
    }
    if (i <= params.detail) {
      max_amplitude = mix(max_amplitude, params.max_distance / scale, amplitude);
      distance = mix(distance, min(distance, octave_distance / scale), amplitude);
      scale *= params.lacunarity;
      amplitude *= params.roughness;
    }
    else {
      const float remainder = params.detail - floorf(params.detail);
      if (remainder != 0.0f) {
        const float lerp_amplitude = mix(max_amplitude, params.max_distance / scale, amplitude);
        max_amplitude = mix(max_amplitude, lerp_amplitude, remainder);
        const float lerp_distance = mix(
            distance, min(distance, octave_distance / scale), amplitude);
        distance = mix(distance, min(distance, lerp_distance), remainder);
      }
    }
  }

  if (params.normalize) {
    distance /= max_amplitude;
  }

  return distance;
}

ccl_device void svm_voronoi_output(const uint4 stack_offsets,
                                   ccl_private float *stack,
                                   const float distance,
                                   const float3 color,
                                   const float3 position,
                                   const float w,
                                   const float radius)
{
  uint distance_stack_offset;
  uint color_stack_offset;
  uint position_stack_offset;
  uint w_out_stack_offset;
  uint radius_stack_offset;
  uint unused;

  svm_unpack_node_uchar4(
      stack_offsets.z, &unused, &unused, &distance_stack_offset, &color_stack_offset);
  svm_unpack_node_uchar3(
      stack_offsets.w, &position_stack_offset, &w_out_stack_offset, &radius_stack_offset);

  if (stack_valid(distance_stack_offset)) {
    stack_store_float(stack, distance_stack_offset, distance);
  }
  if (stack_valid(color_stack_offset)) {
    stack_store_float3(stack, color_stack_offset, color);
  }
  if (stack_valid(position_stack_offset)) {
    stack_store_float3(stack, position_stack_offset, position);
  }
  if (stack_valid(w_out_stack_offset)) {
    stack_store_float(stack, w_out_stack_offset, w);
  }
  if (stack_valid(radius_stack_offset)) {
    stack_store_float(stack, radius_stack_offset, radius);
  }
}

template<uint node_feature_mask>
ccl_device_noinline int svm_node_tex_voronoi(KernelGlobals kg,
                                             ccl_private float *stack,
                                             const uint dimensions,
                                             const uint feature,
                                             const uint metric,
                                             int offset)
{
  /* Read node defaults and stack offsets. */
  const uint4 stack_offsets = read_node(kg, &offset);
  const uint4 defaults1 = read_node(kg, &offset);
  const uint4 defaults2 = read_node(kg, &offset);

  uint coord_stack_offset;
  uint w_stack_offset;
  uint scale_stack_offset;
  uint detail_stack_offset;
  uint roughness_stack_offset;
  uint lacunarity_stack_offset;
  uint smoothness_stack_offset;
  uint exponent_stack_offset;
  uint randomness_stack_offset;
  uint normalize;

  svm_unpack_node_uchar4(stack_offsets.x,
                         &coord_stack_offset,
                         &w_stack_offset,
                         &scale_stack_offset,
                         &detail_stack_offset);
  svm_unpack_node_uchar4(stack_offsets.y,
                         &roughness_stack_offset,
                         &lacunarity_stack_offset,
                         &smoothness_stack_offset,
                         &exponent_stack_offset);
  svm_unpack_node_uchar2(stack_offsets.z, &randomness_stack_offset, &normalize);

  /* Read from stack. */
  float3 coord = stack_load_float3(stack, coord_stack_offset);
  float w = stack_load_float_default(stack, w_stack_offset, defaults1.x);

  VoronoiParams params;
  params.feature = (NodeVoronoiFeature)feature;
  params.metric = (NodeVoronoiDistanceMetric)metric;
  params.scale = stack_load_float_default(stack, scale_stack_offset, defaults1.y);
  params.detail = stack_load_float_default(stack, detail_stack_offset, defaults1.z);
  params.roughness = stack_load_float_default(stack, roughness_stack_offset, defaults1.w);
  params.lacunarity = stack_load_float_default(stack, lacunarity_stack_offset, defaults2.x);
  params.smoothness = stack_load_float_default(stack, smoothness_stack_offset, defaults2.y);
  params.exponent = stack_load_float_default(stack, exponent_stack_offset, defaults2.z);
  params.randomness = stack_load_float_default(stack, randomness_stack_offset, defaults2.w);
  params.max_distance = 0.0f;
  params.normalize = normalize;

  params.detail = clamp(params.detail, 0.0f, 15.0f);
  params.roughness = clamp(params.roughness, 0.0f, 1.0f);
  params.randomness = clamp(params.randomness, 0.0f, 1.0f);
  params.smoothness = clamp(params.smoothness / 2.0f, 0.0f, 0.5f);

  coord *= params.scale;
  w *= params.scale;

  /* Compute output, specialized for each dimension. */
  switch (params.feature) {
    case NODE_VORONOI_DISTANCE_TO_EDGE: {
      float distance = 0.0f;
      params.max_distance = 0.5f + 0.5f * params.randomness;
      switch (dimensions) {
        case 1:
          distance = fractal_voronoi_distance_to_edge(params, w);
          break;
        case 2:
          distance = fractal_voronoi_distance_to_edge(params, make_float2(coord));
          break;
        case 3:
          distance = fractal_voronoi_distance_to_edge(params, coord);
          break;
        case 4:
          distance = fractal_voronoi_distance_to_edge(params, make_float4(coord, w));
          break;
        default:
          kernel_assert(0);
          break;
      }

      svm_voronoi_output(stack_offsets, stack, distance, zero_float3(), zero_float3(), 0.0f, 0.0f);
      break;
    }
    case NODE_VORONOI_N_SPHERE_RADIUS: {
      float radius = 0.0f;
      switch (dimensions) {
        case 1:
          radius = voronoi_n_sphere_radius(params, w);
          break;
        case 2:
          radius = voronoi_n_sphere_radius(params, make_float2(coord));
          break;
        case 3:
          radius = voronoi_n_sphere_radius(params, coord);
          break;
        case 4:
          radius = voronoi_n_sphere_radius(params, make_float4(coord, w));
          break;
        default:
          kernel_assert(0);
          break;
      }

      svm_voronoi_output(stack_offsets, stack, 0.0f, zero_float3(), zero_float3(), 0.0f, radius);
      break;
    }
    default: {
      VoronoiOutput output;
      switch (dimensions) {
        case 1:
          params.max_distance = (0.5f + 0.5f * params.randomness) *
                                ((params.feature == NODE_VORONOI_F2) ? 2.0f : 1.0f);
          output = fractal_voronoi_x_fx(params, w);
          break;
        case 2:
          IF_KERNEL_NODES_FEATURE(VORONOI_EXTRA)
          {
            params.max_distance = voronoi_distance(zero_float2(),
                                                   make_float2(0.5f + 0.5f * params.randomness,
                                                               0.5f + 0.5f * params.randomness),
                                                   params) *
                                  ((params.feature == NODE_VORONOI_F2) ? 2.0f : 1.0f);
            output = fractal_voronoi_x_fx(params, make_float2(coord));
          }
          break;
        case 3:
          IF_KERNEL_NODES_FEATURE(VORONOI_EXTRA)
          {
            params.max_distance = voronoi_distance(zero_float3(),
                                                   make_float3(0.5f + 0.5f * params.randomness,
                                                               0.5f + 0.5f * params.randomness,
                                                               0.5f + 0.5f * params.randomness),
                                                   params) *
                                  ((params.feature == NODE_VORONOI_F2) ? 2.0f : 1.0f);
            output = fractal_voronoi_x_fx(params, coord);
          }
          break;
        case 4:
          IF_KERNEL_NODES_FEATURE(VORONOI_EXTRA)
          {
            params.max_distance = voronoi_distance(zero_float4(),
                                                   make_float4(0.5f + 0.5f * params.randomness,
                                                               0.5f + 0.5f * params.randomness,
                                                               0.5f + 0.5f * params.randomness,
                                                               0.5f + 0.5f * params.randomness),
                                                   params) *
                                  ((params.feature == NODE_VORONOI_F2) ? 2.0f : 1.0f);
            output = fractal_voronoi_x_fx(params, make_float4(coord, w));
          }
          break;
        default:
          kernel_assert(0);
          break;
      }

      svm_voronoi_output(stack_offsets,
                         stack,
                         output.distance,
                         output.color,
                         make_float3(output.position),
                         output.position.w,
                         0.0f);
      break;
    }
  }

  return offset;
}

CCL_NAMESPACE_END
