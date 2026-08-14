AC_PREREQ([2.69])
AC_INIT([docklight6], [6.0.32], [yoosamui@example.com])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL

PKG_PROG_PKG_CONFIG

GETTEXT_PACKAGE=docklight6
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED(
    [GETTEXT_PACKAGE],
    ["$GETTEXT_PACKAGE"],
    [Gettext message catalog domain])

AC_PATH_PROG([MSGFMT], [msgfmt])
AS_IF(
    [test -z "$MSGFMT"],
    [AC_MSG_ERROR([msgfmt is required to build translation catalogs])])

AC_PATH_PROG([XGETTEXT], [xgettext])
AS_IF(
    [test -z "$XGETTEXT"],
    [AC_MSG_ERROR([xgettext is required to extract translatable strings])])

AC_PATH_PROG([MSGMERGE], [msgmerge])
AS_IF(
    [test -z "$MSGMERGE"],
    [AC_MSG_ERROR([msgmerge is required to update translation catalogs])])

ALL_LINGUAS=`sed -e '/^#/d' -e '/^$/d' "$srcdir/po/LINGUAS" | tr '\n' ' '`
AC_SUBST([ALL_LINGUAS])

PKG_CHECK_MODULES([GTKMM],
[
    gtkmm-3.0 >= 3.24
])

PKG_CHECK_MODULES([GTK_LAYER_SHELL],
[
    gtk-layer-shell-0
])

PKG_CHECK_MODULES([PIPEWIRE],
[
    libpipewire-0.3 >= 0.3
])

PKG_CHECK_MODULES([WAYLAND],
[
    wayland-client
    gdk-wayland-3.0
])

PKG_CHECK_MODULES([X11_BACKEND],
[
    libwnck-3.0 >= 3.20
    gdk-x11-3.0 >= 3.24
    x11
    xcomposite
    xrender
])

AC_PATH_PROG(
    [WAYLAND_SCANNER],
    [wayland-scanner],
    [no])
AS_IF(
    [test "x$WAYLAND_SCANNER" = "xno"],
    [AC_MSG_ERROR([wayland-scanner is required to build KWin live previews])])

##CXXFLAGS="$CXXFLAGS -std=c++17"
# current
##CXXFLAGS="$CXXFLAGS -std=c++17 -Wall -Wextra -Wpedantic"
# strict:  CXXFLAGS="$CXXFLAGS \    -std=c++17 \    -Wall \    -Wextra \    -Wpedantic \    -Wshadow \    -Wconversion"
    
AC_SUBST(GTKMM_CFLAGS)
AC_SUBST(GTKMM_LIBS)

AC_SUBST(GTK_LAYER_SHELL_CFLAGS)
AC_SUBST(GTK_LAYER_SHELL_LIBS)
AC_SUBST(PIPEWIRE_CFLAGS)
AC_SUBST(PIPEWIRE_LIBS)
AC_SUBST(WAYLAND_CFLAGS)
AC_SUBST(WAYLAND_LIBS)
AC_SUBST(X11_BACKEND_CFLAGS)
AC_SUBST(X11_BACKEND_LIBS)

AC_CONFIG_FILES([
Makefile
po/Makefile
src/Makefile
])

AC_OUTPUT
