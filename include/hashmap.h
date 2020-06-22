/*
 * Generic hashmap manipulation functions
 *
 * Originally by Elliot C Back - http://elliottback.com/wp/hashmap-implementation-in-c/
 *
 * Modified by Pete Warden to fix a serious performance problem, support strings as keys
 * and removed thread synchronization - http://petewarden.typepad.com
 */
#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include <stddef.h>

#define MAP_MISSING -3  /* No such element */
#define MAP_FULL -2 	/* Hashmap is full */
#define MAP_OMEM -1 	/* Out of Memory */
#define MAP_OK 0 	/* OK */

struct hashmap {
	int table_size;
	int size;
    size_t itemsize;
	void *data;
};

/*
 * PFany is a pointer to a function that can take two any_t arguments
 * and return an integer. Returns status code..
 */
typedef int (*hashmap_iter_cb)(void *cl, void *item);

/*
 * Return an empty hashmap. Returns NULL if empty.
*/
extern void hashmap_init(struct hashmap *m, size_t itemsize);

/*
 * Iteratively call f with argument (cl, item) for
 * each element data in the hashmap. The function must
 * return a map status code. If it returns anything other
 * than MAP_OK the traversal is terminated. f must
 * not reenter any hashmap functions, or deadlock may arise.
 */
extern int hashmap_iterate(struct hashmap *m, hashmap_iter_cb f, void *cl);

/*
 * Add an element to the hashmap. Return MAP_OK or MAP_OMEM.
 */
extern int hashmap_put(struct hashmap *m, const char* key, void *value);

/*
 * Get an element from the hashmap. Return MAP_OK or MAP_MISSING.
 */
extern int hashmap_get(struct hashmap *m, const char* key, void **arg);

/*
 * Remove an element from the hashmap. Return MAP_OK or MAP_MISSING.
 */
extern int hashmap_remove(struct hashmap *m, const char* key);

/*
 * Free the hashmap
 */
extern void hashmap_finish(struct hashmap *m);

/*
 * Get the current size of a hashmap
 */
extern int hashmap_length(const struct hashmap *m);

#endif // __HASHMAP_H__
