// ------------------------------------------------------------
// Docklight 6.0
//
// Selects how Docklight's GTK surfaces are presented independently from the
// desktop-specific window integration used for discovery and actions.
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
