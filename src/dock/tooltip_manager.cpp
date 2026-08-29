// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// tooltip_manager.cpp
//
// Implementation overview:
// Applies tooltip configuration, hover timing, geometry calculation, and
// overlay-window visibility changes.
//
// Important implementation decisions:
// - Request generations invalidate stale timeout callbacks.
// - Monitor-local geometry is used for placement calculations.
// - Native placement receives an overlay work area derived from dock bounds.
// - Coordination with previews is emitted through manager signals.
//
// ------------------------------------------------------------

#include "tooltip_manager.h"

#include "dock_constants.h"
#include "dock_window.h"

TooltipManager::TooltipManager(
    DockWindow &window,
    std::function<ScreenPosition()> dock_position)
    : m_window(window),
      m_dock_position(std::move(dock_position))
{
}

TooltipManager::~TooltipManager()
{
    cancel_show_timer();
    cancel_hide_timer();
}

void TooltipManager::apply_configuration(
    const DockSettings &settings)
{
    m_settings = settings;
    if (!m_settings.display_tooltips())
        hide_immediately();
}

void TooltipManager::set_layout_request(
    const DockLayoutRequest &request)
{
    m_layout_request = request;
}

void TooltipManager::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    m_monitor = monitor;
    m_window.m_overlay_window.set_monitor(monitor);
}

void TooltipManager::set_layout_geometry(
    const MonitorGeometry &usable_monitor,
    const MonitorGeometry &output)
{
    m_usable_monitor_geometry = usable_monitor;
    m_output_geometry = output;
    m_window.m_overlay_window.set_workarea_geometry(usable_monitor);
}

void TooltipManager::schedule_show(
    Gtk::Widget &item,
    const Glib::ustring &text,
    bool preserve_pending_preview)
{
    if (m_hovered_item == &item &&
        (m_pending_item == &item || m_visible_item == &item))
        return;

    begin_item_hover(item);

    if (!m_settings.display_tooltips())
    {
        hide_immediately();
        return;
    }

    cancel_hide_timer();
    cancel_show_timer();
    hide();
    start_show_timer(item, text, preserve_pending_preview);
}

void TooltipManager::begin_item_hover(Gtk::Widget &item)
{
    m_pointer_inside = true;
    m_hovered_item = &item;
}

void TooltipManager::show_immediately(
    Gtk::Widget &item,
    const Glib::ustring &text)
{
    begin_item_hover(item);
    cancel_show_timer();
    show_now(item, text, false);
}

void TooltipManager::schedule_hide(Gtk::Widget &item)
{
    if (m_hovered_item != &item)
        return;

    m_hovered_item = nullptr;
    m_pointer_inside = false;
    cancel_show_timer();
    m_pending_item = nullptr;
}

void TooltipManager::hide_immediately()
{
    m_hovered_item = nullptr;
    m_pointer_inside = false;
    cancel_show_timer();
    cancel_hide_timer();
    m_pending_item = nullptr;
    m_visible_item = nullptr;
    m_window.m_overlay_window.hide_tooltip_immediately();
}

void TooltipManager::hide()
{
    m_visible_item = nullptr;
    m_window.m_overlay_window.hide_tooltip();
}

void TooltipManager::start_show_timer(
    Gtk::Widget &item,
    const Glib::ustring &text,
    bool preserve_pending_preview)
{
    m_pending_item = &item;
    auto *requested_item = &item;
    const auto requested_text = text;
    const auto generation = m_request_generation;

    m_show_timer = Glib::signal_timeout().connect(
        [this,
         requested_item,
         requested_text,
         generation,
         preserve_pending_preview]()
        {
            if (generation == m_request_generation &&
                requested_item == m_hovered_item)
            {
                show_now(
                    *requested_item,
                    requested_text,
                    preserve_pending_preview);
            }

            if (generation == m_request_generation)
                m_pending_item = nullptr;
            return false;
        },
        DockConstants::TOOLTIP_SHOW_DELAY_MS);
}

void TooltipManager::show_now(
    Gtk::Widget &item,
    const Glib::ustring &text,
    bool preserve_pending_preview)
{
    if (!m_settings.display_tooltips())
        return;

    m_signal_will_show.emit(preserve_pending_preview);

    const int tooltip_width =
        m_window.m_overlay_window.preferred_width_for(text);
    auto item_geometry =
        m_layout_geometry.item_geometry(item, m_window);
    auto dock_geometry =
        m_layout_geometry.dock_geometry(m_window);
    const auto dock_position = m_dock_position();

    dock_geometry.x = dock_position.x - m_output_geometry.x;
    dock_geometry.y = dock_position.y - m_output_geometry.y;
    dock_geometry.has_position = true;

    auto monitor_geometry = m_usable_monitor_geometry;
    if (monitor_geometry.width <= 0 || monitor_geometry.height <= 0)
    {
        monitor_geometry = m_layout_geometry.output_geometry(m_monitor);
        monitor_geometry.x = 0;
        monitor_geometry.y = 0;
    }

    const auto position = m_layout_engine.calculate_tooltip_position(
        m_layout_request,
        monitor_geometry,
        dock_geometry,
        item_geometry,
        tooltip_width,
        m_window.m_overlay_window.tooltip_height(),
        m_window.m_overlay_window.tooltip_distance());

    if (m_window.surface_uses_native_placement())
    {
        m_window.m_overlay_window.set_workarea_geometry(
            overlay_workarea_for_dock(
                monitor_geometry,
                m_layout_request.location,
                m_layout_request.autohide,
                dock_geometry.x,
                dock_geometry.y,
                dock_geometry.width,
                dock_geometry.height));
    }

    m_visible_item = &item;
    m_window.m_overlay_window.show_tooltip(
        text,
        m_layout_request.location,
        tooltip_width,
        position);
}

void TooltipManager::start_hide_timer(
    std::function<bool()> pointer_inside)
{
    cancel_hide_timer();
    m_hide_timer = Glib::signal_timeout().connect(
        [this, pointer_inside = std::move(pointer_inside)]()
        {
            if (pointer_inside())
                return false;
            m_signal_hide_requested.emit();
            return false;
        },
        DockConstants::TOOLTIP_HIDE_DELAY_MS);
}

void TooltipManager::cancel_show_timer()
{
    ++m_request_generation;
    if (m_show_timer.connected())
        m_show_timer.disconnect();
}

void TooltipManager::cancel_hide_timer()
{
    if (m_hide_timer.connected())
        m_hide_timer.disconnect();
}

bool TooltipManager::has_request_for(const Gtk::Widget &item) const
{
    return m_pending_item == &item || m_visible_item == &item;
}

bool TooltipManager::pointer_inside() const
{
    return m_pointer_inside;
}

Gtk::Widget *TooltipManager::hovered_item() const
{
    return m_hovered_item;
}

sigc::signal<void, bool> &TooltipManager::signal_will_show()
{
    return m_signal_will_show;
}

sigc::signal<void> &TooltipManager::signal_hide_requested()
{
    return m_signal_hide_requested;
}
