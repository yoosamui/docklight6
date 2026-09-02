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

#include <gtkmm.h>

class DockSessionItem : public Gtk::Frame
{
  public:
    DockSessionItem();

    sigc::signal<void> &signal_remove_requested();

  private:
    Gtk::Grid m_layout;
    Gtk::Image m_app_icon;

    Gtk::Label m_app_title_label;
    Gtk::Entry m_app_title;
    Gtk::Box m_actions;
    Gtk::Button m_copy_button;
    Gtk::Button m_launch_button;
    Gtk::Button m_remove_button;

    Gtk::Label m_parameters_label;
    Gtk::Entry m_parameters;
    Gtk::Label m_workspace_label;
    Gtk::Entry m_workspace;
    Gtk::Label m_dimensions_label;
    Gtk::Entry m_dimensions;
    Gtk::Label m_position_label;
    Gtk::Entry m_position;

    sigc::signal<void> m_remove_requested;
};
