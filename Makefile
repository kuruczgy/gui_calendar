
XDG_SHELL_XML="/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml"

MY_FLAGS=`pkg-config --cflags --libs cairo pango pangocairo`

all: xdg_shell
	$(CC) -g -o a.out \
		main.c util.c parse.c pango.c xdg-shell-client-protocol.c \
		-lwayland-client $(MY_FLAGS)
xdg_shell:
	wayland-scanner client-header $(XDG_SHELL_XML) xdg-shell-client-protocol.h
	wayland-scanner private-code $(XDG_SHELL_XML) xdg-shell-client-protocol.c
