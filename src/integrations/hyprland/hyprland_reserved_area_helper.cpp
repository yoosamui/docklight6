// ------------------------------------------------------------
// Docklight 6.0
//
// Creates an invisible native layer-shell surface which lets Hyprland reserve
// workspace space for Docklight while the visible GTK dock uses XWayland.
// ------------------------------------------------------------

#include <gtkmm.h>
#include <gtk-layer-shell.h>

#include <sys/prctl.h>

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

struct Options
{
    Gdk::Rectangle geometry;
    std::string edge;
    int size = 0;
    bool valid = false;
};

Options parse_options(int argc, char *argv[])
{
    Options options;
    std::string geometry;
    for (int index = 1; index + 1 < argc; index += 2)
    {
        const std::string name = argv[index];
        const std::string value = argv[index + 1];
        if (name == "--geometry")
            geometry = value;
        else if (name == "--edge")
            options.edge = value;
        else if (name == "--size")
            options.size = std::atoi(value.c_str());
        else
            return options;
    }

    std::array<int, 4> values{};
    if (std::sscanf(
            geometry.c_str(),
            "%d,%d,%d,%d",
            &values[0],
            &values[1],
            &values[2],
            &values[3]) != 4 ||
        values[2] <= 0 ||
        values[3] <= 0 ||
        options.size <= 0 ||
        (options.edge != "top" &&
         options.edge != "bottom" &&
         options.edge != "left" &&
         options.edge != "right"))
    {
        return options;
    }

    options.geometry.set_x(values[0]);
    options.geometry.set_y(values[1]);
    options.geometry.set_width(values[2]);
    options.geometry.set_height(values[3]);
    options.valid = true;
    return options;
}

Glib::RefPtr<Gdk::Monitor> find_monitor(
    const Gdk::Rectangle &geometry)
{
    const auto display = Gdk::Display::get_default();
    if (!display)
        return {};

    Glib::RefPtr<Gdk::Monitor> closest;
    long long closest_distance = 0;
    for (int index = 0;
         index < display->get_n_monitors();
         ++index)
    {
        const auto monitor = display->get_monitor(index);
        if (!monitor)
            continue;
        Gdk::Rectangle candidate;
        monitor->get_geometry(candidate);
        if (candidate.get_x() == geometry.get_x() &&
            candidate.get_y() == geometry.get_y() &&
            candidate.get_width() == geometry.get_width() &&
            candidate.get_height() == geometry.get_height())
            return monitor;

        const auto delta_x = static_cast<long long>(candidate.get_x()) -
            geometry.get_x();
        const auto delta_y = static_cast<long long>(candidate.get_y()) -
            geometry.get_y();
        const auto distance = delta_x * delta_x + delta_y * delta_y;
        if (!closest || distance < closest_distance)
        {
            closest = monitor;
            closest_distance = distance;
        }
    }
    return closest;
}

}

int main(int argc, char *argv[])
{
    const auto options = parse_options(argc, argv);
    if (!options.valid)
        return 2;

    prctl(PR_SET_PDEATHSIG, SIGTERM);
    g_setenv("GDK_BACKEND", "wayland", TRUE);
    argc = 1;
    argv[1] = nullptr;
    Gtk::Main runtime(argc, argv);

    if (!gtk_layer_is_supported())
        return 1;
    const auto monitor = find_monitor(options.geometry);
    if (!monitor)
        return 1;

    Gtk::Window window;
    window.set_decorated(false);
    window.set_accept_focus(false);
    window.set_focus_on_map(false);
    window.set_app_paintable(true);
    window.set_opacity(0.0);
    window.set_size_request(1, 1);

    auto *gtk_window = GTK_WINDOW(window.gobj());
    gtk_layer_init_for_window(gtk_window);
    gtk_layer_set_namespace(
        gtk_window,
        "docklight6-reservation");
    gtk_layer_set_keyboard_mode(
        gtk_window,
        GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_layer(
        gtk_window,
        GTK_LAYER_SHELL_LAYER_BOTTOM);
    gtk_layer_set_monitor(
        gtk_window,
        monitor->gobj());

    const bool horizontal =
        options.edge == "top" || options.edge == "bottom";
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_TOP,
        options.edge == "top" || !horizontal);
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        options.edge == "bottom" || !horizontal);
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        options.edge == "left" || horizontal);
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        options.edge == "right" || horizontal);
    gtk_layer_set_exclusive_zone(
        gtk_window,
        options.size);

    window.show();
    Gtk::Main::run(window);
    return 0;
}
