// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_item_effects.cpp
//
// Implementation overview:
// Implements DockItem primary-action feedback and hover animations.
//
// ------------------------------------------------------------

#include "dock_item.h"
#include "rendering/dock_icon_renderer.h"

#include <algorithm>

namespace
{

    constexpr unsigned int ZOOM_FRAME_INTERVAL_MS = 16; // Delay between zoom frames
    constexpr unsigned int BLUR_FRAME_INTERVAL_MS = 16; // Delay between blur frames
    constexpr unsigned int PRIMARY_ACTION_EFFECT_INTERVAL_MS = 35;
    constexpr int PRIMARY_ACTION_EFFECT_FRAME_COUNT = 4;
    constexpr double PRIMARY_ACTION_EFFECT_MIN_OPACITY = 0.55;
} // namespace

void DockItem::start_primary_action_effect()
{
    m_primary_action_effect.disconnect();
    m_primary_action_effect_frame = 0;

    image.set_opacity(
        PRIMARY_ACTION_EFFECT_MIN_OPACITY);

    m_primary_action_effect =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_primary_action_effect),
            PRIMARY_ACTION_EFFECT_INTERVAL_MS);
}

bool DockItem::advance_primary_action_effect()
{
    ++m_primary_action_effect_frame;

    const double progress =
        static_cast<double>(
            m_primary_action_effect_frame) /
        static_cast<double>(
            PRIMARY_ACTION_EFFECT_FRAME_COUNT - 1);

    image.set_opacity(
        PRIMARY_ACTION_EFFECT_MIN_OPACITY +
        (1.0 -
         PRIMARY_ACTION_EFFECT_MIN_OPACITY) *
            std::min(1.0, progress));

    if (m_primary_action_effect_frame <
        PRIMARY_ACTION_EFFECT_FRAME_COUNT - 1)
    {
        return true;
    }

    image.set_opacity(1.0);

    return false;
}

void DockItem::apply_hover_effect()
{
    if (!m_icon_pixbuf)
        return;

    switch (m_hover_effect)
    {
    case DockHoverEffect::standard:
        m_zoom_animation.disconnect();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;
        m_blur_animation.disconnect();
        m_blur_frame = 0;
        m_blur_target_frame = 0;

        image.set(
            m_hovered && m_hover_pixbuf
                ? m_hover_pixbuf
                : m_icon_pixbuf);
        break;

    case DockHoverEffect::zoom:
        m_blur_animation.disconnect();
        m_blur_frame = 0;
        m_blur_target_frame = 0;

        if (m_zoom_frames.empty())
        {
            image.set(m_icon_pixbuf);
            return;
        }

        m_zoom_target_frame =
            m_hovered
                ? static_cast<int>(
                      m_zoom_frames.size()) -
                      1
                : 0;

        image.set(
            m_zoom_frames[static_cast<std::size_t>(
                m_zoom_frame)]);

        start_zoom_animation();
        break;

    case DockHoverEffect::blur:
        m_zoom_animation.disconnect();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;

        if (m_blur_frames.empty())
        {
            image.set(m_icon_pixbuf);
            return;
        }

        m_blur_target_frame =
            m_hovered
                ? static_cast<int>(
                      m_blur_frames.size()) -
                      1
                : 0;

        image.set(
            m_blur_frames[static_cast<std::size_t>(
                m_blur_frame)]);

        start_blur_animation();
        break;
    }
}

void DockItem::create_zoom_frames()
{
    m_zoom_animation.disconnect();
    m_zoom_frame = 0;
    m_zoom_target_frame = 0;
    m_zoom_frames =
        DockIconRenderer::create_zoom_frames(
            m_icon_pixbuf,
            m_icon_size);
}

void DockItem::start_zoom_animation()
{
    if (m_zoom_frame ==
            m_zoom_target_frame ||
        m_zoom_animation.connected())
    {
        return;
    }

    m_zoom_animation =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_zoom_animation),
            ZOOM_FRAME_INTERVAL_MS);
}

bool DockItem::advance_zoom_animation()
{
    if (m_hover_effect !=
            DockHoverEffect::zoom ||
        m_zoom_frames.empty())
    {
        return false;
    }

    if (m_zoom_frame <
        m_zoom_target_frame)
    {
        ++m_zoom_frame;
    }
    else if (m_zoom_frame >
             m_zoom_target_frame)
    {
        --m_zoom_frame;
    }

    image.set(
        m_zoom_frames[static_cast<std::size_t>(
            m_zoom_frame)]);

    return m_zoom_frame !=
           m_zoom_target_frame;
}

void DockItem::create_blur_frames()
{
    m_blur_animation.disconnect();
    m_blur_frame = 0;
    m_blur_target_frame = 0;
    m_blur_frames =
        DockIconRenderer::create_blur_frames(
            m_icon_pixbuf,
            m_icon_size);
}

void DockItem::start_blur_animation()
{
    if (m_blur_frame ==
            m_blur_target_frame ||
        m_blur_animation.connected())
    {
        return;
    }

    m_blur_animation =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_blur_animation),
            BLUR_FRAME_INTERVAL_MS);
}

bool DockItem::advance_blur_animation()
{
    if (m_hover_effect !=
            DockHoverEffect::blur ||
        m_blur_frames.empty())
    {
        return false;
    }

    if (m_blur_frame <
        m_blur_target_frame)
    {
        ++m_blur_frame;
    }
    else if (m_blur_frame >
             m_blur_target_frame)
    {
        --m_blur_frame;
    }

    image.set(
        m_blur_frames[static_cast<std::size_t>(
            m_blur_frame)]);

    return m_blur_frame !=
           m_blur_target_frame;
}
