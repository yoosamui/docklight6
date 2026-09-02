// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.h
//
// Purpose:
// Declares the UI-only editor for one item in a Session.
//
// Design notes:
// This widget deliberately owns presentation state only. Application
// discovery, launching, geometry handling, and persistence belong to later
// Sessions work.
//
// ------------------------------------------------------------

#pragma once

#include "windowing/managed_window.h"

#include <gtkmm.h>

#include <functional>
#include <optional>

class DockSessionItem : public Gtk::Frame
{
  public:
    using CaptureWindowProvider =
        std::function<std::optional<ManagedWindow>()>;

    explicit DockSessionItem(
        CaptureWindowProvider capture_window = {});

    sigc::signal<void> &signal_remove_requested();

  private:
    void capture();

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
    sigc::signal<void> m_remove_requested;
};
