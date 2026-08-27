// Private implementation details shared by the DockPreviewWindow build
// units. This header is not part of the public preview-window API.

#pragma once

#include "dock_preview_window.h"

#include <algorithm>

namespace dock_preview_detail
{

inline constexpr double CARD_CORNER_RADIUS = 7.0;
inline constexpr double PREVIEW_PI =
    3.14159265358979323846;

inline void append_rounded_rectangle(
    const Cairo::RefPtr<Cairo::Context> &context,
    double width,
    double height,
    double radius)
{
    const double effective_radius =
        std::max(
            0.0,
            std::min(
                radius,
                std::min(width, height) / 2.0));

    context->begin_new_sub_path();
    context->arc(
        width - effective_radius,
        effective_radius,
        effective_radius,
        -PREVIEW_PI / 2.0,
        0.0);
    context->arc(
        width - effective_radius,
        height - effective_radius,
        effective_radius,
        0.0,
        PREVIEW_PI / 2.0);
    context->arc(
        effective_radius,
        height - effective_radius,
        effective_radius,
        PREVIEW_PI / 2.0,
        PREVIEW_PI);
    context->arc(
        effective_radius,
        effective_radius,
        effective_radius,
        PREVIEW_PI,
        3.0 * PREVIEW_PI / 2.0);
    context->close_path();
}

} // namespace dock_preview_detail

class DockPreviewCardCanvas : public Gtk::DrawingArea
{
public:
    DockPreviewCardCanvas(
        int width,
        int header_height,
        int image_height,
        const Gdk::RGBA &preview_color)
        : m_header_height(header_height),
          m_image_height(image_height),
          m_preview_color(preview_color)
    {
        set_size_request(
            width,
            header_height + image_height);
    }

    void set_selected(bool selected)
    {
        if (m_selected == selected)
            return;

        m_selected = selected;
        queue_draw();
    }

    void set_preview_color(
        const Gdk::RGBA &preview_color)
    {
        m_preview_color = preview_color;
        queue_draw();
    }

    void set(
        const Glib::RefPtr<Gdk::Pixbuf> &pixbuf)
    {
        m_pixbuf = pixbuf;
        queue_draw();
    }

    void set_pixel_size(int size)
    {
        m_fallback_size = size;
    }

    void set_from_icon_name(
        const std::string &name,
        Gtk::IconSize)
    {
        try
        {
            m_pixbuf = Gtk::IconTheme::get_default()
                           ->load_icon(
                               name,
                               std::max(1, m_fallback_size),
                               Gtk::ICON_LOOKUP_FORCE_SIZE);
        }
        catch (const Glib::Error &)
        {
            m_pixbuf.reset();
        }

        queue_draw();
    }

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>
            &context) override
    {
        const auto allocation = get_allocation();
        const int width = allocation.get_width();
        const int height = allocation.get_height();

        context->set_source_rgba(
            m_selected
                ? m_preview_color.get_red()
                : 1.0,
            m_selected
                ? m_preview_color.get_green()
                : 1.0,
            m_selected
                ? m_preview_color.get_blue()
                : 1.0,
            m_selected
                ? 0.32 * m_preview_color.get_alpha()
                : 0.06);
        dock_preview_detail::append_rounded_rectangle(
            context,
            width,
            height,
            dock_preview_detail::CARD_CORNER_RADIUS);
        context->fill();

        // The thumbnail and selector are deliberately painted in this same
        // Cairo pass. The opaque image-area base prevents the selector from
        // showing through capture alpha, and the pixbuf is always painted
        // after the selector.
        context->rectangle(
            0,
            m_header_height,
            width,
            m_image_height);
        context->set_source_rgb(
            28.0 / 255.0,
            28.0 / 255.0,
            32.0 / 255.0);
        context->fill();

        if (m_pixbuf)
        {
            const double x =
                (width - m_pixbuf->get_width()) / 2.0;
            const double y =
                m_header_height +
                (m_image_height -
                 m_pixbuf->get_height()) /
                    2.0;
            Gdk::Cairo::set_source_pixbuf(
                context,
                m_pixbuf,
                x,
                y);
            context->paint();
        }

        return true;
    }

private:
    Glib::RefPtr<Gdk::Pixbuf> m_pixbuf;
    int m_header_height = 0;
    int m_image_height = 0;
    int m_fallback_size = 1;
    Gdk::RGBA m_preview_color;
    bool m_selected = false;
};
