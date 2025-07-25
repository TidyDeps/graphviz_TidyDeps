// basic unit tester for list.h

#ifdef NDEBUG
#error this is not intended to be compiled with assertions off
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/list.h>

DEFINE_LIST(ints, int)

// test construction and destruction, with nothing in-between
static void test_create_reset(void) {
  ints_t i = {0};
  ints_free(&i);
}

// a list should start in a known initial state
static void test_init(void) {
  ints_t i = {0};
  assert(ints_is_empty(&i));
  assert(ints_size(&i) == 0);
}

// reset of an initialized list should be OK and idempotent
static void test_init_reset(void) {
  ints_t i = {0};
  ints_free(&i);
  ints_free(&i);
  ints_free(&i);
}

// repeated append
static void test_append(void) {
  ints_t xs = {0};
  assert(ints_is_empty(&xs));

  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
    assert(ints_size(&xs) == i + 1);
  }

  ints_free(&xs);
}

static void test_get(void) {
  ints_t xs = {0};
  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  for (size_t i = 0; i < 10; ++i) {
    assert(ints_get(&xs, i) == (int)i);
  }
  for (size_t i = 9;; --i) {
    assert(ints_get(&xs, i) == (int)i);
    if (i == 0) {
      break;
    }
  }

  ints_free(&xs);
}

static void test_set(void) {
  ints_t xs = {0};
  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  for (size_t i = 0; i < 10; ++i) {
    ints_set(&xs, i, (int)(i + 1));
    assert(ints_get(&xs, i) == (int)i + 1);
  }
  for (size_t i = 9;; --i) {
    ints_set(&xs, i, (int)i - 1);
    assert(ints_get(&xs, i) == (int)i - 1);
    if (i == 0) {
      break;
    }
  }

  ints_free(&xs);
}

/// removing from an empty list should be a no-op
static void test_remove_empty(void) {
  ints_t xs = {0};
  ints_remove(&xs, 10);
  assert(ints_size(&xs) == 0);
  ints_free(&xs);
}

/// some basic removal tests
static void test_remove(void) {
  ints_t xs = {0};

  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  // remove something that does not exist
  ints_remove(&xs, 42);
  for (size_t i = 0; i < 10; ++i) {
    assert(ints_get(&xs, i) == (int)i);
  }

  // remove in the middle
  ints_remove(&xs, 4);
  assert(ints_size(&xs) == 9);
  for (size_t i = 0; i < 9; ++i) {
    if (i < 4) {
      assert(ints_get(&xs, i) == (int)i);
    } else {
      assert(ints_get(&xs, i) == (int)i + 1);
    }
  }

  // remove the first
  ints_remove(&xs, 0);
  assert(ints_size(&xs) == 8);
  for (size_t i = 0; i < 8; ++i) {
    if (i < 3) {
      assert(ints_get(&xs, i) == (int)i + 1);
    } else {
      assert(ints_get(&xs, i) == (int)i + 2);
    }
  }

  // remove the last
  ints_remove(&xs, 9);
  assert(ints_size(&xs) == 7);
  for (size_t i = 0; i < 7; ++i) {
    if (i < 3) {
      assert(ints_get(&xs, i) == (int)i + 1);
    } else {
      assert(ints_get(&xs, i) == (int)i + 2);
    }
  }

  // remove all the rest
  for (size_t i = 0; i < 7; ++i) {
    ints_remove(&xs, ints_get(&xs, 0));
  }
  assert(ints_size(&xs) == 0);

  ints_free(&xs);
}

static void test_at(void) {
  ints_t xs = {0};
  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  for (size_t i = 0; i < 10; ++i) {
    assert(ints_get(&xs, i) == *ints_at(&xs, i));
  }

  for (size_t i = 0; i < 10; ++i) {
    int *j = ints_at(&xs, i);
    *j = (int)i + 1;
    assert(ints_get(&xs, i) == (int)i + 1);
  }

  ints_free(&xs);
}

static void test_clear_empty(void) {
  ints_t xs = {0};
  ints_clear(&xs);
  assert(ints_is_empty(&xs));

  ints_free(&xs);
}

static void test_clear(void) {
  ints_t xs = {0};
  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  assert(!ints_is_empty(&xs));
  ints_clear(&xs);
  assert(ints_is_empty(&xs));

  ints_free(&xs);
}

// basic push then pop
static void test_push_one(void) {
  ints_t s = {0};
  int arbitrary = 42;
  ints_push_back(&s, arbitrary);
  assert(ints_size(&s) == 1);
  int top = ints_pop_back(&s);
  assert(top == arbitrary);
  assert(ints_is_empty(&s));
  ints_free(&s);
}

static void push_then_pop(int count) {
  ints_t s = {0};
  for (int i = 0; i < count; ++i) {
    ints_push_back(&s, i);
    assert(ints_size(&s) == (size_t)i + 1);
  }
  for (int i = count - 1;; --i) {
    assert(ints_size(&s) == (size_t)i + 1);
    int p = ints_pop_back(&s);
    assert(p == i);
    if (i == 0) {
      break;
    }
  }
  ints_free(&s);
}

// push a series of items
static void test_push_then_pop_ten(void) { push_then_pop(10); }

// push enough to cause an expansion
static void test_push_then_pop_many(void) { push_then_pop(4096); }

// interleave some push and pop operations
static void test_push_pop_interleaved(void) {
  ints_t s = {0};
  size_t size = 0;
  for (int i = 0; i < 4096; ++i) {
    if (i % 3 == 1) {
      int p = ints_pop_back(&s);
      assert(p == i - 1);
      --size;
    } else {
      ints_push_back(&s, i);
      ++size;
    }
    assert(ints_size(&s) == size);
  }
  ints_free(&s);
}

/// an int comparer
static int cmp_int(const int *a, const int *b) {
  if (*a < *b) {
    return -1;
  }
  if (*a > *b) {
    return 1;
  }
  return 0;
}

/// sort on an empty list should be a no-op
static void test_sort_empty(void) {
  ints_t xs = {0};
  ints_sort(&xs, cmp_int);
  assert(ints_size(&xs) == 0);
  ints_free(&xs);
}

static void test_sort(void) {
  ints_t xs = {0};

  // a list of ints in an arbitrary order
  const int ys[] = {4, 2, 10, 5, -42, 3};

  // setup this list and sort it
  for (size_t i = 0; i < sizeof(ys) / sizeof(ys[0]); ++i) {
    ints_append(&xs, ys[i]);
  }
  ints_sort(&xs, cmp_int);

  // we should now have a sorted version of `ys`
  assert(ints_size(&xs) == sizeof(ys) / sizeof(ys[0]));
  assert(ints_get(&xs, 0) == -42);
  assert(ints_get(&xs, 1) == 2);
  assert(ints_get(&xs, 2) == 3);
  assert(ints_get(&xs, 3) == 4);
  assert(ints_get(&xs, 4) == 5);
  assert(ints_get(&xs, 5) == 10);

  ints_free(&xs);
}

/// sorting an already sorted list should be a no-op
static void test_sort_sorted(void) {
  ints_t xs = {0};
  const int ys[] = {-42, 2, 3, 4, 5, 10};

  for (size_t i = 0; i < sizeof(ys) / sizeof(ys[0]); ++i) {
    ints_append(&xs, ys[i]);
  }
  ints_sort(&xs, cmp_int);

  for (size_t i = 0; i < sizeof(ys) / sizeof(ys[0]); ++i) {
    assert(ints_get(&xs, i) == ys[i]);
  }

  ints_free(&xs);
}

typedef struct {
  int x;
  int y;
} pair_t;

DEFINE_LIST(pairs, pair_t)

/// a pair comparer, using only the first element
static int cmp_pair(const pair_t *a, const pair_t *b) {
  if (a->x < b->x) {
    return -1;
  }
  if (a->x > b->x) {
    return 1;
  }
  return 0;
}

/// sorting a complex type should move entire values of the type together
static void test_sort_complex(void) {
  pairs_t xs = {0};

  const pair_t ys[] = {{1, 2}, {-2, 3}, {-10, 4}, {0, 7}};

  for (size_t i = 0; i < sizeof(ys) / sizeof(ys[0]); ++i) {
    pairs_append(&xs, ys[i]);
  }
  pairs_sort(&xs, cmp_pair);

  assert(pairs_size(&xs) == sizeof(ys) / sizeof(ys[0]));
  assert(pairs_get(&xs, 0).x == -10);
  assert(pairs_get(&xs, 0).y == 4);
  assert(pairs_get(&xs, 1).x == -2);
  assert(pairs_get(&xs, 1).y == 3);
  assert(pairs_get(&xs, 2).x == 0);
  assert(pairs_get(&xs, 2).y == 7);
  assert(pairs_get(&xs, 3).x == 1);
  assert(pairs_get(&xs, 3).y == 2);

  pairs_free(&xs);
}

static void test_shrink(void) {
  ints_t xs = {0};

  // to test this one we need to access the list internals
  while (ints_size(&xs) == xs.capacity) {
    ints_append(&xs, 42);
  }

  assert(xs.capacity > ints_size(&xs));
  ints_shrink_to_fit(&xs);
  assert(xs.capacity == ints_size(&xs));

  ints_free(&xs);
}

static void test_shrink_empty(void) {
  ints_t xs = {0};
  ints_shrink_to_fit(&xs);
  assert(xs.capacity == 0);
  ints_free(&xs);
}

static void test_free(void) {
  ints_t xs = {0};
  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  ints_free(&xs);
  assert(ints_size(&xs) == 0);
  assert(xs.capacity == 0);
}

static void test_push_back(void) {
  ints_t xs = {0};
  ints_t ys = {0};

  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
    ints_push_back(&ys, (int)i);
    assert(ints_size(&xs) == ints_size(&ys));
    for (size_t j = 0; j <= i; ++j) {
      assert(ints_get(&xs, j) == ints_get(&ys, j));
    }
  }

  ints_free(&ys);
  ints_free(&xs);
}

static void test_pop_back(void) {
  ints_t xs = {0};

  for (size_t i = 0; i < 10; ++i) {
    ints_push_back(&xs, (int)i);
  }
  for (size_t i = 0; i < 10; ++i) {
    assert(ints_size(&xs) == 10 - i);
    int x = ints_pop_back(&xs);
    assert(x == 10 - (int)i - 1);
  }

  for (size_t i = 0; i < 10; ++i) {
    ints_push_back(&xs, (int)i);
    (void)ints_pop_back(&xs);
    assert(ints_is_empty(&xs));
  }

  ints_free(&xs);
}

static void test_large(void) {
  ints_t xs = {0};

  for (int i = 0; i < 5000; ++i) {
    ints_append(&xs, i);
  }
  for (size_t i = 0; i < 5000; ++i) {
    assert(ints_get(&xs, i) == (int)i);
  }

  ints_free(&xs);
}

static void test_detach(void) {
  ints_t xs = {0};
  for (size_t i = 0; i < 10; ++i) {
    ints_append(&xs, (int)i);
  }

  int *ys = ints_detach(&xs);
  assert(ys != NULL);
  assert(ints_is_empty(&xs));

  for (size_t i = 0; i < 10; ++i) {
    assert(ys[i] == (int)i);
  }

  free(ys);
}

DEFINE_LIST_WITH_DTOR(strs, char *, free)

static void test_dtor(void) {

  // setup a list with a non-trivial destructor
  strs_t xs = {0};

  for (size_t i = 0; i < 10; ++i) {
    char *hello = strdup("hello");
    assert(hello != NULL);
    strs_append(&xs, hello);
  }

  for (size_t i = 0; i < 10; ++i) {
    assert(strcmp(strs_get(&xs, i), "hello") == 0);
  }

  strs_free(&xs);
}

/// test removal does not leak memory
static void test_remove_with_dtor(void) {
  strs_t xs = {0};

  char *hello = strdup("hello");
  assert(hello != NULL);

  strs_append(&xs, hello);
  strs_remove(&xs, hello);
  assert(strs_size(&xs) == 0);

  strs_free(&xs);
}

int main(void) {

#define RUN(t)                                                                 \
  do {                                                                         \
    printf("running test_%s... ", #t);                                         \
    fflush(stdout);                                                            \
    test_##t();                                                                \
    printf("OK\n");                                                            \
  } while (0)

  RUN(create_reset);
  RUN(init);
  RUN(init_reset);
  RUN(append);
  RUN(get);
  RUN(set);
  RUN(remove_empty);
  RUN(remove);
  RUN(at);
  RUN(clear_empty);
  RUN(clear);
  RUN(push_one);
  RUN(push_then_pop_ten);
  RUN(push_then_pop_many);
  RUN(push_pop_interleaved);
  RUN(sort_empty);
  RUN(sort);
  RUN(sort_sorted);
  RUN(sort_complex);
  RUN(shrink);
  RUN(shrink_empty);
  RUN(free);
  RUN(push_back);
  RUN(pop_back);
  RUN(large);
  RUN(detach);
  RUN(dtor);
  RUN(remove_with_dtor);

#undef RUN

  return EXIT_SUCCESS;
}
