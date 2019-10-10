#include "algo.h"

/* Helper functions */
static void swap(int* elements, size_t a, size_t b) {
  int tmp = elements[a]; elements[a] = elements[b]; elements[b] = tmp;
}
static void flip(int* elements, size_t lo, size_t hi) {
  for (; lo + 1 < hi; ++lo, --hi) swap(elements, lo, hi - 1);
}

/* Given an array of n elements, finds the next permutation in
 * lexicographical order with a different k-prefix; in effect, it
 * generates all k-permutations of the array.
 * It is required that the suffix be sorted in ascending order. This
 * invariant will be maintained by the function.
 * Before the first call, the array must be sorted in ascending order.
 * Returns true unless the input is the last k-permutation.
 * From: https://stackoverflow.com/a/51292710
 */
int next_k_permutation(int* elements, size_t n, size_t k) {
  // Find the rightmost element which is strictly less than some element to its
  // right.
  int tailmax = elements[n - 1];
  size_t tail = k;
  while (tail && elements[tail - 1] >= tailmax)
    tailmax = elements[--tail];
  // If no pivot was found, the given permutation is the last one.
  if (tail) {
    size_t swap_in;
    int pivot = elements[tail - 1];
    // Find the smallest element strictly greater than the pivot, either
    // by searching forward from the pivot or backwards from the end.
    if (pivot >= elements[n - 1]) {
      for (swap_in = tail; swap_in + 1 < k && elements[swap_in + 1] > pivot; ++swap_in) {}
    } else {
      for (swap_in = n - 1; swap_in > k && elements[swap_in - 1] > pivot; --swap_in) {}
    }
    // Swap the pivots
    elements[tail - 1] = elements[swap_in];
    elements[swap_in] = pivot;
    // Flip the tail.
    flip(elements, k, n);
    flip(elements, tail, n);
  }
  return tail;
}
