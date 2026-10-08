#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "BlobAlloc.h"
#include "BlobBox.h"
#include "BlobVec.h"

typedef struct TestArena_s TestArena;

TestArena*
test_arena_create(size_t capacity);

void
test_arena_destroy(TestArena* arena);

void*
test_arena_alloc(void* ctx, size_t size);

void
test_arena_free(void* ctx, void* ptr, size_t size);

void*
test_arena_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size);

BlobAlloc
test_arena_block(TestArena* arena);

void
test_arena_fail_on_nth_alloc(TestArena* arena, size_t n);

void
test_arena_assert_balanced(const TestArena* arena);

size_t
test_arena_alloc_calls(const TestArena* arena);

size_t
test_arena_free_calls(const TestArena* arena);

size_t
test_arena_live_count(const TestArena* arena);

int
cmp_bytes(const void* a, const void* b, size_t alen, size_t blen);

int
cmp_len_then_bytes(const void* a, const void* b, size_t alen, size_t blen);

int
cmp_reverse(const void* a, const void* b, size_t alen, size_t blen);

extern size_t g_test_destroy_count;

void
test_counting_destroy(BlobVecEnt* ent);

void
test_reset_counters(void);

#endif /* TEST_COMMON_H */