ACLOCAL_AMFLAGS = -I m4 --install
# program that will be installed in $bindir
bin_PROGRAMS = docklight

ALLCPPFLAGS = $(GTKMM_CFLAGS)
ALLCPPFLAGS += $(WNCK_CFLAGS)
ALLCPPFLAGS += -DPROGRAMNAME_LOCALEDIR=\""$(bindir)/docklight-5/locale"\"
ALLCPPFLAGS += -DWNCK_I_KNOW_THIS_IS_UNSTABLE  -Lbamf3 -I/usr/include/libbamf3/
#ALLCPPFLAGS += pkg-config --libs '-lX11 -lXcomposite -lXdamage -lXfixes -lXtst'
#pkg-config --libs fontconfig
docklight_CPPFLAGS = $(ALLCPPFLAGS)

# detailed
#docklight_CPPFLAGS += -Wall -Wextra -Werror -std=c++17 -pedantic

# release
##docklight_CPPFLAGS += O2 -s -DNDEBUG -Wall -Wextra -Werror -std=c++11 -pedantic
docklight_CPPFLAGS +=  -s -DNDEBUG -Wall -Wextra -Werror -std=c++17 -pedantic




# standard debug
#
# -g adds debug information.
# -O0 disables optimizations, making debugging easier.

docklight_CPPFLAGS += -DDEBUG -Wall -std=c++17 -ggdb -O0

#docklight_LDFLAGS = -lX11 -lXcomposite -lXdamage -lXfixes -lXtst


ALLDADDLIBS = $(GTKMM_LIBS)
ALLDADDLIBS +=  -lXext -lX11 -lXcomposite -lXdamage -lXrender -lXfixes -lXtst

docklight_LDADD = $(ALLDADDLIBS)

docklight_SOURCES =  \
main.cpp \
$(NULL)

