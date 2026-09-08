// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_surface.cpp
//
// Implementation overview:
// Implements DockWindow surface transforms, placement delegation,
// orientation, visual styling, and autohide surface effects.
// Native X11 magnification grows the painted background without reallocating children.
// X11 slide distance follows the visible box, excluding magnified overflow.
// Local transforms clear exposed pixels and invalidate the full dock frame.
// Bottom magnification omits 10 logical pixels of unused surface headroom.
//
// The controller calculates placement; this file performs GTK effects and
// delegates native placement to the selected surface backend.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_home_item.h"
#include "layout/dock_layout_metrics.h"
#include "dock_window_controller.h"

#include <algorithm>
#include <cmath>
#include <string>

DockSurfaceBox::DockSurfaceBox()
    : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL)
{
}

void DockSurfaceBox::set_external_background(bool external)
{
    m_external_background = external;
}

void DockSurfaceBox::set_horizontal_scale(
    double scale,
    double anchor)
{
    const double clamped =
        std::clamp(scale, 0.0, 1.0);
    const double clamped_anchor =
        std::clamp(anchor, 0.0, 1.0);
    if (std::abs(
            clamped -
            m_horizontal_scale) < 0.0001 &&
        std::abs(
            clamped_anchor -
            m_horizontal_scale_anchor) < 0.0001)
    {
        return;
    }

    m_horizontal_scale = clamped;
    m_horizontal_scale_anchor = clamped_anchor;
    queue_draw();
}

double DockSurfaceBox::horizontal_scale() const
{
    return m_horizontal_scale;
}

void DockSurfaceBox::set_vertical_scale(
    double scale,
    double anchor)
{
    const double clamped =
        std::clamp(scale, 0.0, 1.0);
    const double clamped_anchor =
        std::clamp(anchor, 0.0, 1.0);
    if (std::abs(
            clamped -
            m_vertical_scale) < 0.0001 &&
        std::abs(
            clamped_anchor -
            m_vertical_scale_anchor) < 0.0001)
    {
        return;
    }

    m_vertical_scale = clamped;
    m_vertical_scale_anchor = clamped_anchor;
    queue_draw();
}

double DockSurfaceBox::vertical_scale() const
{
    return m_vertical_scale;
}

void DockSurfaceBox::set_vertical_offset(
    double offset)
{
    if (std::abs(offset - m_vertical_offset) < 0.0001)
        return;

    m_vertical_offset = offset;
    queue_draw();
}

double DockSurfaceBox::vertical_offset() const
{
    return m_vertical_offset;
}

void DockSurfaceBox::set_horizontal_offset(
    double offset)
{
    if (std::abs(offset - m_horizontal_offset) < 0.0001)
        return;

    m_horizontal_offset = offset;
    queue_draw();
}

double DockSurfaceBox::horizontal_offset() const
{
    return m_horizontal_offset;
}

bool DockSurfaceBox::on_draw(
    const Cairo::RefPtr<Cairo::Context>
        &context)
{
    if (m_external_background)
    {
        // The X11 magnified frame paints the extended background once. Keep
        // GTK's real children allocated and drawing without a second body.
        for (auto *child : get_children())
        {
            if (child->get_visible())
                propagate_draw(*child, context);
        }
        return true;
    }

    const bool transformed =
        m_horizontal_scale < 1.0 ||
        m_vertical_scale < 1.0 ||
        std::abs(m_horizontal_offset) >= 0.0001 ||
        std::abs(m_vertical_offset) >= 0.0001;
    if (transformed)
    {
        // GTK can redraw this child without replacing its parent's pixels.
        // Clear every intermediate frame, not just the fully hidden frame,
        // so the old body cannot remain behind the moving/scaling content.
        context->save();
        context->set_operator(Cairo::OPERATOR_CLEAR);
        context->paint();
        context->restore();
    }

    const bool fully_translated =
        std::abs(m_horizontal_offset) >=
            get_allocated_width() ||
        std::abs(m_vertical_offset) >=
            get_allocated_height();
    if (fully_translated)
        return true;

    if (m_horizontal_scale <= 0.0 ||
        m_vertical_scale <= 0.0)
    {
        return true;
    }

    if (m_horizontal_scale >= 1.0 &&
        m_vertical_scale >= 1.0 &&
        std::abs(m_horizontal_offset) < 0.0001 &&
        std::abs(m_vertical_offset) < 0.0001)
        return Gtk::Box::on_draw(context);

    context->save();
    context->rectangle(
        0.0,
        0.0,
        get_allocated_width(),
        get_allocated_height());
    context->clip();
    context->translate(
        m_horizontal_offset +
            get_allocated_width() *
            (1.0 - m_horizontal_scale) *
            m_horizontal_scale_anchor,
        get_allocated_height() *
                (1.0 - m_vertical_scale) *
                m_vertical_scale_anchor +
            m_vertical_offset);
    context->scale(
        m_horizontal_scale,
        m_vertical_scale);
    const bool handled =
        Gtk::Box::on_draw(context);
    context->restore();
    return handled;
}

void DockWindow::set_x11_horizontal_scale(
    double scale,
    double anchor)
{
    m_dock_box.set_horizontal_scale(
        scale,
        anchor);
    // The top-level clear pass must accompany child transform frames.
    queue_draw();
}

double DockWindow::x11_horizontal_scale() const
{
    return m_dock_box.horizontal_scale();
}

void DockWindow::set_x11_vertical_scale(
    double scale,
    double anchor)
{
    m_dock_box.set_vertical_scale(
        scale,
        anchor);
    // The top-level clear pass must accompany child transform frames.
    queue_draw();
}

double DockWindow::x11_vertical_scale() const
{
    return m_dock_box.vertical_scale();
}

ScreenPosition DockWindow::x11_autohide_slide_content_offset(
    const DockPlacement &placement) const
{
    // The local transform clips against this box, not the larger transparent
    // toplevel reserved for magnification. Extra travel delays visible reveal.
    return autohide_slide_content_offset(
        placement,
        m_dock_box.get_allocated_width(),
        m_dock_box.get_allocated_height(),
        1.0);
}

void DockWindow::set_x11_horizontal_offset(
    double offset)
{
    const double previous = m_dock_box.horizontal_offset();
    m_dock_box.set_horizontal_offset(offset);
    if (m_dock_box.horizontal_offset() != previous)
        queue_draw();
}

double DockWindow::x11_horizontal_offset() const
{
    return m_dock_box.horizontal_offset();
}

void DockWindow::set_x11_vertical_offset(
    double offset)
{
    const double previous = m_dock_box.vertical_offset();
    m_dock_box.set_vertical_offset(offset);
    if (m_dock_box.vertical_offset() != previous)
        queue_draw();
}

double DockWindow::x11_vertical_offset() const
{
    return m_dock_box.vertical_offset();
}

void DockWindow::set_surface_horizontal_offset(
    double offset)
{
    m_dock_box.set_horizontal_offset(offset);
}

void DockWindow::set_surface_vertical_offset(
    double offset)
{
    m_dock_box.set_vertical_offset(offset);
}

void DockWindow::set_surface_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    m_surface_backend->set_monitor(monitor);
}

DockAutohideEffect
DockWindow::effective_autohide_effect() const
{
    const auto platform_default =
        m_surface_backend
            ->default_autohide_effect();
    const auto configured =
        m_controller
            ->settings()
            .autohide_effect();

    if (!configured.has_value())
        return platform_default;

    // Plasma Wayland previously stored its client-rendered slide as
    // slide_fade. Treat that value as the renamed movement-only Slide effect
    // without changing GNOME's compositor-owned Slide and Fade selection.
    if (*configured == DockAutohideEffect::slide_fade &&
        m_surface_backend->supports_autohide_slide())
    {
        return DockAutohideEffect::slide;
    }

    if (*configured == platform_default ||
        *configured == DockAutohideEffect::fade)
        return *configured;

    const auto supported =
        m_surface_backend
            ->configurable_autohide_effects();
    if (std::find(
            supported.begin(),
            supported.end(),
            *configured) != supported.end())
    {
        return *configured;
    }

    // A shared configuration can retain a desktop-specific choice after the
    // user changes sessions. Never dispatch that value through a surface
    // backend which cannot reveal it; use the backend's safe default instead.
    return platform_default;
}

std::vector<DockAutohideEffect>
DockWindow::configurable_autohide_effects() const
{
    return m_surface_backend
        ->configurable_autohide_effects();
}

DockWindowGeometry
DockWindow::content_geometry() const
{
    DockWindowGeometry geometry;

    int minimum_width = 0;
    int natural_width = 0;
    int minimum_height = 0;
    int natural_height = 0;
    m_dock_overlay.get_preferred_width(
        minimum_width,
        natural_width);
    m_dock_overlay.get_preferred_height(
        minimum_height,
        natural_height);
    geometry.width = natural_width;
    geometry.height = natural_height;

    if (m_magnified_enabled)
    {
        // The native surface keeps one constant worst-case main-axis extent.
        // Each frame exchanges unused transparent capacity for equal styled
        // box margins, so the visible background can grow without a native
        // resize or compositor reposition.
        const int overflow_size =
            magnified_surface_cross_axis_size();
        const int main_axis_capacity =
            magnified_main_axis_extra_size();
        const int unused_main_axis_capacity =
            std::max(
                0,
                main_axis_capacity -
                    2 * m_magnified_main_axis_margin_extra);
        if (m_dock_box.get_orientation() ==
            Gtk::ORIENTATION_HORIZONTAL)
        {
            geometry.width +=
                unused_main_axis_capacity;
            geometry.height = std::max(
                geometry.height,
                overflow_size);
        }
        else
        {
            geometry.height +=
                unused_main_axis_capacity;
            geometry.width = std::max(
                geometry.width,
                overflow_size);
        }
    }

    return geometry;
}

// Applies generic dock widget state, then delegates native placement and
// reservation side effects to the selected surface backend.
void DockWindow::apply_dock_layout(
    const DockPlacement &placement,
    const MonitorGeometry &output,
    const MonitorGeometry &workarea)
{
    apply_visual_style();
    apply_dock_orientation(
        placement.orientation);

    if (placement.is_horizontal())
    {
        m_dock_alignment.set(
            Gtk::ALIGN_CENTER,
            placement.anchor_top
                ? Gtk::ALIGN_START
                : Gtk::ALIGN_END,
            0.0F,
            0.0F);
        m_dock_box.set_halign(
            Gtk::ALIGN_CENTER);
        m_dock_box.set_valign(
            placement.anchor_top
                ? Gtk::ALIGN_START
                : Gtk::ALIGN_END);
    }
    else
    {
        m_dock_alignment.set(
            placement.anchor_left
                ? Gtk::ALIGN_START
                : Gtk::ALIGN_END,
            Gtk::ALIGN_CENTER,
            0.0F,
            0.0F);
        m_dock_box.set_halign(
            placement.anchor_left
                ? Gtk::ALIGN_START
                : Gtk::ALIGN_END);
        m_dock_box.set_valign(
            Gtk::ALIGN_CENTER);
    }

    m_surface_backend->apply_dock_placement(
        placement,
        output,
        workarea);
}

MonitorGeometry
DockWindow::surface_output_geometry() const
{
    return m_surface_backend->output_geometry();
}

MonitorGeometry
DockWindow::surface_work_area() const
{
    return m_surface_backend->work_area();
}

MonitorGeometry
DockWindow::surface_effective_work_area(
    const MonitorGeometry &output,
    const MonitorGeometry &workarea)
{
    return m_surface_backend->effective_work_area(
        output,
        workarea);
}

void DockWindow::prepare_surface_change()
{
    m_surface_backend->clear_reserved_space();
}

bool DockWindow::surface_uses_native_placement() const
{
    return m_surface_backend->uses_native_placement();
}

bool DockWindow::surface_is_native_x11() const
{
    return m_surface_backend->is_native_x11();
}

bool DockWindow::surface_is_ordinary_wayland() const
{
    return m_surface_backend->is_ordinary_wayland();
}

bool DockWindow::surface_delegates_autohide_effect(
    DockAutohideEffect effect) const
{
    return m_surface_backend
        ->delegates_autohide_effect(effect);
}

double DockWindow::surface_autohide_fade_opacity() const
{
    return m_surface_backend
        ->autohide_fade_opacity();
}

void DockWindow::set_surface_autohide_fade_opacity(
    double opacity)
{
    m_surface_backend
        ->set_autohide_fade_opacity(opacity);
}

void DockWindow::finish_surface_autohide_fade(
    bool hidden)
{
    m_surface_backend
        ->finish_autohide_fade(hidden);
}

bool DockWindow::
    surface_supports_autohide_slide() const
{
    return m_surface_backend
        ->supports_autohide_slide();
}

double DockWindow::
    surface_autohide_slide_progress() const
{
    return m_surface_backend
        ->autohide_slide_progress();
}

void DockWindow::
    set_surface_autohide_slide_progress(
    const DockPlacement &placement,
    double progress)
{
    m_surface_backend
        ->set_autohide_slide_progress(
            placement,
            progress);
}

void DockWindow::
    finish_surface_autohide_slide(
    bool hidden)
{
    m_surface_backend
        ->finish_autohide_slide(hidden);
}

bool DockWindow::surface_initial_placement_pending() const
{
    return m_surface_backend->initial_placement_pending();
}

void DockWindow::complete_surface_initial_placement()
{
    m_surface_backend->complete_initial_placement();
}

void DockWindow::apply_dock_orientation(
    DockOrientation orientation)
{
    if (orientation ==
        DockOrientation::vertical)
    {
        m_dock_box.set_orientation(
            Gtk::ORIENTATION_VERTICAL);
    }
    else
    {
        m_dock_box.set_orientation(
            Gtk::ORIENTATION_HORIZONTAL);
    }

    const bool vertical =
        orientation == DockOrientation::vertical;
    for (auto *item : m_dock_items_cache)
        item->set_vertical(vertical);
    if (m_home_item)
        m_home_item->set_vertical(vertical);

    apply_main_axis_end_margins(
        orientation);
}

int DockWindow::magnified_surface_cross_axis_size() const
{
    const int base_size =
        DockLayoutMetrics::item_size_for(
            std::max(1, m_effective_icon_size));
    const int surface_size = static_cast<int>(
        std::lround(base_size * 2.25));
    // Scaling the padded item adds 20 pixels beyond the maximum icon
    // extent plus its normal padding. Trim half that spare room at bottom.
    return location() == DockLocation::bottom
               ? surface_size - 10
               : surface_size;
}

int DockWindow::magnified_main_axis_extra_size() const
{
    return DockLayoutMetrics::
        magnified_main_axis_extra_for(
            m_effective_icon_size);
}

void DockWindow::set_magnified_main_axis_overflow(
    double requested_extra)
{
    const double maximum_margin_extra =
        magnified_main_axis_extra_size() / 2;
    requested_extra = std::clamp(
        requested_extra, 0.0, maximum_margin_extra);

    if (m_magnified_x11_buffered)
    {
        // Hover only changes the painted rectangle on X11. Updating spacer
        // requests here creates GTK allocation and icon-geometry work on
        // every frame even though the outer surface size stays constant.
        m_magnified_painted_margin_extra = requested_extra;
        return;
    }

    // Preserve the existing integer spacer sizing on other presentations.
    const int margin_extra = static_cast<int>(std::ceil(requested_extra));
    if (m_magnified_main_axis_margin_extra ==
        margin_extra)
    {
        return;
    }

    m_leading_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN +
        margin_extra;
    m_trailing_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN +
        margin_extra;
    m_magnified_main_axis_margin_extra =
        margin_extra;
    apply_main_axis_end_margins(
        m_dock_box.get_orientation() ==
                Gtk::ORIENTATION_HORIZONTAL
            ? DockOrientation::horizontal
            : DockOrientation::vertical);
}

int DockWindow::normal_dock_cross_axis_size() const
{
    DockWindowGeometry geometry;
    const bool horizontal =
        m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_HORIZONTAL;

    for (auto *child : m_dock_box.get_children())
    {
        if (!child->get_visible())
            continue;

        int minimum_width = 0;
        int natural_width = 0;
        int minimum_height = 0;
        int natural_height = 0;
        child->get_preferred_width(
            minimum_width,
            natural_width);
        child->get_preferred_height(
            minimum_height,
            natural_height);

        if (horizontal)
        {
            geometry.width += natural_width;
            geometry.height = std::max(
                geometry.height,
                natural_height);
        }
        else
        {
            geometry.width = std::max(
                geometry.width,
                natural_width);
            geometry.height += natural_height;
        }
    }

    return std::max(
        1,
        horizontal ? geometry.height : geometry.width);
}

bool DockWindow::magnified_surface_enabled() const
{
    return m_magnified_enabled;
}

void DockWindow::apply_visual_style()
{
    auto dock_context =
        m_dock_box.get_style_context();

    auto window_context =
        get_style_context();

    const auto &layout_request =
        m_controller->layout_request();

    if (layout_request.rounded_corners)
    {
        dock_context->add_class(
            "dock-rounded");
        window_context->add_class(
            "dock-rounded");
    }
    else
    {
        dock_context->remove_class(
            "dock-rounded");
        window_context->remove_class(
            "dock-rounded");
    }

    const int configured_radius =
        layout_request.corner_radius;

    const int derived_radius =
        DockLayoutMetrics::corner_radius_for(
            m_effective_icon_size);

    const int effective_radius =
        layout_request.rounded_corners
            ? std::max(
                  0,
                  configured_radius < 0
                      ? derived_radius
                      : configured_radius)
            : 0;

    const std::string background_css =
        m_controller->settings()
                .gradient_background()
            ? " background-color: @theme_bg_color;"
              " background-image: linear-gradient("
              "to top, shade(@theme_bg_color, 0.70) 0, "
              "shade(@theme_bg_color, 0.70) 2px, "
              "shade(@theme_bg_color, 1.20) 90%);"
            : " background-color: @theme_bg_color;"
              " background-image: none;";

    m_visual_css->load_from_data(
        "window.dock-window {"
        " background-color: transparent;"
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}"
        ".dock-surface {" +
        background_css +
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}");

    for (auto *item : dock_items())
    {
        item->set_context_menu_corner_radius(
            effective_radius);
    }

    if (m_home_item)
    {
        m_home_item
            ->set_context_menu_corner_radius(
                effective_radius);
    }

    m_overlay_window.set_rounded_corners(
        layout_request.rounded_corners,
        effective_radius,
        m_effective_icon_size);

    m_controller->set_preview_rounded_corners(
        layout_request.rounded_corners,
        effective_radius);
}

void DockWindow::apply_main_axis_end_margins(
    DockOrientation orientation)
{
    const bool horizontal =
        orientation ==
        DockOrientation::horizontal;

    const int leading_width =
        horizontal
            ? m_leading_main_axis_margin
            : 0;

    const int leading_height =
        horizontal
            ? 0
            : m_leading_main_axis_margin;

    const int trailing_width =
        horizontal
            ? m_trailing_main_axis_margin
            : 0;

    const int trailing_height =
        horizontal
            ? 0
            : m_trailing_main_axis_margin;

    // These spacers are children of m_dock_box, so GTK includes them in the
    // natural size used by DockLayoutEngine. This avoids a separate margin
    // calculation that could disagree with item and tooltip coordinates.
    if (leading_width != m_applied_leading_margin_width ||
        leading_height != m_applied_leading_margin_height)
    {
        m_leading_margin.set_size_request(
            leading_width,
            leading_height);
        m_applied_leading_margin_width = leading_width;
        m_applied_leading_margin_height = leading_height;
    }

    if (trailing_width != m_applied_trailing_margin_width ||
        trailing_height != m_applied_trailing_margin_height)
    {
        m_trailing_margin.set_size_request(
            trailing_width,
            trailing_height);
        m_applied_trailing_margin_width = trailing_width;
        m_applied_trailing_margin_height = trailing_height;
    }
}
