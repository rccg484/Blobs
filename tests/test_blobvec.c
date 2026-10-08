#include "BlobVec.h"
#include "test_common.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

void
setUp(void) {
    test_reset_counters();
}

void
tearDown(void) {}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void
noop_destroy(BlobVecEnt* ent) {
    (void)ent;
}

static BlobVecEnt*
make_ent(const BlobAlloc* ab, const char* s) {
    return bv_ent_create(ab, s, strlen(s));
}

static int
ent_equals_str(const BlobVecEnt* e, const char* s) {
    size_t len = strlen(s);
    if (bv_ent_len(e) != len) {
        return 0;
    }

    return memcmp(bv_ent_data(e), s, len) == 0;
}

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
test_create_basic(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_size_t(0, bv_getlen(vec));
    TEST_ASSERT_EQUAL_size_t(4, bv_getcap(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_zero_init_gives_capacity_one(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 0, cmp_bytes, noop_destroy);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_EQUAL_size_t(1, bv_getcap(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_null_args(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bv_create(NULL, 4, cmp_bytes, noop_destroy));
    TEST_ASSERT_NULL(bv_create(&ab, 4, cmp_bytes, NULL));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_array_alloc_failure_frees_vec(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    test_arena_fail_on_nth_alloc(arena, 2);

    BlobVec* vec = bv_create(&ab, 8, cmp_bytes, noop_destroy);
    TEST_ASSERT_NULL(vec);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_destroy_null_is_noop(void) {
    bv_destroy(NULL);
}

static void
test_create_allows_null_cmp(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, NULL, noop_destroy);
    TEST_ASSERT_NOT_NULL(vec);

    /* Sort and search refuse without a comparator */
    TEST_ASSERT_EQUAL(BV_NPARAM, bv_sort(vec));
    TEST_ASSERT_NULL(bv_binarysearch(vec, "x", 1));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* entry lifecycle                                                     */
/* ------------------------------------------------------------------ */

static void
test_ent_create_roundtrip(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVecEnt* e     = bv_ent_create(&ab, "hello", 5);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_size_t(5, bv_ent_len(e));
    TEST_ASSERT_EQUAL_MEMORY("hello", bv_ent_data(e), 5);
    TEST_ASSERT_EQUAL_PTR(&ab, bv_ent_alloc(e));
    TEST_ASSERT_NULL(bv_ent_owner(e));

    TEST_ASSERT_EQUAL(BV_OK, bv_ent_destroy(e));
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_rejects_zero_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bv_ent_create(&ab, "x", 0));
    TEST_ASSERT_NULL(bv_ent_create_uninit(&ab, 0));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_rejects_null_args(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bv_ent_create(NULL, "x", 1));
    TEST_ASSERT_NULL(bv_ent_create(&ab, NULL, 1));
    TEST_ASSERT_NULL(bv_ent_create_uninit(NULL, 1));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_uninit_writable(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVecEnt* e     = bv_ent_create_uninit(&ab, 8);
    TEST_ASSERT_NOT_NULL(e);

    unsigned char* p = bv_ent_data_mut(e);
    memcpy(p, "abcdefgh", 8); // NOLINT
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", bv_ent_data(e), 8);

    TEST_ASSERT_EQUAL(BV_OK, bv_ent_destroy(e));
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_destroy_null_returns_ok(void) {
    TEST_ASSERT_EQUAL(BV_OK, bv_ent_destroy(NULL));
}

static void
test_ent_accessors_on_null(void) {
    TEST_ASSERT_NULL(bv_ent_alloc(NULL));
    TEST_ASSERT_NULL(bv_ent_owner(NULL));
    TEST_ASSERT_EQUAL_size_t(0, bv_ent_len(NULL));
    TEST_ASSERT_NULL(bv_ent_data(NULL));
    TEST_ASSERT_NULL(bv_ent_data_mut(NULL));
}

/* ------------------------------------------------------------------ */
/* append - ownership                                                  */
/* ------------------------------------------------------------------ */

static void
test_append_basic_sets_owner(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "one");

    TEST_ASSERT_EQUAL(BV_OK, bv_append(vec, e));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(vec));
    TEST_ASSERT_EQUAL_PTR(vec, bv_ent_owner(e));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_same_entry_twice_is_refused(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BV_OK, bv_append(vec, e));
    TEST_ASSERT_EQUAL(BV_OWNED, bv_append(vec, e));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_to_second_vector_is_refused(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    v1    = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVec*    v2    = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BV_OK, bv_append(v1, e));
    TEST_ASSERT_EQUAL(BV_OWNED, bv_append(v2, e));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(v1));
    TEST_ASSERT_EQUAL_size_t(0, bv_getlen(v2));

    bv_destroy(v1);
    bv_destroy(v2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_destroy_refuses_owned_entry(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");

    bv_append(vec, e);

    TEST_ASSERT_EQUAL(BV_OWNED, bv_ent_destroy(e));
    /* Entry still alive */
    TEST_ASSERT_TRUE(ent_equals_str(e, "x"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_null_args(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BV_NPARAM, bv_append(NULL, e));
    TEST_ASSERT_EQUAL(BV_NPARAM, bv_append(vec, NULL));

    bv_ent_destroy(e);
    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_grows_past_capacity(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 2, cmp_bytes, noop_destroy);
    for (int i = 0; i < 50; i++) {
        char buf[8];
        int  n = snprintf(buf, sizeof buf, "e%d", i);
        TEST_ASSERT_EQUAL(BV_OK, bv_append(vec, bv_ent_create(&ab, buf, (size_t)n)));
    }
    TEST_ASSERT_EQUAL_size_t(50, bv_getlen(vec));
    TEST_ASSERT_TRUE(bv_getcap(vec) >= 50);

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* appendidx - ownership                                               */
/* ------------------------------------------------------------------ */

static void
test_appendidx_front_sets_owner(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "b"));
    bv_append(vec, make_ent(&ab, "c"));

    BlobVecEnt* a = make_ent(&ab, "a");
    TEST_ASSERT_EQUAL(BV_OK, bv_appendidx(vec, a, 0));
    TEST_ASSERT_EQUAL_PTR(vec, bv_ent_owner(a));

    TEST_ASSERT_EQUAL_size_t(3, bv_getlen(vec));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 2), "c"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_refuses_owned_entry(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");
    bv_append(vec, e);

    TEST_ASSERT_EQUAL(BV_OWNED, bv_appendidx(vec, e, 0));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_out_of_range(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BV_RANGE, bv_appendidx(vec, e, 1));

    bv_ent_destroy(e);
    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* deassign                                                            */
/* ------------------------------------------------------------------ */

static void
test_deassignidx_clears_owner(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "a"));
    bv_append(vec, make_ent(&ab, "b"));
    bv_append(vec, make_ent(&ab, "c"));

    BlobVecEnt* out = bv_deassignidx(vec, 0);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(ent_equals_str(out, "a"));
    TEST_ASSERT_NULL(bv_ent_owner(out));
    TEST_ASSERT_EQUAL_size_t(2, bv_getlen(vec));

    /* After deassign, bv_ent_destroy succeeds */
    TEST_ASSERT_EQUAL(BV_OK, bv_ent_destroy(out));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignidx_can_be_reappended_elsewhere(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    v1    = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVec*    v2    = bv_create(&ab, 4, cmp_bytes, noop_destroy);

    BlobVecEnt* e     = make_ent(&ab, "moving");
    bv_append(v1, e);

    BlobVecEnt* moved = bv_deassignidx(v1, 0);
    TEST_ASSERT_EQUAL_PTR(e, moved);

    TEST_ASSERT_EQUAL(BV_OK, bv_append(v2, moved));
    TEST_ASSERT_EQUAL_PTR(v2, bv_ent_owner(moved));

    bv_destroy(v1);
    bv_destroy(v2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignidx_out_of_range(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "a"));

    TEST_ASSERT_NULL(bv_deassignidx(vec, 1));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignptr_clears_owner(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");
    bv_append(vec, e);

    BlobVecEnt* out = bv_deassignptr(vec, e);
    TEST_ASSERT_EQUAL_PTR(e, out);
    TEST_ASSERT_NULL(bv_ent_owner(out));
    TEST_ASSERT_EQUAL_size_t(0, bv_getlen(vec));

    bv_ent_destroy(out);
    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_deassignptr_rejects_entry_from_other_vec(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    v1    = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVec*    v2    = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e     = make_ent(&ab, "x");

    bv_append(v1, e);

    /* e's owner is v1, not v2. The fast-path check refuses. */
    TEST_ASSERT_NULL(bv_deassignptr(v2, e));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(v1));

    bv_destroy(v1);
    bv_destroy(v2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* capacity                                                            */
/* ------------------------------------------------------------------ */

static void
test_realloc_refuses_zero_and_below_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 8, cmp_bytes, noop_destroy);
    TEST_ASSERT_EQUAL(BV_RANGE, bv_realloc(vec, 0));

    bv_append(vec, make_ent(&ab, "a"));
    bv_append(vec, make_ent(&ab, "b"));
    TEST_ASSERT_EQUAL(BV_RANGE, bv_realloc(vec, 1));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_shrink_to_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 32, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "a"));
    bv_append(vec, make_ent(&ab, "b"));

    TEST_ASSERT_EQUAL(BV_OK, bv_realloc(vec, 2));
    TEST_ASSERT_EQUAL_size_t(2, bv_getcap(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_reserve_noop_when_sufficient(void) {
    TestArena* arena  = test_arena_create(1 << 16);
    BlobAlloc  ab     = test_arena_block(arena);

    BlobVec*   vec    = bv_create(&ab, 8, cmp_bytes, noop_destroy);
    size_t     before = test_arena_alloc_calls(arena);

    TEST_ASSERT_EQUAL(BV_OK, bv_reserve(vec, 4));
    TEST_ASSERT_EQUAL_size_t(before, test_arena_alloc_calls(arena));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* teardown - destructor                                               */
/* ------------------------------------------------------------------ */

static void
test_destroy_calls_destructor_per_entry(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, test_counting_destroy);
    bv_append(vec, make_ent(&ab, "a"));
    bv_append(vec, make_ent(&ab, "b"));
    bv_append(vec, make_ent(&ab, "c"));

    bv_destroy(vec);
    TEST_ASSERT_EQUAL_size_t(3, g_test_destroy_count);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_clear_calls_destructor_and_keeps_capacity(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 8, cmp_bytes, test_counting_destroy);
    for (int i = 0; i < 8; i++) {
        bv_append(vec, make_ent(&ab, "x"));
    }

    bv_clear(vec);
    TEST_ASSERT_EQUAL_size_t(8, g_test_destroy_count);
    TEST_ASSERT_EQUAL_size_t(0, bv_getlen(vec));
    TEST_ASSERT_EQUAL_size_t(8, bv_getcap(vec));

    /* Reusable after clear */
    bv_append(vec, make_ent(&ab, "y"));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(vec));

    bv_destroy(vec);
    TEST_ASSERT_EQUAL_size_t(9, g_test_destroy_count);

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

    BlobVec*   vec   = bv_create(&ab, 8, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "banana"));
    bv_append(vec, make_ent(&ab, "apple"));
    bv_append(vec, make_ent(&ab, "cherry"));
    bv_append(vec, make_ent(&ab, "date"));

    TEST_ASSERT_EQUAL(BV_OK, bv_sort(vec));

    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 0), "apple"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 1), "banana"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 2), "cherry"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 3), "date"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_sort_stable_with_equal_keys(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 8, cmp_first_byte_only, noop_destroy);
    bv_append(vec, make_ent(&ab, "aa"));
    bv_append(vec, make_ent(&ab, "ab"));
    bv_append(vec, make_ent(&ab, "ac"));
    bv_append(vec, make_ent(&ab, "ad"));

    TEST_ASSERT_EQUAL(BV_OK, bv_sort(vec));

    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 0), "aa"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 1), "ab"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 2), "ac"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 3), "ad"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_sort_without_comparator_fails(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, NULL, noop_destroy);
    bv_append(vec, make_ent(&ab, "x"));

    TEST_ASSERT_EQUAL(BV_NPARAM, bv_sort(vec));

    bv_destroy(vec);
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

    BlobVec*   vec   = bv_create(&ab, 8, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "apple"));
    bv_append(vec, make_ent(&ab, "banana"));
    bv_append(vec, make_ent(&ab, "cherry"));
    bv_append(vec, make_ent(&ab, "date"));
    bv_sort(vec);

    const BlobVecEnt* hit = bv_binarysearch(vec, "cherry", 6);
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_TRUE(ent_equals_str(hit, "cherry"));

    TEST_ASSERT_NULL(bv_binarysearch(vec, "aaa", 3));
    TEST_ASSERT_NULL(bv_binarysearch(vec, "zzz", 3));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_binarysearch_on_empty_and_singleton(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   empty = bv_create(&ab, 1, cmp_bytes, noop_destroy);
    TEST_ASSERT_NULL(bv_binarysearch(empty, "x", 1));

    BlobVec* single = bv_create(&ab, 1, cmp_bytes, noop_destroy);
    bv_append(single, make_ent(&ab, "only"));

    TEST_ASSERT_NOT_NULL(bv_binarysearch(single, "only", 4));
    TEST_ASSERT_NULL(bv_binarysearch(single, "other", 5));

    bv_destroy(empty);
    bv_destroy(single);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_binarysearch_without_comparator_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, NULL, noop_destroy);
    bv_append(vec, make_ent(&ab, "x"));

    TEST_ASSERT_NULL(bv_binarysearch(vec, "x", 1));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* misc                                                                */
/* ------------------------------------------------------------------ */

static void
test_create_overflowing_init_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bv_create(&ab, SIZE_MAX, cmp_bytes, noop_destroy));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_getidx_mut_is_writable(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "hello"));

    BlobVecEnt* e = bv_getidx_mut(vec, 0);
    TEST_ASSERT_NOT_NULL(e);

    unsigned char* p = bv_ent_data_mut(e);
    p[0]             = 'H';
    TEST_ASSERT_EQUAL_MEMORY("Hello", bv_ent_data(bv_getidx(vec, 0)), 5);

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_reserve_overflowing_request_returns_range(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec*   vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);

    TEST_ASSERT_EQUAL(BV_RANGE, bv_reserve(vec, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(4, bv_getcap(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_same_cap_does_not_touch_allocator(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec* vec    = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    size_t   before = test_arena_alloc_calls(arena)
                    + test_arena_free_calls(arena);

    TEST_ASSERT_EQUAL(BV_OK, bv_realloc(vec, 4));

    size_t after = test_arena_alloc_calls(arena)
                 + test_arena_free_calls(arena);
    TEST_ASSERT_EQUAL_size_t(before, after);

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_size_overflow_returns_range(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec* vec = bv_create(&ab, 4, cmp_bytes, noop_destroy);

    TEST_ASSERT_EQUAL(BV_RANGE, bv_realloc(vec, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(4, bv_getcap(vec));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_oom_leaves_vec_intact(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec* vec = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "a"));

    /* Next allocation through the block is the realloc's internal one */
    test_arena_fail_on_nth_alloc(arena, 1);

    TEST_ASSERT_EQUAL(BV_OOM, bv_realloc(vec, 16));
    TEST_ASSERT_EQUAL_size_t(4, bv_getcap(vec));
    TEST_ASSERT_EQUAL_size_t(1, bv_getlen(vec));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 0), "a"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_grows_when_full(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec* vec = bv_create(&ab, 2, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "b"));
    bv_append(vec, make_ent(&ab, "d"));
    TEST_ASSERT_EQUAL_size_t(2, bv_getlen(vec));
    TEST_ASSERT_EQUAL_size_t(2, bv_getcap(vec));

    /* Insert at front into a full vector - forces growth plus memmove */
    TEST_ASSERT_EQUAL(BV_OK, bv_appendidx(vec, make_ent(&ab, "a"), 0));

    TEST_ASSERT_TRUE(bv_getcap(vec) > 2);
    TEST_ASSERT_EQUAL_size_t(3, bv_getlen(vec));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 2), "d"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_appendidx_grows_when_full_middle_insert(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobVec* vec = bv_create(&ab, 2, cmp_bytes, noop_destroy);
    bv_append(vec, make_ent(&ab, "a"));
    bv_append(vec, make_ent(&ab, "c"));

    TEST_ASSERT_EQUAL(BV_OK, bv_appendidx(vec, make_ent(&ab, "b"), 1));

    TEST_ASSERT_TRUE(bv_getcap(vec) > 2);
    TEST_ASSERT_EQUAL_size_t(3, bv_getlen(vec));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bv_getidx(vec, 2), "c"));

    bv_destroy(vec);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_basic);
    RUN_TEST(test_create_zero_init_gives_capacity_one);
    RUN_TEST(test_create_null_args);
    RUN_TEST(test_create_array_alloc_failure_frees_vec);
    RUN_TEST(test_destroy_null_is_noop);
    RUN_TEST(test_create_allows_null_cmp);

    RUN_TEST(test_ent_create_roundtrip);
    RUN_TEST(test_ent_create_rejects_zero_len);
    RUN_TEST(test_ent_create_rejects_null_args);
    RUN_TEST(test_ent_create_uninit_writable);
    RUN_TEST(test_ent_destroy_null_returns_ok);
    RUN_TEST(test_ent_accessors_on_null);

    RUN_TEST(test_append_basic_sets_owner);
    RUN_TEST(test_append_same_entry_twice_is_refused);
    RUN_TEST(test_append_to_second_vector_is_refused);
    RUN_TEST(test_ent_destroy_refuses_owned_entry);
    RUN_TEST(test_append_null_args);
    RUN_TEST(test_append_grows_past_capacity);

    RUN_TEST(test_appendidx_front_sets_owner);
    RUN_TEST(test_appendidx_refuses_owned_entry);
    RUN_TEST(test_appendidx_out_of_range);

    RUN_TEST(test_deassignidx_clears_owner);
    RUN_TEST(test_deassignidx_can_be_reappended_elsewhere);
    RUN_TEST(test_deassignidx_out_of_range);
    RUN_TEST(test_deassignptr_clears_owner);
    RUN_TEST(test_deassignptr_rejects_entry_from_other_vec);

    RUN_TEST(test_realloc_refuses_zero_and_below_len);
    RUN_TEST(test_realloc_shrink_to_len);
    RUN_TEST(test_reserve_noop_when_sufficient);

    RUN_TEST(test_destroy_calls_destructor_per_entry);
    RUN_TEST(test_clear_calls_destructor_and_keeps_capacity);

    RUN_TEST(test_sort_ascending);
    RUN_TEST(test_sort_stable_with_equal_keys);
    RUN_TEST(test_sort_without_comparator_fails);

    RUN_TEST(test_binarysearch_hit_and_miss);
    RUN_TEST(test_binarysearch_on_empty_and_singleton);
    RUN_TEST(test_binarysearch_without_comparator_returns_null);

    // Misc
    RUN_TEST(test_create_overflowing_init_returns_null);
    RUN_TEST(test_getidx_mut_is_writable);
    RUN_TEST(test_reserve_overflowing_request_returns_range);
    RUN_TEST(test_realloc_same_cap_does_not_touch_allocator);
    RUN_TEST(test_realloc_size_overflow_returns_range);
    RUN_TEST(test_realloc_oom_leaves_vec_intact);
    RUN_TEST(test_appendidx_grows_when_full);
    RUN_TEST(test_appendidx_grows_when_full_middle_insert);

    return UNITY_END();
}