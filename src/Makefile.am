bin_PROGRAMS = docklight-prototype

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic

# $(abs_top_srcdir) expands to the absolute 
# path of the top-level source directory, regardless of where you build.
AM_CPPFLAGS = \
  -DSOURCE_DIR=\"$(abs_top_srcdir)/src\"

docklight_prototype_SOURCES = \
    main.cpp \
    dock_window.cpp \
    dock_settings.cpp \
    launcher_manager.cpp \
    dock_layout_geometry.cpp \
    dock_layout_metrics.cpp \
    dock_layout_engine.cpp \
    dock_tooltip_window.cpp \
    dock_item.cpp

docklight_prototype_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    $(GTKMM_CFLAGS) \
    $(GTK_LAYER_SHELL_CFLAGS)

docklight_prototype_LDADD = \
    $(GTKMM_LIBS) \
    $(GTK_LAYER_SHELL_LIBS)
