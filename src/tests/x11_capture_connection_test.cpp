// ------------------------------------------------------------
// Docklight 6.0
// File: x11_capture_connection_test.cpp
// Purpose: Verify real X errors stay on private capture connections while an
// unrelated connection retains its process handler, even during replacement.
// Decisions: Run against an isolated X server; skip when DISPLAY is absent.
// ------------------------------------------------------------

#include "preview/x11_capture_connection.h"
#include <X11/Xutil.h>

#include <atomic>
#include <cassert>
#include <thread>

namespace
{
    std::atomic<int> first_errors{0};
    std::atomic<int> second_errors{0};

    int first_handler(Display *, XErrorEvent *)
    {
        ++first_errors;
        return 0;
    }

    int second_handler(Display *, XErrorEvent *)
    {
        ++second_errors;
        return 0;
    }
}

int main()
{
    assert(XInitThreads());
    auto *unrelated = XOpenDisplay(nullptr);
    if (!unrelated)
        return 77;
    const auto previous = XSetErrorHandler(first_handler);
    std::atomic<bool> ready{false};
    std::atomic<bool> finish{false};
    std::thread worker([&]()
    {
        auto *capture = X11Capture::open_display();
        assert(capture);
        X11Capture::error = false;
        auto *bad_image = XGetImage(
            capture, None, 0, 0, 2, 2, AllPlanes, ZPixmap);
        XSync(capture, False);
        assert(!bad_image && X11Capture::error);
        X11Capture::error = false;
        auto *good_image = XGetImage(
            capture, DefaultRootWindow(capture),
            0, 0, 2, 2, AllPlanes, ZPixmap);
        XSync(capture, False);
        assert(good_image && !X11Capture::error);
        XDestroyImage(good_image);
        ready = true;
        do
        {
            X11Capture::error = false;
            XDestroyWindow(capture, None);
            XSync(capture, False);
            assert(X11Capture::error);
            X11Capture::error = false;
            XNoOp(capture);
            XSync(capture, False);
            assert(!X11Capture::error);
        } while (!finish);
        XCloseDisplay(capture);
    });

    while (!ready)
        std::this_thread::yield();
    for (int i = 0; i < 20; ++i)
    {
        XDestroyWindow(unrelated, None);
        XSync(unrelated, False);
    }
    assert(XSetErrorHandler(second_handler) == first_handler);
    for (int i = 0; i < 20; ++i)
    {
        XDestroyWindow(unrelated, None);
        XSync(unrelated, False);
    }
    finish = true;
    worker.join();
    assert(first_errors == 20 && second_errors == 20);
    assert(!X11Capture::error);
    assert(XSetErrorHandler(previous) == second_handler);
    XCloseDisplay(unrelated);
}
