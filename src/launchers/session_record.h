// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// session_record.h
//
// Purpose:
// Declares the plain value types describing one saved Session.
//
// Responsibilities:
// - Carry a Session's name, icon, and ordered items between the editor, the
//   launcher store, the session launcher, and the dock.
//
// Dependencies and ownership:
// Values only. No GTK, no Gio, and no knowledge of where a Session is stored.
//
// Design notes:
// These live in their own header so a presentation widget can exchange Session
// values without depending on LauncherManager, and so the persistence format
// stays the store's private concern. Fields are stored verbatim as the user
// left them; interpretation belongs to the consumer.
//
// ------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

// One Session item as the user edited it.
struct SessionItemRecord
{
    std::string desktop_file;
    std::string title;
    std::string parameters;
    std::string workspace;
    std::string dimensions;
    std::string position;
};

// One named Session and its ordered items.
struct SessionRecord
{
    std::string name;
    std::string icon;
    std::vector<SessionItemRecord> items;
};
