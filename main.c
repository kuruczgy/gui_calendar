#include "application.h"
#include "util.h"
#include <unistd.h>

enum backend_type {
    BACKEND_SVG,
    BACKEND_DUMMY,
    BACKEND_WAYLAND,
    BACKEND_FBDEV,
    BACKEND_NONE
};

int main(int argc, char **argv) {
    struct application_options opts = {
        .show_private_events = false,
        .default_vis = 0,
        .editor = NULL,
        .terminal = NULL
    };
    enum backend_type bt = BACKEND_NONE;

    if (getenv("WAYLAND_DISPLAY")) bt = BACKEND_WAYLAND;
    else if (access("/dev/fb0", F_OK) == 0) bt = BACKEND_FBDEV;

    int opt, d;
    while ((opt = getopt(argc, argv, "pd:e:b:")) != -1) {
        switch (opt) {
        case 'p':
            opts.show_private_events = true;
            break;
        case 'd':
            d = atoi(optarg) - 1;
            if (d < 0 || d >= 16) {
                fprintf(stderr, "bad -d option index");
                exit(1);
            }
            opts.default_vis |= (1U << d);
            break;
        case 'e':
            opts.editor = str_dup(optarg);
            break;
        case 't':
            opts.terminal = str_dup(optarg);
            break;
        case 'b':
            if (strcmp("svg", optarg) == 0) bt = BACKEND_SVG;
            else if (strcmp("dummy", optarg) == 0) bt = BACKEND_DUMMY;
            else if (strcmp("fbdev", optarg) == 0) bt = BACKEND_FBDEV;
            break;
        }
    }
    opts.argc = argc - optind;
    opts.argv = argv + optind;

    struct backend backend;
    switch (bt) {
#ifdef CONFIG_BACKEND_SVG
        case BACKEND_SVG:
            backend = backend_init_svg("out.svg", 500, 500);
            break;
#endif
#ifdef CONFIG_BACKEND_DUMMY
        case BACKEND_DUMMY:
            backend = backend_init_dummy();
            break;
#endif
#ifdef CONFIG_BACKEND_WAYLAND
        case BACKEND_WAYLAND:
            backend = backend_init_wayland();
            break;
#endif
#ifdef CONFIG_BACKEND_FBDEV
        case BACKEND_FBDEV:
            backend = backend_init_fbdev();
#endif
        default:
            fprintf(stderr, "backend not supported\n");
            exit(1);
    }
    if (!backend.self) {
        fprintf(stderr, "backend failed init\n");
        exit(1);
    }

    int res = application_main(opts, &backend);

    free(opts.editor);
    free(opts.terminal);

    return res;
}
