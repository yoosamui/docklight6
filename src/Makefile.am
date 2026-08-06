bin_PROGRAMS = docklight6

check_PROGRAMS = \
    dock_application_controller_test \
    dock_autohide_layout_test \
    dock_configuration_manager_test \
    dock_icon_renderer_test \
    launcher_manager_test \
    window_registry_test \
    kwin_window_backend_test \
    kwin_protocol_codec_test \
    kwin_integration_service_test

TESTS = \
    $(check_PROGRAMS) \
    tests/kwin_minimize_effect_test.js \
    tests/kwin_script_test.js \
    tests/plasma_geometry_bridge_test.js

AM_CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic

# $(abs_top_srcdir) expands to the absolute 
# path of the top-level source directory, regardless of where you build.
AM_CPPFLAGS = \
  -DSOURCE_DIR=\"$(abs_top_srcdir)/src\"

BUILT_SOURCES = \
    zkde-screencast-unstable-v1-client-protocol.h \
    zkde-screencast-unstable-v1-protocol.c

CLEANFILES = $(BUILT_SOURCES)

zkde-screencast-unstable-v1-client-protocol.h: $(top_srcdir)/protocols/zkde-screencast-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@

zkde-screencast-unstable-v1-protocol.c: $(top_srcdir)/protocols/zkde-screencast-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@

docklight6-dock_window_stream_provider.$(OBJEXT): zkde-screencast-unstable-v1-client-protocol.h

docklight6_SOURCES = \
    main.cpp \
    managed_window.h \
    running_application.h \
    window_icon_geometry.h \
    window_backend.h \
    window_backend.cpp \
    window_registry.h \
    window_registry.cpp \
    window_system_controller.h \
    window_system_controller.cpp \
    kwin_integration_protocol.h \
    kwin_protocol_codec.h \
    kwin_protocol_codec.cpp \
    kwin_integration_service.h \
    kwin_integration_service.cpp \
    kwin_script_manager.h \
    kwin_script_manager.cpp \
    plasma_geometry_bridge_manager.h \
    plasma_geometry_bridge_manager.cpp \
    kwin_window_command.h \
    kwin_window_backend.h \
    kwin_window_backend.cpp \
    dock_application_controller.h \
    dock_application_controller.cpp \
    config/dock_configuration.h \
    config/dock_configuration_manager.h \
    config/dock_configuration_manager.cpp \
    dock_monitor_manager.h \
    dock_monitor_manager.cpp \
    dock_intellihide_policy.h \
    dock_intellihide_policy.cpp \
    dock_media_playback_monitor.h \
    dock_media_playback_monitor.cpp \
    dock_window_thumbnail_provider.h \
    dock_window_thumbnail_provider.cpp \
    dock_window_stream_provider.h \
    dock_window_stream_provider.cpp \
    zkde-screencast-unstable-v1-client-protocol.h \
    zkde-screencast-unstable-v1-protocol.c \
    dock_preview_window.h \
    dock_preview_window.cpp \
    dock_reveal_window.h \
    dock_reveal_window.cpp \
    dock_autohide_controller.h \
    dock_autohide_controller.cpp \
    dock_window_controller.h \
    dock_window_controller.cpp \
    dock_window.cpp \
    dock_home_item.h \
    dock_home_item.cpp \
    dock_settings_dialog.h \
    dock_settings_dialog.cpp \
    dock_about_dialog.h \
    dock_about_dialog.cpp \
    dock_icon_renderer.h \
    dock_icon_renderer.cpp \
    config/dock_settings.h \
    config/dock_settings.cpp \
    launcher_manager.cpp \
    dock_layout_geometry.cpp \
    dock_layout_metrics.cpp \
    dock_layout_engine.cpp \
    dock_tooltip_window.cpp \
    dock_item.cpp

docklight6_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    -DDOCKLIGHT_DATA_DIR=\"$(pkgdatadir)\" \
    -DLOCALEDIR=\"$(localedir)\" \
    $(GTKMM_CFLAGS) \
    $(GTK_LAYER_SHELL_CFLAGS) \
    $(PIPEWIRE_CFLAGS) \
    $(WAYLAND_CFLAGS)

docklight6_LDADD = \
    $(GTKMM_LIBS) \
    $(GTK_LAYER_SHELL_LIBS) \
    $(PIPEWIRE_LIBS) \
    $(WAYLAND_LIBS)

dock_application_controller_test_SOURCES = \
    tests/dock_application_controller_test.cpp \
    tests/fake_window_backend.h \
    tests/fake_window_backend.cpp \
    managed_window.h \
    running_application.h \
    window_icon_geometry.h \
    window_backend.h \
    window_backend.cpp \
    window_registry.h \
    window_registry.cpp \
    dock_application_controller.h \
    dock_application_controller.cpp

dock_application_controller_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_application_controller_test_LDADD = \
    $(GTKMM_LIBS)

dock_autohide_layout_test_SOURCES = \
    tests/dock_autohide_layout_test.cpp \
    dock_layout_types.h \
    dock_window_geometry.h \
    managed_window.h \
    dock_intellihide_policy.h \
    dock_intellihide_policy.cpp \
    dock_layout_engine.h \
    dock_layout_engine.cpp

dock_autohide_layout_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_autohide_layout_test_LDADD = \
    $(GTKMM_LIBS)

dock_configuration_manager_test_SOURCES = \
    tests/dock_configuration_manager_test.cpp \
    config/dock_configuration.h \
    config/dock_configuration_manager.h \
    config/dock_configuration_manager.cpp \
    dock_layout_types.h \
    config/dock_settings.h \
    config/dock_settings.cpp

dock_configuration_manager_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_configuration_manager_test_LDADD = \
    $(GTKMM_LIBS)

dock_icon_renderer_test_SOURCES = \
    tests/dock_icon_renderer_test.cpp \
    dock_icon_renderer.h \
    dock_icon_renderer.cpp \
    dock_layout_metrics.h \
    dock_layout_metrics.cpp \
    dock_layout_types.h

dock_icon_renderer_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_icon_renderer_test_LDADD = \
    $(GTKMM_LIBS)

launcher_manager_test_SOURCES = \
    tests/launcher_manager_test.cpp \
    launcher_manager.h \
    launcher_manager.cpp

launcher_manager_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

launcher_manager_test_LDADD = \
    $(GTKMM_LIBS)

window_registry_test_SOURCES = \
    tests/window_registry_test.cpp \
    tests/fake_window_backend.h \
    tests/fake_window_backend.cpp \
    managed_window.h \
    running_application.h \
    window_icon_geometry.h \
    window_backend.h \
    window_backend.cpp \
    window_registry.h \
    window_registry.cpp

window_registry_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

window_registry_test_LDADD = \
    $(GTKMM_LIBS)

kwin_window_backend_test_SOURCES = \
    tests/kwin_window_backend_test.cpp \
    managed_window.h \
    running_application.h \
    window_icon_geometry.h \
    window_backend.h \
    window_backend.cpp \
    window_registry.h \
    window_registry.cpp \
    kwin_integration_protocol.h \
    kwin_window_command.h \
    kwin_window_backend.h \
    kwin_window_backend.cpp

kwin_window_backend_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

kwin_window_backend_test_LDADD = \
    $(GTKMM_LIBS)

kwin_protocol_codec_test_SOURCES = \
    tests/kwin_protocol_codec_test.cpp \
    managed_window.h \
    kwin_protocol_codec.h \
    kwin_protocol_codec.cpp

kwin_protocol_codec_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

kwin_protocol_codec_test_LDADD = \
    $(GTKMM_LIBS)

kwin_integration_service_test_SOURCES = \
    tests/kwin_integration_service_test.cpp \
    managed_window.h \
    window_icon_geometry.h \
    window_backend.h \
    window_backend.cpp \
    kwin_integration_protocol.h \
    kwin_protocol_codec.h \
    kwin_protocol_codec.cpp \
    kwin_integration_service.h \
    kwin_integration_service.cpp \
    kwin_window_command.h \
    kwin_window_backend.h \
    kwin_window_backend.cpp

kwin_integration_service_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

kwin_integration_service_test_LDADD = \
    $(GTKMM_LIBS)

EXTRA_DIST = \
    tests/kwin_minimize_effect_test.js \
    tests/kwin_script_test.js \
    tests/plasma_geometry_bridge_test.js
