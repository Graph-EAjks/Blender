/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

/**
 * Copyright (C) 2001 NaN Technologies B.V.
 */

#include <vector>

#include "GHOST_ISystem.hh"
#include "GHOST_SystemHeadless.hh"

#if defined(WITH_GHOST_X11) && defined(WITH_GHOST_WAYLAND)
#  include "GHOST_SystemWayland.hh"
#  include "GHOST_SystemX11.hh"
#elif defined(WITH_GHOST_X11)
#  include "GHOST_SystemX11.hh"
#elif defined(WITH_GHOST_WAYLAND)
#  include "GHOST_SystemWayland.hh"
#elif defined(WITH_GHOST_SDL)
#  include "GHOST_SystemSDL.hh"
#elif defined(WIN32)
#  include "GHOST_SystemWin32.hh"
#elif defined(__APPLE__)
#  include "GHOST_SystemCocoa.hh"
#endif

#include "CLG_log.h"

static CLG_LogRef LOG = {"ghost.system"};

GHOST_ISystem *GHOST_ISystem::m_system = nullptr;
const char *GHOST_ISystem::m_system_backend_id = nullptr;

GHOST_TBacktraceFn GHOST_ISystem::m_backtrace_fn = nullptr;

GHOST_TSuccess GHOST_ISystem::createSystem(bool verbose, [[maybe_unused]] bool background)
{

  /* When GHOST fails to start, report the back-ends that were attempted.
   * A Verbose argument could be supported in printing isn't always desired. */
  struct GHOST_BackendInfo {
    const char *id = nullptr;
    /** The cause of the failure. */
    std::string failure_msg;
  };
  std::vector<GHOST_BackendInfo> backends_attempted;

  GHOST_TSuccess success;
  if (!m_system) {

#if defined(WITH_HEADLESS)
    /* Pass. */
#elif defined(WITH_GHOST_WAYLAND)
#  if defined(WITH_GHOST_WAYLAND_DYNLOAD)
    const bool has_wayland_libraries = ghost_wl_dynload_libraries_init();
#  else
    const bool has_wayland_libraries = true;
#  endif
#endif

#if defined(WITH_HEADLESS)
    /* Pass. */
#elif defined(WITH_GHOST_X11) && defined(WITH_GHOST_WAYLAND)
    /* Special case, try Wayland, fall back to X11. */
    if (has_wayland_libraries) {
      backends_attempted.push_back({"WAYLAND"});
      try {
        CLOG_INFO(&LOG, "Create Wayland system");
        m_system = new GHOST_SystemWayland(background);
      }
      catch (const std::runtime_error &e) {
        if (verbose) {
          backends_attempted.back().failure_msg = e.what();
        }
        CLOG_INFO(&LOG, "Wayland system not created, falling back to X11");
        delete m_system;
        m_system = nullptr;
#  ifdef WITH_GHOST_WAYLAND_DYNLOAD
        ghost_wl_dynload_libraries_exit();
#  endif
      }
    }
    else {
      m_system = nullptr;
    }

    if (!m_system) {
      /* Try to fall back to X11. */
      backends_attempted.push_back({"X11"});
      try {
        CLOG_INFO(&LOG, "Create X11 system");
        m_system = new GHOST_SystemX11();
      }
      catch (const std::runtime_error &e) {
        if (verbose) {
          backends_attempted.back().failure_msg = e.what();
        }
        delete m_system;
        m_system = nullptr;
      }
    }
#elif defined(WITH_GHOST_X11)
    backends_attempted.push_back({"X11"});
    try {
      CLOG_INFO(&LOG, "Create X11 system");
      m_system = new GHOST_SystemX11();
    }
    catch (const std::runtime_error &e) {
      if (verbose) {
        backends_attempted.back().failure_msg = e.what();
      }
      delete m_system;
      m_system = nullptr;
    }
#elif defined(WITH_GHOST_WAYLAND)
    if (has_wayland_libraries) {
      backends_attempted.push_back({"WAYLAND"});
      try {
        CLOG_INFO(&LOG, "Create Wayland system");
        m_system = new GHOST_SystemWayland(background);
      }
      catch (const std::runtime_error &e) {
        if (verbose) {
          backends_attempted.back().failure_msg = e.what();
        }
        delete m_system;
        m_system = nullptr;
#  ifdef WITH_GHOST_WAYLAND_DYNLOAD
        ghost_wl_dynload_libraries_exit();
#  endif
      }
    }
    else {
      m_system = nullptr;
    }
#elif defined(WITH_GHOST_SDL)
    backends_attempted.push_back({"SDL"});
    try {
      CLOG_INFO(&LOG, "Create SDL system");
      m_system = new GHOST_SystemSDL();
    }
    catch (const std::runtime_error &e) {
      if (verbose) {
        backends_attempted.back().failure_msg = e.what();
      }
      delete m_system;
      m_system = nullptr;
    }
#elif defined(WIN32)
    backends_attempted.push_back({"WIN32"});
    CLOG_INFO(&LOG, "Create Windows system");
    m_system = new GHOST_SystemWin32();
#elif defined(__APPLE__)
    backends_attempted.push_back({"COCOA"});
    CLOG_INFO(&LOG, "Create Cocoa system");
    m_system = new GHOST_SystemCocoa();
#endif

    if (m_system) {
      m_system_backend_id = backends_attempted.back().id;
    }
    else if (verbose || CLOG_CHECK(&LOG, CLG_LEVEL_INFO)) {
      bool show_messages = false;
      std::string msg = "Failed to initialize display for back-end(s): [";
      for (int i = 0; i < backends_attempted.size(); i++) {
        const GHOST_BackendInfo &backend_item = backends_attempted[i];
        if (i != 0) {
          msg += ", ";
        }
        msg += "'" + std::string(backend_item.id) + "'";
        if (!backend_item.failure_msg.empty()) {
          show_messages = true;
        }
      }
      msg += "]\n";
      if (show_messages) {
        for (int i = 0; i < backends_attempted.size(); i++) {
          const GHOST_BackendInfo &backend_item = backends_attempted[i];
          msg += "  '";
          msg += backend_item.id;
          msg += "': ";
          msg += backend_item.failure_msg.empty() ? "<unknown>" : backend_item.failure_msg.c_str();
          msg += "\n";
        }
      }
      CLOG_STR_INFO_NOCHECK(&LOG, msg.c_str());
    }
    success = m_system != nullptr ? GHOST_kSuccess : GHOST_kFailure;
  }
  else {
    success = GHOST_kFailure;
  }
  if (success) {
    success = m_system->init();
  }
  return success;
}

GHOST_TSuccess GHOST_ISystem::createSystemBackground()
{
  GHOST_TSuccess success;
  if (!m_system) {
#if !defined(WITH_HEADLESS)
    /* Try to create a off-screen render surface with the graphical systems. */
    CLOG_INFO(&LOG, "Create background system");
    success = createSystem(false, true);
    if (success) {
      return success;
    }
    /* Try to fall back to headless mode if all else fails. */
#endif
    CLOG_INFO(&LOG, "Create headless system");
    m_system = new GHOST_SystemHeadless();
    success = m_system != nullptr ? GHOST_kSuccess : GHOST_kFailure;
  }
  else {
    success = GHOST_kFailure;
  }
  if (success) {
    success = m_system->init();
  }
  return success;
}

GHOST_TSuccess GHOST_ISystem::disposeSystem()
{
  CLOG_DEBUG(&LOG, "Dispose system");
  GHOST_TSuccess success = GHOST_kSuccess;
  if (m_system) {
    delete m_system;
    m_system = nullptr;
  }
  else {
    success = GHOST_kFailure;
  }
  return success;
}

GHOST_ISystem *GHOST_ISystem::getSystem()
{
  return m_system;
}

const char *GHOST_ISystem::getSystemBackend()
{
  return m_system_backend_id;
}

GHOST_TBacktraceFn GHOST_ISystem::getBacktraceFn()
{
  return GHOST_ISystem::m_backtrace_fn;
}

void GHOST_ISystem::setBacktraceFn(GHOST_TBacktraceFn backtrace_fn)
{
  GHOST_ISystem::m_backtrace_fn = backtrace_fn;
}
