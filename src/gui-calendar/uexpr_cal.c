
#include <string.h>

#include "calendar.h"
#include "hashmap.h"
#include "core.h"
#include "util.h"
#include "uexpr.h"
#include "application.h"

static char *nums[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
    "11", "12", "13", "14", "15", "16" };

static uexpr_val get_cal(int cal_index) {
    asrt(cal_index < 16, "cal_index too big");
    return uexpr_create_string(nums[cal_index + 1]);
}

uexpr_val uexpr_cal_ac_get(void *cl, const char *key) {
    if (!cl) return NULL;
    struct active_comp *ac = cl;
    if (strcmp(key, "ev") == 0)
        return uexpr_create_boolean(ac->ci->c->type == COMP_TYPE_EVENT);
    if (strcmp(key, "sum") == 0)
        return uexpr_create_string(props_get_summary(ac->ci->p));
    if (strcmp(key, "color") == 0)
        return uexpr_create_string(props_get_color(ac->ci->p));
    if (strcmp(key, "loc") == 0)
        return uexpr_create_string(props_get_location(ac->ci->p));
    if (strcmp(key, "desc") == 0)
        return uexpr_create_string(props_get_desc(ac->ci->p));
    if (strcmp(key, "st") == 0) {
        enum prop_status status;
        bool has_status = props_get_status(ac->ci->p, &status);
        return uexpr_create_string(has_status ? cal_status_str(status) : NULL);
    }
    if (strcmp(key, "clas") == 0) {
        enum prop_class class;
        bool has_class = props_get_class(ac->ci->p, &class);
        return uexpr_create_string(has_class ? cal_class_str(class) : NULL);
    }
    if (strcmp(key, "cats") == 0) {
        struct vec v = vec_new_empty(sizeof(const char *));
        const struct vec *cats = props_get_categories(ac->ci->p);
        for (int i = 0; i < cats->len; ++i) {
            const struct str *s = vec_get((struct vec*)cats, i); /*const cast*/
            const char *cstr = str_cstr(s);
            vec_append(&v, &cstr);
        }
        uexpr_val res = uexpr_create_list_string(v.d, v.len);
        vec_free(&v);
        return res;
    }

    if (strcmp(key, "cal") == 0)
        return get_cal(ac->cal_index);

    return NULL;
}
bool uexpr_cal_ac_set(void *cl, const char *key, uexpr_val val) {
    if (!cl) return false;
    struct active_comp *ac = cl;
    bool b;
    if (strcmp(key, "fade") == 0 && uexpr_get_boolean(val, &b))
        ac->fade = b;
    else if (strcmp(key, "hide") == 0 && uexpr_get_boolean(val, &b))
        ac->hide = b;
    else if (strcmp(key, "vis") == 0 && uexpr_get_boolean(val, &b))
        ac->vis = b;
    else
        return false;
    return true;
}
