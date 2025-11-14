# SPDX-FileCopyrightText: 2025 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(LIBHEIF_EXTRA_ARGS
  -DAOM_DECODER=ON
  -DAOM_DECODER=OFF
  -DAOM_ROOT=${LIBDIR}/aom/
  -DBUILD_SHARED_LIBS=OFF
  -DWITH_EXAMPLES=OFF
  -DWITH_EXAMPLE_HEIF_VIEW=OFF
  -DWITH_GDK_PIXBUF=OFF
)

ExternalProject_Add(external_libheif
  URL file://${PACKAGE_DIR}/${LIBHEIF_FILE}
  DOWNLOAD_DIR ${DOWNLOAD_DIR}
  URL_HASH ${LIBHEIF_HASH_TYPE}=${LIBHEIF_HASH}
  CMAKE_GENERATOR ${PLATFORM_ALT_GENERATOR}
  PREFIX ${BUILD_DIR}/libheif

  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${LIBDIR}/libheif
    ${DEFAULT_CMAKE_FLAGS}
    ${LIBHEIF_EXTRA_ARGS}

  INSTALL_DIR ${LIBDIR}/libheif
)


if(WIN32)
    ExternalProject_Add_Step(external_libheif after_install
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${LIBDIR}/libheif
        ${HARVEST_TARGET}/libheif

      DEPENDEES install
    )
else()
  # TODO 
endif()