// Watches MPRIS players and reports whether a dock application is playing.

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
    std::string playing_title(
        const std::string &desktop_id) const;

    sigc::signal<void> &signal_changed();

private:
    std::shared_ptr<State> m_state;
};
