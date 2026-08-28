bin_PROGRAMS = docklight6

check_PROGRAMS = \
    dock_application_controller_test \
    dock_autohide_layout_test \
    dock_configuration_manager_test \
    dock_icon_renderer_test \
    dock_process_application_test \
    gnome_wayland_window_backend_test \
    launcher_manager_test \
    presentation_selector_test \
    window_registry_test \
    kwin_window_backend_test \
    kwin_protocol_codec_test \
    kwin_integration_service_test \
    x11_backend_selection_test

TESTS = \
    $(check_PROGRAMS) \
    tests/gnome_placement_test.js \
    tests/kwin_minimize_effect_test.js \
    tests/kwin_script_test.js \
    tests/kwin_surface_identity_test.js \
    tests/plasma_geometry_bridge_test.js \
    tests/marco_restore_test.js \
    tests/openbox_group_minimize_test.js \
    tests/xfwm4_restore_test.js

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

preview/docklight6-dock_window_stream_provider.$(OBJEXT): zkde-screencast-unstable-v1-client-protocol.h

docklight6_SOURCES = \
    main.cpp \
    docklight_log.h \
    docklight_log.cpp \
    presentation/docklight_surface_identity.h \
    presentation/presentation_selector.h \
    presentation/presentation_selector.cpp \
    windowing/managed_window.h \
    windowing/running_application.h \
    windowing/window_icon_geometry.h \
    windowing/window_backend.h \
    windowing/window_backend.cpp \
    windowing/window_registry.h \
    windowing/window_registry.cpp \
    integrations/desktop_session_identity.h \
    integrations/window_system_controller.h \
    integrations/window_system_controller.cpp \
    integrations/gnome/gnome_wayland_window_backend.h \
    integrations/gnome/gnome_wayland_window_backend.cpp \
    integrations/gnome/gnome_x11_window_backend.h \
    integrations/gnome/gnome_x11_window_backend.cpp \
    integrations/x11/ewmh_window_backend.h \
    integrations/x11/ewmh_window_backend.cpp \
    integrations/x11/ewmh_fallback_window_backend.h \
    integrations/x11/ewmh_fallback_window_backend.cpp \
    integrations/x11/kwin_x11_window_backend.h \
    integrations/x11/kwin_x11_window_backend.cpp \
    integrations/x11/marco_window_backend.h \
    integrations/x11/marco_window_backend.cpp \
    integrations/x11/muffin_window_backend.h \
    integrations/x11/muffin_window_backend.cpp \
    integrations/x11/mutter_window_backend.h \
    integrations/x11/mutter_window_backend.cpp \
    integrations/x11/openbox_window_backend.h \
    integrations/x11/openbox_window_backend.cpp \
    integrations/x11/xfwm4_window_backend.h \
    integrations/x11/xfwm4_window_backend.cpp \
    integrations/x11/x11_backend_selection.h \
    integrations/x11/x11_backend_selection.cpp \
    integrations/kwin/kwin_integration_protocol.h \
    integrations/kwin/kwin_protocol_codec.h \
    integrations/kwin/kwin_protocol_codec.cpp \
    integrations/kwin/kwin_integration_service.h \
    integrations/kwin/kwin_integration_service.cpp \
    integrations/kwin/kwin_script_manager.h \
    integrations/kwin/kwin_script_manager.cpp \
    integrations/plasma/plasma_geometry_bridge_manager.h \
    integrations/plasma/plasma_geometry_bridge_manager.cpp \
    integrations/kwin/kwin_window_command.h \
    integrations/kwin/kwin_window_backend.h \
    integrations/kwin/kwin_window_backend.cpp \
    application/dock_runtime_info.h \
    application/dock_process_application.h \
    application/dock_process_application.cpp \
    application/dock_application_controller.h \
    application/dock_application_controller.cpp \
    config/dock_configuration.h \
    config/dock_configuration_manager.h \
    config/dock_configuration_manager.cpp \
    monitors/dock_monitor_manager.h \
    monitors/dock_monitor_manager.cpp \
    autohide/dock_intellihide_policy.h \
    autohide/dock_intellihide_policy.cpp \
    media/dock_media_playback_monitor.h \
    media/dock_media_playback_monitor.cpp \
    preview/dock_window_thumbnail_provider.h \
    preview/dock_window_thumbnail_provider.cpp \
    preview/dock_window_stream_provider.h \
    preview/dock_window_stream_provider.cpp \
    zkde-screencast-unstable-v1-client-protocol.h \
    zkde-screencast-unstable-v1-protocol.c \
    preview/dock_preview_window.h \
    preview/dock_preview_window_internal.h \
    preview/dock_preview_window.cpp \
    preview/dock_preview_thumbnail_cache.cpp \
    preview/dock_preview_animation.cpp \
    preview/dock_preview_layout.cpp \
    autohide/dock_reveal_window.h \
    autohide/dock_reveal_window.cpp \
    autohide/dock_autohide_controller.h \
    autohide/dock_autohide_controller.cpp \
    dock/backends/dock_surface_backend.h \
    dock/backends/plasma_wayland_dock_surface_backend.h \
    dock/backends/plasma_wayland_dock_surface_backend.cpp \
    dock/backends/legacy_dock_surface_backend.h \
    dock/backends/legacy_dock_surface_backend.cpp \
    dock/dock_constants.h \
    dock/dock_window.h \
    dock/dock_window.cpp \
    dock/dock_window_dnd.cpp \
    dock/dock_window_surface.cpp \
    dock/dock_window_items.cpp \
    dock/dock_window_controller.h \
    dock/dock_window_controller.cpp \
    dock/tooltip_manager.h \
    dock/tooltip_manager.cpp \
    dock/preview_manager.h \
    dock/preview_manager.cpp \
    dock/layout_coordinator.h \
    dock/layout_coordinator.cpp \
    dock/dock_home_item.h \
    dock/dock_home_item.cpp \
    dock/dock_item.h \
    dock/dock_item.cpp \
    dock/dock_item_context_menu.cpp \
    dock/dock_item_effects.cpp \
    dock/dock_item_dnd.cpp \
    dock/dock_tooltip_window.h \
    dock/dock_tooltip_window.cpp \
    dialogs/dock_settings_dialog.h \
    dialogs/dock_settings_dialog.cpp \
    dialogs/dock_about_dialog.h \
    dialogs/dock_about_dialog.cpp \
    rendering/dock_icon_renderer.h \
    rendering/dock_icon_renderer.cpp \
    config/dock_settings.h \
    config/dock_settings.cpp \
    launchers/launcher_manager.h \
    launchers/launcher_manager.cpp \
    layout/dock_layout_types.h \
    layout/dock_window_geometry.h \
    layout/dock_layout_geometry.h \
    layout/dock_layout_geometry.cpp \
    layout/dock_layout_metrics.h \
    layout/dock_layout_metrics.cpp \
    layout/dock_layout_engine.h \
    layout/dock_layout_engine.cpp

docklight6_CPPFLAGS = \
    $(AM_CPPFLAGS) \
    -DDOCKLIGHT_DATA_DIR=\"$(pkgdatadir)\" \
    -DLOCALEDIR=\"$(localedir)\" \
    $(GTKMM_CFLAGS) \
    $(GTK_LAYER_SHELL_CFLAGS) \
    $(PIPEWIRE_CFLAGS) \
    $(WAYLAND_CFLAGS) \
    $(X11_BACKEND_CFLAGS)

docklight6_LDADD = \
    $(GTKMM_LIBS) \
    $(GTK_LAYER_SHELL_LIBS) \
    $(PIPEWIRE_LIBS) \
    $(WAYLAND_LIBS) \
    $(X11_BACKEND_LIBS)

dock_application_controller_test_SOURCES = \
    tests/dock_application_controller_test.cpp \
    tests/fake_window_backend.h \
    tests/fake_window_backend.cpp \
    windowing/managed_window.h \
    windowing/running_application.h \
    windowing/window_icon_geometry.h \
    windowing/window_backend.h \
    windowing/window_backend.cpp \
    windowing/window_registry.h \
    windowing/window_registry.cpp \
    application/dock_application_controller.h \
    application/dock_application_controller.cpp

dock_application_controller_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_application_controller_test_LDADD = \
    $(GTKMM_LIBS)

dock_autohide_layout_test_SOURCES = \
    tests/dock_autohide_layout_test.cpp \
    layout/dock_layout_types.h \
    layout/dock_window_geometry.h \
    windowing/managed_window.h \
    autohide/dock_intellihide_policy.h \
    autohide/dock_intellihide_policy.cpp \
    layout/dock_layout_engine.h \
    layout/dock_layout_engine.cpp

dock_autohide_layout_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_autohide_layout_test_LDADD = \
    $(GTKMM_LIBS)

dock_configuration_manager_test_SOURCES = \
    tests/dock_configuration_manager_test.cpp \
    config/dock_configuration.h \
    config/dock_configuration_manager.h \
    config/dock_configuration_manager.cpp \
    layout/dock_layout_types.h \
    config/dock_settings.h \
    config/dock_settings.cpp

dock_configuration_manager_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_configuration_manager_test_LDADD = \
    $(GTKMM_LIBS)

dock_icon_renderer_test_SOURCES = \
    tests/dock_icon_renderer_test.cpp \
    rendering/dock_icon_renderer.h \
    rendering/dock_icon_renderer.cpp \
    layout/dock_layout_metrics.h \
    layout/dock_layout_metrics.cpp \
    layout/dock_layout_types.h

dock_icon_renderer_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_icon_renderer_test_LDADD = \
    $(GTKMM_LIBS)

dock_process_application_test_SOURCES = \
    tests/dock_process_application_test.cpp \
    application/dock_process_application.h \
    application/dock_process_application.cpp

dock_process_application_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

dock_process_application_test_LDADD = \
    $(GTKMM_LIBS)

launcher_manager_test_SOURCES = \
    tests/launcher_manager_test.cpp \
    launchers/launcher_manager.h \
    launchers/launcher_manager.cpp

launcher_manager_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

launcher_manager_test_LDADD = \
    $(GTKMM_LIBS)

window_registry_test_SOURCES = \
    tests/window_registry_test.cpp \
    tests/fake_window_backend.h \
    tests/fake_window_backend.cpp \
    windowing/managed_window.h \
    windowing/running_application.h \
    windowing/window_icon_geometry.h \
    windowing/window_backend.h \
    windowing/window_backend.cpp \
    windowing/window_registry.h \
    windowing/window_registry.cpp

window_registry_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

window_registry_test_LDADD = \
    $(GTKMM_LIBS)

kwin_window_backend_test_SOURCES = \
    tests/kwin_window_backend_test.cpp \
    windowing/managed_window.h \
    windowing/running_application.h \
    windowing/window_icon_geometry.h \
    windowing/window_backend.h \
    windowing/window_backend.cpp \
    windowing/window_registry.h \
    windowing/window_registry.cpp \
    integrations/kwin/kwin_integration_protocol.h \
    integrations/kwin/kwin_window_command.h \
    integrations/kwin/kwin_window_backend.h \
    integrations/kwin/kwin_window_backend.cpp

kwin_window_backend_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

kwin_window_backend_test_LDADD = \
    $(GTKMM_LIBS)

gnome_wayland_window_backend_test_SOURCES = \
    tests/gnome_wayland_window_backend_test.cpp \
    windowing/managed_window.h \
    windowing/window_icon_geometry.h \
    windowing/window_backend.h \
    windowing/window_backend.cpp \
    integrations/kwin/kwin_integration_protocol.h \
    integrations/kwin/kwin_window_command.h \
    integrations/kwin/kwin_window_backend.h \
    integrations/kwin/kwin_window_backend.cpp \
    integrations/gnome/gnome_wayland_window_backend.h \
    integrations/gnome/gnome_wayland_window_backend.cpp

gnome_wayland_window_backend_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

gnome_wayland_window_backend_test_LDADD = \
    $(GTKMM_LIBS)

kwin_protocol_codec_test_SOURCES = \
    tests/kwin_protocol_codec_test.cpp \
    windowing/managed_window.h \
    integrations/kwin/kwin_protocol_codec.h \
    integrations/kwin/kwin_protocol_codec.cpp

kwin_protocol_codec_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

kwin_protocol_codec_test_LDADD = \
    $(GTKMM_LIBS)

kwin_integration_service_test_SOURCES = \
    tests/kwin_integration_service_test.cpp \
    windowing/managed_window.h \
    windowing/window_icon_geometry.h \
    windowing/window_backend.h \
    windowing/window_backend.cpp \
    integrations/kwin/kwin_integration_protocol.h \
    integrations/kwin/kwin_protocol_codec.h \
    integrations/kwin/kwin_protocol_codec.cpp \
    integrations/kwin/kwin_integration_service.h \
    integrations/kwin/kwin_integration_service.cpp \
    integrations/kwin/kwin_window_command.h \
    integrations/kwin/kwin_window_backend.h \
    integrations/kwin/kwin_window_backend.cpp

kwin_integration_service_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

kwin_integration_service_test_LDADD = \
    $(GTKMM_LIBS)

x11_backend_selection_test_SOURCES = \
    tests/x11_backend_selection_test.cpp \
    integrations/desktop_session_identity.h \
    integrations/x11/x11_backend_selection.h \
    integrations/x11/x11_backend_selection.cpp

presentation_selector_test_SOURCES = \
    tests/presentation_selector_test.cpp \
    presentation/presentation_selector.h \
    presentation/presentation_selector.cpp

presentation_selector_test_CPPFLAGS = \
    $(GTKMM_CFLAGS)

presentation_selector_test_LDADD = \
    $(GTKMM_LIBS)

EXTRA_DIST = \
    tests/gnome_placement_test.js \
    tests/kwin_minimize_effect_test.js \
    tests/kwin_script_test.js \
    tests/kwin_surface_identity_test.js \
    tests/plasma_geometry_bridge_test.js \
    tests/openbox_group_minimize_test.js \
    tests/xfwm4_restore_test.js
