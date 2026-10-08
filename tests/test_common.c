#include "test_common.h"
#include "unity.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestArenaRec_s {
    void*                  ptr;
    size_t                 size;
    struct TestArenaRec_s* next;
} TestArenaRec;

struct TestArena_s {
    unsigned char* slab;
    size_t         slab_size;
    size_t         slab_used;

    TestArenaRec*  live_head;
    size_t         live_count;

    size_t         total_alloc_bytes;
    size_t         total_free_bytes;

    size_t         alloc_calls;
    size_t         free_calls;
    size_t         realloc_calls;

    size_t         size_mismatch_count;
    size_t         invalid_free_count;

    size_t         fail_at_call; /* 0-indexed; SIZE_MAX = disabled */
    size_t         call_counter;
};

TestArena*
test_arena_create(size_t capacity) {
    TestArena* arena = calloc(1, sizeof *arena);
    if (!arena) {
        return NULL;
    }

    arena->slab = malloc(capacity);
    if (!arena->slab) {
        free(arena);
        return NULL;
    }

    arena->slab_size    = capacity;
    arena->slab_used    = 0;
    arena->fail_at_call = SIZE_MAX;
    return arena;
}

void
test_arena_destroy(TestArena* arena) {
    if (!arena) {
        return;
    }

    TestArenaRec* rec = arena->live_head;
    while (rec) {
        TestArenaRec* next = rec->next;
        free(rec);
        rec = next;
    }

    free(arena->slab);
    free(arena);
}

void*
test_arena_alloc(void* ctx, size_t size) {
    TestArena* arena     = ctx;

    size_t     this_call = arena->call_counter++;
    arena->alloc_calls++;

    if (this_call == arena->fail_at_call) {
        return NULL;
    }

    const size_t align   = _Alignof(max_align_t);
    size_t       aligned = (size + align - 1) & ~(align - 1);

    if (arena->slab_used + aligned > arena->slab_size) {
        return NULL;
    }

    void* p            = arena->slab + arena->slab_used;
    arena->slab_used  += aligned;

    TestArenaRec* rec  = malloc(sizeof *rec);
    if (!rec) {
        return NULL;
    }

    rec->ptr         = p;
    rec->size        = size;
    rec->next        = arena->live_head;
    arena->live_head = rec;
    arena->live_count++;
    arena->total_alloc_bytes += size;

    return p;
}

void
test_arena_free(void* ctx, void* ptr, size_t size) {
    TestArena* arena = ctx;
    arena->free_calls++;

    if (!ptr) {
        return;
    }

    TestArenaRec** pp = &arena->live_head;
    while (*pp) {
        TestArenaRec* rec = *pp;
        if (rec->ptr == ptr) {
            if (rec->size != size) {
                arena->size_mismatch_count++;
            }

            arena->total_free_bytes += size;
            *pp                      = rec->next;
            free(rec);
            arena->live_count--;
            return;
        }

        pp = &rec->next;
    }
    arena->invalid_free_count++;
}

void*
test_arena_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    TestArena* arena = ctx;
    arena->realloc_calls++;

    if (!ptr) {
        return test_arena_alloc(ctx, new_size);
    }

    void* new_ptr = test_arena_alloc(ctx, new_size);
    if (!new_ptr) {
        return NULL;
    }

    size_t to_copy = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, to_copy);
    test_arena_free(ctx, ptr, old_size);
    return new_ptr;
}

BlobAlloc
test_arena_block(TestArena* arena) {
    BlobAlloc ab = {
        .ctx     = arena,
        .alloc   = test_arena_alloc,
        .free    = test_arena_free,
        .realloc = test_arena_realloc,
    };

    return ab;
}

void
test_arena_fail_on_nth_alloc(TestArena* arena, size_t n) {
    if (n == 0) {
        arena->fail_at_call = SIZE_MAX;   /* disabled */
        return;
    }
    arena->fail_at_call = arena->call_counter + (n - 1);
}

void
test_arena_assert_balanced(const TestArena* arena) {
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, arena->live_count, "arena has live allocations at end of test");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, arena->size_mismatch_count, "free called with wrong size");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, arena->invalid_free_count, "free called on untracked pointer");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(arena->total_alloc_bytes, arena->total_free_bytes,
                                     "total bytes allocated != total bytes freed");
}

size_t
test_arena_alloc_calls(const TestArena* arena) {
    return arena->alloc_calls;
}

size_t
test_arena_free_calls(const TestArena* arena) {
    return arena->free_calls;
}

size_t
test_arena_live_count(const TestArena* arena) {
    return arena->live_count;
}

/* ------------------------------------------------------------------ */
/* Comparators                                                         */
/* ------------------------------------------------------------------ */

int
cmp_bytes(const void* a, const void* b, size_t alen, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    int    r = memcmp(a, b, n);

    if (r != 0) {
        return r;
    }

    if (alen < blen) {
        return -1;
    }

    if (alen > blen) {
        return 1;
    }

    return 0;
}

int
cmp_len_then_bytes(const void* a, const void* b, size_t alen, size_t blen) {
    if (alen != blen) {
        return alen < blen ? -1 : 1;
    }

    return memcmp(a, b, alen);
}

int
cmp_reverse(const void* a, const void* b, size_t alen, size_t blen) {
    return -cmp_bytes(a, b, alen, blen);
}

/* ------------------------------------------------------------------ */
/* Counting destructor                                                 */
/* ------------------------------------------------------------------ */

size_t g_test_destroy_count = 0;

void
test_counting_destroy(BlobVecEnt* ent) {
    (void)ent;
    g_test_destroy_count++;
}

void
test_reset_counters(void) {
    g_test_destroy_count = 0;
}