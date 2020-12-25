#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "util.h"
#include "editor.h"

int main() {
	struct edit_spec es;
	edit_spec_init_parse(&es, stdin, NULL, 0);
}
