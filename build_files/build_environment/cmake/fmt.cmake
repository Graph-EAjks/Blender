# SPDX-FileCopyrightText: 2022 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(FMT_EXTRA_ARGS
  -DFMT_TEST=OFF
  -DFMT_DOC=OFF
)

ExternalProject_Add(external_fmt
  URL file://${PACKAGE_DIR}/${FMT_FILE}
  DOWNLOAD_DIR ${DOWNLOAD_DIR}
  URL_HASH ${FMT_HASH_TYPE}=${FMT_HASH}
  PREFIX ${BUILD_DIR}/fmt

  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${LIBDIR}/fmt
    ${DEFAULT_CMAKE_FLAGS}
    ${FMT_EXTRA_ARGS}

  INSTALL_DIR ${LIBDIR}/fmt
)

if(WIN32)
  # TODO
else()
  harvest(external_fmt fmt/include fmt/include "*.h")
  harvest(external_fmr fmt/lib/cmake/fmt fmt/lib/cmake/fmt "*.cmake")
  harvest(external_fmr fmt/lib fmt/lib "*.a")
endif()
