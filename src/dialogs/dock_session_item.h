// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.h
//
// Purpose:
// Declares the editor and transient launch controller for one Session item.
//
// Design notes:
// Values remain transient; application launch and placement are routed through
// GIO and WindowRegistry rather than platform APIs.
//
// ------------------------------------------------------------

#pragma once

#include "windowing/window_backend.h"

#include <gtkmm.h>
#include <sigc++/connection.h>

#include <functional>
#include <optional>
#include <set>
#include <string>

class WindowRegistry;

class DockSessionItem : public Gtk::Frame
{
  public:
    using CaptureWindowProvider =
        std::function<std::optional<ManagedWindow>()>;

    explicit DockSessionItem(
        CaptureWindowProvider capture_window = {},
        WindowRegistry *window_registry = nullptr);
    ~DockSessionItem() override;

    sigc::signal<void> &signal_remove_requested();

  private:
    void capture();
    void launcher();
    void stop_launch_tracking();
    void on_window_registry_changed();
    bool on_launch_timeout();
    const ManagedWindow *launched_window() const;
    WindowPlacement placement() const;

    Gtk::Grid m_layout;
    Gtk::Image m_app_icon;

    Gtk::Label m_app_title_label;
    Gtk::ComboBoxText m_app_title;
    Gtk::Box m_actions;
    Gtk::Button m_paste_button;
    Gtk::Button m_launch_button;
    Gtk::Button m_remove_button;

    Gtk::Label m_desktop_file_label;
    Gtk::Entry m_desktop_file;
    Gtk::Label m_app_name_label;
    Gtk::Entry m_app_name;
    Gtk::Label m_parameters_label;
    Gtk::Entry m_parameters;
    Gtk::Label m_workspace_label;
    Gtk::Entry m_workspace;
    Gtk::Label m_dimensions_label;
    Gtk::Entry m_dimensions;
    Gtk::Label m_position_label;
    Gtk::Entry m_position;

    CaptureWindowProvider m_capture_window;
    WindowRegistry *m_window_registry = nullptr;
    std::set<WindowId> m_windows_before_launch;
    std::string m_launched_application_id;
    std::string m_launched_title;
    WindowPlacement m_launch_placement;
    sigc::connection m_launch_window_changed;
    sigc::connection m_launch_timeout;
    sigc::signal<void> m_remove_requested;
};
