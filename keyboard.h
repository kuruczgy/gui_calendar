#ifndef _KEYBOARD_H_
#define _KEYBOARD_H_
#include <stdint.h>
#include <stdbool.h>

struct key_gen {
    int N, k;
    int A[32];
    bool begin;
};

bool key_sym(uint32_t code, char sym);
int key_num(uint32_t code);
void key_gen_init(int n, struct key_gen *g);
const char* key_gen_get(struct key_gen *g);

#endif
