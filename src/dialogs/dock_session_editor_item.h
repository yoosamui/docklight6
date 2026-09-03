// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_editor_item.h
//
// Purpose:
// Declares the editor and transient launch controller for one Session item.
//
// Design notes:
// The card stays a presentation widget. It exposes its field values and
// restores them, but the containing dialog owns persistence; application
// launch and placement are routed through GIO and WindowRegistry rather than
// platform APIs.
//
// ------------------------------------------------------------

#pragma once

#include "launchers/session_launcher.h"
#include "launchers/session_record.h"
#include "windowing/window_backend.h"

#include <gtkmm.h>
#include <sigc++/connection.h>

#include <functional>
#include <optional>
#include <string>

class WindowRegistry;

class DockSessionEditorItem : public Gtk::Frame
{
  public:
    using CaptureWindowProvider =
        std::function<std::optional<ManagedWindow>()>;

    explicit DockSessionEditorItem(
        CaptureWindowProvider capture_window = {},
        WindowRegistry *window_registry = nullptr);
    ~DockSessionEditorItem() override;

    sigc::signal<void> &signal_remove_requested();

    // Emitted whenever a value the store keeps has changed, so the editor can
    // persist the Session without waiting for the user to press Save. Paste
    // happens after Add, so a Session committed only on Add would always be
    // missing its most recently added item.
    sigc::signal<void> &signal_changed();

    // Persistence reads and writes plain field values. The card stays a
    // presentation widget and never touches the launcher store itself.
    std::string desktop_file() const;
    std::string app_title() const;
    std::string parameters() const;
    std::string workspace() const;
    std::string dimensions() const;
    std::string position() const;

    // Restores one stored item. The application name and icon are re-resolved
    // from the desktop entry rather than stored, so a renamed or reinstalled
    // application still presents correctly.
    void restore(
        const std::string &desktop_file,
        const std::string &app_title,
        const std::string &parameters,
        const std::string &workspace,
        const std::string &dimensions,
        const std::string &position);

    // A removed card is hidden rather than destroyed, so saving must skip it.
    bool removed() const;

  private:
    void apply_application(
        const std::string &desktop_file);
    void capture();
    void launcher();
    void report(
        const Glib::ustring &message);

    Gtk::Grid m_layout;
    Gtk::Image m_app_icon;

    Gtk::Label m_app_title_label;
    Gtk::Entry m_app_title;
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
    Gtk::Label m_status;

    // Suppresses change notifications while values are being restored from
    // the store, so loading a Session cannot trigger a rewrite of it.
    bool m_restoring = false;

    CaptureWindowProvider m_capture_window;
    SessionLauncher m_launcher;
    bool m_removed = false;
    sigc::signal<void> m_remove_requested;
    sigc::signal<void> m_changed;
};
