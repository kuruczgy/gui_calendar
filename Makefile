
XDG_SHELL_XML="/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"

MY_FLAGS=`pkg-config --cflags --libs cairo pango pangocairo libical`

all: xdg_shell
	$(CC) -g -o gui_calendar \
		main.c util.c parse.c pango.c gui.c subprocess.c colors.c \
		calendar_layout.c libical_parse.c keyboard.c algo.c \
		util/hashmap.c \
		xdg-shell-client-protocol.c \
		-lwayland-client $(MY_FLAGS)
xdg_shell:
	wayland-scanner client-header $(XDG_SHELL_XML) xdg-shell-client-protocol.h
	wayland-scanner private-code $(XDG_SHELL_XML) xdg-shell-client-protocol.c
