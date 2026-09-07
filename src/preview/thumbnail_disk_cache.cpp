// ------------------------------------------------------------
// Docklight 6.0
//
// File: thumbnail_disk_cache.cpp
// Purpose: Evict old thumbnail pairs and interrupted writes from disk.
// Decisions: PNG mtime orders retention; identity bytes count toward the cap.
// Unknown files and nonregular entries are left alone. No directory recursion.
// ------------------------------------------------------------

#include "thumbnail_disk_cache.h"

#include <glib/gstdio.h>

#include <algorithm>
#include <map>
#include <vector>

namespace ThumbnailDiskCache
{
    std::set<std::string> prune(
        const std::string &directory,
        Limits limits,
        std::time_t now)
    {
        struct Pair
        {
            std::string stem;
            bool image = false;
            bool identity = false;
            std::uintmax_t bytes = 0;
            std::time_t modified = 0;
        };

        std::set<std::string> removed;
        auto *dir = g_dir_open(directory.c_str(), 0, nullptr);
        if (!dir)
            return removed;

        std::map<std::string, Pair> pairs;
        while (const auto *name = g_dir_read_name(dir))
        {
            const std::string filename(name);
            if (filename.size() <= 64 ||
                filename.substr(0, 64).find_first_not_of(
                    "0123456789abcdef") != std::string::npos)
            {
                continue;
            }

            const auto suffix = filename.substr(64);
            if (suffix != ".png" && suffix != ".identity" &&
                suffix != ".png.tmp" && suffix != ".identity.tmp")
            {
                continue;
            }

            const auto path = directory + G_DIR_SEPARATOR_S + filename;
            GStatBuf info;
            if (g_lstat(path.c_str(), &info) != 0 ||
                !S_ISREG(info.st_mode))
            {
                continue;
            }

            if (suffix == ".png.tmp" || suffix == ".identity.tmp")
            {
                g_unlink(path.c_str());
                continue;
            }

            const auto stem = filename.substr(0, 64);
            auto &pair = pairs[stem];
            pair.stem = stem;
            pair.bytes += static_cast<std::uintmax_t>(info.st_size);
            if (suffix == ".png")
            {
                pair.image = true;
                pair.modified = info.st_mtime;
            }
            else
            {
                pair.identity = true;
            }
        }
        g_dir_close(dir);

        std::vector<Pair> ordered;
        for (const auto &entry : pairs)
            ordered.push_back(entry.second);
        std::sort(ordered.begin(), ordered.end(),
            [](const Pair &left, const Pair &right)
            {
                if (left.modified != right.modified)
                    return left.modified > right.modified;
                return left.stem < right.stem;
            });

        std::size_t retained = 0;
        std::uintmax_t bytes = 0;
        for (const auto &pair : ordered)
        {
            const bool expired = now > pair.modified &&
                std::difftime(now, pair.modified) > limits.age_seconds;
            if (pair.image && pair.identity && !expired &&
                retained < limits.entries &&
                pair.bytes <= limits.bytes - bytes)
            {
                ++retained;
                bytes += pair.bytes;
                continue;
            }

            const auto base = directory + G_DIR_SEPARATOR_S + pair.stem;
            if (pair.image)
                g_unlink((base + ".png").c_str());
            if (pair.identity)
                g_unlink((base + ".identity").c_str());
            removed.insert(pair.stem);
        }
        return removed;
    }
}
