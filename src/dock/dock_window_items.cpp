// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_items.cpp
//
// Implementation overview:
// Implements DockWindow item registration, magnified-frame geometry,
// application resolution, launcher synchronization, and persistent
// dock-container construction.
//
// - Dock item identity is based on normalized desktop identifiers.
// - Item synchronization preserves configured order while merging apps.
// - Auxiliary overlays follow painted magnified centers, not fixed widgets.
// - Magnification waits for reveal and interpolates using elapsed frame time.
// - Internal drags freeze the painted positions until GTK finishes reordering.
// - Native X11 expands only the painted background, keeping GTK allocations fixed.
// - Frame origins use allocated spacers, never pending margin requests.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_home_item.h"
#include "dock_session_item.h"

#include "application/dock_runtime_info.h"
#include "dock_constants.h"
#include "presentation/presentation_selector.h"
#include "dock_window_controller.h"
#include "layout/dock_layout_metrics.h"
#include "layout/dock_layout_geometry.h"
#include "windowing/running_application.h"
#include "windowing/window_registry.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

const std::vector<DockItem *> &
DockWindow::dock_items() const
{
    return m_dock_items_cache;
}

void DockWindow::register_dock_item(
    DockItem *item)
{
    if (!item ||
        std::find(
            m_dock_items_cache.begin(),
            m_dock_items_cache.end(),
            item) != m_dock_items_cache.end())
    {
        return;
    }

    // Magnified anchors and pixbufs are a snapshot of the current children.
    // Clear that snapshot before changing membership; hiding a newly added
    // base item without rebuilding the snapshot would make it invisible.
    if (m_magnified_pointer_active)
        reset_magnified_hover();

    // Update the typed view first: signal_add() observers can query the dock
    // synchronously while Gtk::Box::pack_start() is still on the stack.
    m_dock_items_cache.push_back(item);
    m_dock_box.pack_start(
        *item,
        Gtk::PACK_SHRINK);
    item->show();
}

void DockWindow::unregister_dock_item(
    DockItem *item)
{
    if (!item)
        return;

    const auto item_position =
        std::find(
            m_dock_items_cache.begin(),
            m_dock_items_cache.end(),
            item);
    if (item_position == m_dock_items_cache.end())
        return;

    // Clear painted copies and borrowed magnified anchors while the item is
    // still alive. Otherwise its final pixbuf can remain visible after GTK
    // removes and destroys the base child.
    if (m_magnified_pointer_active)
        reset_magnified_hover();

    // Gtk::manage() allows removal to destroy the child. Erase the borrowed
    // pointer before remove() and before signal_remove() observers run.
    m_dock_items_cache.erase(item_position);
    m_dock_box.remove(*item);
}

void DockWindow::update_magnified_hover(int x, int y)
{
    // A drag uses the painted positions captured before GTK takes the grab.
    // Do not capture a new snapshot while reorder allocation is pending.
    if (m_dragged_item)
        return;

    if (!m_magnified_enabled ||
        !m_controller ||
        !m_controller->is_fully_revealed() ||
        m_controller->settings().hover_effect() !=
            DockHoverEffect::magnified)
        return;

    // The mapped surface includes transparent overflow reserved for enlarged
    // pixels. That region is not part of the hover target: begin returning to
    // normal size as soon as motion leaves the visible dock body.
    if (!point_is_over_dock_body(x, y))
    {
        if (m_magnified_pointer_active)
            release_magnified_hover();
        return;
    }

    int box_x = 0;
    int box_y = 0;
    m_dock_box.translate_coordinates(
        *this,
        0,
        0,
        box_x,
        box_y);
    const bool vertical =
        m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_VERTICAL;
    const auto leading_geometry =
        DockLayoutGeometry{}.item_geometry(m_leading_margin, *this);
    const int main_axis_origin = vertical
                                     ? leading_geometry.y +
                                           leading_geometry.height
                                     : leading_geometry.x +
                                           leading_geometry.width;
    m_magnified_pointer_x = vertical
                                ? x - box_x
                                : x - main_axis_origin;
    m_magnified_pointer_y = vertical
                                ? y - main_axis_origin
                                : y - box_y;

    const bool activate_layer =
        !m_magnified_pointer_active;
    m_magnified_releasing = false;
    if (activate_layer)
    {
        m_magnified_anchors.clear();
        for (auto *item : m_dock_items_cache)
        {
            if (!item->get_visible())
                continue;

            const auto geometry = item->icon_geometry();
            m_magnified_anchors.emplace_back(
                item,
                vertical
                    ? geometry.center_y - main_axis_origin
                    : geometry.center_x - main_axis_origin);
        }
        if (m_home_item && m_home_item->get_visible())
        {
            const auto geometry = m_home_item->icon_geometry();
            m_home_magnified_anchor = vertical
                                          ? geometry.center_y - main_axis_origin
                                          : geometry.center_x - main_axis_origin;
        }
        m_magnified_pointer_active = true;
    }

    if (m_magnified_tick_callback == 0)
    {
        m_magnified_tick_callback =
            add_tick_callback(
                [this](
                    const Glib::RefPtr<Gdk::FrameClock> &)
                {
                    return advance_magnified_hover();
                });
    }

    // The frame-clock callback is the sole steady-state renderer. Pointer
    // motion only updates the target coordinates; rendering here as well can
    // queue competing full overlay repaints while the pointer is moving
    // quickly.
    if (activate_layer)
    {
        // Hide every normal Gtk::Image before producing the first magnified
        // frame. The first draw must be an atomic handoff; otherwise the
        // compositor can present the base icon underneath the enlarged copy
        // for one frame while the hover layer is being initialized.
        set_magnified_layer_active(true);
        advance_magnified_hover();
    }
}

void DockWindow::set_magnified_layer_active(
    bool active)
{
    m_magnified_x11_buffered = active && is_native_x11_presentation();
    m_dock_box.set_external_background(m_magnified_x11_buffered);
    if (!active)
        m_magnified_painted_margin_extra = 0.0;
    for (auto *item : m_dock_items_cache)
        item->set_magnified_layer_active(active);
    if (m_home_item)
        m_home_item->set_magnified_layer_active(active);
}

void DockWindow::reset_magnified_hover()
{
    if (m_magnified_tick_callback != 0)
    {
        remove_tick_callback(
            m_magnified_tick_callback);
        m_magnified_tick_callback = 0;
    }

    clear_magnified_hover_frame();
}

void DockWindow::release_magnified_hover()
{
    if (!m_magnified_pointer_active ||
        m_magnified_releasing)
    {
        return;
    }

    // Retain the frame snapshot and hidden base images while the same
    // frame-clock interpolation used for hover growth eases every scale back
    // to 1.0. Re-entering the dock clears this flag and reverses smoothly from
    // the current intermediate scales.
    m_magnified_releasing = true;
}

void DockWindow::clear_magnified_hover_frame()
{
    set_magnified_main_axis_overflow(0);
    m_magnified_layer.set_icons({});
    set_magnified_layer_active(false);
    for (auto *item : m_dock_items_cache)
        item->set_magnified_scale(1.0);
    if (m_home_item)
        m_home_item->set_magnified_scale(1.0);
    m_magnified_anchors.clear();
    m_magnified_visual_centers.clear();
    m_magnified_pointer_active = false;
    m_magnified_frame_initialized = false;
    m_magnified_frame_time_us = 0;
    m_magnified_releasing = false;
    queue_draw();
}

bool DockWindow::advance_magnified_hover()
{
    if (m_dragged_item)
        return true;

    if (!m_magnified_enabled ||
        !m_magnified_pointer_active)
    {
        m_magnified_layer.set_icons({});
        return false;
    }

    const bool vertical =
        m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_VERTICAL;
    const double pointer = vertical
                               ? m_magnified_pointer_y
                               : m_magnified_pointer_x;
    const bool pointer_changed =
        !m_magnified_frame_initialized ||
        m_magnified_pointer_x !=
            m_magnified_last_rendered_pointer_x ||
        m_magnified_pointer_y !=
            m_magnified_last_rendered_pointer_y;
    bool frame_changed = false;
    const auto clock = get_frame_clock();
    const gint64 now = clock ? clock->get_frame_time()
                             : g_get_monotonic_time();
    const double elapsed_ms = m_magnified_frame_time_us == 0
        ? 1000.0 / 60.0
        : std::max(0.0, (now - m_magnified_frame_time_us) / 1000.0);
    m_magnified_frame_time_us = now;
    // Preserve the 60 Hz response without slowing down on missed frames or
    // speeding up on high-refresh displays.
    const double smoothing =
        1.0 - std::pow(1.0 - 0.28, elapsed_ms / (1000.0 / 60.0));

    int box_x = 0;
    int box_y = 0;
    m_dock_box.translate_coordinates(
        *this,
        0,
        0,
        box_x,
        box_y);
    const auto box_allocation =
        m_dock_box.get_allocation();
    const auto dock_location = location();
    // set_size_request() changes the requested margin immediately, but GTK
    // allocates it later. Read both origin and spacer from the same allocated
    // geometry so a pending layout cannot kick the entire icon group sideways.
    const auto leading_geometry =
        DockLayoutGeometry{}.item_geometry(m_leading_margin, *this);
    const double main_axis_origin = vertical
                                        ? leading_geometry.y +
                                              leading_geometry.height
                                        : leading_geometry.x +
                                              leading_geometry.width;
    std::vector<DockMagnifiedIcon> icons;

    std::vector<double> anchors;
    if (m_home_item && m_home_item->get_visible())
        anchors.push_back(m_home_magnified_anchor);
    for (const auto &[item, anchor] : m_magnified_anchors)
    {
        if (item && item->get_visible())
            anchors.push_back(anchor);
    }
    std::sort(anchors.begin(), anchors.end());

    double distance = static_cast<double>(
        DockLayoutMetrics::item_size_for(
            std::max(1, m_effective_icon_size)));
    for (std::size_t index = 1; index < anchors.size(); ++index)
    {
        distance = std::min(
            distance,
            anchors[index] - anchors[index - 1]);
    }
    distance = std::max(1.0, distance);

    const auto proximity_for = [pointer, distance](double anchor)
    {
        const double linear = std::max(
            0.0,
            1.0 - std::abs(pointer - anchor) / distance);
        return linear * linear * (3.0 - 2.0 * linear);
    };

    const auto target_scale_for =
        [this, &proximity_for](double anchor)
    {
        if (m_magnified_releasing)
            return 1.0;

        constexpr double max_scale = 2.25;
        return 1.0 + (max_scale - 1.0) *
                          proximity_for(anchor);
    };

    // Once every icon has reached its target, an idle timer tick must not
    // recreate pixbufs or queue another full overlay repaint.
    if (m_magnified_frame_initialized && !pointer_changed)
    {
        bool animation_pending = false;
        if (m_home_item &&
            m_home_item->get_visible() &&
            std::abs(
                m_home_item->magnified_scale() -
                target_scale_for(m_home_magnified_anchor)) >=
                0.001)
        {
            animation_pending = true;
        }
        for (const auto &[item, anchor] : m_magnified_anchors)
        {
            if (item &&
                item->get_visible() &&
                std::abs(
                    item->magnified_scale() -
                    target_scale_for(anchor)) >=
                    0.001)
            {
                animation_pending = true;
                break;
            }
        }
        if (!animation_pending)
        {
            if (m_magnified_releasing)
            {
                // Returning false removes the callback which is currently
                // executing, so clear its stored identifier before handing
                // rendering back to the normal Gtk::Image children.
                m_magnified_tick_callback = 0;
                clear_magnified_hover_frame();
                return false;
            }
            return true;
        }
    }

    struct MagnifiedFrame
    {
        Gtk::Widget *widget = nullptr;
        DockItem *item = nullptr;
        double anchor = 0.0;
        double extent = 0.0;
        double center = 0.0;
        Glib::RefPtr<Gdk::Pixbuf> pixbuf;
    };

    std::vector<MagnifiedFrame> frames;
    frames.reserve(
        m_magnified_anchors.size() +
        (m_home_item ? 1 : 0));

    const auto add_icon = [
                              &icons,
                              vertical,
                              box_x,
                              box_y,
                              box_allocation,
                              dock_location,
                              this](
                              double anchor,
                              double visual_extent,
                              const Glib::RefPtr<Gdk::Pixbuf> &pixbuf,
                              DockItem *item)
    {
        if (!pixbuf)
            return;

        DockMagnifiedIcon icon;
        icon.pixbuf = pixbuf;
        // The canvas always has maximum-scale dimensions, but its visible
        // current-scale extent is centered inside it. Offset the canvas so
        // that visible extent—not the transparent canvas edge—stays attached
        // to the dock and grows only toward the screen interior.
        if (vertical)
        {
            const double extent = std::min(
                visual_extent,
                static_cast<double>(pixbuf->get_width()));
            const double inset =
                (pixbuf->get_width() - extent) / 2.0;
            icon.y = anchor - pixbuf->get_height() / 2.0;
            icon.x =
                dock_location == DockLocation::left
                    ? box_x - inset
                    : box_x + box_allocation.get_width() -
                          extent - inset;
        }
        else
        {
            const double extent = std::min(
                visual_extent,
                static_cast<double>(pixbuf->get_height()));
            const double inset =
                (pixbuf->get_height() - extent) / 2.0;
            icon.x = anchor - pixbuf->get_width() / 2.0;
            icon.y =
                dock_location == DockLocation::top
                    ? box_y - inset
                    : box_y + box_allocation.get_height() -
                          extent - inset;
        }

        if (item)
        {
            const auto allocation = item->get_allocation();
            int item_x = 0;
            int item_y = 0;
            item->translate_coordinates(
                *this,
                0,
                0,
                item_x,
                item_y);
            icon.indicator = item->indicator_visual();
            icon.indicator_width = allocation.get_width();
            icon.indicator_height = allocation.get_height();
            if (vertical)
            {
                icon.indicator_x = item_x;
                icon.indicator_y = static_cast<int>(
                    std::lround(
                        anchor -
                        allocation.get_height() / 2.0));
            }
            else
            {
                icon.indicator_x = static_cast<int>(
                    std::lround(
                        anchor -
                        allocation.get_width() / 2.0));
                icon.indicator_y = item_y;
            }
        }
        icons.push_back(std::move(icon));
    };

    const auto add_frame = [
                              &frames,
                              this](
                              Gtk::Widget *widget,
                              DockItem *item,
                              double anchor,
                              double scale,
                              const Glib::RefPtr<Gdk::Pixbuf> &pixbuf)
    {
        if (!pixbuf)
            return;

        const double visual_extent =
            std::max(1, m_effective_icon_size) * scale +
            2 * DockLayoutMetrics::DOCK_ITEM_PADDING;
        frames.push_back({
            widget,
            item,
            anchor,
            visual_extent,
            anchor,
            pixbuf});
    };

    const auto next_scale = [smoothing, &frame_changed](
                                double current,
                                double target)
    {
        double next =
            current + (target - current) * smoothing;
        if (std::abs(next - target) < 0.001)
            next = target;
        if (next != current)
            frame_changed = true;
        return next;
    };

    if (m_home_item && m_home_item->get_visible())
    {
        const double target =
            target_scale_for(m_home_magnified_anchor);
        const double scale = next_scale(
            m_home_item->magnified_scale(),
            target);
        m_home_item->set_magnified_scale(scale);
        add_frame(
            m_home_item,
            nullptr,
            m_home_magnified_anchor,
            scale,
            m_home_item->magnified_icon(scale));
    }

    for (const auto &[item, anchor] : m_magnified_anchors)
    {
        if (!item || !item->get_visible())
            continue;

        const double target =
            target_scale_for(anchor);
        const double scale = next_scale(
            item->magnified_scale(),
            target);
        item->set_magnified_scale(scale);
        add_frame(
            item,
            item,
            anchor,
            scale,
            item->magnified_icon(scale));
    }

    if (!pointer_changed && !frame_changed)
        return true;

    m_magnified_last_rendered_pointer_x =
        m_magnified_pointer_x;
    m_magnified_last_rendered_pointer_y =
        m_magnified_pointer_y;
    m_magnified_frame_initialized = true;

    std::sort(
        frames.begin(),
        frames.end(),
        [](const MagnifiedFrame &left,
           const MagnifiedFrame &right)
        {
            return left.anchor < right.anchor;
        });
    // Enlarge the visual centers smoothly when the current icon extents need
    // more room. Recenter the complete visible bounds, rather than only the
    // displaced centers, so both dock ends need the same live extension. The
    // outer surface already owns fixed transparent capacity; only the styled
    // box margins follow this interpolated frame.
    if (!frames.empty())
    {
        for (std::size_t index = 1; index < frames.size(); ++index)
        {
            const double required_spacing =
                (frames[index - 1].extent +
                 frames[index].extent) /
                2.0;
            frames[index].center = std::max(
                frames[index].anchor,
                frames[index - 1].center +
                    required_spacing);
        }

        const double normal_extent =
            DockLayoutMetrics::item_size_for(
                std::max(1, m_effective_icon_size));
        const double normal_leading_edge =
            frames.front().anchor -
            normal_extent / 2.0;
        const double normal_trailing_edge =
            frames.back().anchor +
            normal_extent / 2.0;
        const double visual_leading_edge =
            frames.front().center -
            frames.front().extent / 2.0;
        const double visual_trailing_edge =
            frames.back().center +
            frames.back().extent / 2.0;
        const double leading_growth =
            normal_leading_edge -
            visual_leading_edge;
        const double trailing_growth =
            visual_trailing_edge -
            normal_trailing_edge;
        const double centering_shift =
            (leading_growth - trailing_growth) /
            2.0;
        for (auto &frame : frames)
            frame.center += centering_shift;

        const double margin_extra = std::max(
            0.0,
            (leading_growth + trailing_growth) / 2.0);
        set_magnified_main_axis_overflow(
            margin_extra);
    }

    for (const auto &frame : frames)
    {
        const double painted_center =
            frame.center +
            main_axis_origin;
        add_icon(
            painted_center,
            frame.extent,
            frame.pixbuf,
            frame.item);
    }

    // GTK item allocations remain fixed while the magnified copies spread
    // apart. Preserve the painted centers so tooltips and previews can anchor
    // to the visible icon rather than its now-hidden base image. This matters
    // most for the first and last icon, which move in opposite directions.
    m_magnified_visual_centers.clear();
    for (const auto &frame : frames)
    {
        if (frame.widget)
        {
            const double painted_center =
                frame.center +
                main_axis_origin;
            m_magnified_visual_centers.emplace_back(
                frame.widget,
                painted_center);
        }
    }

    m_magnified_layer.set_icons(std::move(icons));
    queue_draw();
    return true;
}

void DockWindow::apply_magnified_visual_center(
    Gtk::Widget &item,
    ItemGeometry &geometry) const
{
    if (!m_magnified_pointer_active)
        return;

    const auto center = std::find_if(
        m_magnified_visual_centers.begin(),
        m_magnified_visual_centers.end(),
        [&item](const auto &entry)
        {
            return entry.first == &item;
        });
    if (center == m_magnified_visual_centers.end())
        return;

    const int visual_center =
        static_cast<int>(std::lround(center->second));
    if (m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_VERTICAL)
    {
        geometry.center_y = visual_center;
    }
    else
    {
        geometry.center_x = visual_center;
    }
}

Glib::RefPtr<Gio::AppInfo>
DockWindow::application_for_running(
    const std::string &desktop_id) const
{
    auto app =
        m_launcher_manager
            .find_application(
                desktop_id);

    if (app)
        return app;

    std::string display_name =
        desktop_id;

    if (m_window_registry)
    {
        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    desktop_id);

        const auto window =
            std::find_if(
                m_window_registry
                    ->windows()
                    .begin(),
                m_window_registry
                    ->windows()
                    .end(),
                [&normalized_id](
                    const ManagedWindow
                        &candidate)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   candidate
                                       .desktop_file_name) ==
                           normalized_id;
                });

        if (window !=
                m_window_registry
                    ->windows()
                    .end() &&
            !window->caption.empty())
        {
            display_name =
                window->caption;
        }
    }

    auto command =
        LauncherManager::
            normalize_desktop_id(
                desktop_id);

    constexpr char suffix[] =
        ".desktop"; // Desktop-entry filename suffix

    if (command.size() >=
        sizeof(suffix) - 1)
    {
        command.erase(
            command.size() -
            (sizeof(suffix) - 1));
    }

    try
    {
        return Gio::AppInfo::
            create_from_commandline(
                command,
                display_name,
                Gio::APP_INFO_CREATE_NONE);
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot create a dock item for running application '%s': %s",
            desktop_id.c_str(),
            error.what().c_str());
        return {};
    }
}

void DockWindow::schedule_dock_item_sync()
{
    if (m_dock_item_sync.connected())
        return;

    m_dock_item_sync =
        Glib::signal_idle().connect(
            [this]()
            {
                synchronize_dock_items();
                return false;
            });
}

// The Session editor calls this after writing a Session. Sessions take part in
// the ordinary item synchronization, so this only has to force a pass that the
// unchanged attached/running snapshots would otherwise skip.
void DockWindow::synchronize_session_items()
{
    m_has_synchronized_items = false;
    synchronize_dock_items();
}

void DockWindow::synchronize_dock_items()
{
    struct DesiredItem
    {
        std::string desktop_id;
        Glib::RefPtr<Gio::AppInfo> app;
        bool attached = false;
        bool is_session = false;
        SessionRecord session;
    };

    std::vector<DesiredItem> desired_items;

    // Saved Sessions are stored after the launcher lines in docklight.data.
    // Collect their identities here for change detection, then append the
    // actual desired items after ordinary dock items below.
    const auto sessions =
        m_launcher_manager.sessions();

    std::vector<std::string> session_ids;
    session_ids.reserve(sessions.size());

    for (const auto &session : sessions)
    {
        const auto desktop_id =
            DockSessionItem::session_desktop_id(
                session.name);

        session_ids.push_back(desktop_id);
    }

    const auto attached_ids =
        m_launcher_manager
            .attached_ids();
    const auto stored_dock_order =
        m_launcher_manager.dock_order();

    std::vector<std::string>
        normalized_attached_ids;
    std::vector<std::string>
        normalized_running_ids;

    for (const auto &desktop_id :
         attached_ids)
    {
        normalized_attached_ids
            .push_back(
                m_launcher_manager
                    .normalize_resolved_id(
                        desktop_id));
    }

    if (m_window_registry)
    {
        for (const auto &running :
             m_window_registry
                 ->running_applications())
        {
            normalized_running_ids
                .push_back(
                    LauncherManager::
                        normalize_desktop_id(
                            running
                                .desktop_file_name));
        }
    }

    std::sort(
        normalized_running_ids.begin(),
        normalized_running_ids.end());

    normalized_running_ids.erase(
        std::unique(
            normalized_running_ids.begin(),
            normalized_running_ids.end()),
        normalized_running_ids.end());

    if (m_has_synchronized_items &&
        normalized_attached_ids ==
            m_synchronized_attached_ids &&
        normalized_running_ids ==
            m_synchronized_running_ids &&
        session_ids == m_synchronized_session_ids &&
        stored_dock_order ==
            m_synchronized_dock_order)
    {
        return;
    }

    m_synchronized_attached_ids =
        normalized_attached_ids;
    m_synchronized_running_ids =
        normalized_running_ids;
    m_synchronized_session_ids = session_ids;
    m_synchronized_dock_order =
        stored_dock_order;
    m_has_synchronized_items = true;

    const int maximum_items =
        std::max(
            0,
            DockConstants::MAX_DOCK_ITEMS -
                1);

    const auto &current_items =
        dock_items();

    // Keep the live visual order, including positions where running,
    // unattached applications have been dropped between attached launchers.
    for (auto *item : current_items)
    {
        if (static_cast<int>(
                desired_items.size()) >=
            maximum_items)
        {
            break;
        }

        // Sessions are appended after every ordinary item below, matching
        // their position after the launcher list in docklight.data.
        if (DockSessionItem::is_session_desktop_id(
                item->desktop_id()))
        {
            continue;
        }

        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    item->desktop_id());

        const bool attached =
            std::find(
                normalized_attached_ids
                    .begin(),
                normalized_attached_ids
                    .end(),
                normalized_id) !=
            normalized_attached_ids.end();

        const bool running =
            item->running();

        if (!attached && !running)
            continue;

        const bool already_present =
            std::any_of(
                desired_items.begin(),
                desired_items.end(),
                [&normalized_id](
                    const DesiredItem
                        &candidate)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   candidate
                                       .desktop_id) ==
                           normalized_id;
                });

        if (already_present)
            continue;

        desired_items.push_back(
            {item->desktop_id(),
             {},
             attached,
             false,
             {}});
    }

    for (const auto &desktop_id :
         attached_ids)
    {
        if (static_cast<int>(
                desired_items.size()) >=
            maximum_items)
        {
            break;
        }

        auto app =
            m_launcher_manager
                .find_application(
                    desktop_id);

        auto normalized_id =
            m_launcher_manager
                .normalize_resolved_id(
                    desktop_id);

        if (!app &&
            std::binary_search(
                normalized_running_ids
                    .begin(),
                normalized_running_ids
                    .end(),
                normalized_id))
        {
            app =
                application_for_running(
                    desktop_id);
        }

        if (!app)
        {
            g_warning(
                "Attached launcher '%s' is not installed",
                desktop_id.c_str());
            continue;
        }

        const auto canonical_id =
            !app->get_id().empty()
                ? app->get_id()
                : desktop_id;

        normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    canonical_id);

        const bool already_present =
            std::any_of(
                desired_items.begin(),
                desired_items.end(),
                [&normalized_id](
                    const DesiredItem
                        &candidate)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   candidate
                                       .desktop_id) ==
                           normalized_id;
                });

        if (already_present)
            continue;

        desired_items.push_back(
            {canonical_id,
             std::move(app),
             true,
             false,
             {}});
    }

    if (m_window_registry)
    {
        for (const auto &running :
             m_window_registry
                 ->running_applications())
        {
            if (static_cast<int>(
                    desired_items.size()) >=
                maximum_items)
            {
                break;
            }

            auto app =
                application_for_running(
                    running
                        .desktop_file_name);

            if (!app)
                continue;

            const auto canonical_id =
                !app->get_id().empty()
                    ? app->get_id()
                    : running
                          .desktop_file_name;

            const auto normalized_id =
                LauncherManager::
                    normalize_desktop_id(
                        canonical_id);

            const bool already_present =
                std::any_of(
                    desired_items.begin(),
                    desired_items.end(),
                    [&normalized_id](
                        const DesiredItem
                            &candidate)
                    {
                        return LauncherManager::
                                   normalize_desktop_id(
                                       candidate
                                           .desktop_id) ==
                               normalized_id;
                    });

            if (already_present)
                continue;

            desired_items.push_back(
                {canonical_id,
                 std::move(app),
                 false,
                 false,
                 {}});
        }
    }

    for (const auto &session : sessions)
    {
        if (static_cast<int>(
                desired_items.size()) >=
            maximum_items)
        {
            break;
        }

        desired_items.push_back(
            {DockSessionItem::session_desktop_id(
                 session.name),
             {},
             true,
             true,
             session});
    }

    // Apply the explicitly persisted mixed order. Old files synthesize the
    // compatibility order (launchers, then Sessions) in dock_order(). Any
    // live, unattached applications are not persistent and remain afterward.
    std::vector<DesiredItem> ordered_desired_items;
    ordered_desired_items.reserve(desired_items.size());

    for (const auto &stored_id : stored_dock_order)
    {
        const bool stored_is_session =
            DockSessionItem::is_session_desktop_id(
                stored_id);
        const auto normalized_id = stored_is_session
            ? stored_id
            : m_launcher_manager.normalize_resolved_id(
                  stored_id);

        const auto desired = std::find_if(
            desired_items.begin(),
            desired_items.end(),
            [this, stored_is_session, &normalized_id](
                const DesiredItem &candidate)
            {
                if (candidate.is_session !=
                    stored_is_session)
                {
                    return false;
                }

                return stored_is_session
                    ? candidate.desktop_id == normalized_id
                    : m_launcher_manager.normalize_resolved_id(
                          candidate.desktop_id) ==
                          normalized_id;
            });

        if (desired == desired_items.end())
            continue;

        ordered_desired_items.push_back(
            std::move(*desired));
        desired_items.erase(desired);
    }

    ordered_desired_items.insert(
        ordered_desired_items.end(),
        std::make_move_iterator(desired_items.begin()),
        std::make_move_iterator(desired_items.end()));
    desired_items =
        std::move(ordered_desired_items);

    // The coarse attached/running snapshots above avoid building this list
    // for title-only registry updates. This structural diff handles the
    // remaining case where a snapshot changed but resolved to the same live
    // widget sequence and attachment state.
    bool dock_structure_changed =
        current_items.size() != desired_items.size();

    if (!dock_structure_changed)
    {
        for (std::size_t index = 0;
             index < current_items.size();
             ++index)
        {
            const auto current_id =
                LauncherManager::normalize_desktop_id(
                    current_items[index]->desktop_id());
            const auto desired_id =
                LauncherManager::normalize_desktop_id(
                    desired_items[index].desktop_id);

            if (current_id != desired_id ||
                current_items[index]->attached() !=
                    desired_items[index].attached)
            {
                dock_structure_changed = true;
                break;
            }
        }
    }

    if (!dock_structure_changed)
    {
        for (std::size_t index = 0;
             index < current_items.size();
             ++index)
        {
            if (desired_items[index].is_session)
            {
                static_cast<DockSessionItem *>(
                    current_items[index])
                    ->set_session(
                        desired_items[index]
                            .session);
            }
            else
            {
                current_items[index]
                    ->refresh_indicator();
            }
        }
        return;
    }

    // Reordering alone changes every captured center even when no child is
    // added or removed. Treat the whole structural update as a renderer
    // boundary and let the next pointer motion capture the completed layout.
    if (m_magnified_pointer_active)
        reset_magnified_hover();

    auto existing_items =
        dock_items();

    std::vector<DockItem *>
        ordered_items;

    bool children_changed = false;

    for (const auto &desired :
         desired_items)
    {
        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    desired.desktop_id);

        const auto existing =
            std::find_if(
                existing_items.begin(),
                existing_items.end(),
                [&normalized_id](
                    DockItem *item)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   item
                                       ->desktop_id()) ==
                           normalized_id;
                });

        DockItem *item = nullptr;

        if (existing !=
            existing_items.end())
        {
            item = *existing;
            existing_items.erase(
                existing);
            item->set_attached(
                desired.attached);

            if (desired.is_session)
            {
                static_cast<DockSessionItem *>(
                    item)
                    ->set_session(
                        desired.session);
            }
        }
        else if (desired.is_session)
        {
            item =
                Gtk::manage(
                    new DockSessionItem(
                        *this,
                        m_window_registry,
                        desired.session,
                        m_effective_icon_size > 0
                            ? m_effective_icon_size
                            : m_controller
                                  ->settings()
                                  .icon_size(),
                        m_controller->settings()
                            .hover_effect(),
                        m_controller->settings()
                            .indicator(),
                        m_controller->settings()
                            .indicator_color()));

            item->set_manage_all_workspaces(
                m_controller->settings()
                    .manage_all_workspaces());

            register_dock_item(item);
            children_changed = true;
        }
        else
        {
            auto app = desired.app;

            if (!app)
            {
                app =
                    application_for_running(
                        desired.desktop_id);
            }

            if (!app)
                continue;

            item =
                Gtk::manage(
                    new DockItem(
                        *this,
                        app,
                        desired.desktop_id,
                        desired.attached,
                        m_window_registry,
                        m_effective_icon_size > 0
                            ? m_effective_icon_size
                            : m_controller
                                  ->settings()
                                  .icon_size(),
                        m_controller
                            ->settings()
                            .hover_effect(),
                        m_controller
                            ->settings()
                            .indicator(),
                        m_controller
                            ->settings()
                            .indicator_color()));

            item->set_manage_all_workspaces(
                m_controller
                    ->settings()
                    .manage_all_workspaces());

            register_dock_item(item);
            children_changed = true;
        }

        ordered_items.push_back(item);
    }

    if (!existing_items.empty())
        hide_tooltip_immediately();

    for (auto *item : existing_items)
    {
        unregister_dock_item(item);
        children_changed = true;
    }

    // GTK3 exposes child-notify freezing on each child widget, not on the
    // container. Queue at most one child-property notification per survivor
    // while their positions are updated as one logical transaction.
    for (auto *item : ordered_items)
    {
        gtk_widget_freeze_child_notify(
            GTK_WIDGET(item->gobj()));
    }
    gtk_widget_freeze_child_notify(
        GTK_WIDGET(m_trailing_margin.gobj()));

    int position = 2;

    for (auto *item : ordered_items)
    {
        m_dock_box.reorder_child(
            *item,
            position++);
    }

    m_dock_items_cache = ordered_items;

    m_dock_box.reorder_child(
        m_trailing_margin,
        -1);

    for (auto *item : ordered_items)
    {
        gtk_widget_thaw_child_notify(
            GTK_WIDGET(item->gobj()));
    }
    gtk_widget_thaw_child_notify(
        GTK_WIDGET(m_trailing_margin.gobj()));

    if (children_changed)
    {
        m_controller->dock_items_changed();
    }
    else if (m_effective_icon_size > 0)
    {
        apply_visual_style();
    }
}

// Creates the persistent dock container and its initial items after the
// controller is available. Separating construction from the window
// constructor also gives later synchronization a single widget setup path.
void DockWindow::create_dock(
    const DockRuntimeInfo &runtime_info)
{
    m_dock_box.pack_start(
        m_leading_margin,
        Gtk::PACK_SHRINK);

    m_home_item =
        Gtk::manage(
            new DockHomeItem(
                *this,
                m_window_registry,
                runtime_info,
                m_controller
                    ->settings()
                    .icon_size(),
                m_controller
                    ->settings()
                    .home_icon_path()));

    m_dock_box.pack_start(
        *m_home_item,
        Gtk::PACK_SHRINK);

    m_dock_box.pack_start(
        m_trailing_margin,
        Gtk::PACK_SHRINK);

    synchronize_dock_items();
    m_dock_box.show_all();
    m_dock_overlay.show();

    if (!m_controller
             ->settings()
             .home_icon_enabled())
    {
        m_home_item->hide();
    }
}
