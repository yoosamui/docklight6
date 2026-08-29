// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// presentation_selector.h
//
// Purpose:
// Declares selection and preparation of Docklight's GTK presentation mode.
//
// Responsibilities:
// - Parse native, XWayland, and automatic presentation requests.
// - Resolve command-line and persisted configuration precedence.
// - Prepare process and child-launch environments before GTK starts.
// - Report the actual GTK presentation backend.
//
// Dependencies and ownership:
// Selection values own their strings. Launch-context helpers borrow the
// supplied GLib context and process-environment access is non-owning.
//
// Design notes:
// Presentation of Docklight surfaces is independent from the integration
// used to discover and control application windows.
//
// ------------------------------------------------------------

#pragma once

#include <optional>
#include <string>

typedef struct _GAppLaunchContext GAppLaunchContext;

enum class PresentationMode
{
    automatic,
    native,
    xwayland
};

struct PresentationSelection
{
    PresentationMode mode =
        PresentationMode::automatic;
    std::string source = "default";
};

const char *presentation_mode_name(
    PresentationMode mode);

std::optional<PresentationMode>
parse_presentation_mode(
    const std::string &value);

bool take_presentation_option(
    int &argc,
    char *argv[],
    std::optional<PresentationMode>
        &requested_mode,
    std::string &error);

PresentationSelection select_presentation(
    const std::optional<PresentationMode>
        &requested_mode,
    const std::string &configuration_path = {});

bool prepare_presentation(
    const PresentationSelection &selection,
    std::string &error);

void prepare_application_launch_context(
    GAppLaunchContext *context);

std::string presentation_configuration_path();
const char *actual_presentation_backend_name();
