// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// launcher_manager_test.cpp
//
// Test scope:
// Verifies launcher order, duplicate removal, identifier normalization,
// attachment changes, reorder validation, and persistence.
//
// A temporary data file contains all filesystem side effects.
//
// ------------------------------------------------------------

#include "launchers/launcher_manager.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <cassert>
#include <fstream>
#include <string>
#include <vector>

namespace
{

void verifies_executable_alias_resolution()
{
    // Exercise the executable-to-desktop-ID alias that originally exposed
    // duplicate Disks launchers. Keep the test portable to systems where
    // GNOME Disks is not installed.
    const auto disks =
        Gio::DesktopAppInfo::create(
            "org.gnome.DiskUtility.desktop");

    if (!disks)
        return;

    GError *error = nullptr;

    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-launcher-alias-XXXXXX",
            &error);

    assert(temporary_directory);
    assert(!error);

    const std::string directory =
        temporary_directory;

    g_free(temporary_directory);

    const auto data_path =
        directory + "/docklight.data";

    {
        std::ofstream data_file(data_path);
        data_file
            << "gnome-disks.desktop\n";
        assert(data_file);
    }

    {
        LauncherManager manager(data_path);

        assert(
            manager.normalize_resolved_id(
                "gnome-disks.desktop") ==
            "org.gnome.diskutility.desktop");
        assert(manager.is_attached(
            "org.gnome.DiskUtility.desktop"));
    }

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

void verifies_order_and_persistence()
{
    GError *error = nullptr;

    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-launchers-XXXXXX",
            &error);

    assert(temporary_directory);
    assert(!error);

    const std::string directory =
        temporary_directory;

    g_free(temporary_directory);

    const auto data_path =
        directory + "/docklight.data";

    {
        std::ofstream file(data_path);

        file << "# Attached launchers\n"
             << "org.example.Second.desktop\n"
             << "org.example.First.desktop\n"
             << "ORG.EXAMPLE.SECOND.DESKTOP\n"
             << "\n";
    }

    LauncherManager manager(data_path);

    assert(
        manager.attached_ids() ==
        std::vector<std::string>({
            "org.example.Second.desktop",
            "org.example.First.desktop"}));

    assert(
        LauncherManager::
            normalize_desktop_id(
                "/usr/share/applications/"
                "ORG.EXAMPLE.FIRST.DESKTOP") ==
        "org.example.first.desktop");

    assert(
        LauncherManager::
            normalize_desktop_id(
                "Mullvad Browser") ==
        "mullvad-browser.desktop");

    assert(manager.is_attached(
        "org.example.second"));
    assert(!manager.is_attached(
        "org.example.third"));

    assert(manager.set_attached(
        "org.example.Third.desktop",
        true));

    assert(
        manager.attached_ids() ==
        std::vector<std::string>({
            "org.example.Second.desktop",
            "org.example.First.desktop",
            "org.example.Third.desktop"}));

    assert(manager.reorder_attached(
        {
            "org.example.third",
            "ORG.EXAMPLE.SECOND.DESKTOP",
            "org.example.first"
        }));

    assert(
        manager.attached_ids() ==
        std::vector<std::string>({
            "org.example.Third.desktop",
            "org.example.Second.desktop",
            "org.example.First.desktop"}));

    assert(!manager.reorder_attached(
        {
            "org.example.third",
            "org.example.missing"
        }));

    assert(manager.set_attached(
        "ORG.EXAMPLE.SECOND",
        true));

    assert(
        manager.attached_ids().size() ==
        3);

    assert(manager.set_attached(
        "org.example.first",
        false));

    assert(
        manager.attached_ids() ==
        std::vector<std::string>({
            "org.example.Third.desktop",
            "org.example.Second.desktop"}));

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

}

int main()
{
    Gio::init();

    verifies_executable_alias_resolution();
    verifies_order_and_persistence();

    return 0;
}
