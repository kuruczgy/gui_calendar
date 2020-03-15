
#include <string.h>

#include "calendar.h"
#include "hashmap.h"
#include "util.h"
#include "uexpr.h"
#include "application.h"

static const char * get(void *cl, const char *key) {
    struct active_event *aev = cl;
    if (strcmp(key, "sum") == 0) return aev->ev->summary;
    if (strcmp(key, "color") == 0) return aev->ev->color_str;
    if (strcmp(key, "loc") == 0) return aev->ev->location;
    if (strcmp(key, "desc") == 0) return aev->ev->desc;
    if (strcmp(key, "st") == 0) return cal_status_str(aev->ev->status);
    if (strcmp(key, "clas") == 0) return cal_class_str(aev->ev->clas);
    return NULL;
}
static bool set(void *cl, const char *key, const char *val) {
    struct active_event *aev = cl;
    if (strcmp(key, "fade") == 0) aev->fade = true;
    else return false;
    return true;
}

bool cal_uexpr_for_active_event(uexpr e, struct active_event *aev) {
    return uexpr_eval(e, aev, &get, &set);
}
