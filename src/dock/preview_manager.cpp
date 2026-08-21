#include "preview_manager.h"

#include "autohide/dock_autohide_controller.h"
#include "dock_constants.h"
#include "dock_item.h"
#include "dock_window.h"
#include "layout/dock_layout_metrics.h"
#include "media/dock_media_playback_monitor.h"
#include "preview/dock_preview_window.h"
#include "tooltip_manager.h"
#include "windowing/window_registry.h"

#include <algorithm>

PreviewManager::PreviewManager(
    DockWindow &window,
    DockAutohideController &autohide,
    TooltipManager &tooltips,
    const DockSettings &settings,
    const DockLayoutRequest &layout_request,
    const Glib::RefPtr<Gdk::Monitor> &monitor,
    std::function<ScreenPosition()> dock_position)
    : m_window(window),
      m_autohide(autohide),
      m_tooltips(tooltips),
      m_preview_window(std::make_unique<DockPreviewWindow>()),
      m_media_monitor(std::make_unique<DockMediaPlaybackMonitor>()),
      m_dock_position(std::move(dock_position)),
      m_settings(settings),
      m_layout_request(layout_request),
      m_monitor(monitor)
{
    set_monitor(monitor);
    apply_configuration(settings);

    m_preview_window->signal_pointer_entered().connect(
        [this]()
        {
            m_pointer_inside = true;
            m_signal_pointer_entered.emit();
        });
    m_preview_window->signal_pointer_left().connect(
        [this]()
        {
            m_pointer_inside = false;
            m_signal_pointer_left.emit();
        });
    m_preview_window->signal_activate_window().connect(
        sigc::mem_fun(*this, &PreviewManager::activate_window));
    m_preview_window->signal_reload_thumbnail().connect(
        sigc::mem_fun(*this, &PreviewManager::reload_thumbnail));
    m_preview_window->signal_close_window().connect(
        sigc::mem_fun(*this, &PreviewManager::close_window));

    m_fully_revealed =
        m_autohide.signal_fully_revealed().connect(
            sigc::mem_fun(
                *this,
                &PreviewManager::show_pending_if_ready));

    m_media_playback_changed =
        m_media_monitor->signal_changed().connect(
            [this]()
            {
                if (m_preview_desktop_id.empty())
                    return;
                m_preview_window->set_dynamic_refresh(
                    m_media_monitor->should_stream(m_preview_desktop_id),
                    m_media_monitor->playing_title(m_preview_desktop_id));
            });
}

PreviewManager::~PreviewManager()
{
    m_fully_revealed.disconnect();
    hide();
    cancel_show_timer();
    m_media_playback_changed.disconnect();
    m_input_forwarding_reset.disconnect();
}

void PreviewManager::apply_configuration(const DockSettings &settings)
{
    m_settings = settings;
    hide();
    m_preview_window->set_card_user_height(settings.preview_card_height());
    m_preview_window->set_preview_color(settings.preview_color());
}

void PreviewManager::set_layout_request(const DockLayoutRequest &request)
{
    m_layout_request = request;
}

void PreviewManager::set_layout_geometry(
    const MonitorGeometry &usable_monitor,
    const MonitorGeometry &output)
{
    m_usable_monitor_geometry = usable_monitor;
    m_output_geometry = output;
    m_preview_window->set_workarea_geometry(usable_monitor);
}

void PreviewManager::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    m_monitor = monitor;
    m_preview_window->set_monitor(monitor);
}

void PreviewManager::set_thumbnail_policy(WindowThumbnailPolicy policy)
{
    m_preview_window->set_thumbnail_policy(policy);
}

void PreviewManager::set_rounded_corners(bool enabled, int radius)
{
    m_preview_window->set_rounded_corners(enabled, radius);
}

void PreviewManager::prime_cache(
    const std::vector<ApplicationWindowEntry> &entries)
{
    m_preview_window->prime_thumbnail_cache(entries);
}

bool PreviewManager::has_request_for(const DockItem &item) const
{
    return m_pending_desktop_id == item.desktop_id() ||
           m_preview_desktop_id == item.desktop_id();
}

void PreviewManager::schedule_show(DockItem &item, int show_delay_ms)
{
    if ((m_tooltips.hovered_item() == &item &&
         m_tooltips.has_request_for(item)) ||
        has_request_for(item))
        return;

    m_tooltips.cancel_hide_timer();
    m_tooltips.cancel_show_timer();
    cancel_show_timer();
    m_tooltips.begin_item_hover(item);

    if (!m_preview_desktop_id.empty() &&
        m_preview_desktop_id == item.desktop_id())
    {
        m_pending_desktop_id.clear();
        m_show_delay_elapsed = false;
        return;
    }

    m_tooltips.hide();

    if (m_settings.display_tooltips() &&
        !m_settings.display_preview())
    {
        m_tooltips.schedule_show(item, item.tooltip_text(), true);
    }

    if (!m_settings.display_preview())
    {
        m_pending_desktop_id.clear();
        return;
    }

    m_pending_desktop_id = item.desktop_id();
    m_show_delay_elapsed = false;
    m_show_timer = Glib::signal_timeout().connect(
        [this]()
        {
            m_tooltips.cancel_show_timer();
            m_show_delay_elapsed = true;
            show_pending_if_ready();
            return false;
        },
        show_delay_ms);
}

void PreviewManager::show_pending_if_ready()
{
    // On native X11 the dock moves in root coordinates during its reveal.
    // Keep the hover request alive, but do not calculate the preview position
    // from an intermediate animation frame.
    if (!m_show_delay_elapsed ||
        m_pending_desktop_id.empty() ||
        !m_autohide.is_fully_revealed())
    {
        return;
    }

    const auto desktop_id = m_pending_desktop_id;
    m_pending_desktop_id.clear();
    m_show_delay_elapsed = false;

    for (auto *item : m_window.dock_items())
    {
        if (item && item->desktop_id() == desktop_id)
        {
            show_now(*item);
            break;
        }
    }
}

void PreviewManager::show_now(
    DockItem &item,
    const WindowId &excluded_window_id)
{
    auto entries = item.window_entries();
    std::stable_sort(
        entries.begin(),
        entries.end(),
        [](const ApplicationWindowEntry &left,
           const ApplicationWindowEntry &right)
        {
            if (left.application_auxiliary != right.application_auxiliary)
                return left.application_auxiliary;
            const auto &left_label =
                left.caption.empty() ? left.id : left.caption;
            const auto &right_label =
                right.caption.empty() ? right.id : right.caption;
            return Glib::ustring(left_label).casefold_collate_key() <
                   Glib::ustring(right_label).casefold_collate_key();
        });

    if (!excluded_window_id.empty())
    {
        entries.erase(
            std::remove_if(
                entries.begin(),
                entries.end(),
                [&excluded_window_id](const ApplicationWindowEntry &entry)
                {
                    return entry.id == excluded_window_id;
                }),
            entries.end());
    }

    if (entries.empty())
    {
        hide();
        if (excluded_window_id.empty())
            m_tooltips.show_immediately(item, item.tooltip_text());
        return;
    }

    m_tooltips.hide();
    auto item_geometry = m_layout_geometry.item_geometry(item, m_window);
    auto dock_geometry = m_layout_geometry.dock_geometry(m_window);
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

    const bool vertical_dock =
        m_layout_request.location == DockLocation::left ||
        m_layout_request.location == DockLocation::right;
    const int preview_distance =
        m_window.m_overlay_window.tooltip_distance();
    const bool dock_reserves_space =
        m_layout_request.autohide == DockAutohide::none;
    const int dock_side_offset =
        vertical_dock
            ? (dock_reserves_space ? 0 : dock_geometry.width) +
                  preview_distance
            : 0;
    const int available_width = std::max(
        1,
        monitor_geometry.width - dock_side_offset -
            (vertical_dock
                 ? DockLayoutMetrics::TOOLTIP_EDGE_MARGIN
                 : 2 * DockLayoutMetrics::TOOLTIP_EDGE_MARGIN));

    const auto preview_size = m_preview_window->preferred_size(
        entries,
        available_width,
        monitor_geometry.height);
    const auto position = m_layout_engine.calculate_tooltip_position(
        m_layout_request,
        monitor_geometry,
        dock_geometry,
        item_geometry,
        preview_size.width,
        preview_size.height,
        preview_distance);

    if (m_window.surface_uses_native_placement())
    {
        m_preview_window->set_workarea_geometry(
            overlay_workarea_for_dock(
                monitor_geometry,
                m_layout_request.location,
                m_layout_request.autohide,
                dock_geometry.x,
                dock_geometry.y,
                dock_geometry.width,
                dock_geometry.height));
    }

    m_preview_desktop_id = item.desktop_id();
    if (!m_inhibits_autohide)
    {
        m_autohide.inhibit();
        m_inhibits_autohide = true;
    }

    m_preview_window->show_preview(
        entries,
        m_layout_request.location,
        position,
        preview_size);
    m_preview_window->set_dynamic_refresh(
        m_media_monitor->should_stream(m_preview_desktop_id),
        m_media_monitor->playing_title(m_preview_desktop_id));
}

void PreviewManager::hide(bool cancel_pending_show)
{
    if (cancel_pending_show)
    {
        cancel_show_timer();
        m_pending_desktop_id.clear();
    }

    m_preview_desktop_id.clear();
    m_pointer_inside = false;
    m_shell_pointer_inside = false;
    m_preview_window->hide_preview();

    if (m_inhibits_autohide)
    {
        m_inhibits_autohide = false;
        m_autohide.uninhibit(
            m_tooltips.pointer_inside() || m_window.pointer_is_inside());
    }
}

void PreviewManager::hide_immediately()
{
    hide();
    m_preview_window->hide_preview_immediately();
}

void PreviewManager::cancel_show_timer()
{
    if (m_show_timer.connected())
        m_show_timer.disconnect();
    m_pending_desktop_id.clear();
    m_show_delay_elapsed = false;
}

void PreviewManager::set_shell_pointer_inside(bool inside)
{
    m_shell_pointer_inside = inside;
    if (inside)
        m_signal_pointer_entered.emit();
    else
        m_signal_pointer_left.emit();
}

bool PreviewManager::pointer_inside() const
{
    return m_pointer_inside || m_shell_pointer_inside;
}

void PreviewManager::set_input_forwarding(bool forwarding)
{
    m_input_forwarding_reset.disconnect();
    m_input_forwarding = forwarding;
    m_preview_window->set_input_forwarding(forwarding);

    if (forwarding)
    {
        m_signal_pointer_entered.emit();
        m_input_forwarding_reset = Glib::signal_timeout().connect(
            [this]()
            {
                m_input_forwarding = false;
                m_preview_window->set_input_forwarding(false);
                return false;
            },
            DockConstants::
                PREVIEW_INPUT_FORWARDING_RESET_MS);
    }
}

bool PreviewManager::input_forwarding() const
{
    return m_input_forwarding;
}

const std::string &PreviewManager::desktop_id() const
{
    return m_preview_desktop_id;
}

void PreviewManager::activate_window(const WindowId &window_id)
{
    const auto desktop_id = m_preview_desktop_id;
    for (auto *item : m_window.dock_items())
    {
        if (!item || item->desktop_id() != desktop_id)
            continue;
        const auto entries = item->window_entries();
        const auto selected = std::find_if(
            entries.begin(),
            entries.end(),
            [&window_id](const ApplicationWindowEntry &entry)
            {
                return entry.id == window_id;
            });
        if (selected != entries.end())
            item->toggle_window(window_id);
        break;
    }

    if (m_settings.close_preview_after_activation())
    {
        Glib::signal_idle().connect_once([this]() { hide(); });
    }
}

void PreviewManager::reload_thumbnail(const WindowId &window_id)
{
    if (m_window.m_window_registry && !window_id.empty())
        m_window.m_window_registry->present_windows({window_id});
}

void PreviewManager::close_window(const WindowId &window_id)
{
    const auto desktop_id = m_preview_desktop_id;
    Glib::signal_idle().connect_once(
        [this, desktop_id, window_id]()
        {
            for (auto *item : m_window.dock_items())
            {
                if (item && item->desktop_id() == desktop_id)
                {
                    if (item->close_window(window_id))
                        show_now(*item, window_id);
                    break;
                }
            }
        });
}

sigc::signal<void> &PreviewManager::signal_pointer_entered()
{
    return m_signal_pointer_entered;
}

sigc::signal<void> &PreviewManager::signal_pointer_left()
{
    return m_signal_pointer_left;
}
