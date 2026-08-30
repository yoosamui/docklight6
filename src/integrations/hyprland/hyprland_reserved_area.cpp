// ------------------------------------------------------------
// Docklight 6.0
//
// Pure Hyprland reserved-area calculations shared by the dock surface and
// focused unit coverage.
// ------------------------------------------------------------

#include "hyprland_reserved_area.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace
{

std::vector<int> parse_custom_gaps(
    const std::string &json)
{
    const auto key = json.find("\"custom\"");
    const auto colon = key == std::string::npos
        ? std::string::npos
        : json.find(':', key);
    const auto start = colon == std::string::npos
        ? std::string::npos
        : json.find('"', colon + 1);
    const auto end = start == std::string::npos
        ? std::string::npos
        : json.find('"', start + 1);
    if (end == std::string::npos)
        return {};

    std::istringstream values(
        json.substr(start + 1, end - start - 1));
    std::vector<int> gaps;
    int value = 0;
    while (values >> value)
        gaps.push_back(std::max(0, value));
    return gaps;
}

std::vector<int> expand_css_gaps(
    const std::vector<int> &gaps)
{
    if (gaps.size() == 1)
        return {gaps[0], gaps[0], gaps[0], gaps[0]};
    if (gaps.size() == 2)
        return {gaps[0], gaps[1], gaps[0], gaps[1]};
    if (gaps.size() == 3)
        return {gaps[0], gaps[1], gaps[2], gaps[1]};
    if (gaps.size() >= 4)
        return {gaps[0], gaps[1], gaps[2], gaps[3]};
    return {};
}

int parse_integer_gap(
    const std::string &json)
{
    const auto key = json.find("\"int\"");
    const auto colon = key == std::string::npos
        ? std::string::npos
        : json.find(':', key);
    if (colon == std::string::npos)
        return 0;

    char *end = nullptr;
    const auto *start = json.c_str() + colon + 1;
    const auto value = std::strtol(start, &end, 10);
    return end == start
        ? 0
        : static_cast<int>(std::max(0L, value));
}

}

int hyprland_outer_gap_from_option_json(
    const std::string &json,
    const std::string &edge)
{
    const auto gaps = expand_css_gaps(
        parse_custom_gaps(json));
    if (gaps.empty())
        return parse_integer_gap(json);

    // CSS order: top, right, bottom, left.
    const std::size_t index =
        edge == "top" ? 0 :
        edge == "right" ? 1 :
        edge == "bottom" ? 2 : 3;
    return gaps[index];
}

int hyprland_reservation_size(
    int dock_size,
    int outer_gap)
{
    return dock_size <= 0
        ? 0
        : std::max(1, dock_size - std::max(0, outer_gap));
}
