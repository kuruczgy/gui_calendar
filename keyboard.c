#include "keyboard.h"
#include <stddef.h>
#include <linux/input-event-codes.h>

#include "algo.h"

/* The reason I want to do the scancode to symbol translation manually, because
 * I want the keybindings to be layout independent.
 */

static const size_t lut_len = KEY_CNT;
static char lut[KEY_CNT] = {
    [KEY_A] = 'a',
    [KEY_B] = 'b',
    [KEY_C] = 'c',
    [KEY_D] = 'd',
    [KEY_E] = 'e',
    [KEY_F] = 'f',
    [KEY_G] = 'g',
    [KEY_H] = 'h',
    [KEY_I] = 'i',
    [KEY_J] = 'j',
    [KEY_K] = 'k',
    [KEY_L] = 'l',
    [KEY_M] = 'm',
    [KEY_N] = 'n',
    [KEY_O] = 'o',
    [KEY_P] = 'p',
    [KEY_Q] = 'q',
    [KEY_R] = 'r',
    [KEY_S] = 's',
    [KEY_T] = 't',
    [KEY_U] = 'u',
    [KEY_V] = 'v',
    [KEY_W] = 'w',
    [KEY_X] = 'x',
    [KEY_Y] = 'y',
    [KEY_Z] = 'z',
};

static const size_t gen_len = 8;
static uint32_t gen[8] = {
    KEY_A, KEY_S, KEY_D, KEY_F,
    KEY_Q, KEY_W, KEY_E, KEY_R
};

bool key_sym(uint32_t code, char sym) {
    if (code >= lut_len) return false;
    return lut[code] == sym;
}

bool key_is_sym(uint32_t code) {
    char sym = key_get_sym(code);
    return 'a' <= sym && 'z' >= sym;
}

char key_get_sym(uint32_t code) {
    return lut[code];
}

int key_num(uint32_t code) {
    if (code < KEY_1 || code > KEY_9) return -1;
    return code - KEY_1 + 1;
}

static int num_k_perm(int n, int k) {
    int res = 1;
    for (int i = n; i > n-k; --i) res *= i;
    return res;
}

void key_gen_init(int n, struct key_gen *g) {
    g->N = gen_len;
    int k = 1;
    while (num_k_perm(g->N, k) < n) k++;
    g->k = k;
    for (int i = 0; i < g->N; ++i) g->A[i] = i;
    g->begin = true;
}

static char* print_perm(int *c, int k) {
    static char cc[33]; cc[k] = '\0';
    for (int i = 0; i < k; ++i) cc[i] = lut[gen[c[i]]];
    return cc;
}
const char* key_gen_get(struct key_gen *g) {
    if (g->N > 32) return NULL;
    if (g->begin) return g->begin = false, print_perm(g->A, g->k);
    if (!next_k_permutation(g->A, g->N, g->k)) return NULL;
    return print_perm(g->A, g->k);
}

/* #include <stdio.h>
static int main() {
    struct key_gen g;
    key_gen_init(9, &g);
    const char *c;
    int i = 0;
    while (c = key_gen_get(&g)) printf("%d: %s\n", ++i, c);
} */
