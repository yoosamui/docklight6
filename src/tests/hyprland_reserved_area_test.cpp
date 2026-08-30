// ------------------------------------------------------------
// Docklight 6.0
//
// Verifies Hyprland CSS-style outer-gap parsing and reservation sizing.
// ------------------------------------------------------------

#include "integrations/hyprland/hyprland_reserved_area.h"

#include <cassert>

int main()
{
    const auto gap = [](const char *json, const char *edge)
    {
        return hyprland_outer_gap_from_option_json(json, edge);
    };

    assert(gap(R"({"custom":"8"})", "bottom") == 8);
    assert(gap(R"({"custom":"8 12"})", "top") == 8);
    assert(gap(R"({"custom":"8 12"})", "left") == 12);
    assert(gap(R"({"custom":"8 12 16"})", "bottom") == 16);
    assert(gap(R"({"custom":"8 12 16"})", "left") == 12);
    assert(gap(R"({"custom":"8 12 16 20"})", "right") == 12);
    assert(gap(R"({"custom":"8 12 16 20"})", "left") == 20);
    assert(gap(R"({"custom":"-8 -4 -2 -1"})", "bottom") == 0);
    assert(gap(R"({"custom":"", "int":6})", "right") == 6);
    assert(gap(R"({"int":10})", "bottom") == 10);
    assert(gap(R"({})", "top") == 0);

    assert(hyprland_reservation_size(64, 20) == 44);
    assert(hyprland_reservation_size(8, 20) == 1);
    assert(hyprland_reservation_size(64, -5) == 64);
    assert(hyprland_reservation_size(0, 20) == 0);
    return 0;
}
