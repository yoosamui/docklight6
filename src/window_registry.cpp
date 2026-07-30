#include "window_registry.h"

#include <algorithm>
#include <cctype>
#include <iterator>

namespace
{

bool has_desktop_suffix(
    const std::string &value)
{
    constexpr char suffix[] = ".desktop"; // Desktop-entry filename suffix
    constexpr std::size_t suffix_length =
        sizeof(suffix) - 1; // Suffix length without the null terminator

    return value.size() >= suffix_length &&
           value.compare(
               value.size() - suffix_length,
               suffix_length,
               suffix) == 0;
}

bool is_docklight_window(
    const ManagedWindow &window)
{
    return window.desktop_file_name ==
               "org.docklight6.desktop" ||
           window.desktop_file_name ==
               "docklight6.desktop" ||
           window.desktop_file_name ==
               "docklight.desktop";
}

bool has_same_dock_state(
    const ManagedWindow &left,
    const ManagedWindow &right)
{
    // Frame geometry is retained in the registry, but it does not affect any
    // dock item state. KWin effects and interactive moves can update it many
    // times per frame, so it must not trigger a full dock refresh.
    return left.id == right.id &&
           left.desktop_file_name ==
               right.desktop_file_name &&
           left.caption == right.caption &&
           left.icon_name == right.icon_name &&
           left.icon_png == right.icon_png &&
           left.activity_ids ==
               right.activity_ids &&
           left.desktop_ids ==
               right.desktop_ids &&
           left.desktop_numbers ==
               right.desktop_numbers &&
           left.process_id == right.process_id &&
           left.minimized == right.minimized &&
           left.maximized == right.maximized &&
           left.skip_taskbar ==
               right.skip_taskbar &&
           left.on_current_desktop ==
               right.on_current_desktop;
}

}

WindowRegistry::WindowRegistry(
    WindowBackend &backend)
    : m_backend(backend)
{
}

WindowRegistry::~WindowRegistry()
{
    stop();
}

void WindowRegistry::start()
{
    if (m_started)
        return;

    m_started = true;

    m_connections.push_back(
        m_backend
            .signal_window_added()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_window_added)));

    m_connections.push_back(
        m_backend
            .signal_window_updated()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_window_updated)));

    m_connections.push_back(
        m_backend
            .signal_window_removed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_window_removed)));

    m_connections.push_back(
        m_backend
            .signal_active_window_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_active_window_changed)));

    m_connections.push_back(
        m_backend
            .signal_stacking_order_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_stacking_order_changed)));

    m_connections.push_back(
        m_backend
            .signal_connection_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_connection_changed)));

    m_connections.push_back(
        m_backend
            .signal_snapshot_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowRegistry::
                        on_snapshot_changed)));

    m_connections.push_back(
        m_backend
            .signal_dock_surface_geometry_changed()
            .connect(
                [this]()
                {
                    m_signal_dock_surface_geometry_changed
                        .emit();
                }));

    m_backend.start();

    if (!m_connected)
        load_snapshot();
}

void WindowRegistry::stop()
{
    if (!m_started)
        return;

    m_backend.stop();

    for (auto &connection :
         m_connections)
    {
        connection.disconnect();
    }

    m_connections.clear();
    m_started = false;

    clear();
}

bool WindowRegistry::connected() const
{
    return m_connected;
}

WindowBackendCapabilities
WindowRegistry::capabilities() const
{
    return m_backend.capabilities();
}

const std::vector<ManagedWindow> &
WindowRegistry::windows() const
{
    return m_windows;
}

const std::vector<RunningApplication> &
WindowRegistry::running_applications() const
{
    return m_running_applications;
}

const std::optional<WindowId> &
WindowRegistry::active_window() const
{
    return m_active_window;
}

std::optional<WindowIconGeometry>
WindowRegistry::dock_surface_geometry() const
{
    return m_backend.dock_surface_geometry();
}

const ManagedWindow *
WindowRegistry::find_window(
    const WindowId &window_id) const
{
    const auto window =
        std::find_if(
            m_windows.begin(),
            m_windows.end(),
            [&window_id](
                const ManagedWindow
                    &candidate)
            {
                return candidate.id ==
                       window_id;
            });

    return window == m_windows.end()
               ? nullptr
               : &*window;
}

const RunningApplication *
WindowRegistry::find_application(
    const std::string
        &desktop_file_name) const
{
    const auto normalized_name =
        normalize_desktop_file_name(
            desktop_file_name);

    const auto application =
        std::find_if(
            m_running_applications.begin(),
            m_running_applications.end(),
            [&normalized_name](
                const RunningApplication
                    &candidate)
            {
                return candidate
                           .desktop_file_name ==
                       normalized_name;
            });

    return application ==
                   m_running_applications.end()
               ? nullptr
               : &*application;
}

bool WindowRegistry::activate_window(
    const WindowId &window_id)
{
    return m_connected &&
           m_backend.activate_window(
               window_id);
}

bool WindowRegistry::present_windows(
    const std::vector<WindowId>
        &window_ids)
{
    return m_connected &&
           !window_ids.empty() &&
           m_backend.present_windows(
               window_ids);
}

bool WindowRegistry::hide_windows(
    const std::vector<WindowId>
        &window_ids)
{
    return m_connected &&
           !window_ids.empty() &&
           m_backend.hide_windows(
               window_ids);
}

bool WindowRegistry::raise_window(
    const WindowId &window_id)
{
    return m_connected &&
           m_backend.raise_window(
               window_id);
}

bool WindowRegistry::close_window(
    const WindowId &window_id)
{
    return m_connected &&
           m_backend.close_window(
               window_id);
}

bool WindowRegistry::set_window_minimized(
    const WindowId &window_id,
    bool minimized)
{
    return m_connected &&
           m_backend
               .set_window_minimized(
                   window_id,
                   minimized);
}

bool WindowRegistry::set_window_maximized(
    const WindowId &window_id,
    bool maximized)
{
    return m_connected &&
           m_backend
               .set_window_maximized(
                   window_id,
                   maximized);
}

bool WindowRegistry::minimize_all()
{
    if (!m_connected ||
        !m_backend
             .capabilities()
             .can_minimize)
    {
        return false;
    }

    std::vector<WindowId> window_ids;

    for (const auto &window : m_windows)
    {
        if (!window.minimized)
        {
            window_ids.push_back(
                window.id);
        }
    }

    return hide_windows(window_ids);
}

bool WindowRegistry::unminimize_all()
{
    if (!m_connected ||
        !m_backend
             .capabilities()
             .can_minimize)
    {
        return false;
    }

    const auto windows_copy = m_windows;
    bool accepted = false;

    for (const auto &window : windows_copy)
    {
        if (window.minimized)
        {
            accepted =
                m_backend
                    .set_window_minimized(
                        window.id,
                        false) ||
                accepted;
        }
    }

    return accepted;
}

bool WindowRegistry::maximize_all()
{
    if (!m_connected ||
        !m_backend
             .capabilities()
             .can_maximize)
    {
        return false;
    }

    const auto windows_copy = m_windows;
    const auto capabilities =
        m_backend.capabilities();
    bool accepted = false;

    for (const auto &window : windows_copy)
    {
        if (window.minimized &&
            capabilities.can_minimize)
        {
            accepted =
                m_backend
                    .set_window_minimized(
                        window.id,
                        false) ||
                accepted;
        }

        if (!window.maximized)
        {
            accepted =
                m_backend
                    .set_window_maximized(
                        window.id,
                        true) ||
                accepted;
        }
    }

    return accepted;
}

bool WindowRegistry::close_all()
{
    if (!m_connected ||
        !m_backend
             .capabilities()
             .can_close)
    {
        return false;
    }

    const auto windows_copy = m_windows;
    bool accepted = false;

    for (const auto &window : windows_copy)
    {
        accepted =
            m_backend.close_window(
                window.id) ||
            accepted;
    }

    return accepted;
}

bool WindowRegistry::set_window_icon_geometry(
    const WindowId &window_id,
    const WindowIconGeometry &geometry)
{
    return m_backend
        .set_window_icon_geometry(
            window_id,
            geometry);
}

sigc::signal<void> &
WindowRegistry::signal_changed()
{
    return m_signal_changed;
}

sigc::signal<void, bool> &
WindowRegistry::signal_connection_changed()
{
    return m_signal_connection_changed;
}

sigc::signal<void> &
WindowRegistry::
    signal_dock_surface_geometry_changed()
{
    return m_signal_dock_surface_geometry_changed;
}

void WindowRegistry::load_snapshot()
{
    m_windows.clear();

    for (auto window :
         m_backend.windows())
    {
        if (window.id.empty())
        {
            continue;
        }

        window.desktop_file_name =
            normalize_desktop_file_name(
                window.desktop_file_name);

        if (window.skip_taskbar ||
            is_docklight_window(window))
        {
            continue;
        }

        m_windows.push_back(
            std::move(window));
    }

    apply_stacking_order(
        m_backend.stacking_order());

    apply_active_window(
        m_backend.active_window());

    m_connected = m_backend.connected();
    rebuild_applications();
    m_signal_changed.emit();
}

void WindowRegistry::clear()
{
    const bool had_state =
        !m_windows.empty() ||
        !m_running_applications.empty() ||
        m_active_window.has_value();

    m_windows.clear();
    m_running_applications.clear();
    m_active_window.reset();
    m_connected = false;

    if (had_state)
        m_signal_changed.emit();
}

void WindowRegistry::rebuild_applications()
{
    m_running_applications.clear();

    for (const auto &window :
         m_windows)
    {
        if (window.desktop_file_name.empty())
            continue;

        auto application =
            std::find_if(
                m_running_applications.begin(),
                m_running_applications.end(),
                [&window](
                    const RunningApplication
                        &candidate)
                {
                    return candidate
                               .desktop_file_name ==
                           window
                               .desktop_file_name;
                });

        if (application ==
            m_running_applications.end())
        {
            RunningApplication
                running_application;

            running_application
                .desktop_file_name =
                window.desktop_file_name;

            m_running_applications.push_back(
                std::move(
                    running_application));

            application =
                std::prev(
                    m_running_applications.end());
        }

        application->window_ids.push_back(
            window.id);

        if (m_active_window &&
            *m_active_window == window.id)
        {
            application
                ->active_window_id =
                window.id;
        }
    }
}

bool WindowRegistry::apply_stacking_order(
    const std::vector<WindowId>
        &stacking_order)
{
    if (stacking_order.empty() ||
        m_windows.size() < 2)
    {
        return false;
    }

    std::vector<WindowId> previous_order;
    std::vector<ManagedWindow>
        ordered_windows;

    previous_order.reserve(
        m_windows.size());

    ordered_windows.reserve(
        m_windows.size());

    for (const auto &window :
         m_windows)
    {
        previous_order.push_back(
            window.id);
    }

    for (const auto &window_id :
         stacking_order)
    {
        const auto window =
            std::find_if(
                m_windows.begin(),
                m_windows.end(),
                [&window_id](
                    const ManagedWindow
                        &candidate)
                {
                    return candidate.id ==
                           window_id;
                });

        if (window != m_windows.end())
        {
            ordered_windows.push_back(
                std::move(*window));

            window->id.clear();
        }
    }

    for (auto &window :
         m_windows)
    {
        if (!window.id.empty())
        {
            ordered_windows.push_back(
                std::move(window));
        }
    }

    m_windows =
        std::move(ordered_windows);

    return !std::equal(
        previous_order.begin(),
        previous_order.end(),
        m_windows.begin(),
        m_windows.end(),
        [](const WindowId &window_id,
           const ManagedWindow &window)
        {
            return window_id == window.id;
        });
}

bool WindowRegistry::apply_active_window(
    const std::optional<WindowId>
        &window_id)
{
    const auto previous_active_window =
        m_active_window;

    m_active_window.reset();

    for (auto &window :
         m_windows)
    {
        window.active =
            window_id &&
            window.id == *window_id;

        if (window.active)
            m_active_window = window.id;
    }

    return previous_active_window !=
           m_active_window;
}

void WindowRegistry::on_window_added(
    const ManagedWindow &window)
{
    on_window_updated(window);
}

void WindowRegistry::on_window_updated(
    const ManagedWindow &window)
{
    if (window.id.empty())
        return;

    ManagedWindow normalized_window =
        window;

    normalized_window
        .desktop_file_name =
        normalize_desktop_file_name(
            window.desktop_file_name);

    const auto current =
        std::find_if(
            m_windows.begin(),
            m_windows.end(),
            [&window](
                const ManagedWindow
                    &candidate)
            {
                return candidate.id ==
                       window.id;
            });

    if (normalized_window.skip_taskbar ||
        is_docklight_window(
            normalized_window))
    {
        if (current == m_windows.end())
            return;

        const bool was_active =
            m_active_window &&
            *m_active_window == window.id;

        m_windows.erase(current);

        if (was_active)
            m_active_window.reset();
    }
    else
    {
        normalized_window.active =
            m_active_window &&
            *m_active_window == window.id;

        if (current == m_windows.end())
        {
            m_windows.push_back(
                std::move(
                    normalized_window));
        }
        else
        {
            const bool dock_state_changed =
                !has_same_dock_state(
                    *current,
                    normalized_window);

            *current =
                std::move(
                    normalized_window);

            if (!dock_state_changed)
                return;
        }
    }

    rebuild_applications();
    m_signal_changed.emit();
}

void WindowRegistry::on_window_removed(
    const WindowId &window_id)
{
    const auto previous_size =
        m_windows.size();

    m_windows.erase(
        std::remove_if(
            m_windows.begin(),
            m_windows.end(),
            [&window_id](
                const ManagedWindow
                    &window)
            {
                return window.id ==
                       window_id;
            }),
        m_windows.end());

    if (m_windows.size() == previous_size)
        return;

    if (m_active_window &&
        *m_active_window == window_id)
    {
        m_active_window.reset();
    }

    rebuild_applications();
    m_signal_changed.emit();
}

void WindowRegistry::
    on_active_window_changed(
        const std::optional<WindowId>
            &window_id)
{
    if (!apply_active_window(window_id))
        return;

    rebuild_applications();
    m_signal_changed.emit();
}

void WindowRegistry::
    on_stacking_order_changed(
        const std::vector<WindowId>
            &stacking_order)
{
    if (!apply_stacking_order(
            stacking_order))
    {
        return;
    }

    rebuild_applications();
    m_signal_changed.emit();
}

void WindowRegistry::on_connection_changed(
    bool connected)
{
    if (m_connected == connected)
        return;

    if (connected)
    {
        m_connected = true;
        load_snapshot();
    }
    else
    {
        clear();
    }

    m_signal_connection_changed.emit(
        connected);
}

void WindowRegistry::on_snapshot_changed()
{
    load_snapshot();
}

std::string
WindowRegistry::normalize_desktop_file_name(
    const std::string
        &desktop_file_name)
{
    const auto first =
        std::find_if_not(
            desktop_file_name.begin(),
            desktop_file_name.end(),
            [](unsigned char character)
            {
                return std::isspace(
                    character);
            });

    const auto last =
        std::find_if_not(
            desktop_file_name.rbegin(),
            desktop_file_name.rend(),
            [](unsigned char character)
            {
                return std::isspace(
                    character);
            })
            .base();

    if (first >= last)
        return {};

    std::string normalized_name(
        first,
        last);

    const auto separator =
        normalized_name.find_last_of(
            "/\\");

    if (separator != std::string::npos)
    {
        normalized_name.erase(
            0,
            separator + 1);
    }

    if (!has_desktop_suffix(
            normalized_name))
    {
        normalized_name += ".desktop";
    }

    std::transform(
        normalized_name.begin(),
        normalized_name.end(),
        normalized_name.begin(),
        [](unsigned char character)
        {
            if (std::isspace(character) ||
                character == '_')
            {
                return '-';
            }

            return static_cast<char>(
                std::tolower(character));
        });

    return normalized_name;
}
