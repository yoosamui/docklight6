AC_PREREQ([2.69])
AC_INIT([docklight6], [6.0.16], [yoosamui@example.com])

AM_INIT_AUTOMAKE([foreign subdir-objects])

AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CXX
AC_PROG_INSTALL

PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES([GTKMM],
[
    gtkmm-3.0 >= 3.24
])

PKG_CHECK_MODULES([GTK_LAYER_SHELL],
[
    gtk-layer-shell-0
])

##CXXFLAGS="$CXXFLAGS -std=c++17"
# current
##CXXFLAGS="$CXXFLAGS -std=c++17 -Wall -Wextra -Wpedantic"
# strict:  CXXFLAGS="$CXXFLAGS \    -std=c++17 \    -Wall \    -Wextra \    -Wpedantic \    -Wshadow \    -Wconversion"
    
AC_SUBST(GTKMM_CFLAGS)
AC_SUBST(GTKMM_LIBS)

AC_SUBST(GTK_LAYER_SHELL_CFLAGS)
AC_SUBST(GTK_LAYER_SHELL_LIBS)

AC_CONFIG_FILES([
Makefile
src/Makefile
])

AC_OUTPUT
