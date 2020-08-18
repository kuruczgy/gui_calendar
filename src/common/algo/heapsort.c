
#include "algo.h"

static void do_swap(unsigned char *a, unsigned char *b, size_t s) {
	for (size_t i = 0; i < s; ++i) {
		unsigned char tmp = a[i];
		a[i] = b[i];
		b[i] = tmp;
	}
}

/* based on pseudocode from https://en.wikipedia.org/wiki/Heapsort */

static int iParent(int j) { return (j - 1) / 2; }
static int iLeftChild(int j) { return j * 2 + 1; }

static void siftDown(void *a, int start, int end, size_t is,
		sort_lt lt, void *cl) {
	int root = start;

	while (iLeftChild(root) <= end) {
		int child = iLeftChild(root);
		int swap = root;

		if (lt(a + swap * is, a + child * is, cl))
			swap = child;
		if (child + 1 <= end && lt(a + swap * is, a + (child + 1) * is, cl))
			swap = child + 1;
		if (swap == root) {
			return;
		} else {
			do_swap(a + root * is, a + swap * is, is);
			root = swap;
		}
	}
}

static void heapify(void *a, int count, size_t is, sort_lt lt, void *cl) {
	int start = iParent(count - 1);

	while (start >= 0) {
		siftDown(a, start, count - 1, is, lt, cl);
		--start;
	}
}

void heapsort(void *a, int count, size_t is, sort_lt lt, void *cl) {
	heapify(a, count, is, lt, cl);

	int end = count - 1;
	while (end > 0) {
		do_swap(a + end * is, a, is);
		--end;
		siftDown(a, 0, end, is, lt, cl);
	}
}
