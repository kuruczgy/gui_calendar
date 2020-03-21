
#include <string.h>

#include "calendar.h"
#include "hashmap.h"
#include "core.h"
#include "util.h"
#include "uexpr.h"
#include "application.h"

static uexpr_val get(void *cl, const char *key) {
    static char buf[128];
    struct active_event *aev = cl;
    if (strcmp(key, "sum") == 0)
        return uexpr_create_string(aev->ev->summary);
    if (strcmp(key, "color") == 0)
        return uexpr_create_string(aev->ev->color_str);
    if (strcmp(key, "loc") == 0)
        return uexpr_create_string(aev->ev->location);
    if (strcmp(key, "desc") == 0)
        return uexpr_create_string(aev->ev->desc);
    if (strcmp(key, "st") == 0)
        return uexpr_create_string(cal_status_str(aev->ev->status));
    if (strcmp(key, "clas") == 0)
        return uexpr_create_string(cal_class_str(aev->ev->clas));

    if (strcmp(key, "cal") == 0) {
        snprintf(buf, sizeof(buf), "%d", aev->cal_index + 1);
        return uexpr_create_string(buf);
    }

    if (strcmp(key, "show_priv") == 0)
        return uexpr_create_boolean(state.show_private_events);

    if (strcmp(key, "vis_cals") == 0) {
        int n = 0, l = 0;
        char *list[32];
        for (int i = 0; i < state.n_cal; ++i) {
            if (state.cal_info[i].visible) {
                asrt(n < 32, "");
                list[n++] = buf + l;
                int n = snprintf(buf + l, sizeof(buf) - l, "%d", i + 1);
                asrt(n < sizeof(buf) - l, "");
                l += n + 1;
            }
        }
        return uexpr_create_list_string(list, n);
    }

    return NULL;
}
static bool set(void *cl, const char *key, uexpr_val val) {
    struct active_event *aev = cl;
    bool b;
    if (strcmp(key, "fade") == 0 && uexpr_get_boolean(val, &b)) aev->fade = b;
    if (strcmp(key, "hide") == 0 && uexpr_get_boolean(val, &b)) aev->hide = b;
    else return false;
    return true;
}

bool cal_uexpr_for_active_event(uexpr e, struct active_event *aev) {
    return uexpr_eval(e, aev, &get, &set);
}
