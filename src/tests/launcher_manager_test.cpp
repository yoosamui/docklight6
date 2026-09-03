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
        // Dock widgets carry the resolved application ID, while the data file
        // may retain an executable alias. A drag must treat them as the same
        // item instead of rejecting the whole drop.
        assert(manager.reorder_dock_items({
            "org.gnome.DiskUtility.desktop"}));
        assert(manager.dock_order() ==
            std::vector<std::string>({
                "gnome-disks.desktop"}));
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

void verifies_transient_window_ids_are_not_persisted()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-transient-launchers-XXXXXX",
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
        file << "org.example.Real.desktop\n"
             << "window:218.desktop\n"
             << "WINDOW:42\n";
        assert(file);
    }

    LauncherManager manager(data_path);
    assert(LauncherManager::is_transient_window_id(
        "window:218"));
    assert(LauncherManager::is_transient_window_id(
        "WINDOW:42.desktop"));
    assert(!LauncherManager::is_transient_window_id(
        "window:editor.desktop"));
    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "org.example.Real.desktop"}));
    assert(!manager.set_attached(
        "window:999", true));

    std::ifstream migrated(data_path);
    std::string contents(
        (std::istreambuf_iterator<char>(migrated)),
        std::istreambuf_iterator<char>());
    assert(contents ==
        "org.example.Real.desktop\n");

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

void verifies_sessions_share_the_launcher_file()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-sessions-XXXXXX",
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
        file << "org.gnome.Terminal.desktop\n"
             << "mullvad browser.desktop\n"
             << "code.desktop\n";
        assert(file);
    }

    LauncherManager manager(data_path);

    SessionRecord work;
    work.name = "Work";
    work.icon =
        "/usr/local/share/docklight6/images/gnome_teal.png";

    SessionItemRecord editor;
    editor.desktop_file = "code.desktop";
    editor.title = "main.cpp - Code";
    editor.parameters = "--new-window /tmp";
    editor.workspace = "2";
    editor.dimensions = "1200x800";
    editor.position = "100x60";
    work.items.push_back(editor);

    SessionItemRecord terminal;
    terminal.desktop_file =
        "org.gnome.Terminal.desktop";
    terminal.workspace = "2";
    terminal.dimensions = "800x500";
    terminal.position = "1300x60";
    work.items.push_back(terminal);

    assert(manager.save_session(work));

    // The launcher list must survive a Session write untouched.
    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "org.gnome.Terminal.desktop",
            "mullvad browser.desktop",
            "code.desktop"}));

    const auto restored = manager.sessions();
    assert(restored.size() == 1);
    assert(restored[0].name == "Work");
    assert(restored[0].icon == work.icon);
    assert(restored[0].items.size() == 2);
    assert(restored[0].items[0].desktop_file ==
        "code.desktop");
    assert(restored[0].items[0].title ==
        "main.cpp - Code");
    assert(restored[0].items[0].parameters ==
        "--new-window /tmp");
    assert(restored[0].items[0].workspace == "2");
    assert(restored[0].items[0].dimensions ==
        "1200x800");
    assert(restored[0].items[0].position ==
        "100x60");
    assert(restored[0].items[1].desktop_file ==
        "org.gnome.Terminal.desktop");
    assert(restored[0].items[1].parameters.empty());

    // A launcher reorder rewrites the whole file. The Session block must
    // survive it, which is the failure this store exists to prevent.
    assert(manager.reorder_attached({
        "code.desktop",
        "mullvad browser.desktop",
        "org.gnome.Terminal.desktop"}));
    assert(manager.sessions().size() == 1);
    assert(manager.sessions()[0].items.size() == 2);

    // Attaching and detaching go through the same writer.
    assert(manager.set_attached(
        "org.example.Added.desktop", true));
    assert(manager.sessions().size() == 1);
    assert(manager.set_attached(
        "org.example.Added.desktop", false));
    assert(manager.sessions().size() == 1);

    // Saving the same name replaces rather than appends.
    SessionRecord replacement = work;
    replacement.items.resize(1);
    assert(manager.save_session(replacement));
    assert(manager.sessions().size() == 1);
    assert(manager.sessions()[0].items.size() == 1);

    SessionRecord writing;
    writing.name = "Writing";
    SessionItemRecord notes;
    notes.desktop_file = "org.gnome.gedit.desktop";
    notes.workspace = "1";
    writing.items.push_back(notes);
    assert(manager.save_session(writing));

    assert(manager.session_names() ==
        std::vector<std::string>({
            "Work",
            "Writing"}));

    // Saving adds lightweight Session markers after the launcher lines while
    // keeping the complete definitions at the bottom.
    assert(manager.dock_order() ==
        std::vector<std::string>({
            "code.desktop",
            "mullvad browser.desktop",
            "org.gnome.Terminal.desktop",
            "session:Work",
            "session:Writing"}));

    // A drag persists the exact mixed marker order without moving the complete
    // Session definitions themselves.
    assert(manager.reorder_dock_items({
        "session:Writing",
        "code.desktop",
        "session:Work",
        "mullvad browser.desktop",
        "org.gnome.Terminal.desktop"}));
    assert(manager.dock_order() ==
        std::vector<std::string>({
            "session:Writing",
            "code.desktop",
            "session:Work",
            "mullvad browser.desktop",
            "org.gnome.Terminal.desktop"}));
    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "code.desktop",
            "mullvad browser.desktop",
            "org.gnome.Terminal.desktop"}));
    assert(manager.session_names() ==
        std::vector<std::string>({
            "Writing",
            "Work"}));
    assert(!manager.reorder_dock_items({
        "session:Writing",
        "org.example.Missing.desktop"}));
    assert(manager.reorder_dock_items({
        "session:Writing",
        "code.desktop"}));
    assert(manager.dock_order() ==
        std::vector<std::string>({
            "session:Writing",
            "code.desktop",
            "mullvad browser.desktop",
            "org.gnome.Terminal.desktop",
            "session:Work"}));

    {
        std::ifstream stored_file(data_path);
        const std::string stored_contents(
            (std::istreambuf_iterator<char>(stored_file)),
            std::istreambuf_iterator<char>());
        assert(stored_contents.find("[dock-order]\n") ==
            std::string::npos);
        const auto writing_marker =
            stored_contents.find("[session:Writing]\n");
        const auto writing_definition =
            stored_contents.rfind("[session:Writing]\n");
        assert(writing_marker != std::string::npos);
        assert(writing_definition != std::string::npos);
        assert(writing_marker != writing_definition);
        assert(writing_marker <
            stored_contents.find("code.desktop\n"));
        assert(writing_definition >
            stored_contents.find(
                "org.gnome.Terminal.desktop\n"));
    }

    assert(!manager.save_session(SessionRecord{}));

    // Drag reordering persists Session order without touching the launchers.
    assert(manager.reorder_sessions({
        "Writing",
        "Work"}));
    assert(manager.session_names() ==
        std::vector<std::string>({
            "Writing",
            "Work"}));
    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "code.desktop",
            "mullvad browser.desktop",
            "org.gnome.Terminal.desktop"}));

    // An unknown name is ignored and an unmentioned Session keeps its place.
    assert(manager.reorder_sessions({
        "Missing",
        "Work"}));
    assert(manager.session_names() ==
        std::vector<std::string>({
            "Work",
            "Writing"}));

    // Renaming keeps the same definition and visual marker slot. It must not
    // append a second Session or overwrite a different existing name.
    SessionRecord renamed_work = replacement;
    renamed_work.name = "Projects";
    assert(manager.rename_session(
        "Work",
        renamed_work));
    assert(manager.session_names() ==
        std::vector<std::string>({
            "Projects",
            "Writing"}));
    assert(manager.sessions()[0].items.size() == 1);
    const auto renamed_order =
        manager.dock_order();
    assert(std::find(
        renamed_order.begin(),
        renamed_order.end(),
        "session:Projects") !=
        renamed_order.end());
    assert(std::find(
        renamed_order.begin(),
        renamed_order.end(),
        "session:Work") ==
        renamed_order.end());

    SessionRecord conflicting = renamed_work;
    conflicting.name = "Writing";
    assert(!manager.rename_session(
        "Projects",
        conflicting));
    assert(manager.session_names() ==
        std::vector<std::string>({
            "Projects",
            "Writing"}));

    assert(manager.remove_session("Projects"));
    assert(manager.session_names() ==
        std::vector<std::string>({"Writing"}));
    assert(!manager.remove_session("Missing"));

    // Session sections must never be mistaken for attached desktop IDs.
    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "code.desktop",
            "mullvad browser.desktop",
            "org.gnome.Terminal.desktop"}));

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

void verifies_items_without_a_desktop_file_are_dropped()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-phantom-XXXXXX",
            &error);

    assert(temporary_directory);
    assert(!error);

    const std::string directory =
        temporary_directory;
    g_free(temporary_directory);

    const auto data_path =
        directory + "/docklight.data";
    {
        // A bare [item] and one carrying only geometry name no application.
        // Either would otherwise load as a card corresponding to nothing and
        // survive every later rewrite.
        std::ofstream file(data_path);
        file << "code.desktop\n"
             << "\n"
             << "[session:Work]\n"
             << "icon=/tmp/icon.png\n"
             << "[item]\n"
             << "desktop-file=code.desktop\n"
             << "[item]\n"
             << "[item]\n"
             << "dimensions=400x500\n"
             << "position=120x200\n";
        assert(file);
    }

    LauncherManager manager(data_path);

    const auto sessions = manager.sessions();
    assert(sessions.size() == 1);
    assert(sessions[0].items.size() == 1);
    assert(sessions[0].items[0].desktop_file ==
        "code.desktop");

    // The rewrite must not reintroduce them either.
    assert(manager.save_session(sessions[0]));
    assert(manager.sessions()[0].items.size() == 1);

    SessionRecord with_empty = sessions[0];
    with_empty.items.emplace_back();
    assert(manager.save_session(with_empty));
    assert(manager.sessions()[0].items.size() == 1);

    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "code.desktop"}));

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

void verifies_session_markers_resolve_bottom_definitions()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-session-markers-XXXXXX",
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
        file << "one.desktop\n"
             << "[session:Office]\n"
             << "two.desktop\n"
             << "[session:JUan]\n"
             << "three.desktop\n\n"
             << "[session:JUan]\n"
             << "icon=/tmp/juan.png\n"
             << "[item]\n"
             << "desktop-file=firefox.desktop\n"
             << "title=JUan window\n\n"
             << "[session:Office]\n"
             << "icon=/tmp/office.png\n"
             << "[item]\n"
             << "desktop-file=terminal.desktop\n"
             << "title=Office window\n";
        assert(file);
    }

    LauncherManager manager(data_path);
    assert(manager.attached_ids() ==
        std::vector<std::string>({
            "one.desktop",
            "two.desktop",
            "three.desktop"}));
    assert(manager.dock_order() ==
        std::vector<std::string>({
            "one.desktop",
            "session:Office",
            "two.desktop",
            "session:JUan",
            "three.desktop"}));

    const auto sessions = manager.sessions();
    assert(sessions.size() == 2);
    assert(sessions[0].name == "Office");
    assert(sessions[0].icon == "/tmp/office.png");
    assert(sessions[0].items.size() == 1);
    assert(sessions[0].items[0].desktop_file ==
        "terminal.desktop");
    assert(sessions[1].name == "JUan");
    assert(sessions[1].icon == "/tmp/juan.png");
    assert(sessions[1].items.size() == 1);
    assert(sessions[1].items[0].desktop_file ==
        "firefox.desktop");

    // Rewriting keeps the marker sequence above both full definitions.
    assert(manager.save_session(sessions[0]));
    std::ifstream stored_file(data_path);
    const std::string stored_contents(
        (std::istreambuf_iterator<char>(stored_file)),
        std::istreambuf_iterator<char>());
    assert(stored_contents.find("[session:Office]\n") <
        stored_contents.find("two.desktop\n"));
    assert(stored_contents.rfind("[session:JUan]\n") >
        stored_contents.find("three.desktop\n"));

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

void verifies_legacy_dock_order_migrates_with_session_items()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-order-migration-XXXXXX",
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
        file << "one.desktop\n"
             << "two.desktop\n\n"
             << "[session:JUan]\n"
             << "icon=/tmp/juan.png\n"
             << "[item]\n"
             << "desktop-file=firefox.desktop\n"
             << "title=JUan window\n\n"
             << "[session:Office]\n"
             << "icon=/tmp/office.png\n"
             << "[item]\n"
             << "desktop-file=terminal.desktop\n"
             << "title=Office window\n\n"
             << "[dock-order]\n"
             << "item=session:Office\n"
             << "item=one.desktop\n"
             << "item=session:JUan\n"
             << "item=two.desktop\n";
        assert(file);
    }

    LauncherManager manager(data_path);
    assert(manager.dock_order() ==
        std::vector<std::string>({
            "session:Office",
            "one.desktop",
            "session:JUan",
            "two.desktop"}));

    const auto sessions = manager.sessions();
    assert(sessions.size() == 2);
    assert(sessions[0].name == "Office");
    assert(sessions[0].items.size() == 1);
    assert(sessions[0].items[0].desktop_file ==
        "terminal.desktop");
    assert(sessions[1].name == "JUan");
    assert(sessions[1].items.size() == 1);
    assert(sessions[1].items[0].desktop_file ==
        "firefox.desktop");

    std::ifstream migrated_file(data_path);
    const std::string migrated_contents(
        (std::istreambuf_iterator<char>(migrated_file)),
        std::istreambuf_iterator<char>());
    assert(migrated_contents.find("[dock-order]\n") ==
        std::string::npos);
    assert(migrated_contents.find(
        "desktop-file=terminal.desktop\n") !=
        std::string::npos);
    assert(migrated_contents.find(
        "desktop-file=firefox.desktop\n") !=
        std::string::npos);

    g_remove(data_path.c_str());
    g_rmdir(directory.c_str());
}

}

int main()
{
    Gio::init();

    verifies_executable_alias_resolution();
    verifies_order_and_persistence();
    verifies_transient_window_ids_are_not_persisted();
    verifies_sessions_share_the_launcher_file();
    verifies_items_without_a_desktop_file_are_dropped();
    verifies_session_markers_resolve_bottom_definitions();
    verifies_legacy_dock_order_migrates_with_session_items();

    return 0;
}
