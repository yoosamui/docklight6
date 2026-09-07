// ------------------------------------------------------------
// Docklight 6.0
//
// File: thumbnail_disk_cache.h
// Purpose: Bound persisted thumbnail pairs independently of GTK and capture.
// Ownership: Operates only on recognized regular cache files in its directory.
// ------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <set>
#include <string>

namespace ThumbnailDiskCache
{
    struct Limits
    {
        std::size_t entries = 256;
        std::uintmax_t bytes = 128 * 1024 * 1024;
        std::time_t age_seconds = 30 * 24 * 60 * 60;
    };

    // Removes incomplete pairs and temporary files, then retains the newest
    // complete pairs within all limits. Returns invalidated SHA-256 stems.
    std::set<std::string> prune(
        const std::string &directory,
        Limits limits = {},
        std::time_t now = std::time(nullptr));
}
