#include "application.h"
#include "util.h"
#include <unistd.h>

enum backend_type {
    BACKEND_SVG,
    BACKEND_IMAGE,
    BACKEND_DUMMY,
    BACKEND_WAYLAND,
    BACKEND_FBDEV,
    BACKEND_NONE
};

int main(int argc, char **argv) {
    struct application_options opts = {
        .show_private_events = false,
        .default_vis = 0,
        .view_days = 7,
        .editor = NULL,
        .terminal = NULL
    };
    enum backend_type bt = BACKEND_NONE;
    int width = 100, height = 100;
    char *output = NULL;

    if (getenv("WAYLAND_DISPLAY")) bt = BACKEND_WAYLAND;
    else if (access("/dev/fb0", F_OK) == 0) bt = BACKEND_FBDEV;

    const char *help =
        "-h: show this help\n"
        "-p: show private events by default\n"
        "-d I: set calendar I to be visible by default. (1 based index)\n"
        "-v N: set the number of visible days to N\n"
        "-e E: set editor command to E\n"
        "-t T: set terminal command to T\n"
        "-b B: set backend to B; values: svg, image, dummy, fbdev, wayland\n"
        "-s WxH: set output size width to W, and height to H\n"
        "-o F: set output filename to F\n";
    int opt, d;
    while ((opt = getopt(argc, argv, "hpd:v:e:t:b:s:o:")) != -1) {
        switch (opt) {
        case 'h':
            fprintf(stdout, help);
            exit(0);
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
        case 'v':
            opts.view_days = atoi(optarg);
            break;
        case 'e':
            opts.editor = str_dup(optarg);
            break;
        case 't':
            opts.terminal = str_dup(optarg);
            break;
        case 'b':
            if (strcmp("svg", optarg) == 0) bt = BACKEND_SVG;
            else if (strcmp("image", optarg) == 0) bt = BACKEND_IMAGE;
            else if (strcmp("dummy", optarg) == 0) bt = BACKEND_DUMMY;
            else if (strcmp("fbdev", optarg) == 0) bt = BACKEND_FBDEV;
            else if (strcmp("wayland", optarg) == 0) bt = BACKEND_WAYLAND;
            break;
        case 's':
            sscanf(optarg, "%dx%d", &width, &height);
            break;
        case 'o':
            output = str_dup(optarg);
            break;
        }
    }
    opts.argc = argc - optind;
    opts.argv = argv + optind;

    const char *cur_out = output;
    (void)cur_out; // pacify warnings
    struct backend backend;
    switch (bt) {
#ifdef CONFIG_BACKEND_SVG
        case BACKEND_SVG:
            if (!cur_out) cur_out = "out.svg";
            backend = backend_init_svg(cur_out, width, height);
            break;
#endif
#ifdef CONFIG_BACKEND_IMAGE
        case BACKEND_IMAGE:
            if (!cur_out) cur_out = "out.png";
            backend = backend_init_image(cur_out, width, height);
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
