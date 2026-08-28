// Aseprite
// Copyright (C) 2001-2015  David Capello
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.

#pragma once

#include "base/recent_items.h"
#include "base/signal.h"

#include <string>

namespace app {

  class RecentFiles {
  public:
    typedef base::RecentItems<std::string> List;
    typedef List::iterator iterator;
    typedef List::const_iterator const_iterator;

    // Iterate through recent files.
    const_iterator files_begin() { return m_files.begin(); }
    const_iterator files_end() { return m_files.end(); }

    // Iterate through recent paths.
    const_iterator paths_begin() { return m_paths.begin(); }
    const_iterator paths_end() { return m_paths.end(); }

    RecentFiles();

    void addRecentFile(const char* filename);
    void removeRecentFile(const char* filename);

    base::Signal0<void> Changed;

  private:
    // Writes both lists to the configuration file. Called on every
    // change: the app is not always given the chance to shut down
    // cleanly (on iOS the system just kills it while suspended), so
    // saving at exit would lose the list every time.
    void save();

    std::string normalizePath(std::string fn);

    List m_files;
    List m_paths;
  };

} // namespace app
