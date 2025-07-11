/* SPDX-FileCopyrightText: 2009 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup GHOST
 */

#pragma once

#include <optional>
#include <string>

#include "GHOST_Types.h"

class GHOST_ISystemPaths {
 public:
  /**
   * Creates the one and only system.
   * \return An indication of success.
   */
  static GHOST_TSuccess create();

  /**
   * Disposes the one and only system.
   * \return An indication of success.
   */
  static GHOST_TSuccess dispose();

  /**
   * Returns a pointer to the one and only system (nil if it hasn't been created).
   * \return A pointer to the system.
   */
  static GHOST_ISystemPaths *get();

 protected:
  /**
   * Constructor.
   * Protected default constructor to force use of static createSystem member.
   */
  GHOST_ISystemPaths() {}

  /**
   * Destructor.
   * Protected default constructor to force use of static dispose member.
   */
  virtual ~GHOST_ISystemPaths() = default;

 public:
  /**
   * Determine the base directory in which shared resources are located. It will first try to use
   * "unpack and run" path, then look for properly installed path, including versioning.
   * \return Unsigned char string pointing to system directory (eg `/usr/share/blender/`).
   */
  virtual const char *getSystemDir(int version, const char *versionstr) const = 0;

  /**
   * Determine the base directory in which user configuration is stored, including versioning.
   * If needed, it will create the base directory.
   * \return Unsigned char string pointing to user directory (eg `~/.blender/`).
   */
  virtual const char *getUserDir(int version, const char *versionstr) const = 0;

  /**
   * Determine a special ("well known") and easy to reach user directory.
   * \return If successfull, a string containing the user directory path (eg `~/Documents/`).
   */
  virtual std::optional<std::string> getUserSpecialDir(GHOST_TUserSpecialDirTypes type) const = 0;

  /**
   * Determine the directory of the current binary
   * \return Unsigned char string pointing to the binary directory.
   */
  virtual const char *getBinaryDir() const = 0;

  /**
   * Add the file to the operating system most recently used files
   */
  virtual void addToSystemRecentFiles(const char *filepath) const = 0;

 private:
  /** The one and only system paths. */
  static GHOST_ISystemPaths *m_systemPaths;

  MEM_CXX_CLASS_ALLOC_FUNCS("GHOST:GHOST_ISystemPaths")
};
