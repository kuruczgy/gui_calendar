#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"
#include "backend.h"
#include "application.h"

int main() {
    struct backend b = backend_init_dummy();

    struct application_options opts = {
        .show_private_events = false,
        .default_vis = 0,
        .view_days = 7,
        .editor = NULL,
        .terminal = NULL,
        .config_file = NULL,
        .argc = 0
    };

    struct app app;
    app_init(&app, opts, &b);

    struct calendar cal;
    calendar_init(&cal);
    libical_parse_ics(stdin, &cal);

    struct calendar_info cal_info = { .visible = true };
    vec_append(&app.cals, &cal);
    vec_append(&app.cal_infos, &cal_info);

    app_update_active_objects(&app);

    app_main(&app);
    app_finish(&app);
}
