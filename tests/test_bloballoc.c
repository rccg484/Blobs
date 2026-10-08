#include "BlobAlloc.h"
#include "test_common.h"
#include "unity.h"

#include <string.h>

void
setUp(void) {}
void
tearDown(void) {}

/* ------------------------------------------------------------------ */
/* default block                                                       */
/* ------------------------------------------------------------------ */

static void
test_default_is_stable_pointer(void) {
    const BlobAlloc* a = ba_default();
    const BlobAlloc* b = ba_default();

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, b);
}

static void
test_default_block_has_all_hooks(void) {
    const BlobAlloc* ab = ba_default();

    TEST_ASSERT_NOT_NULL(ab->alloc);
    TEST_ASSERT_NOT_NULL(ab->free);
    TEST_ASSERT_NOT_NULL(ab->realloc);
    /* ctx is allowed to be NULL for the default */
    TEST_ASSERT_NULL(ab->ctx);
}

static void
test_default_alloc_returns_usable_memory(void) {
    const BlobAlloc* ab = ba_default();

    unsigned char*   p  = ba_alloc(ab, 64);
    TEST_ASSERT_NOT_NULL(p);

    /* Should be writable without faulting */
    memset(p, 0xA5, 64);
    TEST_ASSERT_EQUAL_UINT8(0xA5, p[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA5, p[63]);

    ba_free(ab, p, 64);
}

static void
test_default_free_null_is_noop(void) {
    const BlobAlloc* ab = ba_default();
    ba_free(ab, NULL, 0); /* must not crash */
}

static void
test_default_realloc_null_acts_as_alloc(void) {
    const BlobAlloc* ab = ba_default();

    void*            p  = ba_realloc(ab, NULL, 0, 32);
    TEST_ASSERT_NOT_NULL(p);

    memset(p, 0x5A, 32);
    ba_free(ab, p, 32);
}

static void
test_default_realloc_grows_and_preserves_contents(void) {
    const BlobAlloc* ab = ba_default();

    unsigned char*   p  = ba_alloc(ab, 16);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x42, 16);

    unsigned char* q = ba_realloc(ab, p, 16, 128);
    TEST_ASSERT_NOT_NULL(q);

    /* Original 16 bytes preserved */
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x42, q[i]);
    }
    /* Newly added region writable */
    memset(q + 16, 0x77, 112);
    TEST_ASSERT_EQUAL_UINT8(0x77, q[127]);

    ba_free(ab, q, 128);
}

/* ------------------------------------------------------------------ */
/* custom block - arena-backed                                         */
/* ------------------------------------------------------------------ */

static void
test_arena_block_alloc_free_roundtrip(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    void*      p     = ba_alloc(&ab, 128);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x11, 128);

    ba_free(&ab, p, 128);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_arena_block_forwards_ctx(void) {
    /* The whole point of the ctx parameter: two blocks sharing the same
     * hooks but different contexts route to different arenas. */
    TestArena* arena_a = test_arena_create(1 << 16);
    TestArena* arena_b = test_arena_create(1 << 16);

    BlobAlloc  ab_a    = test_arena_block(arena_a);
    BlobAlloc  ab_b    = test_arena_block(arena_b);

    void*      pa      = ba_alloc(&ab_a, 64);
    void*      pb      = ba_alloc(&ab_b, 64);
    TEST_ASSERT_NOT_NULL(pa);
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_EQUAL(pa, pb);

    TEST_ASSERT_EQUAL_size_t(1, test_arena_live_count(arena_a));
    TEST_ASSERT_EQUAL_size_t(1, test_arena_live_count(arena_b));

    ba_free(&ab_a, pa, 64);
    ba_free(&ab_b, pb, 64);

    test_arena_assert_balanced(arena_a);
    test_arena_assert_balanced(arena_b);
    test_arena_destroy(arena_a);
    test_arena_destroy(arena_b);
}

static void
test_arena_block_realloc_preserves_contents(void) {
    TestArena*     arena = test_arena_create(1 << 16);
    BlobAlloc      ab    = test_arena_block(arena);

    unsigned char* p     = ba_alloc(&ab, 16);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x99, 16);

    unsigned char* q = ba_realloc(&ab, p, 16, 64);
    TEST_ASSERT_NOT_NULL(q);
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x99, q[i]);
    }

    ba_free(&ab, q, 64);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* failure injection                                                   */
/* ------------------------------------------------------------------ */

static void
test_alloc_returns_null_when_block_fails(void) {
    TestArena* arena = test_arena_create(1 << 16);
    BlobAlloc  ab    = test_arena_block(arena);

    test_arena_fail_on_nth_alloc(arena, 1);

    void* p = ba_alloc(&ab, 128);
    TEST_ASSERT_NULL(p);

    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_realloc_returns_null_and_leaves_original_intact(void) {
    TestArena*     arena = test_arena_create(1 << 16);
    BlobAlloc      ab    = test_arena_block(arena);

    unsigned char* p     = ba_alloc(&ab, 32);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0xCC, 32);

    /* Fail the next allocation call. test_arena_realloc internally
     * calls test_arena_alloc, so this triggers the failure inside
     * the realloc path. */
    test_arena_fail_on_nth_alloc(arena, 1);

    unsigned char* q = ba_realloc(&ab, p, 32, 128);
    TEST_ASSERT_NULL(q);

    /* Original must still be live and unchanged */
    TEST_ASSERT_EQUAL_size_t(1, test_arena_live_count(arena));
    TEST_ASSERT_EQUAL_UINT8(0xCC, p[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, p[31]);

    ba_free(&ab, p, 32);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* direct calls to default hooks                                       */
/* ------------------------------------------------------------------ */

static void
test_default_hooks_usable_directly(void) {
    /* Composition case: a custom block might delegate to these. */

    void* p = ba_default_alloc(NULL, 64);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x33, 64);

    void* q = ba_default_realloc(NULL, p, 64, 128);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_UINT8(0x33, ((unsigned char*)q)[0]);
    TEST_ASSERT_EQUAL_UINT8(0x33, ((unsigned char*)q)[63]);

    ba_default_free(NULL, q, 128);
}

static void
test_default_free_ignores_size(void) {
    /* Should not crash or corrupt, regardless of size argument. */
    void* p = ba_default_alloc(NULL, 64);
    TEST_ASSERT_NOT_NULL(p);
    ba_default_free(NULL, p, 9999);
}

/* ------------------------------------------------------------------ */
/* composite: delegating block                                         */
/* ------------------------------------------------------------------ */

/* A block that falls through to malloc for oversized requests, arena
 * otherwise. Demonstrates the composition pattern the header documents. */

typedef struct {
    TestArena* arena;
    size_t     threshold;
} SplitCtx;

static void*
split_alloc(void* ctx, size_t size) {
    SplitCtx* sc = ctx;
    if (size > sc->threshold) {
        return ba_default_alloc(NULL, size);
    }
    return test_arena_alloc(sc->arena, size);
}

static void
split_free(void* ctx, void* ptr, size_t size) {
    SplitCtx* sc = ctx;
    if (size > sc->threshold) {
        ba_default_free(NULL, ptr, size);
        return;
    }
    test_arena_free(sc->arena, ptr, size);
}

static void*
split_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    SplitCtx* sc = ctx;

    /* Same bucket: delegate to the arena (which handles copy + free). */
    if (old_size <= sc->threshold && new_size <= sc->threshold) {
        return test_arena_realloc(sc->arena, ptr, old_size, new_size);
    }

    /* Cross bucket: allocate from the new bucket, copy, free to old. */
    void* new_ptr
        = (new_size > sc->threshold) ? ba_default_alloc(NULL, new_size) : test_arena_alloc(sc->arena, new_size);
    if (!new_ptr) {
        return NULL;
    }

    size_t to_copy = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, to_copy);

    if (old_size > sc->threshold) {
        ba_default_free(NULL, ptr, old_size);
    } else {
        test_arena_free(sc->arena, ptr, old_size);
    }
    return new_ptr;
}

static void
test_split_block_small_stays_in_arena(void) {
    TestArena* arena = test_arena_create(1 << 16);
    SplitCtx   sc    = { .arena = arena, .threshold = 1024 };
    BlobAlloc  ab    = {
        .ctx     = &sc,
        .alloc   = split_alloc,
        .free    = split_free,
        .realloc = split_realloc,
    };

    void* p = ba_alloc(&ab, 64);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, test_arena_live_count(arena));

    ba_free(&ab, p, 64);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_split_block_large_bypasses_arena(void) {
    TestArena* arena = test_arena_create(1 << 16);
    SplitCtx   sc    = { .arena = arena, .threshold = 1024 };
    BlobAlloc  ab    = {
        .ctx     = &sc,
        .alloc   = split_alloc,
        .free    = split_free,
        .realloc = split_realloc,
    };

    void* p = ba_alloc(&ab, 4096);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(0, test_arena_live_count(arena));

    memset(p, 0xAB, 4096);
    ba_free(&ab, p, 4096);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

static void
test_split_block_growing_across_threshold(void) {
    TestArena* arena = test_arena_create(1 << 16);
    SplitCtx   sc    = { .arena = arena, .threshold = 1024 };
    BlobAlloc  ab    = {
        .ctx     = &sc,
        .alloc   = split_alloc,
        .free    = split_free,
        .realloc = split_realloc,
    };

    unsigned char* p = ba_alloc(&ab, 128);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0xEE, 128);
    TEST_ASSERT_EQUAL_size_t(1, test_arena_live_count(arena));

    /* Grow past the threshold - moves to malloc-land */
    unsigned char* q = ba_realloc(&ab, p, 128, 8192);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_UINT8(0xEE, q[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, q[127]);
    TEST_ASSERT_EQUAL_size_t(0, test_arena_live_count(arena));

    ba_free(&ab, q, 8192);
    test_arena_assert_balanced(arena);
    test_arena_destroy(arena);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int
main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_default_is_stable_pointer);
    RUN_TEST(test_default_block_has_all_hooks);
    RUN_TEST(test_default_alloc_returns_usable_memory);
    RUN_TEST(test_default_free_null_is_noop);
    RUN_TEST(test_default_realloc_null_acts_as_alloc);
    RUN_TEST(test_default_realloc_grows_and_preserves_contents);

    RUN_TEST(test_arena_block_alloc_free_roundtrip);
    RUN_TEST(test_arena_block_forwards_ctx);
    RUN_TEST(test_arena_block_realloc_preserves_contents);

    RUN_TEST(test_alloc_returns_null_when_block_fails);
    RUN_TEST(test_realloc_returns_null_and_leaves_original_intact);

    RUN_TEST(test_default_hooks_usable_directly);
    RUN_TEST(test_default_free_ignores_size);

    RUN_TEST(test_split_block_small_stays_in_arena);
    RUN_TEST(test_split_block_large_bypasses_arena);
    RUN_TEST(test_split_block_growing_across_threshold);

    return UNITY_END();
}