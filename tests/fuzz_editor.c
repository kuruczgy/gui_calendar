#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"
#include "editor.h"

int main() {
    struct edit_spec es;
    parse_edit_template(stdin, &es, NULL);
}
