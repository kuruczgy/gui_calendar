
#include <string.h>

#include "calendar.h"
#include "hashmap.h"
#include "core.h"
#include "util.h"
#include "uexpr.h"
#include "application.h"

static char *nums[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
    "11", "12", "13", "14", "15", "16" };

static uexpr_val get_cats(struct cats *cats) {
    return uexpr_create_list_string(cats->list, cats->n);
}
static uexpr_val get_cal(int cal_index) {
    asrt(cal_index < 16, "cal_index too big");
    return uexpr_create_string(nums[cal_index + 1]);
}
static uexpr_val get_vis_cals() {
    int n = 0;
    char *list[32];
    for (int i = 0; i < state.n_cal; ++i) {
        if (state.cal_info[i].visible) {
            asrt(n < 32, "");
            asrt(i < 16, "cal_index too big");
            list[n++] = nums[i + 1];
        }
    }
    return uexpr_create_list_string(list, n);
}

uexpr_val uexpr_cal_aev_get(void *cl, const char *key) {
    if (!cl) return NULL;
    struct active_event *aev = cl;
    if (strcmp(key, "ev") == 0)
        return uexpr_create_boolean(true);
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
    if (strcmp(key, "cats") == 0)
        return get_cats(&aev->ev->cats);

    if (strcmp(key, "cal") == 0)
        return get_cal(aev->cal_index);
    if (strcmp(key, "show_priv") == 0)
        return uexpr_create_boolean(state.show_private_events);
    if (strcmp(key, "vis_cals") == 0)
        return get_vis_cals();

    return NULL;
}
bool uexpr_cal_aev_set(void *cl, const char *key, uexpr_val val) {
    if (!cl) return false;
    struct active_event *aev = cl;
    bool b;
    if (strcmp(key, "fade") == 0 && uexpr_get_boolean(val, &b))
        aev->fade = b;
    else if (strcmp(key, "hide") == 0 && uexpr_get_boolean(val, &b))
        aev->hide = b;
    else if (strcmp(key, "vis") == 0 && uexpr_get_boolean(val, &b))
        aev->vis = b;
    else
        return false;
    return true;
}

uexpr_val uexpr_cal_todo_get(void *cl, const char *key) {
    // TODO: code duplication with uexpr_cal_aev_get
    if (!cl) return NULL;
    struct todo_tag *tag = cl;
    if (strcmp(key, "ev") == 0)
        return uexpr_create_boolean(false);
    if (strcmp(key, "sum") == 0)
        return uexpr_create_string(tag->td->summary);
    if (strcmp(key, "desc") == 0)
        return uexpr_create_string(tag->td->desc);
    if (strcmp(key, "st") == 0)
        return uexpr_create_string(cal_status_str(tag->td->status));
    if (strcmp(key, "clas") == 0)
        return uexpr_create_string(cal_class_str(tag->td->clas));
    if (strcmp(key, "cats") == 0)
        return get_cats(&tag->td->cats);

    if (strcmp(key, "cal") == 0)
        return get_cal(tag->cal_index);
    if (strcmp(key, "show_priv") == 0)
        return uexpr_create_boolean(state.show_private_events);
    if (strcmp(key, "vis_cals") == 0)
        return get_vis_cals();

    return NULL;
}
bool uexpr_cal_todo_set(void *cl, const char *key, uexpr_val val) {
    // TODO: code duplication with uexpr_cal_aev_set
    if (!cl) return false;
    struct todo_tag *tag = cl;
    bool b;
    if (strcmp(key, "fade") == 0 && uexpr_get_boolean(val, &b))
        tag->fade = b;
    else if (strcmp(key, "vis") == 0 && uexpr_get_boolean(val, &b))
        tag->vis = b;
    else
        return false;
    return true;
}
