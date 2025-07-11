/* SPDX-FileCopyrightText: 2021 Tangent Animation. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "usd.hh"
#include "usd_reader_xform.hh"

struct Main;

namespace blender::io::usd {

class USDLightReader : public USDXformReader {

 public:
  USDLightReader(const pxr::UsdPrim &prim,
                 const USDImportParams &import_params,
                 const ImportSettings &settings)
      : USDXformReader(prim, import_params, settings)
  {
  }

  void create_object(Main *bmain) override;

  void read_object_data(Main *bmain, pxr::UsdTimeCode time) override;
};

}  // namespace blender::io::usd
