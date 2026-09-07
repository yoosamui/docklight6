// ------------------------------------------------------------
// Docklight 6.0
//
// File: thumbnail_disk_cache_test.cpp
// Purpose: Exercise retention against real files in an isolated directory.
// Decisions: Small injected limits test count, bytes, age and orphan cleanup
// without a desktop session or access to the user's thumbnail cache.
// ------------------------------------------------------------

#include "preview/thumbnail_disk_cache.h"

#include <glib/gstdio.h>

#include <cassert>
#include <filesystem>
#include <string>
#include <utime.h>

int main()
{
    auto *temporary = g_dir_make_tmp("docklight-cache-test-XXXXXX", nullptr);
    assert(temporary);
    const std::string directory(temporary);
    g_free(temporary);
    constexpr std::time_t now = 2000000000;
    const auto path = [&](char id, const std::string &suffix)
    {
        return directory + "/" + std::string(64, id) + suffix;
    };
    const auto write = [&](char id, const std::string &suffix,
                           std::size_t size, std::time_t modified)
    {
        const std::string contents(size, 'x');
        assert(g_file_set_contents(path(id, suffix).c_str(),
            contents.data(), contents.size(), nullptr));
        struct utimbuf times { modified, modified };
        assert(g_utime(path(id, suffix).c_str(), &times) == 0);
    };
    const auto pair = [&](char id, std::time_t modified)
    {
        write(id, ".png", 10, modified);
        write(id, ".identity", 5, modified);
    };
    const auto exists = [&](char id, const std::string &suffix)
    {
        return g_file_test(path(id, suffix).c_str(), G_FILE_TEST_EXISTS);
    };

    pair('a', now - 20);
    pair('b', now - 10);
    pair('c', now);
    auto removed = ThumbnailDiskCache::prune(directory, {2, 100, 100}, now);
    assert(removed.count(std::string(64, 'a')) == 1);
    assert(!exists('a', ".png") && !exists('a', ".identity"));
    assert(exists('b', ".png") && exists('c', ".identity"));

    // Byte accounting includes the identity, not only the PNG.
    removed = ThumbnailDiskCache::prune(directory, {10, 20, 100}, now);
    assert(removed.count(std::string(64, 'b')) == 1);
    assert(exists('c', ".png") && exists('c', ".identity"));
    assert(ThumbnailDiskCache::prune(directory, {10, 20, 100}, now).empty());

    pair('d', now - 101);
    write('e', ".png", 10, now);
    write('f', ".identity", 5, now);
    write('a', ".png.tmp", 10, now);
    write('b', ".identity.tmp", 5, now);
    write('c', ".notes", 10, now);
    removed = ThumbnailDiskCache::prune(directory, {10, 100, 100}, now);
    assert(removed.size() == 3);
    assert(!exists('d', ".png") && !exists('d', ".identity"));
    assert(!exists('e', ".png") && !exists('f', ".identity"));
    assert(!exists('a', ".png.tmp") && !exists('b', ".identity.tmp"));
    assert(exists('c', ".notes"));
    assert(exists('c', ".png") && exists('c', ".identity"));

    // Do not follow symlinks or recurse into cache-looking directories.
    std::filesystem::create_symlink(path('c', ".png"), path('e', ".png"));
    std::filesystem::create_directory(path('f', ".png"));
    assert(ThumbnailDiskCache::prune(directory, {10, 100, 100}, now).empty());
    assert(std::filesystem::is_symlink(path('e', ".png")));
    assert(std::filesystem::is_directory(path('f', ".png")));

    // A pair larger than the entire budget cannot keep growing the cache.
    removed = ThumbnailDiskCache::prune(directory, {10, 14, 100}, now);
    assert(removed.count(std::string(64, 'c')) == 1);
    assert(!exists('c', ".png") && !exists('c', ".identity"));
    std::filesystem::remove_all(directory);
    assert(ThumbnailDiskCache::prune(directory).empty());
}
