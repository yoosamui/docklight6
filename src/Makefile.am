bin_PROGRAMS = docklight6

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic

# $(abs_top_srcdir) expands to the absolute 
# path of the top-level source directory, regardless of where you build.
AM_CPPFLAGS = \
  -DSOURCE_DIR=\"$(abs_top_srcdir)/src\"

docklight6_SOURCES = \
    main.cpp \
    dock_configuration.h \
    dock_configuration_manager.h \
    dock_configuration_manager.cpp \
    dock_monitor_manager.h \
    dock_monitor_manager.cpp \
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
