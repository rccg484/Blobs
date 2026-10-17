#include "BlobList.h"
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
noop_destroy(BlobListEnt* ent) {
    (void)ent;
}

static BlobListEnt*
make_ent(const BlobAlloc* ab, const char* s) {
    return bl_ent_create(ab, s, strlen(s));
}

static int
ent_equals_str(const BlobListEnt* e, const char* s) {
    size_t len = strlen(s);
    if (bl_ent_len(e) != len) {
        return 0;
    }
    return memcmp(bl_ent_data(e), s, len) == 0;
}

/* ------------------------------------------------------------------ */
/* construction / destruction                                          */
/* ------------------------------------------------------------------ */

static void
test_create_empty(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL_size_t(0, bl_getlen(list));
    TEST_ASSERT_NULL(bl_first(list));
    TEST_ASSERT_NULL(bl_last(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_null_args(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bl_create(NULL, noop_destroy));
    TEST_ASSERT_NULL(bl_create(&ab, NULL));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_create_oom_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    test_arena_fail_on_nth_alloc(arena, 1);

    BlobList* list = bl_create(&ab, noop_destroy);
    TEST_ASSERT_NULL(list);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_destroy_null_is_noop(void) {
    bl_destroy(NULL);
}

/* ------------------------------------------------------------------ */
/* entry lifecycle                                                     */
/* ------------------------------------------------------------------ */

static void
test_ent_create_roundtrip(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobListEnt* e     = bl_ent_create(&ab, "hello", 5);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_size_t(5, bl_ent_len(e));
    TEST_ASSERT_EQUAL_MEMORY("hello", bl_ent_data(e), 5);
    TEST_ASSERT_EQUAL_PTR(&ab, bl_ent_alloc(e));
    TEST_ASSERT_NULL(bl_ent_owner(e));
    TEST_ASSERT_NULL(bl_next(e));
    TEST_ASSERT_NULL(bl_prev(e));

    TEST_ASSERT_EQUAL(BL_OK, bl_ent_destroy(e));
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_rejects_zero_len(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bl_ent_create(&ab, "x", 0));
    TEST_ASSERT_NULL(bl_ent_create_uninit(&ab, 0));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_rejects_null_args(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    TEST_ASSERT_NULL(bl_ent_create(NULL, "x", 1));
    TEST_ASSERT_NULL(bl_ent_create(&ab, NULL, 1));
    TEST_ASSERT_NULL(bl_ent_create_uninit(NULL, 1));

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_create_uninit_writable(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobListEnt* e     = bl_ent_create_uninit(&ab, 8);
    TEST_ASSERT_NOT_NULL(e);

    unsigned char* p = bl_ent_data_mut(e);
    TEST_ASSERT_NOT_NULL(p);
    memcpy(p, "abcdefgh", 8);
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", bl_ent_data(e), 8);

    bl_ent_destroy(e);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_destroy_null_returns_ok(void) {
    TEST_ASSERT_EQUAL(BL_OK, bl_ent_destroy(NULL));
}

static void
test_ent_accessors_on_null(void) {
    TEST_ASSERT_NULL(bl_ent_alloc(NULL));
    TEST_ASSERT_NULL(bl_ent_owner(NULL));
    TEST_ASSERT_EQUAL_size_t(0, bl_ent_len(NULL));
    TEST_ASSERT_NULL(bl_ent_data(NULL));
    TEST_ASSERT_NULL(bl_ent_data_mut(NULL));
}

/* ------------------------------------------------------------------ */
/* append - ownership                                                  */
/* ------------------------------------------------------------------ */

static void
test_append_basic_sets_owner(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "one");

    TEST_ASSERT_EQUAL(BL_OK, bl_append(list, e));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(list));
    TEST_ASSERT_EQUAL_PTR(list, bl_ent_owner(e));
    TEST_ASSERT_EQUAL_PTR(e, bl_first(list));
    TEST_ASSERT_EQUAL_PTR(e, bl_last(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_same_entry_twice_is_refused(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BL_OK, bl_append(list, e));
    TEST_ASSERT_EQUAL(BL_OWNED, bl_append(list, e));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_to_second_list_is_refused(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    l1    = bl_create(&ab, noop_destroy);
    BlobList*    l2    = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BL_OK, bl_append(l1, e));
    TEST_ASSERT_EQUAL(BL_OWNED, bl_append(l2, e));

    bl_destroy(l1);
    bl_destroy(l2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_ent_destroy_refuses_owned_entry(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "x");

    bl_append(list, e);
    TEST_ASSERT_EQUAL(BL_OWNED, bl_ent_destroy(e));
    TEST_ASSERT_TRUE(ent_equals_str(e, "x"));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_null_args(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "x");

    TEST_ASSERT_EQUAL(BL_NPARAM, bl_append(NULL, e));
    TEST_ASSERT_EQUAL(BL_NPARAM, bl_append(list, NULL));

    bl_ent_destroy(e);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* append - edge cases                                                 */
/* ------------------------------------------------------------------ */

static void
test_append_into_empty(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "a");

    bl_append(list, e);
    TEST_ASSERT_EQUAL_PTR(e, bl_first(list));
    TEST_ASSERT_EQUAL_PTR(e, bl_last(list));
    TEST_ASSERT_NULL(bl_prev(e));
    TEST_ASSERT_NULL(bl_next(e));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_append_to_nonempty(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");

    bl_append(list, a);
    bl_append(list, b);

    TEST_ASSERT_EQUAL_PTR(a, bl_first(list));
    TEST_ASSERT_EQUAL_PTR(b, bl_last(list));
    TEST_ASSERT_EQUAL_PTR(b, bl_next(a));
    TEST_ASSERT_EQUAL_PTR(a, bl_prev(b));
    TEST_ASSERT_NULL(bl_next(b));
    TEST_ASSERT_NULL(bl_prev(a));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* prepend - edge cases                                                */
/* ------------------------------------------------------------------ */

static void
test_prepend_into_empty(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "a");

    bl_prepend(list, e);
    TEST_ASSERT_EQUAL_PTR(e, bl_first(list));
    TEST_ASSERT_EQUAL_PTR(e, bl_last(list));
    TEST_ASSERT_NULL(bl_prev(e));
    TEST_ASSERT_NULL(bl_next(e));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_prepend_to_nonempty(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");

    bl_append(list, a);
    bl_prepend(list, b);

    TEST_ASSERT_EQUAL_PTR(b, bl_first(list));
    TEST_ASSERT_EQUAL_PTR(a, bl_last(list));
    TEST_ASSERT_EQUAL_PTR(a, bl_next(b));
    TEST_ASSERT_EQUAL_PTR(b, bl_prev(a));
    TEST_ASSERT_NULL(bl_prev(b));
    TEST_ASSERT_NULL(bl_next(a));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* insert_before                                                       */
/* ------------------------------------------------------------------ */

static void
test_insert_before_head(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* c     = make_ent(&ab, "c");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(list, a);
    bl_append(list, b);
    bl_append(list, c);

    TEST_ASSERT_EQUAL(BL_OK, bl_insert_before(list, a, x));
    TEST_ASSERT_EQUAL_PTR(x, bl_first(list));
    TEST_ASSERT_EQUAL_PTR(a, bl_next(x));
    TEST_ASSERT_EQUAL_PTR(x, bl_prev(a));
    TEST_ASSERT_NULL(bl_prev(x));
    TEST_ASSERT_EQUAL_size_t(4, bl_getlen(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_insert_before_middle(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* c     = make_ent(&ab, "c");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(list, a);
    bl_append(list, b);
    bl_append(list, c);

    TEST_ASSERT_EQUAL(BL_OK, bl_insert_before(list, b, x));
    TEST_ASSERT_EQUAL_PTR(a, bl_prev(x));
    TEST_ASSERT_EQUAL_PTR(b, bl_next(x));
    TEST_ASSERT_EQUAL_PTR(x, bl_next(a));
    TEST_ASSERT_EQUAL_PTR(x, bl_prev(b));
    TEST_ASSERT_EQUAL_size_t(4, bl_getlen(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_insert_before_tail(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(list, a);
    bl_append(list, b);

    TEST_ASSERT_EQUAL(BL_OK, bl_insert_before(list, b, x));
    TEST_ASSERT_EQUAL_PTR(x, bl_prev(b));
    TEST_ASSERT_EQUAL_PTR(a, bl_prev(x));
    TEST_ASSERT_EQUAL_PTR(x, bl_next(a));
    TEST_ASSERT_EQUAL_PTR(b, bl_last(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_insert_before_wrong_list(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    l1    = bl_create(&ab, noop_destroy);
    BlobList*    l2    = bl_create(&ab, noop_destroy);
    BlobListEnt* e1    = make_ent(&ab, "e1");
    BlobListEnt* e2    = make_ent(&ab, "e2");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(l1, e1);
    bl_append(l2, e2);

    TEST_ASSERT_EQUAL(BL_OWNED, bl_insert_before(l1, e2, x));

    bl_ent_destroy(x);
    bl_destroy(l1);
    bl_destroy(l2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_insert_before_owned_ent_refused(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");

    bl_append(list, a);
    bl_append(list, b);

    /* b is already owned; refuse */
    TEST_ASSERT_EQUAL(BL_OWNED, bl_insert_before(list, a, b));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* insert_after                                                        */
/* ------------------------------------------------------------------ */

static void
test_insert_after_head(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(list, a);
    bl_append(list, b);

    TEST_ASSERT_EQUAL(BL_OK, bl_insert_after(list, a, x));
    TEST_ASSERT_EQUAL_PTR(x, bl_next(a));
    TEST_ASSERT_EQUAL_PTR(b, bl_next(x));
    TEST_ASSERT_EQUAL_PTR(a, bl_prev(x));
    TEST_ASSERT_EQUAL_PTR(x, bl_prev(b));
    TEST_ASSERT_EQUAL_PTR(a, bl_first(list));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_insert_after_tail(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(list, a);
    bl_append(list, b);

    TEST_ASSERT_EQUAL(BL_OK, bl_insert_after(list, b, x));
    TEST_ASSERT_EQUAL_PTR(x, bl_last(list));
    TEST_ASSERT_EQUAL_PTR(b, bl_prev(x));
    TEST_ASSERT_NULL(bl_next(x));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_insert_after_middle(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* c     = make_ent(&ab, "c");
    BlobListEnt* x     = make_ent(&ab, "x");

    bl_append(list, a);
    bl_append(list, b);
    bl_append(list, c);

    TEST_ASSERT_EQUAL(BL_OK, bl_insert_after(list, b, x));
    TEST_ASSERT_EQUAL_PTR(b, bl_prev(x));
    TEST_ASSERT_EQUAL_PTR(c, bl_next(x));
    TEST_ASSERT_EQUAL_PTR(x, bl_next(b));
    TEST_ASSERT_EQUAL_PTR(x, bl_prev(c));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* removal                                                             */
/* ------------------------------------------------------------------ */

static void
test_remove_head_single(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    bl_append(list, a);

    BlobListEnt* out = bl_remove_head(list);
    TEST_ASSERT_EQUAL_PTR(a, out);
    TEST_ASSERT_NULL(bl_ent_owner(out));
    TEST_ASSERT_NULL(bl_prev(out));
    TEST_ASSERT_NULL(bl_next(out));
    TEST_ASSERT_EQUAL_size_t(0, bl_getlen(list));
    TEST_ASSERT_NULL(bl_first(list));
    TEST_ASSERT_NULL(bl_last(list));

    bl_ent_destroy(out);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_head_multi(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    bl_append(list, a);
    bl_append(list, b);

    BlobListEnt* out = bl_remove_head(list);
    TEST_ASSERT_EQUAL_PTR(a, out);
    TEST_ASSERT_NULL(bl_ent_owner(out));
    TEST_ASSERT_EQUAL_PTR(b, bl_first(list));
    TEST_ASSERT_NULL(bl_prev(b));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(list));

    bl_ent_destroy(out);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_head_empty_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    TEST_ASSERT_NULL(bl_remove_head(list));
    TEST_ASSERT_NULL(bl_remove_head(NULL));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_tail_single(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    bl_append(list, a);

    BlobListEnt* out = bl_remove_tail(list);
    TEST_ASSERT_EQUAL_PTR(a, out);
    TEST_ASSERT_NULL(bl_ent_owner(out));
    TEST_ASSERT_EQUAL_size_t(0, bl_getlen(list));
    TEST_ASSERT_NULL(bl_first(list));
    TEST_ASSERT_NULL(bl_last(list));

    bl_ent_destroy(out);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_tail_multi(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    bl_append(list, a);
    bl_append(list, b);

    BlobListEnt* out = bl_remove_tail(list);
    TEST_ASSERT_EQUAL_PTR(b, out);
    TEST_ASSERT_EQUAL_PTR(a, bl_last(list));
    TEST_ASSERT_NULL(bl_next(a));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(list));

    bl_ent_destroy(out);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_tail_empty_returns_null(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    TEST_ASSERT_NULL(bl_remove_tail(list));
    TEST_ASSERT_NULL(bl_remove_tail(NULL));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_head_equals_remove_head_direct(void) {
    /* bl_remove(list, list->head) should give the same result as
     * bl_remove_head(list). */
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    bl_append(list, a);
    bl_append(list, b);

    BlobListEnt* via_remove = bl_remove(list, a);
    TEST_ASSERT_EQUAL_PTR(a, via_remove);
    TEST_ASSERT_NULL(bl_ent_owner(via_remove));
    TEST_ASSERT_EQUAL_PTR(b, bl_first(list));
    TEST_ASSERT_NULL(bl_prev(b));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(list));

    bl_ent_destroy(via_remove);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_middle(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    list  = bl_create(&ab, noop_destroy);
    BlobListEnt* a     = make_ent(&ab, "a");
    BlobListEnt* b     = make_ent(&ab, "b");
    BlobListEnt* c     = make_ent(&ab, "c");
    bl_append(list, a);
    bl_append(list, b);
    bl_append(list, c);

    BlobListEnt* out = bl_remove(list, b);
    TEST_ASSERT_EQUAL_PTR(b, out);
    TEST_ASSERT_NULL(bl_ent_owner(out));
    TEST_ASSERT_NULL(bl_next(out));
    TEST_ASSERT_NULL(bl_prev(out));
    TEST_ASSERT_EQUAL_PTR(c, bl_next(a));
    TEST_ASSERT_EQUAL_PTR(a, bl_prev(c));
    TEST_ASSERT_EQUAL_size_t(2, bl_getlen(list));

    bl_ent_destroy(out);
    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_wrong_list_returns_null(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    l1    = bl_create(&ab, noop_destroy);
    BlobList*    l2    = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "e");
    bl_append(l1, e);

    TEST_ASSERT_NULL(bl_remove(l2, e));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(l1));

    bl_destroy(l1);
    bl_destroy(l2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_remove_can_be_reappended_elsewhere(void) {
    TestArena*   arena = test_arena_create(1 << 16);
    BlobAlloc    ab    = test_arena_block(arena);

    BlobList*    l1    = bl_create(&ab, noop_destroy);
    BlobList*    l2    = bl_create(&ab, noop_destroy);
    BlobListEnt* e     = make_ent(&ab, "moving");

    bl_append(l1, e);
    BlobListEnt* moved = bl_remove(l1, e);
    TEST_ASSERT_EQUAL_PTR(e, moved);
    TEST_ASSERT_NULL(bl_ent_owner(moved));

    TEST_ASSERT_EQUAL(BL_OK, bl_append(l2, moved));
    TEST_ASSERT_EQUAL_PTR(l2, bl_ent_owner(moved));
    TEST_ASSERT_EQUAL_size_t(0, bl_getlen(l1));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(l2));

    bl_destroy(l1);
    bl_destroy(l2);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* iteration                                                           */
/* ------------------------------------------------------------------ */

static void
test_iteration_forward(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    bl_append(list, make_ent(&ab, "a"));
    bl_append(list, make_ent(&ab, "b"));
    bl_append(list, make_ent(&ab, "c"));

    const char* expected[] = { "a", "b", "c" };
    size_t      i          = 0;
    for (BlobListEnt* e = bl_first(list); e; e = bl_next(e)) {
        TEST_ASSERT_TRUE(ent_equals_str(e, expected[i]));
        i++;
    }
    TEST_ASSERT_EQUAL_size_t(3, i);

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_iteration_backward(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    bl_append(list, make_ent(&ab, "a"));
    bl_append(list, make_ent(&ab, "b"));
    bl_append(list, make_ent(&ab, "c"));

    const char* expected[] = { "c", "b", "a" };
    size_t      i          = 0;
    for (BlobListEnt* e = bl_last(list); e; e = bl_prev(e)) {
        TEST_ASSERT_TRUE(ent_equals_str(e, expected[i]));
        i++;
    }
    TEST_ASSERT_EQUAL_size_t(3, i);

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_iteration_with_removal(void) {
    /* The documented idiom: capture next before removing. */
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    for (int i = 0; i < 6; i++) {
        char buf[8];
        int  n = snprintf(buf, sizeof buf, "%d", i);
        bl_append(list, bl_ent_create(&ab, buf, (size_t)n));
    }

    /* Remove all even-numbered entries */
    for (BlobListEnt* e = bl_first(list); e;) {
        BlobListEnt* next = bl_next(e);
        if ((bl_ent_data(e)[0] - '0') % 2 == 0) {
            BlobListEnt* removed = bl_remove(list, e);
            bl_ent_destroy(removed);
        }
        e = next;
    }

    /* Only odd entries remain */
    TEST_ASSERT_EQUAL_size_t(3, bl_getlen(list));
    for (BlobListEnt* e = bl_first(list); e; e = bl_next(e)) {
        TEST_ASSERT_TRUE((bl_ent_data(e)[0] - '0') % 2 == 1);
    }

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* getidx                                                              */
/* ------------------------------------------------------------------ */

static void
test_getidx_hits_and_misses(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, noop_destroy);
    bl_append(list, make_ent(&ab, "a"));
    bl_append(list, make_ent(&ab, "b"));
    bl_append(list, make_ent(&ab, "c"));

    TEST_ASSERT_TRUE(ent_equals_str(bl_getidx(list, 0), "a"));
    TEST_ASSERT_TRUE(ent_equals_str(bl_getidx(list, 1), "b"));
    TEST_ASSERT_TRUE(ent_equals_str(bl_getidx(list, 2), "c"));
    TEST_ASSERT_NULL(bl_getidx(list, 3));
    TEST_ASSERT_NULL(bl_getidx(NULL, 0));

    TEST_ASSERT_TRUE(ent_equals_str(bl_getidx_mut(list, 1), "b"));

    bl_destroy(list);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* teardown                                                            */
/* ------------------------------------------------------------------ */

static void
test_destroy_calls_destructor_per_entry(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, test_counting_destroy_list);
    bl_append(list, make_ent(&ab, "a"));
    bl_append(list, make_ent(&ab, "b"));
    bl_append(list, make_ent(&ab, "c"));

    bl_destroy(list);
    TEST_ASSERT_EQUAL_size_t(3, g_test_destroy_count);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_clear_calls_destructor_and_keeps_list(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    BlobList*  list  = bl_create(&ab, test_counting_destroy_list);
    for (int i = 0; i < 5; i++) {
        bl_append(list, make_ent(&ab, "x"));
    }

    bl_clear(list);
    TEST_ASSERT_EQUAL_size_t(5, g_test_destroy_count);
    TEST_ASSERT_EQUAL_size_t(0, bl_getlen(list));
    TEST_ASSERT_NULL(bl_first(list));
    TEST_ASSERT_NULL(bl_last(list));

    /* Reusable after clear */
    bl_append(list, make_ent(&ab, "y"));
    TEST_ASSERT_EQUAL_size_t(1, bl_getlen(list));

    bl_destroy(list);
    TEST_ASSERT_EQUAL_size_t(6, g_test_destroy_count);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_empty);
    RUN_TEST(test_create_null_args);
    RUN_TEST(test_create_oom_returns_null);
    RUN_TEST(test_destroy_null_is_noop);

    RUN_TEST(test_ent_create_roundtrip);
    RUN_TEST(test_ent_create_rejects_zero_len);
    RUN_TEST(test_ent_create_rejects_null_args);
    RUN_TEST(test_ent_create_uninit_writable);
    RUN_TEST(test_ent_destroy_null_returns_ok);
    RUN_TEST(test_ent_accessors_on_null);

    RUN_TEST(test_append_basic_sets_owner);
    RUN_TEST(test_append_same_entry_twice_is_refused);
    RUN_TEST(test_append_to_second_list_is_refused);
    RUN_TEST(test_ent_destroy_refuses_owned_entry);
    RUN_TEST(test_append_null_args);
    RUN_TEST(test_append_into_empty);
    RUN_TEST(test_append_to_nonempty);

    RUN_TEST(test_prepend_into_empty);
    RUN_TEST(test_prepend_to_nonempty);

    RUN_TEST(test_insert_before_head);
    RUN_TEST(test_insert_before_middle);
    RUN_TEST(test_insert_before_tail);
    RUN_TEST(test_insert_before_wrong_list);
    RUN_TEST(test_insert_before_owned_ent_refused);

    RUN_TEST(test_insert_after_head);
    RUN_TEST(test_insert_after_tail);
    RUN_TEST(test_insert_after_middle);

    RUN_TEST(test_remove_head_single);
    RUN_TEST(test_remove_head_multi);
    RUN_TEST(test_remove_head_empty_returns_null);
    RUN_TEST(test_remove_tail_single);
    RUN_TEST(test_remove_tail_multi);
    RUN_TEST(test_remove_tail_empty_returns_null);
    RUN_TEST(test_remove_head_equals_remove_head_direct);
    RUN_TEST(test_remove_middle);
    RUN_TEST(test_remove_wrong_list_returns_null);
    RUN_TEST(test_remove_can_be_reappended_elsewhere);

    RUN_TEST(test_iteration_forward);
    RUN_TEST(test_iteration_backward);
    RUN_TEST(test_iteration_with_removal);

    RUN_TEST(test_getidx_hits_and_misses);

    RUN_TEST(test_destroy_calls_destructor_per_entry);
    RUN_TEST(test_clear_calls_destructor_and_keeps_list);

    return UNITY_END();
}