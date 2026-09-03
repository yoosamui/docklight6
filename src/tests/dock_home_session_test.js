#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const homeItemPath = path.resolve(
    __dirname,
    "../dock/dock_home_item.cpp");
const sessionDialogPath = path.resolve(
    __dirname,
    "../dialogs/dock_session_dialog.cpp");
const sessionItemPath = path.resolve(
    __dirname,
    "../dialogs/dock_session_editor_item.cpp");
const sessionLauncherPath = path.resolve(
    __dirname,
    "../launchers/session_launcher.cpp");
const makefilePath = path.resolve(
    __dirname,
    "../Makefile.am");
const topMakefilePath = path.resolve(
    __dirname,
    "../../Makefile.am");
const potfilesPath = path.resolve(
    __dirname,
    "../../po/POTFILES.in");

const homeItemSource = fs.readFileSync(homeItemPath, "utf8");
const sessionDialogSource = fs.readFileSync(sessionDialogPath, "utf8");
const sessionItemSource = fs.readFileSync(sessionItemPath, "utf8");
const sessionLauncherSource = fs.readFileSync(sessionLauncherPath, "utf8");
const makefileSource = fs.readFileSync(makefilePath, "utf8");
const topMakefileSource = fs.readFileSync(topMakefilePath, "utf8");
const potfilesSource = fs.readFileSync(potfilesPath, "utf8");
const dockSessionItemSource =
    fs.readFileSync(
        path.resolve(__dirname, "../dock/dock_session_item.h"),
        "utf8") +
    fs.readFileSync(
        path.resolve(__dirname, "../dock/dock_session_item.cpp"),
        "utf8");
const dockWindowItemsSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_window_items.cpp"),
    "utf8");
const dockItemHeaderSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_item.h"),
    "utf8");
const dockWindowDndSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_window_dnd.cpp"),
    "utf8");

assert.match(
    homeItemSource,
    /set_menu_label\(\s*m_session_item,\s*_\("Sessi_on"\)\)[\s\S]*?append\(\s*m_settings_item\)[\s\S]*?append\(\s*m_session_separator\)[\s\S]*?append\(\s*m_session_item\)[\s\S]*?append\(\s*m_window_separator\)/,
    "the home context menu must contain a separated Session entry");
assert.match(
    homeItemSource,
    /m_session_item[\s\S]*?signal_activate\(\)[\s\S]*?DockHomeItem::open_session[\s\S]*?void DockHomeItem::open_session\(\)[\s\S]*?m_dock\.inhibit_autohide\(\)[\s\S]*?DockSessionDialog::show\(\s*m_dock,\s*m_source_icon,\s*m_window_registry,\s*m_dock\.launcher_manager\(\),[\s\S]*?m_dock\.synchronize_session_items\(\);[\s\S]*?m_dock\.uninhibit_autohide\(\)/,
    "activating Session must open the dialog with the launcher store and refresh the dock's Session items");

assert.strictEqual(
    Array.from(sessionDialogSource.matchAll(/Gtk::Dialog dialog\(/g)).length,
    1,
    "the Session action must create exactly one dialog");
assert.match(
    sessionDialogSource,
    /Gtk::Dialog dialog\(\s*_\("Session"\)[\s\S]*?Gtk::Image header_icon[\s\S]*?titlebar\.set_title\(\s*_\("Session"\)\)[\s\S]*?if \(icon\)[\s\S]*?dialog\.set_icon\(icon\)[\s\S]*?header_icon\.set\(small_icon\)[\s\S]*?titlebar\.pack_start\(header_icon\)[\s\S]*?dialog\.add_button\(\s*_\("_Save"\),\s*Gtk::RESPONSE_APPLY\)[\s\S]*?dialog\.add_button\(\s*_\("_Cancel"\),\s*Gtk::RESPONSE_CANCEL\)[\s\S]*?dialog\.run\(\)[\s\S]*?dialog\.hide\(\)/,
    "the Session dialog must use the Docklight icon and present Save before Cancel");
assert.match(
    sessionDialogSource,
    /_\("_Session Name"\)[\s\S]*?Gtk::ComboBoxText session_name\(true\)[\s\S]*?session_name\.get_entry\(\)->set_placeholder_text[\s\S]*?_\("_Icon"\)[\s\S]*?Gtk::ComboBox icon_selector[\s\S]*?_\("Select _Icon"\)[\s\S]*?_\("_Add"\)/,
    "Session Name must be an editable list combo box beside the header controls");
assert.match(
    sessionDialogSource,
    /class SessionIconColumns[\s\S]*?Gtk::TreeModelColumn<Glib::ustring> id;[\s\S]*?Gtk::TreeModelColumn<Glib::ustring> label;[\s\S]*?Gtk::TreeModelColumn<\s*\n?\s*Glib::RefPtr<Gdk::Pixbuf>>\s*\n?\s*image;/,
    "the icon selector needs an id/label/pixbuf model because ComboBoxText is text-only");
assert.match(
    sessionDialogSource,
    /icon_selector\.set_model\(icon_model\);[\s\S]*?icon_selector\.pack_start\(\s*icon_columns\.image,\s*\n\s*false\);[\s\S]*?icon_selector\.pack_start\(\s*icon_columns\.label,\s*\n\s*true\);[\s\S]*?icon_selector\.set_id_column\(/,
    "each row must render its image beside its label, keyed by the id column");
assert.doesNotMatch(
    sessionDialogSource,
    /Gtk::Image icon_preview|icon_preview|set_selected_icon|set_named_preview_icon|FALLBACK_ICON_NAME/,
    "the icon combobox must be the only icon display and selection control");
assert.match(
    sessionDialogSource,
    /session_image_directories\(\)[\s\S]*?DOCKLIGHT_DATA_DIR,\s*\n\s*"images"\)[\s\S]*?SOURCE_DIR,\s*\n\s*"\.\.",\s*\n\s*"data",\s*\n\s*"images"\)/,
    "the icon selector must read images from the installed and source data/images directories");
assert.match(
    sessionDialogSource,
    /std::vector<SessionIcon> available_session_icons\(\)\s*\n\{\s*\n\s*for \(const auto &directory :[\s\S]*?if \(result\.empty\(\)\)\s*\n\s*continue;[\s\S]*?return result;\s*\n\s*\}\s*\n\s*return \{\};/,
    "one directory must win outright so an installed set cannot merge with a checkout set");
assert.match(
    sessionDialogSource,
    /const auto session_icons =\s*\n\s*available_session_icons\(\);\s*\n\s*for \(const auto &icon : session_icons\)\s*\n\s*\{\s*\n\s*auto row = \*icon_model->append\(\);\s*\n\s*row\[icon_columns\.id\] = icon\.path;\s*\n\s*row\[icon_columns\.label\] = icon\.label;\s*\n\s*row\[icon_columns\.image\] =\s*\n\s*load_image_icon\(/,
    "the list must be populated from data/images and nothing else");
assert.doesNotMatch(
    sessionDialogSource.slice(
        0,
        sessionDialogSource.indexOf("select_icon.signal_clicked()")),
    /_\("Custom"\)/,
    "the icon list must not carry a Custom row before one is chosen");
assert.match(
    sessionDialogSource,
    /Gtk::TreeModel::iterator custom_row;[\s\S]*?select_icon\.signal_clicked\(\)[\s\S]*?if \(!custom_row\)\s*\n\s*\{\s*\n\s*custom_row =\s*\n\s*icon_model\s*\n?\s*->append\(\);[\s\S]*?\(\*custom_row\)\s*\n?\s*\[icon_columns\.label\] =\s*\n?\s*_\("Custom"\);[\s\S]*?icon_selector\.set_active\(\s*\n?\s*custom_row\)/,
    "choosing a custom icon must add and select a Custom row");
assert.match(
    sessionDialogSource,
    /if \(!session_icons\.empty\(\)\)\s*icon_selector\.set_active\(0\);/,
    "the first image must be the default combobox selection");
assert.match(
    sessionDialogSource,
    /const auto update_add_sensitivity =[\s\S]*?session_name\.get_entry\(\)->get_text\(\)[\s\S]*?const bool has_icon =\s*\n\s*!icon_selector\.get_active_id\(\)\s*\n?\s*\.empty\(\);[\s\S]*?add_item\.set_sensitive\(\s*\n\s*has_name && has_icon\)/,
    "Add must require both a session name and a selected icon");
assert.match(
    sessionDialogSource,
    /session_name\.get_entry\(\)\s*\n?\s*->signal_changed\(\)\s*\n?\s*\.connect\(update_add_sensitivity\);\s*\n\s*icon_selector\.signal_changed\(\)\.connect\(\s*\n?\s*update_add_sensitivity\);\s*\n\s*update_add_sensitivity\(\);/,
    "Add sensitivity must refresh on name edits, icon changes, and at startup");
assert.match(
    sessionDialogSource,
    /Gtk::ScrolledWindow item_scroller[\s\S]*?Gtk::POLICY_AUTOMATIC[\s\S]*?Gtk::Box item_list[\s\S]*?item_scroller\.add\(item_list\)/,
    "Session Items must live in a vertically scrollable area");
assert.match(
    sessionDialogSource,
    /const auto create_item =[\s\S]*?new DockSessionEditorItem\([\s\S]*?capture_window[\s\S]*?signal_remove_requested\(\)[\s\S]*?item->hide\(\)[\s\S]*?item_list\.pack_start\([\s\S]*?add_item\.signal_clicked\(\)[\s\S]*?create_item\(\)/,
    "Add and restore must share one factory that appends an item whose Remove hides it");

for (const label of [
    "App Title",
    "Desktop File",
    "App Name",
    "Parameters",
    "Workspace",
    "Dimensions",
    "Position",
    "_Paste",
    "_Launch",
    "_Remove",
]) {
    assert.ok(
        sessionItemSource.includes(`_("${label}")`),
        `Session Item must contain ${label}`);
}
assert.doesNotMatch(
    sessionItemSource,
    /Gtk::ComboBoxText m_app_title/,
    "App Title must not be a combo box");
assert.match(
    sessionItemSource,
    /m_app_title\.set_editable\(false\)[\s\S]*?m_app_title\.set_placeholder_text[\s\S]*?m_desktop_file\.set_editable\(false\)[\s\S]*?m_app_name\.set_editable\(false\)/,
    "App Title, Desktop File, and App Name must be read-only text fields");
assert.match(
    sessionItemSource,
    /m_dimensions\.set_text\("400x500"\)[\s\S]*?m_position\.set_text\("120x200"\)/,
    "Session Item geometry fields must contain the requested initial text");
assert.match(
    sessionItemSource,
    /m_remove_button\.signal_clicked\(\)[\s\S]*?m_remove_requested\.emit\(\)/,
    "Remove must emit only the card's presentation-level removal request");
assert.match(
    sessionItemSource,
    /m_launch_button\.signal_clicked\(\)[\s\S]*?DockSessionEditorItem::launcher/,
    "Launch must call the Session item's launcher function");
// Launching lives in SessionLauncher so the editor card and the dock's Session
// item resolve, launch, and place identically.
assert.match(
    sessionLauncherSource,
    /application_with_parameters[\s\S]*?launch_application->launch[\s\S]*?signal_changed\(\)[\s\S]*?place_window/,
    "the session launcher must pass parameters and place the matching launched window");
// A browser tab is not a restorable Session window. Browser detection must
// force a new top-level window even when Parameters explicitly asked for a
// tab; otherwise the launcher has no new window identity to bind to the row.
assert.match(
    sessionLauncherSource,
    /const bool has_window_target =[\s\S]*?opens_new_window\(argument\)[\s\S]*?requests_new_tab\(argument\)[\s\S]*?if \(browser && !has_window_target\)[\s\S]*?NEW_WINDOW_ARGUMENT[\s\S]*?if \(browser && requests_new_tab\(argument\)\)[\s\S]*?argument = NEW_WINDOW_ARGUMENT;/,
    "a Session browser launch must always request a new top-level window");
assert.match(
    sessionLauncherSource,
    /requires_standalone_instance[\s\S]*?normalized_application_id\(value\) ==\s*\n?\s*"gedit"[\s\S]*?if \(requires_standalone_instance\(application\)\)[\s\S]*?append_argument\("--standalone"\)/,
    "a Session gedit launch must always use a fresh standalone instance");
assert.doesNotMatch(
    sessionLauncherSource,
    /requests_own_target/,
    "a new-tab parameter must not opt a browser out of Session window creation");
assert.match(
    sessionItemSource,
    /SessionItemRecord item;[\s\S]*?item\.desktop_file = desktop_file\(\);[\s\S]*?m_launcher\.launch\(item\)/,
    "the card must launch through the shared SessionLauncher rather than its own copy");
assert.doesNotMatch(
    sessionItemSource,
    /application_with_parameters|place_window|create_from_commandline/,
    "the card must not carry a second copy of the launch logic");
// A whole-session launch that took no tags would start every item while
// telling nobody which window each produced, which is the one thing a Session
// needs. The caller loops instead, so every launch carries its row identity.
assert.doesNotMatch(
    sessionLauncherSource,
    /SessionLauncher::launch\(\s*\n\s*const SessionRecord &session\)/,
    "the launcher must not offer an untagged whole-session launch");
assert.match(
    sessionLauncherSource,
    /windows_before_launch\s*\n?\s*\.count\(window\.id\) != 0/,
    "placement must never move a window that existed before the launch");
// Sessions persist through the launcher store. The item card stays a
// presentation widget: it exposes field values and must not reach the store.
assert.doesNotMatch(
    sessionItemSource,
    /docklight\.data|DockConfiguration|LauncherManager|SessionRecord/,
    "the Session item card must not reach the persistence layer itself");
assert.doesNotMatch(
    // Comments may name the file; opening it here is what must not happen.
    sessionDialogSource.replace(/\/\/[^\n]*/g, ""),
    /docklight\.data|DockConfiguration|std::ofstream|std::ifstream/,
    "the Session dialog must persist through LauncherManager, not its own file access");
assert.match(
    sessionItemSource,
    /std::string DockSessionEditorItem::desktop_file\(\) const[\s\S]*?DockSessionEditorItem::app_title\(\)[\s\S]*?DockSessionEditorItem::parameters\(\)[\s\S]*?DockSessionEditorItem::workspace\(\)[\s\S]*?DockSessionEditorItem::dimensions\(\)[\s\S]*?DockSessionEditorItem::position\(\)[\s\S]*?bool DockSessionEditorItem::removed\(\) const/,
    "the card must expose every persisted field plus its removed state");
assert.match(
    sessionItemSource,
    /void DockSessionEditorItem::restore\(\s*\n\s*const std::string &desktop_file,[\s\S]*?apply_application\(desktop_file\)/,
    "restoring an item must re-resolve the application from its desktop entry");
assert.match(
    sessionDialogSource,
    /const auto save_session =[\s\S]*?SessionRecord record;[\s\S]*?record\.name =\s*\n\s*trimmed_session_name\([\s\S]*?session_name\.get_entry\(\)[\s\S]*?record\.icon =\s*\n\s*icon_selector\.get_active_id\(\);[\s\S]*?for \(auto \*item : live_items\(\)\)[\s\S]*?launcher_manager\.save_session\(/,
    "Save must record the session name, icon, and every live item");
assert.match(
    sessionDialogSource,
    /std::string editing_session_name;[\s\S]*?const bool name_conflict =[\s\S]*?Gtk::MessageDialog message\([\s\S]*?A Session with this name already exists\.[\s\S]*?launcher_manager\.rename_session\(\s*\n\s*editing_session_name,[\s\S]*?editing_session_name = record\.name/,
    "renaming must update the loaded Session and show an error for a duplicate name");
assert.match(
    sessionDialogSource,
    /const auto live_items =[\s\S]*?dynamic_cast<DockSessionEditorItem \*>\([\s\S]*?if \(item && !item->removed\(\)\)/,
    "saving must skip cards the user removed");
assert.match(
    sessionDialogSource,
    /auto \*save_button =\s*\n\s*dialog\.add_button\(\s*\n\s*_\("_Save"\),\s*\n\s*Gtk::RESPONSE_APPLY\);\s*\n\s*save_button->signal_clicked\(\)\.connect\(\s*\n\s*save_session\);/,
    "a Save button beside Close must invoke the save function on click");
assert.match(
    sessionDialogSource,
    /while \(dialog\.run\(\) == Gtk::RESPONSE_APPLY\)/,
    "saving must leave the dialog open");
assert.match(
    sessionDialogSource,
    /const auto load_session =[\s\S]*?const auto sessions =\s*\n\s*launcher_manager\.sessions\(\);[\s\S]*?stored_session == sessions\.end\(\)[\s\S]*?for \(auto \*child :[\s\S]*?create_item\(\)->restore\(/,
    "selecting a stored session must rebuild its item cards");
assert.match(
    sessionDialogSource,
    /session_name\.signal_changed\(\)[\s\S]*?session_name\.get_active_text\(\)[\s\S]*?if \(editing_session_name !=\s*\n\s*initial_session_name\)[\s\S]*?load_session\(\s*\n\s*initial_session_name\)/,
    "Session selection must use active-row text and explicitly guarantee the initial load");
assert.match(
    sessionDialogSource,
    /for \(const auto &name :\s*\n\s*launcher_manager\.session_names\(\)\)\s*\n\s*\{\s*\n\s*session_name\.append\(name\);/,
    "the Session Name combo must list the saved sessions");

assert.match(
    makefileSource,
    /dialogs\/dock_session_dialog\.h[\s\S]*?dialogs\/dock_session_dialog\.cpp[\s\S]*?dialogs\/dock_session_editor_item\.h[\s\S]*?dialogs\/dock_session_editor_item\.cpp/,
    "the Session dialog and editor card must be compiled into Docklight");
assert.match(
    makefileSource,
    /dock\/dock_session_item\.h[\s\S]*?dock\/dock_session_item\.cpp/,
    "the dock Session item must be compiled into Docklight");
assert.match(
    potfilesSource,
    /^src\/dialogs\/dock_session_dialog\.cpp\nsrc\/dialogs\/dock_session_editor_item\.cpp$/m,
    "Session UI strings must participate in gettext extraction");

assert.match(
    topMakefileSource,
    /docklightimagesdir = \$\(pkgdatadir\)\/images[\s\S]*?docklight_session_images = [\s\S]*?\$\(wildcard \$\(srcdir\)\/data\/images\/\*\.png\)/,
    "Session icons must install from data/images into the package data directory");
assert.match(
    topMakefileSource,
    /install-data-local:[\s\S]*?\$\(MKDIR_P\) "\$\(DESTDIR\)\$\(docklightimagesdir\)"[\s\S]*?\$\(INSTALL_DATA\) "\$\$image" "\$\(DESTDIR\)\$\(docklightimagesdir\)"/,
    "the install hook must copy every enumerated Session image");
assert.match(
    topMakefileSource,
    /install-data-local:[\s\S]*?for installed in "\$\(DESTDIR\)\$\(docklightimagesdir\)"\/\*;[\s\S]*?rm -f "\$\$installed"[\s\S]*?\$\(INSTALL_DATA\) "\$\$image"/,
    "the install hook must prune images no longer in data/images before copying");
assert.match(
    topMakefileSource,
    /EXTRA_DIST = [\s\S]*?\$\(docklight_session_images\)/,
    "Session images must be distributed with the tarball");

// A saved Session is a DockItem subclass, so it inherits hover effects, the
// context menu chrome, tooltips, and drag reordering unchanged. It overrides
// the icon, the display name, what activation launches, and the dynamic rows,
// because its content comes from docklight.data rather than the registry.
assert.match(
    dockSessionItemSource,
    /class DockSessionItem : public DockItem/,
    "the dock Session item must be a DockItem");
assert.match(
    dockSessionItemSource,
    /void reload_icon\(\) override;[\s\S]*?Glib::ustring app_name\(\) const override;[\s\S]*?context_menu_entries\(\) const override;[\s\S]*?void activate_context_menu_entry\([\s\S]*?void launch_application\(\) override;/,
    "only the Session-specific points of DockItem may be overridden");
assert.match(
    dockItemHeaderSource,
    /void schedule_window_action\(\s*\n\s*const WindowId &window_id,\s*\n\s*bool minimize\);[\s\S]*?private:/,
    "a subclass routing its own rows to a window must reuse the deferred dispatch");
assert.match(
    dockItemHeaderSource,
    /bool include_icon_names = true\);/,
    "icon-name matching must be optional so a grouped item can leave it out");
assert.match(
    dockItemHeaderSource,
    /static std::vector<std::string>\s*\n\s*application_identifiers\(/,
    "the identity builder must be shared with subclasses");
assert.match(
    dockItemHeaderSource,
    /virtual std::vector<ApplicationWindowEntry>\s*\n\s*context_menu_entries\(\) const;\s*\n\s*virtual void\s*\n\s*activate_context_menu_entry\(/,
    "the dynamic rows and their activation must be overridable");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::launch_next_stored_item\(\)[\s\S]*?m_launcher\.launch\([\s\S]*?stored_item_id\(index\)[\s\S]*?DockSessionItem::on_launch_finished[\s\S]*?launch_next_stored_item\(\)[\s\S]*?DockSessionItem::launch_application\(\)[\s\S]*?launch_next_stored_item\(\);/,
    "activation must serialize stored items through the shared SessionLauncher");
assert.match(
    sessionLauncherSource,
    /m_window_identified\.emit\([\s\S]*?m_launch_finished\.emit\(pending->tag\)[\s\S]*?on_tracking_timeout\(\)[\s\S]*?m_launch_finished\.emit\(pending\.tag\)/,
    "a serialized Session must advance after identification or timeout");
assert.match(
    dockSessionItemSource,
    /apply_icon_pixbuf\(\s*\n\s*Gdk::Pixbuf::create_from_file\(\s*\n\s*m_session\.icon,/,
    "the item must render the session's stored image through DockItem's pixbuf hook");
assert.doesNotMatch(
    dockSessionItemSource,
    /drag_source_unset\(\)/,
    "a Session item must be draggable like any other dock item");
assert.match(
    dockWindowDndSource,
    /std::vector<std::string> dock_order;[\s\S]*?is_session_desktop_id\([\s\S]*?dock_order\.push_back\([\s\S]*?reorder_dock_items\(\s*dock_order\)/,
    "a drag reorder must persist one combined launcher and Session order");
assert.doesNotMatch(
    dockWindowDndSource,
    /std::copy_if/,
    "a drop must not move Sessions back to a category boundary");
assert.match(
    dockWindowDndSource,
    /if \(!m_launcher_manager\.reorder_dock_items\([\s\S]*?synchronize_session_items\(\);[\s\S]*?return false;/,
    "a drop must report persistence failure and restore the stored order");
assert.match(
    dockWindowDndSource,
    /m_synchronized_dock_order =\s*\n\s*m_launcher_manager\.dock_order\(\);/,
    "a successful drop must immediately synchronize the cached persisted order");
assert.match(
    fs.readFileSync(
        path.resolve(__dirname, "../launchers/launcher_manager.cpp"),
        "utf8"),
    /LauncherManager::reorder_dock_items[\s\S]*?const auto identity =\s*\n\s*\[this\][\s\S]*?normalize_resolved_id\(value\)/,
    "drag persistence must match a widget's resolved desktop ID to its stored alias");

// DockItem::initialize() reaches reload_icon() during base construction, before
// a subclass vtable exists, so the base must tolerate a null application.
const dockItemSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_item.cpp"),
    "utf8");
assert.match(
    dockItemSource,
    /void DockItem::reload_icon\(\)\s*\n\{[\s\S]*?if \(!m_app\)\s*\n\s*return;/,
    "reload_icon must tolerate an item without an installed application");
assert.doesNotMatch(
    dockItemSource,
    /m_app->get_name\(\)|m_app->get_id\(\)/,
    "diagnostics must use the virtual display name, not the raw application");
assert.match(
    fs.readFileSync(
        path.resolve(__dirname, "../dock/dock_item_context_menu.cpp"),
        "utf8"),
    /const bool has_application = bool\(m_app\);[\s\S]*?m_attach_item\.set_visible\([\s\S]*?m_open_new_window_item\.set_visible\(/,
    "Attach and Open New Window must be hidden for an item without an application");
assert.match(
    fs.readFileSync(
        path.resolve(__dirname, "../dock/dock_item_context_menu.cpp"),
        "utf8"),
    /context_menu_window_icon\([\s\S]*?Gio::DesktopAppInfo::create\([\s\S]*?if \(icon_theme &&\s*\n\s*!icon_name\.empty\(\)\)/,
    "dynamic menu rows must prefer the saved window application's icon over a backend hint");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::context_menu_entry_icon\([\s\S]*?stored_item\(entry\.id\)[\s\S]*?find_session_application\(\s*\n\s*item->desktop_file\)[\s\S]*?application->get_icon\(\)/,
    "a Session dynamic row must resolve the icon from the saved item it stands for");
assert.match(
    fs.readFileSync(
        path.resolve(__dirname, "../dock/dock_item_context_menu.cpp"),
        "utf8"),
    /m_context_menu\.append\(\s*\n\s*m_edit_item\);\s*\n\s*m_context_menu\.append\(\s*\n\s*m_window_separator\);\s*\n\s*m_context_menu\.append\(\s*\n\s*m_minimize_item\);[\s\S]*?int position = 0;[\s\S]*?context_menu_entry_icon\(entry\)/,
    "Edit must be immediately before Minimize while dynamic rows use the virtual icon resolver");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::has_edit_action\(\) const[\s\S]*?return true;[\s\S]*?DockSessionItem::edit_item\(\)[\s\S]*?dock\(\)\.edit_session\(m_session\.name\)/,
    "a Session Edit action must open the editor for that Session");
assert.match(
    sessionDialogSource,
    /const std::string &initial_session_name[\s\S]*?name == initial_session_name[\s\S]*?session_name\.set_active\(/,
    "the Session editor must select and load the requested Session name");
// Files without lightweight Session markers use the legacy layout, with
// Sessions after launcher lines. Saved markers override that default.
assert.match(
    dockWindowItemsSource,
    /const auto sessions =\s*\n\s*m_launcher_manager\.sessions\(\);/,
    "the item sync must read the saved Sessions");
assert.match(
    dockWindowItemsSource,
    /if \(m_window_registry\)[\s\S]*?for \(const auto &running :[\s\S]*?desired_items\.push_back\([\s\S]*?for \(const auto &session : sessions\)[\s\S]*?desired_items\.push_back\([\s\S]*?true,\s*\n\s*true,\s*\n\s*session/,
    "Session items must be collected after ordinary items for the compatibility order");
assert.match(
    dockWindowItemsSource,
    /if \(DockSessionItem::is_session_desktop_id\(\s*\n\s*item->desktop_id\(\)\)\)\s*\n\s*\{\s*\n\s*continue;/,
    "the live item pass must not override persisted Session order");
assert.match(
    dockWindowItemsSource,
    /const auto stored_dock_order =\s*\n\s*m_launcher_manager\.dock_order\(\);[\s\S]*?for \(const auto &stored_id : stored_dock_order\)[\s\S]*?desired_items =\s*\n\s*std::move\(ordered_desired_items\);/,
    "the synchronizer must restore an explicitly saved mixed dock order");
assert.match(
    dockWindowItemsSource,
    /else if \(desired\.is_session\)[\s\S]*?new DockSessionItem\([\s\S]*?register_dock_item\(item\);/,
    "a Session must be created and registered as an ordinary dock item");
assert.match(
    dockWindowItemsSource,
    /session_ids == m_synchronized_session_ids/,
    "the sync short-circuit must notice a Session change");
assert.match(
    dockWindowItemsSource,
    /if \(!dock_structure_changed\)[\s\S]*?desired_items\[index\]\.is_session[\s\S]*?->set_session\(\s*\n\s*desired_items\[index\][\s\S]*?\.session\)/,
    "an existing Session widget must receive saved edits even when its name and order are unchanged");
assert.match(
    sessionDialogSource,
    /add_item\.signal_clicked\(\)[\s\S]*?lock_session_name\(\);[\s\S]*?create_item\(\);[\s\S]*?save_session\(\);/,
    "Add must lock the name, add a card, and commit the session");
assert.match(
    sessionDialogSource,
    /const auto lock_session_name =[\s\S]*?session_name\.get_entry\(\)\s*\n?\s*->set_editable\(false\);[\s\S]*?set_button_sensitivity\(\s*\n?\s*Gtk::SENSITIVITY_OFF\)/,
    "Add must make the Session Name read-only");
assert.match(
    sessionDialogSource,
    /const auto saved_sessions = launcher_manager\.sessions\(\);[\s\S]*?on_sessions_changed\(\*saved\);/,
    "a saved session must reload its canonical record and notify the dock");

// An item may stand for several applications. Returning only the first match
// would show one application's windows in the previews and the context menu.
const controllerSource = fs.readFileSync(
    path.resolve(__dirname, "../application/dock_application_controller.cpp"),
    "utf8");
assert.match(
    controllerSource,
    /DockApplicationController::application\(\) const[\s\S]*?matches\.push_back\(running_application\);[\s\S]*?if \(!m_window_filter &&\s*\n\s*matches\.size\(\) <= 1\)[\s\S]*?m_merged_application\.window_ids\s*\n?\s*\.push_back\(window_id\);/,
    "an item standing for several applications must merge all of their windows");
// A stored item names no live window: the saved title is the caption captured
// at save time and stops matching the moment the window retitles itself, and
// the application alone would claim windows the user opened. The launch is the
// only moment the Session can learn that a window is its own.
assert.doesNotMatch(
    dockSessionItemSource,
    /matches_saved_window|window_matches_spec|m_window_specs|caption != spec\.title/,
    "a Session must not attribute a live window by matching its saved title");
assert.match(
    sessionLauncherSource,
    /pending\.tag = tag;[\s\S]*?if \(!pending->tag\.empty\(\)\)\s*\n\s*\{\s*\n\s*m_window_identified\.emit\(\s*\n\s*pending->tag,\s*\n\s*match->id\);/,
    "the launcher must report each window it identifies under that launch's tag");
assert.match(
    sessionLauncherSource,
    /m_claimed_window_ids[\s\S]*?claimed_windows\.count\(window\.id\) != 0[\s\S]*?claimed_windows\.insert\(match->id\)[\s\S]*?m_claimed_window_ids\.insert\(match->id\)/,
    "concurrent Session launches must reserve each new window only once across registry callbacks");
assert.match(
    dockSessionItemSource,
    /m_launcher\.signal_window_identified\(\)[\s\S]*?on_window_identified[\s\S]*?set_window_filter\([\s\S]*?owns_window\(window\.id\)/,
    "a Session must own exactly the windows its own launcher produced");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::on_window_identified\([\s\S]*?m_launched_windows\[tag\] =\s*\n\s*std::move\(window_id\);/,
    "an identified window must be bound to the stored item that launched it");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::launch_stored_item\([\s\S]*?m_launcher\.launch\(\s*\n\s*item,\s*\n\s*stored_item_id\(index\)\)/,
    "each item must be launched under its own row identity");
// The dock re-applies the stored record on every synchronization pass, and
// launching a Session is itself what changes the running set that triggers
// one. Clearing unconditionally erased each binding moments after it was made.
assert.match(
    dockSessionItemSource,
    /DockSessionItem::set_session\([\s\S]*?if \(!same_items\(\s*\n\s*session\.items,\s*\n\s*m_session\.items\)\)\s*\n\s*\{\s*\n\s*m_launched_windows\.clear\(\);[\s\S]*?m_session = std::move\(session\);[\s\S]*?set_application_identifiers\(/,
    "window bindings must survive a resync and drop only when the stored items change");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::context_menu_entries\(\) const[\s\S]*?index < m_session\.items\.size\(\)[\s\S]*?entry\.id = stored_item_id\(index\);[\s\S]*?launched_window\(entry\.id\);[\s\S]*?entry\.caption = window->caption;[\s\S]*?entry\.caption = item\.title;/,
    "every persisted item must get a row, live state when it has a window and its saved title otherwise");
assert.match(
    dockSessionItemSource,
    /entry\.caption\.empty\(\)[\s\S]*?find_session_application\(\s*\n\s*item\.desktop_file\)[\s\S]*?get_display_name\(\)/,
    "an item saved without a window title must still label its row");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::activate_context_menu_entry\([\s\S]*?launched_window\(entry\.id\);[\s\S]*?schedule_window_action\(\s*\n\s*window->id,[\s\S]*?launch_stored_item\(index\);/,
    "a row holding a window must act on it, and a row without one must launch its item");
assert.match(
    dockSessionItemSource,
    /DockSessionItem::launch_next_stored_item\(\)[\s\S]*?m_next_launch_index >= m_session\.items\.size\(\)[\s\S]*?m_next_launch_index\+\+[\s\S]*?m_launcher\.launch/,
    "activating the Session must advance through every stored item");
assert.match(
    sessionLauncherSource,
    /tracking_application_ids\([\s\S]*?add\(requested\);[\s\S]*?add\(application->get_id\(\)\);[\s\S]*?add\(application->get_executable\(\)\);[\s\S]*?add\(startup_wm_class\);/,
    "launch tracking must accept the identities a backend can report");
assert.match(
    sessionLauncherSource,
    /pending\.application_ids =\s*\n\s*tracking_application_ids\([\s\S]*?std::find\(\s*\n\s*pending->application_ids\.begin\(\),[\s\S]*?window_application_id/,
    "a launched window must be attributed through any resolved application alias");

// Paste happens after Add, so committing only on Add would always lose the
// most recently added item.
assert.match(
    sessionItemSource,
    /sigc::signal<void> &DockSessionEditorItem::signal_changed\(\)/,
    "the editor card must report value changes");
assert.match(
    sessionItemSource,
    /m_restoring = true;[\s\S]*?m_restoring = false;/,
    "restoring stored values must not report a change back");
assert.match(
    sessionDialogSource,
    /schedule_save =\s*\n\s*\[&save_debounce,\s*\n\s*&save_session\]\(\)[\s\S]*?save_debounce\.disconnect\(\);[\s\S]*?save_session\(\);/,
    "edits must be written back on a debounce");
assert.match(
    sessionDialogSource,
    /item->signal_changed\(\)\.connect\(/,
    "every card must be connected to the debounced save");
assert.match(
    sessionDialogSource,
    /if \(save_debounce\.connected\(\)\)\s*\n\s*\{\s*\n\s*save_debounce\.disconnect\(\);\s*\n\s*save_session\(\);/,
    "a pending save must be flushed when the dialog closes");

// A grouped item must show each window's own application icon, not its own
// image, and must not dereference an application it does not have.
const contextMenuSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_item_context_menu.cpp"),
    "utf8");
assert.match(
    contextMenuSource,
    /CONTEXT_MENU_ICON_SIZE = 16/,
    "all dynamic context-menu icons must be 16 by 16");
assert.match(
    contextMenuSource,
    /pixbuf->get_width\(\) != CONTEXT_MENU_ICON_SIZE[\s\S]*?pixbuf = pixbuf->scale_simple\([\s\S]*?CONTEXT_MENU_ICON_SIZE,[\s\S]*?Gdk::INTERP_BILINEAR/,
    "all dynamic context-menu pixbufs must be scaled to 16 by 16");
assert.match(
    contextMenuSource,
    /m_desktop_id\.rfind\("session:", 0\)[\s\S]*?remove_session\([\s\S]*?synchronize_session_items\(\)/,
    "Session Remove must delete the saved Session and refresh the dock");
assert.match(
    contextMenuSource,
    /context_menu_window_icon\(\s*\n\s*entry\.icon_name,\s*\n\s*entry\.desktop_file_name\)/,
    "a window menu entry must resolve its own application icon");
assert.match(
    contextMenuSource,
    /if \(icon_theme &&\s*\n\s*!desktop_file_name\.empty\(\)\)[\s\S]*?Gio::DesktopAppInfo::create\(\s*\n\s*desktop_file_name\)/,
    "the window icon must fall back to the window's application before this item's image");
assert.doesNotMatch(
    contextMenuSource,
    /m_app\s*\n?\s*->get_display_name\(\)/,
    "the entry label must not dereference an absent application");
// The rows and their activation are virtual so an item whose content does not
// come from the window registry, such as a Session, can supply both.
assert.match(
    contextMenuSource,
    /auto entries =\s*\n\s*context_menu_entries\(\);/,
    "the dynamic rows must come from the virtual entry source");
assert.match(
    contextMenuSource,
    /item->signal_activate\(\)\s*\n?\s*\.connect\(\s*\n\s*\[this,\s*\n\s*entry\]\(\)\s*\n\s*\{\s*\n\s*activate_context_menu_entry\(\s*\n\s*entry\);/,
    "activating a dynamic row must go through the virtual activation");
assert.match(
    contextMenuSource,
    /DockItem::context_menu_entries\(\) const[\s\S]*?m_application_controller\s*\n?\s*\.window_entries\(\);[\s\S]*?DockItem::activate_context_menu_entry\([\s\S]*?schedule_window_action\(\s*\n\s*entry\.id,\s*\n\s*entry\.active && !entry\.minimized\);/,
    "an ordinary item must still list live windows and show or minimize the chosen one");

// An [item] with no desktop-file names no application. It must not be loaded
// as a card corresponding to nothing, nor written back.
const launcherManagerSource = fs.readFileSync(
    path.resolve(__dirname, "../launchers/launcher_manager.cpp"),
    "utf8");
assert.match(
    launcherManagerSource,
    /LauncherManager::read_data\(\)[\s\S]*?return trimmed\(item\.desktop_file\)\s*\n?\s*\.empty\(\);[\s\S]*?session\.items\.erase\(/,
    "reading must drop Session items that name no application");
assert.match(
    launcherManagerSource,
    /for \(const auto &item :\s*\n\s*session\.items\)[\s\S]*?if \(trimmed\(item\.desktop_file\)\s*\n?\s*\.empty\(\)\)\s*\n\s*\{\s*\n\s*continue;/,
    "writing must not emit a Session item that names no application");

console.log("Dock home Session tests passed");
