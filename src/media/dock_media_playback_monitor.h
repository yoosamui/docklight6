// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_media_playback_monitor.h
//
// Purpose:
// Declares an MPRIS monitor that reports whether a dock application is
// actively playing media.
//
// Responsibilities:
// - Discover available MPRIS players.
// - Track playback status and application identity.
// - Notify consumers when effective playback state changes.
//
// Dependencies and ownership:
// The monitor owns its shared implementation and D-Bus subscriptions;
// returned strings and signals use value semantics.
//
// Design notes:
// Media state is exposed independently from preview rendering.
//
// ------------------------------------------------------------

#pragma once

#include <gio/gio.h>
#include <sigc++/signal.h>

#include <memory>
#include <string>

class DockMediaPlaybackMonitor
{
public:
    struct State;

    DockMediaPlaybackMonitor();
    ~DockMediaPlaybackMonitor();

    bool is_playing(
        const std::string &desktop_id) const;
    bool should_stream(
        const std::string &desktop_id) const;
    std::string playing_title(
        const std::string &desktop_id) const;

    sigc::signal<void> &signal_changed();

private:
    std::shared_ptr<State> m_state;
};
