#include <gtkmm.h>

#include "dock_configuration_manager.h"
#include "dock_window.h"
#include "config.h"

#include <iostream>

int main(int argc, char *argv[])
{
    auto app = Gtk::Application::create(
        argc,
        argv,
        "org.docklight6");

    auto css = Gtk::CssProvider::create();

    try
    {
        css->load_from_path(
            std::string(SOURCE_DIR) + "/style.css");

        Gtk::StyleContext::add_provider_for_screen(
            Gdk::Screen::get_default(),
            css,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        std::cout << "CSS loaded successfully." << std::endl;
    }
    catch (const Glib::Error &ex)
    {
        std::cerr
            << "Failed to load CSS: "
            << ex.what()
            << std::endl;
    }

    DockConfigurationManager configuration;

    DockWindow window(
        configuration.current());

    configuration.signal_changed().connect(
        sigc::mem_fun(
            window,
            &DockWindow::apply_configuration));

    configuration.start_monitoring();

    return app->run(window);
}
