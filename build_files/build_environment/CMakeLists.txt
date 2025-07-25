# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

##################################################################################################
#
# This is a build system used by platform maintainers to build library dependencies.
# For users building Blender, we recommend using the pre-compiled libraries from `lib/`
# on all platforms.
#
# This CMake project is usually built through wrappers:
# * Windows: `build_files/build_environment/windows/build_deps.cmd`.
# * macOS and Linux: `make deps` from the blender directory.
#
##################################################################################################

cmake_minimum_required(VERSION 3.10)
project("BlenderDependencies")
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW) # CMake 3.24+ Set the date/time for extracted files to time of extraction
endif()
include(ExternalProject)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake/Modules")

include(cmake/check_software.cmake)
include(cmake/options.cmake)
# `versions.cmake` needs to be included after `options.cmake`
# due to the `BLENDER_PLATFORM_ARM` variable being needed.
include(cmake/versions.cmake)
include(cmake/download.cmake)
include(cmake/macros.cmake)
# `setup_msys2.cmake` to install `perl`,
# which HIP needs to function for the compiler detection to work properly,
# hence needs to be included before `check_compilers.cmake`.
if(ENABLE_MSYS2)
  include(cmake/setup_msys2.cmake)
else()
  set(mingw_LIBDIR ${LIBDIR})
endif()
include(cmake/check_compilers.cmake)
include(cmake/harvest.cmake)

include(cmake/ssl.cmake)
include(cmake/zlib.cmake)
include(cmake/zstd.cmake)
include(cmake/openal.cmake)
include(cmake/png.cmake)
include(cmake/jpeg.cmake)
include(cmake/blosc.cmake)
include(cmake/pthreads.cmake)
include(cmake/deflate.cmake)
include(cmake/imath.cmake)
include(cmake/openexr.cmake)
include(cmake/brotli.cmake)
include(cmake/freetype.cmake)
include(cmake/epoxy.cmake)
include(cmake/alembic.cmake)
include(cmake/opensubdiv.cmake)
include(cmake/sdl.cmake)
if(UNIX)
  include(cmake/nasm.cmake)
endif()
include(cmake/tiff.cmake)
if(WIN32)
  include(cmake/flexbison.cmake)
elseif(UNIX AND NOT APPLE)
  include(cmake/flex.cmake)
endif()
include(cmake/tbb.cmake)
include(cmake/python.cmake)
include(cmake/llvm.cmake)
include(cmake/osl.cmake)
include(cmake/cython.cmake)
include(cmake/numpy.cmake)
include(cmake/zstandard.cmake)
include(cmake/python_site_packages.cmake)
include(cmake/package_python.cmake)
include(cmake/openimageio.cmake)
include(cmake/usd.cmake)
include(cmake/materialx.cmake)
include(cmake/openvdb.cmake)
include(cmake/potrace.cmake)
include(cmake/haru.cmake)
include(cmake/pugixml.cmake)
include(cmake/fribidi.cmake)
include(cmake/harfbuzz.cmake)
if(NOT APPLE)
  include(cmake/xr_openxr.cmake)
  if(NOT BLENDER_PLATFORM_ARM)
    include(cmake/hiprt.cmake)
    include(cmake/dpcpp.cmake)
    include(cmake/dpcpp_deps.cmake)
    if(NOT WIN32)
      include(cmake/igc.cmake)
      include(cmake/gmmlib.cmake)
      include(cmake/ocloc.cmake)
    endif()
  endif()
endif()
include(cmake/ispc.cmake)
if(BLENDER_PLATFORM_WINDOWS_ARM)
  include(cmake/openpgl_windows_arm.cmake)
else()
  include(cmake/openpgl.cmake)
endif()
# EMBREE needs to be included after `dpcpp` as it uses it for compiling with GPU support
if(BLENDER_PLATFORM_WINDOWS_ARM)
  # WoA needs EMBREE to be built with the VS Generator + LLVM,
  # put it in its own file to avoid clutter.
  include(cmake/embree_windows_arm.cmake)
else()
  include(cmake/embree.cmake)
endif()
include(cmake/fmt.cmake)
include(cmake/robinmap.cmake)
include(cmake/xml2.cmake)
# OpenColorIO and dependencies.
include(cmake/expat.cmake)
include(cmake/pystring.cmake)
include(cmake/yamlcpp.cmake)
include(cmake/minizipng.cmake)
include(cmake/opencolorio.cmake)

if(BLENDER_PLATFORM_ARM)
  include(cmake/sse2neon.cmake)
endif()

include(cmake/webp.cmake)
if(NOT APPLE)
  include(cmake/level-zero.cmake)
endif()

if(NOT WIN32 OR ENABLE_MSYS2)
  include(cmake/gmp.cmake)
  include(cmake/openjpeg.cmake)
  if(NOT WIN32 OR BUILD_MODE STREQUAL Release)
    include(cmake/openimagedenoise.cmake)
    include(cmake/lame.cmake)
    include(cmake/ogg.cmake)
    include(cmake/vorbis.cmake)
    include(cmake/theora.cmake)
    include(cmake/opus.cmake)
    include(cmake/vpx.cmake)
    include(cmake/x264.cmake)
    include(cmake/x265.cmake)
    include(cmake/aom.cmake)
    include(cmake/ffmpeg.cmake)
    include(cmake/fftw.cmake)
    include(cmake/flac.cmake)
    include(cmake/sndfile.cmake)
    if(UNIX)
      if(NOT APPLE)
        include(cmake/spnav.cmake)
        include(cmake/jemalloc.cmake)
      endif()
    endif()
  endif()
endif()

if(UNIX)
  include(cmake/bzip2.cmake)
  include(cmake/ffi.cmake)
  include(cmake/lzma.cmake)
  include(cmake/sqlite.cmake)
endif()

if(UNIX AND NOT APPLE)
  include(cmake/libglu.cmake)
  include(cmake/mesa.cmake)
  include(cmake/wayland_protocols.cmake)
  # Can be removed when the build-bot upgrades to v1.20.x or newer.
  include(cmake/wayland.cmake)
  include(cmake/wayland_libdecor.cmake)
  include(cmake/wayland_weston.cmake)
endif()
include(cmake/shaderc_deps.cmake)
include(cmake/shaderc.cmake)
include(cmake/vulkan.cmake)
include(cmake/vulkan-memory-allocator.cmake)
include(cmake/spirv-reflect.cmake)
include(cmake/pybind11.cmake)
include(cmake/nanobind.cmake)
include(cmake/manifold.cmake)
# Keep these last.
include(cmake/deps_html.cmake)
include(cmake/cve_check.cmake)

if(UNIX)
  # Strip all installed libraries.
  harvest_strip_all_libraries()
endif()
