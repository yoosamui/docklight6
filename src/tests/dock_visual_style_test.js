#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const surfacePath = path.resolve(
    __dirname,
    "../dock/dock_window_surface.cpp");
const surfaceSource = fs.readFileSync(surfacePath, "utf8");
const settingsPath = path.resolve(
    __dirname,
    "../dialogs/dock_settings_dialog.cpp");
const settingsSource = fs.readFileSync(settingsPath, "utf8");

assert.match(
    surfaceSource,
    /gradient_background\(\)[\s\S]*?background-color: @theme_bg_color;[\s\S]*?linear-gradient\([\s\S]*?shade\(@theme_bg_color, 0\.70\)[\s\S]*?shade\(@theme_bg_color, 1\.20\)/,
    "the dock gradient must derive every color from the active GTK theme");
assert.doesNotMatch(
    surfaceSource,
    /linear-gradient\([\s\S]*?#(?:000000|413f3f)/,
    "the dock gradient must not retain its old hardcoded colors");
assert.match(
    surfaceSource,
    /: " background-color: @theme_bg_color;"[\s\S]*?" background-image: none;"/,
    "disabling the gradient must use the active GTK theme background");

assert.match(
    settingsSource,
    /SETTINGS_DIALOG_DEFAULT_WIDTH = 900;[\s\S]*?SETTINGS_DIALOG_DEFAULT_HEIGHT = 560;[\s\S]*?SETTINGS_DIALOG_MINIMUM_WIDTH = 680;[\s\S]*?SETTINGS_DIALOG_MINIMUM_HEIGHT = 320;/,
    "the settings dialog must retain its reviewed geometry");
assert.match(
    settingsSource,
    /set_default_size\(\s*SETTINGS_DIALOG_DEFAULT_WIDTH,\s*SETTINGS_DIALOG_DEFAULT_HEIGHT\)[\s\S]*?set_size_request\(\s*SETTINGS_DIALOG_MINIMUM_WIDTH,\s*SETTINGS_DIALOG_MINIMUM_HEIGHT\)/,
    "the settings dialog must default to a wide layout and remain shrinkable");
assert.match(
    settingsSource,
    /Gtk::Label appearance_tab\([\s\S]*?_\("Appearance"\)[\s\S]*?Gtk::Label behavior_tab\([\s\S]*?_\("Behavior"\)[\s\S]*?Gtk::Label position_tab\([\s\S]*?_\("Position & Autohide"\)/,
    "settings must be grouped into clear notebook pages");
assert.match(
    settingsSource,
    /configure_settings_page\([\s\S]*?Gtk::POLICY_AUTOMATIC[\s\S]*?set_min_content_height\(\s*SETTINGS_PAGE_MINIMUM_HEIGHT\)[\s\S]*?set_max_content_height\(\s*SETTINGS_PAGE_NATURAL_HEIGHT\)/,
    "each settings page must scroll vertically on a short display");
assert.match(
    settingsSource,
    /available_width[\s\S]*?geometry\.get_width\(\) -\s*SETTINGS_DIALOG_HORIZONTAL_MARGIN[\s\S]*?available_height[\s\S]*?geometry\.get_height\(\) -\s*SETTINGS_DIALOG_VERTICAL_MARGIN[\s\S]*?dialog\.resize\(width, height\)/,
    "the initial dialog size must be bounded by the parent monitor");
assert.match(
    settingsSource,
    /dialog\.move\([\s\S]*?geometry\.get_y\(\)[\s\S]*?std::max\([\s\S]*?\(geometry\.get_height\(\) - height\)[\s\S]*?-\s*SETTINGS_DIALOG_VERTICAL_OFFSET\)/,
    "the settings dialog must move upward by 60 pixels without crossing the monitor top");
assert.match(
    settingsSource,
    /details->set_markup\([\s\S]*?"<small>"[\s\S]*?escape_text\(description\)[\s\S]*?"<\/small>"/,
    "setting descriptions must use escaped small-font markup");

const expectedSettingLabels = [
    "monitor_label",
    "hover_label",
    "indicator_label",
    "indicator_color_label",
    "preview_color_label",
    "home_icon_enabled_label",
    "home_icon_path_label",
    "display_tooltips_label",
    "display_preview_label",
    "close_preview_after_activation_label",
    "manage_all_workspaces_label",
    "icon_size_label",
    "preview_card_height_label",
    "preview_show_delay_label",
    "location_label",
    "gradient_background_label",
    "rounded_corners_label",
    "corner_radius_label",
    "alignment_label",
    "autohide_label",
    "autohide_effect_label",
    "autohide_hide_delay_label"
];
const describedSettingLabels = new Set(
    Array.from(
        settingsSource.matchAll(
            /attach_setting\(\s*\w+,\s*(\w+),\s*\w+,\s*_\("[^"]+"\)/g),
        match => match[1]));

assert.deepStrictEqual(
    describedSettingLabels,
    new Set(expectedSettingLabels),
    "every visible setting must be attached with a short description");

console.log("Dock visual style tests passed");
