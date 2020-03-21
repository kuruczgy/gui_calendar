#ifndef GUI_CALENDAR_CORE_H
#define GUI_CALENDAR_CORE_H
#include <stdbool.h>
#include <time.h>

void asrt(bool cond, const char *msg);
void * malloc_check(size_t size);

int mini(int a, int b);
int maxi(int a, int b);

struct stopwatch { struct timespec fr; };
struct stopwatch sw_start();
void sw_end_print(struct stopwatch, const char *msg);

#endif
