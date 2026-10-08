/**
 * \file BlobAlloc.h
 * \author Rory Graham (rorygraham2\@gmail.com)
 * \brief Allocator substrate for the Blob* family.
 * \version 0.1
 *
 * Provides the hook types, the allocator block, and a default
 * malloc-backed implementation. BlobAlloc knows nothing about entries,
 * containers, or ownership; it exists only so that BlobVec and BlobBox
 * can allocate through a single, substitutable interface.
 *
 * Allocator contract:
 *   - Every hook takes a leading void* ctx, passed through unchanged from
 *     the block. This is what allows multiple arenas to coexist in one
 *     process without global state.
 *   - free takes the size of the allocation. This is required by
 *     allocators that do not track sizes internally: bump arenas, pool
 *     allocators, embedded allocators with a size header.
 *   - realloc takes both old_size and new_size, for the same reason.
 *     libc's realloc hides this by reading its own header; custom
 *     allocators are not obliged to.
 *   - A block's hooks must be non-NULL. ba_alloc/ba_free/ba_realloc do
 *     not check; a NULL hook is a construction bug, not a runtime
 *     condition.
 *
 * Block lifetime:
 *   A BlobAlloc carries no lifetime of its own. The block must outlive
 *   every allocation made through it. Stack-local blocks that escape
 *   their scope are the primary way to misuse this layer. Blocks may be
 *   embedded in larger context structs, made static, or heap-allocated
 *   and freed once every allocation from them has been returned.
 *
 * Default block:
 *   ba_default() returns a shared, static, malloc-backed block. It is
 *   never freed and is safe to call from any thread. The individual
 *   default hooks are exported so custom blocks can compose them, e.g.
 *   an arena that falls through to ba_default_alloc for oversized
 *   requests.
 *
 * Single-header library. Define BLOBALLOC_IMPL in exactly one translation
 * unit before including this file to emit the implementation.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef BLOBALLOC_H
#define BLOBALLOC_H

#include <stddef.h>

/**
 * \brief Allocates a block of memory.
 *
 * \param[in] ctx   The context pointer from the block. Use it to identify
 *                  which arena, pool, or region this call refers to.
 * \param[in] size  The number of bytes to allocate. Must be greater than
 *                  zero.
 *
 * \return A pointer to the first byte of the block.
 * \return NULL if the allocator cannot satisfy the request.
 *
 * \pre  size > 0
 *
 * \note The returned block is aligned for any type. If your allocator
 *       cannot guarantee this, do not use it as a BlobAlloc hook.
 * \note The caller is responsible for tracking the size and passing it
 *       to the matching free hook.
 */
typedef void* (*BlobAllocFn)(void* ctx, size_t size);

/**
 * \brief Returns a block of memory to its allocator.
 *
 * \param[in] ctx   The context pointer from the block.
 * \param[in] ptr   The pointer to free. May be NULL.
 * \param[in] size  The size of the block, in bytes. Must equal the size
 *                  passed to the matching alloc or realloc call.
 *
 * \note A NULL ptr must be a no-op. Every block must give this guarantee.
 * \note The size argument exists so that allocators which do not track
 *       size internally can still free correctly. libc's free ignores
 *       it; a bump arena needs it.
 */
typedef void  (*BlobFreeFn)(void* ctx, void* ptr, size_t size);

/**
 * \brief Changes the size of an allocated block.
 *
 * \param[in] ctx       The context pointer from the block.
 * \param[in] ptr       The block to resize. May be NULL.
 * \param[in] old_size  The current size of the block, in bytes. Must be
 *                      correct if ptr is not NULL.
 * \param[in] new_size  The requested size, in bytes. Must be greater
 *                      than zero.
 *
 * \return A pointer to the resized block.
 * \return NULL if the allocator cannot satisfy the request.
 *
 * \pre  new_size > 0
 * \pre  If ptr is NULL, old_size is ignored.
 *
 * \note On success, the original pointer is invalid. Use the returned
 *       pointer.
 * \note On failure, the original block is unchanged and still valid.
 * \note The default hook follows libc semantics: realloc(NULL, n)
 *       behaves as malloc(n), and realloc(p, 0) behaves as free(p)
 *       and returns NULL. A custom block is not required to do the
 *       same. If your block differs, document it, and do not call it
 *       with new_size == 0.
 */
typedef void* (*BlobReallocFn)(void* ctx, void* ptr, size_t old_size, size_t new_size);

/**
 * \brief A set of allocation hooks plus a context pointer.
 *
 * This is a value type. Copy it, embed it in a larger struct, or make it
 * static. The block itself owns no memory. It exists so that allocations
 * can be redirected without a global allocator.
 *
 * \invariant alloc is not NULL.
 * \invariant free is not NULL.
 * \invariant realloc is not NULL.
 * \invariant ctx is passed to each hook unchanged. Its meaning is private
 *            to the hooks. It may be NULL if the hooks do not need it.
 *
 * \note The block does not own its ctx. If ctx points to an arena or
 *       pool, that arena or pool must outlive the block and every
 *       allocation made through it.
 *
 * \example
 * // A stack-local block wrapping a bump arena
 * Arena a = arena_create(1 << 20);
 * BlobAlloc ab = {
 *     .ctx     = &a,
 *     .alloc   = arena_alloc,
 *     .free    = arena_free,
 *     .realloc = arena_realloc,
 * };
 * // ab is valid until this scope ends. Every allocation from it must be
 * // returned before then.
 */
typedef struct BlobAlloc_s {
    void*         ctx;     /**< Opaque pointer passed through to each hook. */
    BlobAllocFn   alloc;   /**< Allocates a block. Must not be NULL. */
    BlobFreeFn    free;    /**< Frees a block. Must not be NULL. */
    BlobReallocFn realloc; /**< Resizes a block. Must not be NULL. */
} BlobAlloc;

/**
 * \brief Returns the shared default allocator block.
 *
 * \return A pointer to a static, malloc-backed block.
 *
 * \note The block is never freed. It is valid for the lifetime of the
 *       program.
 * \note The block is safe to use from any thread. The hooks it exposes
 *       are thread-safe to the same degree as libc's malloc, free, and
 *       realloc.
 * \note The returned pointer is const. Do not cast it away.
 *
 * \example
 * const BlobAlloc* ab = ba_default();
 * void* p = ba_alloc(ab, 64);
 * if (p != NULL) {
 *     ba_free(ab, p, 64);
 * }
 */
const BlobAlloc*
ba_default(void);

/**
 * \brief Allocates memory through a block.
 *
 * \param[in] ab    The allocator block. Must not be NULL.
 * \param[in] size  The number of bytes to allocate. Must be greater than
 *                  zero.
 *
 * \return A pointer to the first byte of the block.
 * \return NULL if the allocator cannot satisfy the request.
 *
 * \pre  ab != NULL
 * \pre  ab->alloc != NULL
 * \pre  size > 0
 *
 * \note This function does not check ab for NULL. A NULL block is a
 *       construction bug, not a runtime condition.
 * \note The caller must pass size to ba_free.
 *
 * \example
 * void* p = ba_alloc(ba_default(), 128);
 * if (p != NULL) {
 *     // use p
 *     ba_free(ba_default(), p, 128);
 * }
 */
void*
ba_alloc(const BlobAlloc* ab, size_t size);

/**
 * \brief Frees memory through a block.
 *
 * \param[in] ab    The allocator block. Must not be NULL.
 * \param[in] ptr   The pointer to free. May be NULL.
 * \param[in] size  The size of the block, in bytes. Must match the size
 *                  used to allocate it.
 *
 * \pre  ab != NULL
 * \pre  ab->free != NULL
 * \pre  If ptr is not NULL, size must equal the size passed to ba_alloc
 *       or ba_realloc.
 *
 * \note A NULL ptr is a no-op. Every block must give this guarantee.
 * \note This function does not check ab for NULL.
 */
void
ba_free(const BlobAlloc* ab, void* ptr, size_t size);

/**
 * \brief Resizes memory through a block.
 *
 * \param[in] ab        The allocator block. Must not be NULL.
 * \param[in] ptr       The block to resize. May be NULL.
 * \param[in] old_size  The current size of the block, in bytes. Must be
 *                      correct if ptr is not NULL.
 * \param[in] new_size  The requested size, in bytes. Must be greater
 *                      than zero.
 *
 * \return A pointer to the resized block.
 * \return NULL if the allocator cannot satisfy the request.
 *
 * \pre  ab != NULL
 * \pre  ab->realloc != NULL
 * \pre  new_size > 0
 *
 * \note On failure, the original block is unchanged. You may retry or
 *       free it.
 * \note This function does not check ab for NULL.
 *
 * \warning If the block follows libc semantics, realloc(ptr, 0) frees
 *          ptr and returns NULL. Treating that NULL as an out-of-memory
 *          result is a use-after-free. Pass new_size > 0, or document
 *          the behaviour of your custom block.
 */
void*
ba_realloc(const BlobAlloc* ab, void* ptr, size_t old_size, size_t new_size);

/* ------------------------------------------------------------------ */
/* default hooks                                                       */
/* ------------------------------------------------------------------ */

/**
 * \brief Default allocation hook. Wraps malloc.
 *
 * \param[in] ctx   Ignored.
 * \param[in] size  The number of bytes to allocate.
 *
 * \return The result of malloc(size).
 *
 * \note Exported so that custom blocks can delegate to it. For example,
 *       an arena that falls through to malloc for oversized requests.
 *
 * \example
 * void* my_alloc(void* ctx, size_t size) {
 *     if (size > BIG_THRESHOLD) return ba_default_alloc(ctx, size);
 *     return arena_bump(ctx, size);
 * }
 */
void*
ba_default_alloc(void* ctx, size_t size);

/**
 * \brief Default free hook. Wraps free.
 *
 * \param[in] ctx   Ignored.
 * \param[in] ptr   The pointer to free. May be NULL.
 * \param[in] size  Ignored.
 *
 * \note Exported for composition with custom blocks.
 */
void
ba_default_free(void* ctx, void* ptr, size_t size);

/**
 * \brief Default realloc hook. Wraps realloc.
 *
 * \param[in] ctx       Ignored.
 * \param[in] ptr       The block to resize. May be NULL.
 * \param[in] old_size  Ignored.
 * \param[in] new_size  The requested size, in bytes.
 *
 * \return The result of realloc(ptr, new_size).
 *
 * \note Exported for composition with custom blocks.
 *
 * \warning Follows libc semantics. realloc(p, 0) frees p and returns
 *          NULL. See ba_realloc for the implications.
 */
void*
ba_default_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size);

#endif /* BLOBALLOC_H */

#ifdef BLOBALLOC_IMPL
#ifndef BLOBALLOC_IMPL_DONE
#define BLOBALLOC_IMPL_DONE

#include <stdlib.h>

void*
ba_default_alloc(void* ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}

void
ba_default_free(void* ctx, void* ptr, size_t size) {
    (void)ctx;
    (void)size;
    free(ptr);
}

void*
ba_default_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size) {
    (void)ctx;
    (void)old_size;
    return realloc(ptr, new_size);
}

static const BlobAlloc ba_default_block = {
    .ctx     = NULL,
    .alloc   = ba_default_alloc,
    .free    = ba_default_free,
    .realloc = ba_default_realloc,
};

const BlobAlloc*
ba_default(void) {
    return &ba_default_block;
}

void*
ba_alloc(const BlobAlloc* ab, size_t size) {
    return ab->alloc(ab->ctx, size);
}

void
ba_free(const BlobAlloc* ab, void* ptr, size_t size) {
    ab->free(ab->ctx, ptr, size);
}

void*
ba_realloc(const BlobAlloc* ab, void* ptr, size_t old_size, size_t new_size) {
    return ab->realloc(ab->ctx, ptr, old_size, new_size);
}

#endif /* BLOBALLOC_IMPL_DONE */
#endif /* BLOBALLOC_IMPL */