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
    "../dialogs/dock_session_item.cpp");
const makefilePath = path.resolve(
    __dirname,
    "../Makefile.am");
const potfilesPath = path.resolve(
    __dirname,
    "../../po/POTFILES.in");

const homeItemSource = fs.readFileSync(homeItemPath, "utf8");
const sessionDialogSource = fs.readFileSync(sessionDialogPath, "utf8");
const sessionItemSource = fs.readFileSync(sessionItemPath, "utf8");
const makefileSource = fs.readFileSync(makefilePath, "utf8");
const potfilesSource = fs.readFileSync(potfilesPath, "utf8");

assert.match(
    homeItemSource,
    /set_menu_label\(\s*m_session_item,\s*_\("Sessi_on"\)\)[\s\S]*?append\(\s*m_settings_item\)[\s\S]*?append\(\s*m_session_separator\)[\s\S]*?append\(\s*m_session_item\)[\s\S]*?append\(\s*m_window_separator\)/,
    "the home context menu must contain a separated Session entry");
assert.match(
    homeItemSource,
    /m_session_item[\s\S]*?signal_activate\(\)[\s\S]*?DockHomeItem::open_session[\s\S]*?void DockHomeItem::open_session\(\)[\s\S]*?m_dock\.inhibit_autohide\(\)[\s\S]*?DockSessionDialog::show\(\s*m_dock,\s*m_window_registry\)[\s\S]*?m_dock\.uninhibit_autohide\(\)/,
    "activating Session must open the dedicated dialog while autohide is inhibited");

assert.strictEqual(
    Array.from(sessionDialogSource.matchAll(/Gtk::Dialog dialog\(/g)).length,
    1,
    "the Session action must create exactly one dialog");
assert.match(
    sessionDialogSource,
    /Gtk::Dialog dialog\(\s*_\("Session"\)[\s\S]*?titlebar\.set_title\(\s*_\("Session"\)\)[\s\S]*?dialog\.add_button\(\s*_\("_Close"\),\s*Gtk::RESPONSE_CLOSE\)[\s\S]*?dialog\.run\(\)[\s\S]*?dialog\.hide\(\)/,
    "the Session dialog must be titled Session and retain its Close action");
assert.match(
    sessionDialogSource,
    /_\("_Session Name"\)[\s\S]*?Gtk::Entry session_name[\s\S]*?_\("_Icon"\)[\s\S]*?Gtk::ComboBoxText icon_selector[\s\S]*?_\("Select _Icon"\)[\s\S]*?_\("_Add"\)/,
    "the Session dialog must expose the requested header controls");
assert.match(
    sessionDialogSource,
    /Gtk::ScrolledWindow item_scroller[\s\S]*?Gtk::POLICY_AUTOMATIC[\s\S]*?Gtk::Box item_list[\s\S]*?item_scroller\.add\(item_list\)/,
    "Session Items must live in a vertically scrollable area");
assert.match(
    sessionDialogSource,
    /add_item\.signal_clicked\(\)[\s\S]*?new DockSessionItem\([\s\S]*?capture_window[\s\S]*?signal_remove_requested\(\)[\s\S]*?item->hide\(\)[\s\S]*?item_list\.pack_start\(/,
    "Add must append one independent item whose Remove signal hides that item");

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
assert.match(
    sessionItemSource,
    /m_app_title\(true\)[\s\S]*?m_app_title\.get_entry\(\)->set_placeholder_text/,
    "App Title must be an editable list combo box");
assert.match(
    sessionItemSource,
    /m_desktop_file\.set_editable\(false\)[\s\S]*?m_app_name\.set_editable\(false\)/,
    "Desktop File and App Name must be read-only fields");
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
    /m_launch_button\.signal_clicked\(\)[\s\S]*?DockSessionItem::launcher/,
    "Launch must call the Session item's launcher function");
assert.match(
    sessionItemSource,
    /application_with_parameters[\s\S]*?signal_changed\(\)[\s\S]*?launch_application->launch[\s\S]*?place_window/,
    "launcher must pass parameters and place the matching launched window");
assert.doesNotMatch(
    sessionDialogSource + sessionItemSource,
    /docklight\.data|DockConfiguration|LauncherManager/,
    "Session launching must not introduce persistence");

assert.match(
    makefileSource,
    /dialogs\/dock_session_dialog\.h[\s\S]*?dialogs\/dock_session_dialog\.cpp[\s\S]*?dialogs\/dock_session_item\.h[\s\S]*?dialogs\/dock_session_item\.cpp/,
    "the Session dialog and item widget must be compiled into Docklight");
assert.match(
    potfilesSource,
    /^src\/dialogs\/dock_session_dialog\.cpp\nsrc\/dialogs\/dock_session_item\.cpp$/m,
    "Session UI strings must participate in gettext extraction");

console.log("Dock home Session tests passed");
