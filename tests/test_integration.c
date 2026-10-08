#include "BlobBox.h"
#include "BlobVec.h"
#include "test_common.h"
#include "unity.h"

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

/* ------------------------------------------------------------------ */
/* cross-allocator: entries and containers from different arenas       */
/* ------------------------------------------------------------------ */

static void
test_box_entry_from_arena_a_container_from_arena_b(void) {
    TestArena*  arena_entry = test_arena_create(1 << 16);
    TestArena*  arena_box   = test_arena_create(1 << 16);

    BlobAlloc   ab_entry    = test_arena_block(arena_entry);
    BlobAlloc   ab_box      = test_arena_block(arena_box);

    BlobBox*    box         = bb_create(&ab_box, 4);
    BlobBoxEnt* e           = bb_ent_create(&ab_entry, "hello", 5);

    TEST_ASSERT_EQUAL(BB_OK, bb_append(box, e));
    bb_destroy_all(box);

    /* If destroy_all routed the entry free through ab_box, arena_box
     * would report an invalid free and arena_entry would show a live
     * allocation. */
    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_box);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_box);
}

static void
test_vec_entry_from_arena_a_container_from_arena_b(void) {
    TestArena*  arena_entry = test_arena_create(1 << 16);
    TestArena*  arena_vec   = test_arena_create(1 << 16);

    BlobAlloc   ab_entry    = test_arena_block(arena_entry);
    BlobAlloc   ab_vec      = test_arena_block(arena_vec);

    BlobVec*    vec         = bv_create(&ab_vec, 4, cmp_bytes, noop_destroy);
    BlobVecEnt* e           = bv_ent_create(&ab_entry, "world", 5);

    TEST_ASSERT_EQUAL(BV_OK, bv_append(vec, e));
    bv_destroy(vec);

    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_vec);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_vec);
}

static void
test_vec_clear_frees_entries_to_their_own_arena(void) {
    TestArena* arena_entry = test_arena_create(1 << 16);
    TestArena* arena_vec   = test_arena_create(1 << 16);

    BlobAlloc  ab_entry    = test_arena_block(arena_entry);
    BlobAlloc  ab_vec      = test_arena_block(arena_vec);

    BlobVec*   vec         = bv_create(&ab_vec, 4, cmp_bytes, noop_destroy);
    for (int i = 0; i < 5; i++) {
        bv_append(vec, bv_ent_create(&ab_entry, "x", 1));
    }
    TEST_ASSERT_EQUAL_size_t(5, test_arena_live_count(arena_entry));

    bv_clear(vec);

    /* Entries returned to arena_entry; vec's array and struct remain */
    TEST_ASSERT_EQUAL_size_t(0, test_arena_live_count(arena_entry));

    bv_destroy(vec);
    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_vec);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_vec);
}

static void
test_many_arenas_no_interference(void) {
    TestArena* ae[3];
    TestArena* ac[3];
    BlobAlloc  be[3];
    BlobAlloc  bc[3];
    BlobBox*   boxes[3];

    for (int i = 0; i < 3; i++) {
        ae[i]    = test_arena_create(1 << 16);
        ac[i]    = test_arena_create(1 << 16);
        be[i]    = test_arena_block(ae[i]);
        bc[i]    = test_arena_block(ac[i]);
        boxes[i] = bb_create(&bc[i], 4);
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            bb_append(boxes[i], bb_ent_create(&be[i], "x", 1));
        }
    }

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_size_t(4, test_arena_live_count(ae[i]));
        bb_destroy_all(boxes[i]);
        test_arena_assert_balanced(ae[i]);
        test_arena_assert_balanced(ac[i]);
    }

    for (int i = 0; i < 3; i++) {
        test_arena_destroy(ae[i]);
        test_arena_destroy(ac[i]);
    }
}

/* ------------------------------------------------------------------ */
/* cross-container: copy-on-transfer, not pointer sharing              */
/* ------------------------------------------------------------------ */

static void
test_copy_from_box_to_vec(void) {
    /* The supported cross-container pattern: read the source entry's
     * data, create a fresh entry of the destination's type with that
     * data. The two entries are independent. */
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobBox*    box   = bb_create(&ab, 4);
    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobBoxEnt* src   = bb_ent_create(&ab, "shared data", 11);
    bb_append(box, src);

    BlobVecEnt* dst = bv_ent_create(&ab, bb_ent_data(src), bb_ent_len(src));
    TEST_ASSERT_EQUAL(BV_OK, bv_append(vec, dst));

    /* Modifying the destination does not touch the source */
    bv_ent_data_mut(dst)[0] = 'X';
    TEST_ASSERT_EQUAL_MEMORY("shared data", bb_ent_data(src), 11);
    TEST_ASSERT_EQUAL_MEMORY("Xhared data", bv_ent_data(dst), 11);

    /* Destroy either in any order; both are independent */
    bb_destroy_all(box);
    bv_destroy(vec);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_copy_from_vec_to_box(void) {
    TestArena*  arena = test_arena_create(1 << 16);
    BlobAlloc   ab    = test_arena_block(arena);

    BlobVec*    vec   = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobBox*    box   = bb_create(&ab, 4);
    BlobVecEnt* src   = bv_ent_create(&ab, "vector data", 11);
    bv_append(vec, src);

    BlobBoxEnt* dst = bb_ent_create(&ab, bv_ent_data(src), bv_ent_len(src));
    TEST_ASSERT_EQUAL(BB_OK, bb_append(box, dst));

    /* Destroying the vector does not affect the box copy */
    bv_destroy(vec);
    TEST_ASSERT_EQUAL_MEMORY("vector data", bb_ent_data(dst), 11);

    bb_destroy_all(box);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_box_holds_indices_into_vec(void) {
    /* If you want a box to act as a registry of vec entries without
     * copying data, the box holds opaque identifiers or indices, not
     * entry pointers. This is the pattern that actually composes. */
    TestArena* arena    = test_arena_create(1 << 16);
    BlobAlloc  ab       = test_arena_block(arena);

    BlobVec*   vec      = bv_create(&ab, 4, cmp_bytes, noop_destroy);
    BlobBox*   registry = bb_create(&ab, 4);

    for (int i = 0; i < 5; i++) {
        bv_append(vec, bv_ent_create(&ab, "x", 1));
        uint32_t idx = (uint32_t)(bv_getlen(vec) - 1);
        bb_append(registry, bb_ent_create(&ab, &idx, sizeof idx));
    }

    TEST_ASSERT_EQUAL_size_t(5, bv_getlen(vec));
    TEST_ASSERT_EQUAL_size_t(5, bb_getlen(registry));

    /* Recover the first entry through the registry */
    uint32_t idx0;
    memcpy(&idx0, bb_ent_data(bb_getidx(registry, 0)), sizeof idx0);
    TEST_ASSERT_EQUAL_UINT32(0, idx0);
    TEST_ASSERT_EQUAL_MEMORY("x", bv_ent_data(bv_getidx(vec, idx0)), 1);

    /* Independent teardown: registry first, then vec */
    bb_destroy_all(registry);
    bv_destroy(vec);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* failure at container level leaves entries untouched                 */
/* ------------------------------------------------------------------ */

static void
test_vec_append_oom_leaves_entry_unowned(void) {
    TestArena*  arena_entry = test_arena_create(1 << 16);
    TestArena*  arena_vec   = test_arena_create(1 << 16);

    BlobAlloc   ab_entry    = test_arena_block(arena_entry);
    BlobAlloc   ab_vec      = test_arena_block(arena_vec);

    BlobVec*    vec         = bv_create(&ab_vec, 1, cmp_bytes, noop_destroy);
    BlobVecEnt* e           = bv_ent_create(&ab_entry, "survivor", 8);

    bv_append(vec, bv_ent_create(&ab_entry, "first", 5));

    test_arena_fail_on_nth_alloc(arena_vec, 1);
    TEST_ASSERT_EQUAL(BV_OOM, bv_append(vec, e));

    /* The failed entry is not owned and still alive */
    TEST_ASSERT_NULL(bv_ent_owner(e));
    TEST_ASSERT_EQUAL_size_t(2, test_arena_live_count(arena_entry));

    bv_destroy(vec);
    bv_ent_destroy(e);
    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_vec);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_vec);
}

static void
test_box_append_oom_leaves_entry_unowned(void) {
    TestArena*  arena_entry = test_arena_create(1 << 16);
    TestArena*  arena_box   = test_arena_create(1 << 16);

    BlobAlloc   ab_entry    = test_arena_block(arena_entry);
    BlobAlloc   ab_box      = test_arena_block(arena_box);

    BlobBox*    box         = bb_create(&ab_box, 1);
    BlobBoxEnt* e           = bb_ent_create(&ab_entry, "survivor", 8);

    bb_append(box, bb_ent_create(&ab_entry, "first", 5));

    test_arena_fail_on_nth_alloc(arena_box, 1);
    TEST_ASSERT_EQUAL(BB_OOM, bb_append(box, e));

    TEST_ASSERT_EQUAL_size_t(2, test_arena_live_count(arena_entry));

    bb_destroy_all(box);
    bb_ent_destroy(e);
    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_box);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_box);
}

/* ------------------------------------------------------------------ */
/* realistic workflow                                                  */
/* ------------------------------------------------------------------ */

static void
test_sort_and_search_across_arenas(void) {
    TestArena*  arena_entry = test_arena_create(1 << 16);
    TestArena*  arena_vec   = test_arena_create(1 << 16);

    BlobAlloc   ab_entry    = test_arena_block(arena_entry);
    BlobAlloc   ab_vec      = test_arena_block(arena_vec);

    BlobVec*    vec         = bv_create(&ab_vec, 4, cmp_bytes, noop_destroy);

    const char* words[]     = { "banana", "apple", "cherry", "date", "elderberry" };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        bv_append(vec, bv_ent_create(&ab_entry, words[i], strlen(words[i])));
    }

    TEST_ASSERT_EQUAL(BV_OK, bv_sort(vec));
    TEST_ASSERT_EQUAL_MEMORY("apple", bv_ent_data(bv_getidx(vec, 0)), 5);
    TEST_ASSERT_EQUAL_MEMORY("banana", bv_ent_data(bv_getidx(vec, 1)), 6);

    const BlobVecEnt* hit = bv_binarysearch(vec, "cherry", 6);
    TEST_ASSERT_NOT_NULL(hit);

    bv_destroy(vec);
    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_vec);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_vec);
}

static void
test_destructor_runs_before_free(void) {
    TestArena* arena_entry = test_arena_create(1 << 16);
    TestArena* arena_vec   = test_arena_create(1 << 16);

    BlobAlloc  ab_entry    = test_arena_block(arena_entry);
    BlobAlloc  ab_vec      = test_arena_block(arena_vec);

    BlobVec*   vec         = bv_create(&ab_vec, 4, cmp_bytes, test_counting_destroy);
    for (int i = 0; i < 3; i++) {
        bv_append(vec, bv_ent_create(&ab_entry, "abc", 3));
    }

    bv_destroy(vec);
    TEST_ASSERT_EQUAL_size_t(3, g_test_destroy_count);
    test_arena_assert_balanced(arena_entry);
    test_arena_assert_balanced(arena_vec);
    test_arena_destroy(arena_entry);
    test_arena_destroy(arena_vec);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_box_entry_from_arena_a_container_from_arena_b);
    RUN_TEST(test_vec_entry_from_arena_a_container_from_arena_b);
    RUN_TEST(test_vec_clear_frees_entries_to_their_own_arena);
    RUN_TEST(test_many_arenas_no_interference);

    RUN_TEST(test_copy_from_box_to_vec);
    RUN_TEST(test_copy_from_vec_to_box);
    RUN_TEST(test_box_holds_indices_into_vec);

    RUN_TEST(test_vec_append_oom_leaves_entry_unowned);
    RUN_TEST(test_box_append_oom_leaves_entry_unowned);

    RUN_TEST(test_sort_and_search_across_arenas);
    RUN_TEST(test_destructor_runs_before_free);

    return UNITY_END();
}