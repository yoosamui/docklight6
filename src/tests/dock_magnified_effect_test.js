#!/usr/bin/env node

// ------------------------------------------------------------
// Docklight 6.0
//
// File:
// dock_magnified_effect_test.js
//
// Purpose:
// Guards magnified frame ownership, stable geometry, popup handoff,
// overlay placement, and native X11 buffered presentation contracts.
// ------------------------------------------------------------

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const dockItemSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_item.cpp"),
    "utf8");
const dockItemEffectsSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_item_effects.cpp"),
    "utf8");
const dockWindowSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_window.cpp"),
    "utf8");
const dockWindowItemsSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_window_items.cpp"),
    "utf8");
const dockWindowSurfaceSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_window_surface.cpp"),
    "utf8");
const dockWindowControllerSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_window_controller.cpp"),
    "utf8");
const dockHomeItemSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_home_item.cpp"),
    "utf8");
const dockItemContextMenuSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_item_context_menu.cpp"),
    "utf8");
const tooltipManagerSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/tooltip_manager.cpp"),
    "utf8");
const previewManagerSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/preview_manager.cpp"),
    "utf8");
const tooltipWindowSource = fs.readFileSync(
    path.resolve(__dirname, "../dock/dock_tooltip_window.cpp"),
    "utf8");

const autohideSource = fs.readFileSync(
    path.resolve(__dirname, "../autohide/dock_autohide_controller.cpp"),
    "utf8");
const presentationSource = fs.readFileSync(
    path.resolve(__dirname, "../presentation/presentation_selector.cpp"),
    "utf8");

assert.match(
    dockItemSource,
    /DockItem::set_magnified_layer_active[\s\S]*?m_magnified_layer_active = active;[\s\S]*?if \(active\)[\s\S]*?m_primary_action_effect\.disconnect\(\);[\s\S]*?image\.set_opacity\(active \? 0\.0 : 1\.0\);/,
    "magnified activation must cancel base-image click feedback before hiding the base icon");

assert.match(
    dockItemEffectsSource,
    /DockItem::start_primary_action_effect[\s\S]*?if \(m_magnified_layer_active\)[\s\S]*?image\.set_opacity\(0\.0\);[\s\S]*?return;[\s\S]*?PRIMARY_ACTION_EFFECT_MIN_OPACITY/,
    "starting click feedback must not reveal the base image under a magnified frame");

assert.match(
    dockItemEffectsSource,
    /DockItem::advance_primary_action_effect[\s\S]*?if \(m_magnified_layer_active\)[\s\S]*?image\.set_opacity\(0\.0\);[\s\S]*?return false;[\s\S]*?\+\+m_primary_action_effect_frame;/,
    "a queued click-feedback frame must stop without revealing the base image");

assert.match(
    dockWindowSource,
    /DockMagnifiedLayer::paint[\s\S]*?paint_indicator_visual[\s\S]*?Gdk::Cairo::set_source_pixbuf/,
    "magnified indicators must be composited behind icon pixbufs");

assert.match(
    dockWindowSource,
    /DockWindow::on_leave_notify_event[\s\S]*?!pointer_is_over_dock_body\(\)[\s\S]*?release_magnified_hover\(\)/,
    "leaving the visible dock body must begin a soft magnification release");

assert.match(
    dockWindowSource,
    /DockWindow::point_is_over_dock_body[\s\S]*?m_dock_box\.translate_coordinates[\s\S]*?m_dock_box\.get_allocation/,
    "magnified hit-testing must use the translated visible dock allocation");

assert.match(
    dockWindowItemsSource,
    /DockWindow::update_magnified_hover[\s\S]*?!point_is_over_dock_body\(x, y\)[\s\S]*?release_magnified_hover\(\)[\s\S]*?m_magnified_pointer_x = vertical[\s\S]*?x - main_axis_origin;[\s\S]*?m_magnified_releasing = false;/,
    "pointer motion outside the dock must release softly and re-entry must reverse that release");

assert.match(
    dockWindowItemsSource,
    /leading_growth[\s\S]*?trailing_growth[\s\S]*?centering_shift[\s\S]*?margin_extra[\s\S]*?set_magnified_main_axis_overflow\([\s\S]*?margin_extra\)/,
    "styled end margins must follow actual centered frame overflow");

assert.match(
    dockWindowItemsSource,
    /DockWindow::clear_magnified_hover_frame[\s\S]*?set_magnified_main_axis_overflow\(0\)/,
    "the final release frame must restore the ordinary styled margins");

assert.match(
    dockWindowItemsSource,
    /DockLayoutGeometry\{\}\.item_geometry\(m_leading_margin, \*this\)[\s\S]*?geometry\.center_x - main_axis_origin[\s\S]*?DockLayoutGeometry\{\}\.item_geometry\(m_leading_margin, \*this\)[\s\S]*?painted_center =[\s\S]*?frame\.center \+[\s\S]*?main_axis_origin/,
    "magnified coordinates must use allocated spacer geometry even while a new margin request is pending");

assert.match(
    dockWindowSurfaceSource,
    /DockWindow::content_geometry[\s\S]*?unused_main_axis_capacity[\s\S]*?main_axis_capacity -[\s\S]*?m_magnified_main_axis_margin_extra[\s\S]*?DockWindow::set_magnified_main_axis_overflow[\s\S]*?maximum_margin_extra[\s\S]*?m_leading_main_axis_margin =[\s\S]*?DockLayoutMetrics::DOCK_MARGIN[\s\S]*?apply_main_axis_end_margins/,
    "the fixed transparent surface capacity must be exchanged for live styled margins without changing total geometry");

assert.match(
    dockWindowSurfaceSource,
    /m_dock_alignment\.set\([\s\S]*?Gtk::ALIGN_CENTER[\s\S]*?0\.0F,[\s\S]*?m_dock_box\.set_halign\([\s\S]*?Gtk::ALIGN_CENTER\)[\s\S]*?m_dock_alignment\.set\([\s\S]*?Gtk::ALIGN_CENTER,[\s\S]*?0\.0F\)[\s\S]*?m_dock_box\.set_valign\([\s\S]*?Gtk::ALIGN_CENTER\)/,
    "the styled dock box must stay centered inside the fixed main-axis surface capacity");

const effectiveSizeBody = dockWindowControllerSource.match(
    /void DockWindowController::update_effective_icon_size[\s\S]*?void DockWindowController::schedule_layout_update/)[0];
assert.match(
    effectiveSizeBody,
    /m_leading_main_axis_margin =\s*DockLayoutMetrics::DOCK_MARGIN;[\s\S]*?m_trailing_main_axis_margin =\s*DockLayoutMetrics::DOCK_MARGIN;[\s\S]*?m_leading_main_axis_margin \+=[\s\S]*?m_magnified_main_axis_margin_extra;[\s\S]*?m_trailing_main_axis_margin \+=[\s\S]*?m_magnified_main_axis_margin_extra/,
    "layout refreshes must preserve the current dynamic styled-margin extension");

const advanceBody = dockWindowItemsSource.match(
    /bool DockWindow::advance_magnified_hover[\s\S]*?void DockWindow::apply_magnified_visual_center/)[0];
assert.doesNotMatch(
    advanceBody,
    /magnified_surface_geometry_changed|schedule_layout_update|apply_dock_layout/,
    "magnified animation frames must not resize or reposition the native surface");

assert.match(
    dockWindowItemsSource,
    /DockWindow::release_magnified_hover[\s\S]*?m_magnified_releasing = true;[\s\S]*?DockWindow::clear_magnified_hover_frame/,
    "a soft release must retain the magnified frame until its scale animation completes");

assert.match(
    dockWindowItemsSource,
    /target_scale_for[\s\S]*?m_magnified_releasing[\s\S]*?return 1\.0;[\s\S]*?!animation_pending[\s\S]*?m_magnified_tick_callback = 0;[\s\S]*?clear_magnified_hover_frame\(\);[\s\S]*?return false;/,
    "release frames must ease toward normal scale before restoring base icons");

assert.match(
    dockWindowItemsSource,
    /DockWindow::register_dock_item[\s\S]*?if \(m_magnified_pointer_active\)[\s\S]*?reset_magnified_hover\(\);[\s\S]*?m_dock_items_cache\.push_back/,
    "adding an item must clear the old magnified snapshot before hiding or inserting children");

assert.match(
    dockWindowItemsSource,
    /DockWindow::unregister_dock_item[\s\S]*?if \(m_magnified_pointer_active\)[\s\S]*?reset_magnified_hover\(\);[\s\S]*?m_dock_items_cache\.erase[\s\S]*?m_dock_box\.remove/,
    "removing an item must clear its painted copy and borrowed anchor first");

assert.match(
    dockWindowItemsSource,
    /if \(!dock_structure_changed\)[\s\S]*?return;[\s\S]*?if \(m_magnified_pointer_active\)[\s\S]*?reset_magnified_hover\(\);[\s\S]*?auto existing_items/,
    "every structural update, including reorder-only changes, must invalidate magnified anchors");

assert.match(
    dockHomeItemSource,
    /DockHomeItem::schedule_open_settings[\s\S]*?reset_magnified_hover\(\);[\s\S]*?DockHomeItem::open_settings[\s\S]*?DockSettingsDialog::show[\s\S]*?reset_magnified_hover\(\);[\s\S]*?uninhibit_autohide\(\);/,
    "the settings modal must clear magnification before and after its pointer grab");

assert.match(
    dockHomeItemSource,
    /DockHomeItem::show_context_menu[\s\S]*?reset_magnified_hover\(\);[\s\S]*?m_context_menu\.popup_at_widget/,
    "the Home popup must clear magnification before taking its pointer grab");

assert.match(
    dockItemContextMenuSource,
    /DockItem::show_context_menu[\s\S]*?reset_magnified_hover\(\);[\s\S]*?m_context_menu\.popup_at_widget/,
    "an item popup must clear magnification before taking its pointer grab");

assert.match(
    dockWindowItemsSource,
    /m_magnified_visual_centers\.clear\(\);[\s\S]*?frame\.widget[\s\S]*?frame\.center[\s\S]*?DockWindow::apply_magnified_visual_center/,
    "magnified frames must retain each visible icon center for overlay placement");

assert.match(
    tooltipManagerSource,
    /item_geometry\(item, m_window\);[\s\S]*?apply_magnified_visual_center\([\s\S]*?item_geometry[\s\S]*?calculate_tooltip_position/,
    "tooltips must use the painted magnified icon center");

assert.match(
    previewManagerSource,
    /item_geometry\(item, m_window\);[\s\S]*?apply_magnified_visual_center\([\s\S]*?item_geometry[\s\S]*?calculate_tooltip_position/,
    "previews must use the painted magnified icon center");

assert.match(
    tooltipWindowSource,
    /DockTooltipWindow::tooltip_distance[\s\S]*?DockHoverEffect::magnified[\s\S]*?m_magnified_tooltip_distance[\s\S]*?m_tooltip_distance/,
    "only magnified hover may select the reduced overlay distance");

for (const source of [tooltipManagerSource, previewManagerSource]) {
    assert.match(
        source,
        /overlay_workarea_for_dock\([\s\S]*?dock_geometry\.height,[\s\S]*?m_window\.magnified_surface_enabled\(\)[\s\S]*?m_window\.normal_dock_cross_axis_size\(\)/,
        "native overlays must convert margins using the magnified dock's reserved body thickness");

    assert.match(
        source,
        /tooltip_distance\([\s\S]*?m_settings\.hover_effect\(\)\)/,
        "tooltip and preview placement must select distance by hover effect");
}

assert.match(
    autohideSource,
    /hide_now[\s\S]*?reset_magnified_hover\(\);[\s\S]*?m_hidden = true;/,
    "hide must restore icons to the transformed box before changing visibility");
assert.match(
    dockWindowItemsSource,
    /update_magnified_hover[\s\S]*?!m_controller->is_fully_revealed\(\)/,
    "reveal crossing events must not start an untransformed magnified layer");
assert.match(
    dockWindowSurfaceSource,
    /x11_autohide_slide_content_offset[\s\S]*?autohide_slide_content_offset\([\s\S]*?m_dock_box.get_allocated_width\(\),[\s\S]*?m_dock_box.get_allocated_height\(\)/,
    "local slide must exclude transparent magnification capacity on all edges");
assert.strictEqual(
    (autohideSource.match(/m_window.x11_autohide_slide_content_offset\(/g) || []).length,
    4,
    "hidden placement, animation, immediate hide, and reveal must share box travel");
assert.match(
    autohideSource,
    /m_x11_animation_tick = m_window.add_tick_callback/,
    "X11 animation must share GTK paint scheduling with magnification");
assert.match(
    autohideSource,
    /cancel_animation\(\)[\s\S]*?remove_tick_callback\(m_x11_animation_tick\)/,
    "animation cancellation must remove the X11 tick callback");

assert.match(
    presentationSource,
    /is_native_x11_presentation[\s\S]*?GDK_IS_X11_DISPLAY\(display\)[\s\S]*?normalized\(session\) == "x11"[\s\S]*?!wayland \|\| !\*wayland[\s\S]*?!xwayland \|\| !\*xwayland/,
    "buffered magnification must exclude Wayland, XWayland, and ambiguous sessions");
assert.match(
    dockWindowItemsSource,
    /set_magnified_layer_active[\s\S]*?m_magnified_x11_buffered = active && is_native_x11_presentation\(\);[\s\S]*?set_external_background\(m_magnified_x11_buffered\)/,
    "only an active native X11 magnified frame owns background painting");
const overflowBody = dockWindowSurfaceSource.match(
    /void DockWindow::set_magnified_main_axis_overflow[\s\S]*?int DockWindow::normal_dock_cross_axis_size/)[0];
assert.match(overflowBody,
    /if \(m_magnified_x11_buffered\)[\s\S]*?m_magnified_painted_margin_extra = requested_extra;\s*return;/,
    "X11 hover must change only painted margins before any GTK size requests");
assert.match(
    dockWindowSource,
    /m_magnified_x11_frame->get_width\(\) != width[\s\S]*?m_magnified_x11_frame->get_height\(\) != height[\s\S]*?cairo_surface_set_device_scale/,
    "the complete X11 buffer must be reused and recreated for size or scale changes");
assert.match(
    dockWindowSource,
    /style->render_background\(frame,[\s\S]*?m_magnified_layer.paint\(frame\);[\s\S]*?set_operator\(Cairo::OPERATOR_SOURCE\);[\s\S]*?set_source\(m_magnified_x11_frame/,
    "background and magnified icons must reach X11 as a single replacing frame");
assert.doesNotMatch(dockWindowSource, /DockIconRenderer::paint_magnified/,
    "the rejected direct-to-X11 icon painter must not be restored");
assert.match(
    dockWindowSurfaceSource,
    /if \(m_external_background\)[\s\S]*?propagate_draw\(\*child, context\);[\s\S]*?return true;/,
    "the GTK box must not paint a second background inside the buffered frame");

for (const name of ["pointer_is_inside", "pointer_is_over_dock_body"]) {
    const body = dockWindowSource.split(`bool DockWindow::${name}()`)[1]
        .split("\n}\n")[0];
    assert.match(body,
        /if \(is_native_x11_presentation\(\)\)[\s\S]*?gdk_device_get_position/,
        "root pointer coordinates must be restricted to native X11");
    assert.doesNotMatch(body, /if \(GDK_IS_X11_DISPLAY/,
        "XWayland must not reuse frozen root coordinates as pointer presence");
    assert.match(body, /gdk_window_get_device_position/,
        "XWayland and Wayland require a pointer window for hit testing");
}

console.log("Dock magnified effect tests passed");
