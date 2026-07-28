bin_PROGRAMS = docklight6

check_PROGRAMS = window_registry_test

TESTS = $(check_PROGRAMS)

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic

# $(abs_top_srcdir) expands to the absolute 
# path of the top-level source directory, regardless of where you build.
AM_CPPFLAGS = \
  -DSOURCE_DIR=\"$(abs_top_srcdir)/src\"

docklight6_SOURCES = \
    main.cpp \
    managed_window.h \
    running_application.h \
    window_backend.h \
    window_backend.cpp \
    window_registry.h \
    window_registry.cpp \
    dock_configuration.h \
    dock_configuration_manager.h \
    dock_configuration_manager.cpp \
    dock_monitor_manager.h \
    dock_monitor_manager.cpp \
    dock_window_controller.h \
    dock_window_controller.cpp \
    dock_window.cpp \
    dock_settings.cpp \
    launcher_manager.cpp \
    dock_layout_geometry.cpp \
    dock_layout_metrics.cpp \
    dock_layout_engine.cpp \
    dock_tooltip_window.cpp \
    dock_item.cpp

docklight6_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    $(GTKMM_CFLAGS) \
    $(GTK_LAYER_SHELL_CFLAGS)

docklight6_LDADD = \
    $(GTKMM_LIBS) \
    $(GTK_LAYER_SHELL_LIBS)

window_registry_test_SOURCES = \
    tests/window_registry_test.cpp \
    tests/fake_window_backend.h \
    tests/fake_window_backend.cpp \
    managed_window.h \
    running_application.h \
    window_backend.h \
    window_backend.cpp \
    window_registry.h \
    window_registry.cpp

window_registry_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

window_registry_test_LDADD = \
    $(GTKMM_LIBS)
