#include "application.h"
#include "util.h"
#include "config.h"
#include <unistd.h>
#include <platform_utils/main.h>
#include <platform_utils/log.h>

void platform_main(struct platform *plat) {

	struct application_options opts = {
		.show_private_events = false,
		.default_vis = 0,
		.view_days = 7,
		.editor = NULL,
		.terminal = NULL,
		.config_file = NULL
	};

#if PU_MAIN_HAS_ARGS
	const char *help =
		"-h: show this help\n"
		"-p: show private events by default\n"
		"-d I: set calendar I to be visible by default."
			" (1 based index)\n"
		"-v N: set the number of visible days to N\n"
		"-e E: set editor command to E\n"
		"-t T: set terminal command to T\n"
		"-c C: provide the path to the config script\n";

	int argc = plat->argc;
	char **argv = plat->argv;
	int opt, d;
	while ((opt = getopt(argc, argv, "hpd:v:e:t:o:c:")) != -1) {
		switch (opt) {
		case 'h':
			pu_log_info("%s", help);
			exit(EXIT_SUCCESS);
		case 'p':
			opts.show_private_events = true;
			break;
		case 'd':
			d = atoi(optarg) - 1;
			if (d < 0 || d >= 16) {
				fprintf(stderr, "bad -d option index");
				exit(EXIT_FAILURE);
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
		case 'c':
			opts.config_file = str_dup(optarg);
			break;
		}
	}
#endif

	struct mgu_disp disp = { 0 };
	mgu_disp_init(&disp, plat);
	struct mgu_win_surf *win =
		mgu_disp_add_surf_default(&disp, CONFIG_TITLE);
	if (!win) exit(EXIT_FAILURE);

	struct app app;
	app_init(&app, opts, plat, win);
	app_main(&app);
	app_finish(&app);

	mgu_disp_finish(&disp);

	free(opts.editor);
	free(opts.terminal);
	free(opts.config_file);
}
