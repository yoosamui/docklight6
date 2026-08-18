// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_system_controller.cpp
//
// Implementation overview:
// Selects a shell/D-Bus backend for KDE or GNOME on Wayland and a
// WM-specific EWMH backend on X11.
//
// Important implementation decisions:
// - Each recognized X11 window manager has its own concrete backend class.
// - Unknown EWMH-compatible X11 managers use an explicit fallback backend.
// - Unsupported sessions keep the dock usable without window control.
// - Owned components are created and destroyed in dependency order.
// - Companion integrations are ensured only after backend connection.
// - Availability reflects a live registry rather than environment alone.
//
// ------------------------------------------------------------

#include "window_system_controller.h"

#include "docklight_log.h"
#include "integrations/gnome/gnome_wayland_window_backend.h"
#include "integrations/kwin/kwin_integration_service.h"
#include "integrations/kwin/kwin_script_manager.h"
#include "integrations/kwin/kwin_window_backend.h"
#include "integrations/plasma/plasma_geometry_bridge_manager.h"
#include "integrations/x11/ewmh_fallback_window_backend.h"
#include "integrations/x11/kwin_x11_window_backend.h"
#include "integrations/x11/marco_window_backend.h"
#include "integrations/x11/muffin_window_backend.h"
#include "integrations/x11/mutter_window_backend.h"
#include "integrations/x11/openbox_window_backend.h"
#include "integrations/x11/xfwm4_window_backend.h"
#include "integrations/x11/x11_backend_selection.h"
#include "windowing/window_backend.h"
#include "windowing/window_registry.h"

#include <glib.h>
#include <X11/Xlib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{

std::string environment_value(
    const char *name)
{
    const auto value =
        std::getenv(name);

    return value
               ? value
               : "";
}

std::string lowercase(
    std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    return value;
}

bool identifies_kde(
    const std::string &desktop)
{
    const auto normalized =
        lowercase(desktop);

    return normalized.find("kde") !=
               std::string::npos ||
           normalized.find("plasma") !=
               std::string::npos;
}

bool identifies_gnome(
    const std::string &desktop)
{
    return lowercase(desktop).find("gnome") !=
           std::string::npos;
}

std::string detected_desktop()
{
    auto desktop = environment_value(
        "XDG_CURRENT_DESKTOP");

    if (desktop.empty())
    {
        desktop = environment_value(
            "XDG_SESSION_DESKTOP");
    }

    if (desktop.empty())
    {
        desktop = environment_value(
            "DESKTOP_SESSION");
    }

    return desktop.empty()
               ? "unknown"
               : desktop;
}

std::string detected_window_manager(
    const std::string &desktop)
{
    const auto session_type =
        lowercase(
            environment_value(
                "XDG_SESSION_TYPE"));

    // GNOME's window manager is Mutter. Avoid initializing libwnck merely to
    // read GNOME Shell's EWMH display name: libwnck subscribes the shared X11
    // connection to property changes from every client window, which keeps an
    // otherwise idle native-X11 dock awake.
    if (identifies_gnome(desktop))
        return "Mutter";

    if (session_type != "wayland")
    {
        auto *handle =
            wnck_handle_new(
                WNCK_CLIENT_TYPE_PAGER);
        auto *screen =
            wnck_handle_get_default_screen(
                handle);
        if (screen)
            wnck_screen_force_update(screen);

        const char *manager =
            screen
                ? wnck_screen_get_window_manager_name(
                      screen)
                : nullptr;
        const std::string result =
            manager
                ? manager
                : "";
        g_clear_object(&handle);

        if (!result.empty())
            return result;
    }

    if (identifies_kde(desktop) ||
        lowercase(
            environment_value(
                "KDE_FULL_SESSION")) ==
            "true")
    {
        return "KWin";
    }

    const auto normalized =
        lowercase(desktop);

    if (normalized.find("cinnamon") !=
        std::string::npos)
    {
        return "Muffin";
    }

    if (normalized.find("xfce") !=
        std::string::npos)
    {
        return "Xfwm4";
    }

    if (normalized.find("mate") !=
        std::string::npos)
    {
        return "Marco";
    }

    if (normalized.find("lxde") !=
            std::string::npos ||
        normalized.find("lxqt") !=
            std::string::npos)
    {
        return "Openbox";
    }

    return "unknown";
}

bool x11_compositor_is_running()
{
    auto *display = XOpenDisplay(nullptr);
    if (!display)
        return false;

    const auto selection_name =
        "_NET_WM_CM_S" +
        std::to_string(DefaultScreen(display));
    const auto selection = XInternAtom(
        display,
        selection_name.c_str(),
        True);
    const bool running =
        selection != None &&
        XGetSelectionOwner(display, selection) != None;

    XCloseDisplay(display);
    return running;
}

std::string detected_compositor(
    bool x11_compositor_running)
{
    const auto session_type =
        lowercase(
            environment_value(
                "XDG_SESSION_TYPE"));

    if (session_type == "wayland" ||
        !environment_value(
             "WAYLAND_DISPLAY")
             .empty())
    {
        return "Wayland";
    }

    if (session_type == "x11" ||
        !environment_value(
             "DISPLAY")
             .empty())
    {
        return x11_compositor_running
                   ? "X11"
                   : "none";
    }

    return "unknown";
}

void log_detected_environment(
    const std::string &desktop,
    const std::string &window_manager,
    const std::string &compositor)
{
    DocklightLog::startup(
        "detected Desktop: %s",
        desktop.c_str());
    DocklightLog::startup(
        "detected WM: %s",
        window_manager.c_str());
    DocklightLog::startup(
        "detected compositor: %s",
        compositor.c_str());
}

}

WindowSystemController::
    WindowSystemController() = default;

WindowSystemController::
    ~WindowSystemController()
{
    stop();
}

void WindowSystemController::start()
{
    if (m_started)
        return;

    m_started = true;

    const auto desktop =
        detected_desktop();
    const auto window_manager =
        detected_window_manager(desktop);

    const bool x11 = is_x11_session();
    const bool x11_compositor_running =
        x11 && x11_compositor_is_running();
    const auto compositor =
        detected_compositor(
            x11_compositor_running);

    m_details.desktop = desktop;
    m_details.window_manager = window_manager;
    m_details.compositor = compositor;
    m_details.backend = "none";

    log_detected_environment(
        desktop,
        window_manager,
        compositor);

    const bool kde_wayland =
        is_kde_wayland_session();
    const bool gnome_shell =
        identifies_gnome(desktop);
    const bool uses_shell_protocol =
        kde_wayland || gnome_shell;

    if (!uses_shell_protocol && !x11)
    {
        g_message(
            "Window integration is not enabled for this desktop session");
        return;
    }

    if (gnome_shell)
    {
        m_details.backend = "GNOME Shell";
        m_backend =
            std::make_unique<
                GnomeWaylandWindowBackend>(
                    !x11);

        DocklightLog::startup(
            "selected backend: GNOME Shell");
    }
    else if (x11)
    {
        const auto backend_kind =
            select_x11_backend_kind(
                window_manager,
                desktop);

        m_details.backend =
            x11_backend_kind_name(
                backend_kind);

        switch (backend_kind)
        {
        case X11BackendKind::kwin:
            m_backend =
                std::make_unique<
                    KWinX11WindowBackend>();
            break;
        case X11BackendKind::marco:
            m_backend =
                std::make_unique<
                    MarcoWindowBackend>();
            break;
        case X11BackendKind::muffin:
            m_backend =
                std::make_unique<
                    MuffinWindowBackend>();
            break;
        case X11BackendKind::mutter:
            m_backend =
                std::make_unique<
                    MutterWindowBackend>();
            break;
        case X11BackendKind::openbox:
            m_backend =
                std::make_unique<
                    OpenboxWindowBackend>();
            break;
        case X11BackendKind::xfwm4:
            m_backend =
                std::make_unique<
                    Xfwm4WindowBackend>();
            break;
        case X11BackendKind::ewmh_fallback:
            m_backend =
                std::make_unique<
                    EwmhFallbackWindowBackend>();
            break;
        }

        DocklightLog::startup(
            "selected backend: %s",
            x11_backend_kind_name(
                backend_kind));

        if (m_backend
                ->capabilities()
                .thumbnails_require_compositor &&
            !x11_compositor_running)
        {
            m_window_previews_available = false;
            g_warning(
                "Window previews are disabled: Openbox requires an "
                "X11 compositor such as compton or picom");
        }
    }
    else
    {
        m_details.backend = "KWin";
        m_backend =
            std::make_unique<KWinWindowBackend>();

        DocklightLog::startup(
            "selected backend: KWin");
    }

    m_registry =
        std::make_unique<
            WindowRegistry>(
            *m_backend);

    if (uses_shell_protocol)
    {
        m_kwin_service =
            std::make_unique<
                KWinIntegrationService>(
                static_cast<KWinWindowBackend &>(
                    *m_backend));
    }

    m_connection_changed =
        m_registry
            ->signal_connection_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowSystemController::
                        on_connection_changed));

    m_registry->start();

    if (x11 && !uses_shell_protocol)
    {
        if (m_registry->connected())
        {
            g_message(
                "%s window integration is ready",
                m_backend->name().c_str());
        }
        else
        {
            g_warning(
                "No EWMH-compatible X11 window manager was detected");
        }
        return;
    }

    if (!m_kwin_service->start())
    {
        g_warning(
            "%s window integration could not be started",
            m_backend->name().c_str());

        stop();
        return;
    }

    g_message(
        "%s window integration is ready for the shell extension",
        m_backend->name().c_str());

    if (kde_wayland)
    {
        PlasmaGeometryBridgeManager
            bridge_manager;
        bridge_manager.ensure();

        KWinScriptManager script_manager;
        script_manager.restart();
    }
}

void WindowSystemController::stop()
{
    if (!m_started)
        return;

    if (m_kwin_service)
        m_kwin_service->stop();

    if (m_registry)
        m_registry->stop();

    m_connection_changed.disconnect();

    m_kwin_service.reset();
    m_registry.reset();
    m_backend.reset();

    m_started = false;
    m_window_previews_available = true;
}

bool WindowSystemController::available() const
{
    return m_registry &&
           m_registry->connected();
}

bool WindowSystemController::
    window_previews_available() const
{
    return m_window_previews_available;
}

const WindowSystemDetails &
WindowSystemController::details() const
{
    return m_details;
}

WindowRegistry *
WindowSystemController::registry()
{
    return m_registry.get();
}

const WindowRegistry *
WindowSystemController::registry() const
{
    return m_registry.get();
}

bool WindowSystemController::
    is_kde_wayland_session()
{
    const auto session_type =
        lowercase(
            environment_value(
                "XDG_SESSION_TYPE"));

    const bool wayland =
        session_type == "wayland" ||
        !environment_value(
             "WAYLAND_DISPLAY")
             .empty();

    const bool kde =
        identifies_kde(
            environment_value(
                "XDG_CURRENT_DESKTOP")) ||
        identifies_kde(
            environment_value(
                "XDG_SESSION_DESKTOP")) ||
        lowercase(
            environment_value(
                "KDE_FULL_SESSION")) ==
            "true";

    return wayland && kde;
}

bool WindowSystemController::
    is_gnome_wayland_session()
{
    const auto session_type =
        lowercase(
            environment_value(
                "XDG_SESSION_TYPE"));

    const bool wayland =
        session_type == "wayland" ||
        !environment_value(
             "WAYLAND_DISPLAY")
             .empty();

    const bool gnome =
        identifies_gnome(
            environment_value(
                "XDG_CURRENT_DESKTOP")) ||
        identifies_gnome(
            environment_value(
                "XDG_SESSION_DESKTOP"));

    return wayland && gnome;
}

bool WindowSystemController::is_x11_session()
{
    const auto session_type =
        lowercase(
            environment_value(
                "XDG_SESSION_TYPE"));

    // An explicit Wayland session wins even when DISPLAY is also exported
    // for XWayland clients.
    if (session_type == "wayland")
        return false;

    return session_type == "x11" ||
           (!environment_value("DISPLAY").empty() &&
            environment_value("WAYLAND_DISPLAY").empty());
}

void WindowSystemController::
    on_connection_changed(
        bool connected)
{
    g_message(
        "Window integration %s",
        connected
            ? "connected"
            : "disconnected");
}
