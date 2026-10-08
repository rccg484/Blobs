#include "BlobBox.h"
#include "test_common.h"
#include "unity.h"


#include <string.h>

void
setUp(void) {}
void
tearDown(void) {}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static BlobBoxEnt*
make_ent(const BlobAlloc* ab, const char* s) {
    return bb_ent_create(ab, s, strlen(s));
}

static int
ent_equals_str(const BlobBoxEnt* e, const char* s) {
    size_t len = strlen(s);
    if (bb_ent_len(e) != len) {
        return 0;
    }
    
    return memcmp(bb_ent_data(e), s, len) == 0;
}

/* Comparator that only looks at the first byte, to test stability. */
static int
cmp_first_byte_only(const void* a, const void* b, size_t alen, size_t blen) {
    (void)alen;
    (void)blen;
    unsigned char ca = ((const unsigned char*)a)[0];
    unsigned char cb = ((const unsigned char*)b)[0];
    return (int)ca - (int)cb;
}

/* ------------------------------------------------------------------ */
/* construction / destruction                                          */
/* ------------------------------------------------------------------ */

static void
test_create_zero_init_gives_capacity_one(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 0);
    TEST_ASSERT_NOT_NULL(box);
    TEST_ASSERT_EQUAL_size_t(0, bb_getlen(box));
    TEST_ASSERT_EQUAL_size_t(1, bb_getcap(box));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_with_capacity(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 8);
    TEST_ASSERT_NOT_NULL(box);
    TEST_ASSERT_EQUAL_size_t(0, bb_getlen(box));
    TEST_ASSERT_EQUAL_size_t(8, bb_getcap(box));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_null_alloc_returns_null(void) {
    TEST_ASSERT_NULL(bb_create(NULL, 8));
}

static void
test_create_array_alloc_failure_frees_box(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    /* 1st alloc (the BlobBox struct) succeeds, 2nd (the array) fails. */
    test_arena_fail_on_nth_alloc(arena, 2);

    BlobBox* box = bb_create(&ab, 8);
    TEST_ASSERT_NULL(box);

    /* If bb_create leaked the box struct on the failure path,
     * assert_balanced catches it here. */
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_destroy_null_is_noop(void) {
    bb_destroy(NULL); /* must not crash */
}

/* ------------------------------------------------------------------ */
/* entry lifecycle                                                     */
/* ------------------------------------------------------------------ */

static void
test_ent_create_roundtrip(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBoxEnt* e     = bb_ent_create(&ab, "hello", 5);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_size_t(5, bb_ent_len(e));
    TEST_ASSERT_EQUAL_MEMORY("hello", bb_ent_data(e), 5);
    TEST_ASSERT_EQUAL_PTR(&ab, bb_ent_alloc(e));

    bb_ent_destroy(e);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_rejects_zero_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bb_ent_create(&ab, "x", 0));
    TEST_ASSERT_NULL(bb_ent_create_uninit(&ab, 0));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_rejects_null_args(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bb_ent_create(NULL, "x", 1));
    TEST_ASSERT_NULL(bb_ent_create(&ab, NULL, 1));
    TEST_ASSERT_NULL(bb_ent_create_uninit(NULL, 1));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_uninit_writable(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBoxEnt* e     = bb_ent_create_uninit(&ab, 8);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_size_t(8, bb_ent_len(e));

    unsigned char* p = bb_ent_data_mut(e);
    TEST_ASSERT_NOT_NULL(p);
    memcpy(p, "abcdefgh", 8);
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", bb_ent_data(e), 8);

    bb_ent_destroy(e);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_destroy_null_is_noop(void) {
    bb_ent_destroy(NULL);
}

static void
test_ent_accessors_on_null_return_defaults(void) {
    TEST_ASSERT_NULL(bb_ent_alloc(NULL));
    TEST_ASSERT_EQUAL_size_t(0, bb_ent_len(NULL));
    TEST_ASSERT_NULL(bb_ent_data(NULL));
    TEST_ASSERT_NULL(bb_ent_data_mut(NULL));
}

/* ------------------------------------------------------------------ */
/* append / insert                                                     */
/* ------------------------------------------------------------------ */

static void
test_append_basic(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBox*    box   = bb_create(&ab, 4);
    BlobBoxEnt* e     = make_ent(&ab, "one");

    TEST_ASSERT_EQUAL(BB_OK, bb_append(box, e));
    TEST_ASSERT_EQUAL_size_t(1, bb_getlen(box));
    TEST_ASSERT_EQUAL_PTR(e, bb_getidx(box, 0));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_null_args(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBox*    box   = bb_create(&ab, 4);
    BlobBoxEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BB_NPARAM, bb_append(NULL, e));
    TEST_ASSERT_EQUAL(BB_NPARAM, bb_append(box, NULL));

    bb_destroy_all(box);
    bb_ent_destroy(e);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_grows_past_capacity(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 2);
    for (int i = 0; i < 50; i++) {
        char buf[8];
        int  n = snprintf(buf, sizeof buf, "e%d", i);
        TEST_ASSERT_EQUAL(BB_OK, bb_append(box, bb_ent_create(&ab, buf, (size_t)n)));
    }
    TEST_ASSERT_EQUAL_size_t(50, bb_getlen(box));
    TEST_ASSERT_TRUE(bb_getcap(box) >= 50);

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_front(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "b"));
    bb_append(box, make_ent(&ab, "c"));
    bb_appendidx(box, make_ent(&ab, "a"), 0);

    TEST_ASSERT_EQUAL_size_t(3, bb_getlen(box));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 2), "c"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_end_is_append(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));
    bb_appendidx(box, make_ent(&ab, "b"), bb_getlen(box));

    TEST_ASSERT_EQUAL_size_t(2, bb_getlen(box));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "b"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_out_of_range(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBox*    box   = bb_create(&ab, 4);
    BlobBoxEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BB_RANGE, bb_appendidx(box, e, 1));
    TEST_ASSERT_EQUAL_size_t(0, bb_getlen(box));

    bb_ent_destroy(e);
    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* deassign                                                            */
/* ------------------------------------------------------------------ */

static void
test_deassignidx_front(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));
    bb_append(box, make_ent(&ab, "b"));
    bb_append(box, make_ent(&ab, "c"));

    BlobBoxEnt* out = bb_deassignidx(box, 0);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(ent_equals_str(out, "a"));
    TEST_ASSERT_EQUAL_size_t(2, bb_getlen(box));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "c"));

    bb_ent_destroy(out);
    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignidx_last(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));
    bb_append(box, make_ent(&ab, "b"));

    BlobBoxEnt* out = bb_deassignidx(box, 1);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(ent_equals_str(out, "b"));
    TEST_ASSERT_EQUAL_size_t(1, bb_getlen(box));

    bb_ent_destroy(out);
    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignidx_out_of_range_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));

    TEST_ASSERT_NULL(bb_deassignidx(box, 1));
    TEST_ASSERT_EQUAL_size_t(1, bb_getlen(box));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignptr_first_occurrence(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBox*    box   = bb_create(&ab, 4);
    BlobBoxEnt* e     = make_ent(&ab, "same");

    bb_append(box, e);
    bb_append(box, make_ent(&ab, "mid"));
    bb_append(box, e); /* same pointer, second time */

    BlobBoxEnt* out = bb_deassignptr(box, e);
    TEST_ASSERT_EQUAL_PTR(e, out);
    TEST_ASSERT_EQUAL_size_t(2, bb_getlen(box));

    /* Second occurrence still present at the tail */
    TEST_ASSERT_EQUAL_PTR(e, bb_getidx(box, 1));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignptr_not_present(void) {
    TestArena*  arena   = test_arena_create(1 << 16);
    BlobAlloc   ab      = test_arena_block(arena);

    BlobBox*    box     = bb_create(&ab, 4);
    BlobBoxEnt* inside  = make_ent(&ab, "in");
    BlobBoxEnt* outside = make_ent(&ab, "out");
    bb_append(box, inside);

    TEST_ASSERT_NULL(bb_deassignptr(box, outside));
    TEST_ASSERT_EQUAL_size_t(1, bb_getlen(box));

    bb_ent_destroy(outside);
    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* capacity                                                            */
/* ------------------------------------------------------------------ */

static void
test_reserve_noop_when_sufficient(void) {
    TestArena* arena  = test_arena_create(1 << 16);
    BlobAlloc  ab     = test_arena_block(arena);

    BlobBox*   box    = bb_create(&ab, 8);
    size_t     before = test_arena_alloc_calls(arena);

    TEST_ASSERT_EQUAL(BB_OK, bb_reserve(box, 4));
    TEST_ASSERT_EQUAL_size_t(before, test_arena_alloc_calls(arena));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_refuses_zero(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    TEST_ASSERT_EQUAL(BB_RANGE, bb_realloc(box, 0));
    TEST_ASSERT_EQUAL_size_t(4, bb_getcap(box));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_refuses_below_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));
    bb_append(box, make_ent(&ab, "b"));
    bb_append(box, make_ent(&ab, "c"));

    TEST_ASSERT_EQUAL(BB_RANGE, bb_realloc(box, 2));
    TEST_ASSERT_EQUAL_size_t(4, bb_getcap(box));
    TEST_ASSERT_EQUAL_size_t(3, bb_getlen(box));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_shrink_to_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 32);
    for (int i = 0; i < 3; i++) {
        bb_append(box, make_ent(&ab, "x"));
    }

    TEST_ASSERT_EQUAL(BB_OK, bb_realloc(box, 3));
    TEST_ASSERT_EQUAL_size_t(3, bb_getcap(box));
    TEST_ASSERT_EQUAL_size_t(3, bb_getlen(box));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* teardown                                                            */
/* ------------------------------------------------------------------ */

static void
test_clear_all_keeps_capacity(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 8);
    for (int i = 0; i < 8; i++) {
        bb_append(box, make_ent(&ab, "x"));
    }

    bb_clear_all(box);
    TEST_ASSERT_EQUAL_size_t(0, bb_getlen(box));
    TEST_ASSERT_EQUAL_size_t(8, bb_getcap(box));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_destroy_all_frees_entries(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));
    bb_append(box, make_ent(&ab, "b"));
    bb_append(box, make_ent(&ab, "c"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_shared_entry_survives_destroy_of_each_box(void) {
    TestArena*  arena  = test_arena_create(1 << 16);
    BlobAlloc   ab     = test_arena_block(arena);

    BlobBoxEnt* shared = make_ent(&ab, "shared");
    BlobBox*    box1   = bb_create(&ab, 2);
    BlobBox*    box2   = bb_create(&ab, 2);

    bb_append(box1, shared);
    bb_append(box2, shared);

    /* Non-owning destroy: entry must survive both */
    bb_destroy(box1);
    TEST_ASSERT_TRUE(ent_equals_str(shared, "shared"));

    bb_destroy(box2);
    TEST_ASSERT_TRUE(ent_equals_str(shared, "shared"));

    bb_ent_destroy(shared);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* sort                                                                */
/* ------------------------------------------------------------------ */

static void
test_sort_ascending(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 8);
    bb_append(box, make_ent(&ab, "banana"));
    bb_append(box, make_ent(&ab, "apple"));
    bb_append(box, make_ent(&ab, "cherry"));
    bb_append(box, make_ent(&ab, "date"));

    TEST_ASSERT_EQUAL(BB_OK, bb_sort(box, cmp_bytes));

    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "apple"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "banana"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 2), "cherry"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 3), "date"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_sort_stable_with_equal_keys(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    /* All start with 'a'. cmp_first_byte_only treats them as equal;
     * stable sort must preserve insertion order. */
    BlobBox*   box   = bb_create(&ab, 8);
    bb_append(box, make_ent(&ab, "aa"));
    bb_append(box, make_ent(&ab, "ab"));
    bb_append(box, make_ent(&ab, "ac"));
    bb_append(box, make_ent(&ab, "ad"));

    TEST_ASSERT_EQUAL(BB_OK, bb_sort(box, cmp_first_byte_only));

    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "aa"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "ab"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 2), "ac"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 3), "ad"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_sort_empty_and_singleton_no_op(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   empty = bb_create(&ab, 1);
    TEST_ASSERT_EQUAL(BB_OK, bb_sort(empty, cmp_bytes));

    BlobBox* single = bb_create(&ab, 1);
    bb_append(single, make_ent(&ab, "only"));
    TEST_ASSERT_EQUAL(BB_OK, bb_sort(single, cmp_bytes));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(single, 0), "only"));

    bb_destroy_all(single);
    bb_destroy(empty);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_sort_null_cmp(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "x"));

    TEST_ASSERT_EQUAL(BB_NPARAM, bb_sort(box, NULL));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* search                                                              */
/* ------------------------------------------------------------------ */

static void
test_binarysearch_hit_and_miss(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 8);
    bb_append(box, make_ent(&ab, "apple"));
    bb_append(box, make_ent(&ab, "banana"));
    bb_append(box, make_ent(&ab, "cherry"));
    bb_append(box, make_ent(&ab, "date"));
    bb_sort(box, cmp_bytes);

    const BlobBoxEnt* hit = bb_binarysearch(box, "cherry", 6, cmp_bytes);
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_TRUE(ent_equals_str(hit, "cherry"));

    /* Miss below */
    TEST_ASSERT_NULL(bb_binarysearch(box, "aaa", 3, cmp_bytes));
    /* Miss above */
    TEST_ASSERT_NULL(bb_binarysearch(box, "zzz", 3, cmp_bytes));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_binarysearch_on_empty_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 1);
    TEST_ASSERT_NULL(bb_binarysearch(box, "x", 1, cmp_bytes));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_binarysearch_on_singleton(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 1);
    bb_append(box, make_ent(&ab, "only"));

    TEST_ASSERT_NOT_NULL(bb_binarysearch(box, "only", 4, cmp_bytes));
    TEST_ASSERT_NULL(bb_binarysearch(box, "other", 5, cmp_bytes));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* misc                                                                */
/* ------------------------------------------------------------------ */

static void
test_getidx_mut_is_writable(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "hello"));

    BlobBoxEnt* e = bb_getidx_mut(box, 0);
    TEST_ASSERT_NOT_NULL(e);

    unsigned char* p = bb_ent_data_mut(e);
    p[0]             = 'H';
    TEST_ASSERT_EQUAL_MEMORY("Hello", bb_ent_data(bb_getidx(box, 0)), 5);

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_same_cap_does_not_touch_allocator(void) {
    TestArena* arena  = test_arena_create(1 << 16);
    BlobAlloc  ab     = test_arena_block(arena);

    BlobBox*   box    = bb_create(&ab, 4);
    size_t     before = test_arena_alloc_calls(arena) + test_arena_free_calls(arena);

    TEST_ASSERT_EQUAL(BB_OK, bb_realloc(box, 4));

    size_t after = test_arena_alloc_calls(arena) + test_arena_free_calls(arena);
    TEST_ASSERT_EQUAL_size_t(before, after);

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_size_overflow_returns_range(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    TEST_ASSERT_EQUAL(BB_RANGE, bb_realloc(box, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(4, bb_getcap(box));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_oom_leaves_box_intact(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);
    bb_append(box, make_ent(&ab, "a"));

    /* Next allocation through the block is the reserve's realloc */
    test_arena_fail_on_nth_alloc(arena, 1);

    TEST_ASSERT_EQUAL(BB_OOM, bb_realloc(box, 16));
    TEST_ASSERT_EQUAL_size_t(4, bb_getcap(box));
    TEST_ASSERT_EQUAL_size_t(1, bb_getlen(box));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "a"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_reserve_overflowing_request_returns_range(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 4);

    TEST_ASSERT_EQUAL(BB_RANGE, bb_reserve(box, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(4, bb_getcap(box));

    bb_destroy(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_grows_when_full(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    /* Capacity 2, filled. The next insert must grow. */
    BlobBox*   box   = bb_create(&ab, 2);
    bb_append(box, make_ent(&ab, "b"));
    bb_append(box, make_ent(&ab, "d"));
    TEST_ASSERT_EQUAL_size_t(2, bb_getlen(box));
    TEST_ASSERT_EQUAL_size_t(2, bb_getcap(box));

    /* Insert at front - growth plus memmove of two elements */
    TEST_ASSERT_EQUAL(BB_OK, bb_appendidx(box, make_ent(&ab, "a"), 0));

    TEST_ASSERT_TRUE(bb_getcap(box) > 2);
    TEST_ASSERT_EQUAL_size_t(3, bb_getlen(box));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 2), "d"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_grows_when_full_middle_insert(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobBox*   box   = bb_create(&ab, 2);
    bb_append(box, make_ent(&ab, "a"));
    bb_append(box, make_ent(&ab, "c"));

    TEST_ASSERT_EQUAL(BB_OK, bb_appendidx(box, make_ent(&ab, "b"), 1));

    TEST_ASSERT_TRUE(bb_getcap(box) > 2);
    TEST_ASSERT_EQUAL_size_t(3, bb_getlen(box));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bb_getidx(box, 2), "c"));

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_zero_init_gives_capacity_one);
    RUN_TEST(test_create_with_capacity);
    RUN_TEST(test_create_null_alloc_returns_null);
    RUN_TEST(test_create_array_alloc_failure_frees_box);
    RUN_TEST(test_destroy_null_is_noop);

    RUN_TEST(test_ent_create_roundtrip);
    RUN_TEST(test_ent_create_rejects_zero_len);
    RUN_TEST(test_ent_create_rejects_null_args);
    RUN_TEST(test_ent_create_uninit_writable);
    RUN_TEST(test_ent_destroy_null_is_noop);
    RUN_TEST(test_ent_accessors_on_null_return_defaults);

    RUN_TEST(test_append_basic);
    RUN_TEST(test_append_null_args);
    RUN_TEST(test_append_grows_past_capacity);
    RUN_TEST(test_appendidx_front);
    RUN_TEST(test_appendidx_end_is_append);
    RUN_TEST(test_appendidx_out_of_range);

    RUN_TEST(test_deassignidx_front);
    RUN_TEST(test_deassignidx_last);
    RUN_TEST(test_deassignidx_out_of_range_returns_null);
    RUN_TEST(test_deassignptr_first_occurrence);
    RUN_TEST(test_deassignptr_not_present);

    RUN_TEST(test_reserve_noop_when_sufficient);
    RUN_TEST(test_realloc_refuses_zero);
    RUN_TEST(test_realloc_refuses_below_len);
    RUN_TEST(test_realloc_shrink_to_len);

    RUN_TEST(test_clear_all_keeps_capacity);
    RUN_TEST(test_destroy_all_frees_entries);
    RUN_TEST(test_shared_entry_survives_destroy_of_each_box);

    RUN_TEST(test_sort_ascending);
    RUN_TEST(test_sort_stable_with_equal_keys);
    RUN_TEST(test_sort_empty_and_singleton_no_op);
    RUN_TEST(test_sort_null_cmp);

    RUN_TEST(test_binarysearch_hit_and_miss);
    RUN_TEST(test_binarysearch_on_empty_returns_null);
    RUN_TEST(test_binarysearch_on_singleton);

    // Misc
    RUN_TEST(test_getidx_mut_is_writable);
    RUN_TEST(test_realloc_same_cap_does_not_touch_allocator);
    RUN_TEST(test_realloc_size_overflow_returns_range);
    RUN_TEST(test_realloc_oom_leaves_box_intact);
    RUN_TEST(test_reserve_overflowing_request_returns_range);
    RUN_TEST(test_appendidx_grows_when_full);
    RUN_TEST(test_appendidx_grows_when_full_middle_insert);

    return UNITY_END();
}