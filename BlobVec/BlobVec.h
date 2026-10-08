/**
 * \file BlobVec.h
 * \author Rory Graham (rorygraham2\@gmail.com)
 * \brief An owning, allocator-aware dynamic container of byte blobs.
 * \version 0.1
 *
 * A sibling to BlobBox with a different ownership contract. Where BlobBox
 * holds entries it does not own, BlobVec owns every entry it holds:
 * entries are bound to one vector at a time, and destroying the vector
 * destroys the entries.
 *
 * Ownership semantics:
 *   - An entry can belong to at most one vector at a time. bv_append and
 *     bv_appendidx refuse an entry that already has an owner, returning
 *     BV_OWNED.
 *   - bv_destroy destroys every entry, then the vector.
 *   - bv_clear destroys every entry but keeps the vector's capacity.
 *   - bv_deassignidx and bv_deassignptr remove an entry from the vector
 *     without destroying it. The entry's owner is cleared, and the
 *     caller owns it. It may be re-appended to another BlobVec.
 *   - bv_ent_destroy refuses to free an owned entry. Remove it from its
 *     vector first, or use bv_deassignidx.
 *
 * Entry model:
 *   Entries are opaque handles to length-prefixed, max_align_t-aligned
 *   byte buffers. They are not typed; interpretation is the caller's.
 *
 * Allocator lifetime:
 *   Each entry holds a const BlobAlloc* to the block that created it.
 *   That block must outlive every entry created from it. The vector's
 *   own allocator (used for the pointer array and the vector struct) is
 *   separate, and may differ from the entries' allocators.
 *
 * Single-header library. Define BV_IMPL in exactly one translation unit
 * before including this file to emit the implementation.
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

#ifndef BLOBVEC_H
#define BLOBVEC_H

#include "BlobAlloc.h"
#include <stddef.h>

/**
 * \brief Return codes for BlobVec operations.
 */
typedef enum BlobVec_res_e {
    BV_OK,     /**< The operation completed. */
    BV_OOM,    /**< The allocator returned NULL. */
    BV_OWNED,  /**< The entry already has an owner. */
    BV_RANGE,  /**< An argument is out of range. */
    BV_NPARAM, /**< A required pointer argument is NULL. */
} BlobVecRes;

/**
 * \brief An opaque handle to a blob entry.
 *
 * \invariant The entry holds a non-NULL pointer to the BlobAlloc block
 *            that created it.
 * \invariant The entry length is greater than zero.
 * \invariant The owner is either NULL or a valid BlobVec.
 * \invariant If the owner is not NULL, the entry is present in that
 *            vector at exactly one index.
 */
typedef struct BlobVecEnt_s BlobVecEnt;

/**
 * \brief An opaque handle to an owning dynamic container of blob entries.
 *
 * \invariant The pointer array is not NULL.
 * \invariant The capacity is greater than zero.
 * \invariant The length is less than or equal to the capacity.
 * \invariant For i in [0, bv_getlen(vec)), array[i] is not NULL and
 *            array[i]->owner == vec.
 * \invariant For i in [bv_getlen(vec), bv_getcap(vec)), array[i] is NULL.
 */
typedef struct BlobVec_s    BlobVec;

/**
 * \brief Compares two byte buffers.
 *
 * \param[in] a     The first buffer.
 * \param[in] b     The second buffer.
 * \param[in] alen  The length of the first buffer, in bytes.
 * \param[in] blen  The length of the second buffer, in bytes.
 *
 * \return A negative value if a is less than b.
 * \return Zero if a is equal to b.
 * \return A positive value if a is greater than b.
 *
 * \pre  a != NULL
 * \pre  b != NULL
 * \pre  alen > 0
 * \pre  blen > 0
 *
 * \note The comparator must give the same order for the same inputs.
 *       bv_sort and bv_binarysearch both rely on this.
 * \note To sort by more than one key, do all comparisons inside this
 *       function. Do not sort by length first and compare bytes second.
 */
typedef int                 (*BlobVecCmpFn)(const void* a, const void* b, size_t alen, size_t blen);

/**
 * \brief Called on each entry before it is destroyed.
 *
 * \param[in] ent  The entry about to be destroyed.
 *
 * \note The destructor runs before the entry's memory is returned to its
 *       allocator. The entry's data is still valid at this point.
 * \note Use this to release resources held inside the blob, such as
 *       nested allocations.
 *
 * \warning Do not call bv_ent_destroy from inside the destructor. The
 *          vector will destroy the entry after the destructor returns.
 */
typedef void                (*BlobVecDestFn)(BlobVecEnt* ent);

/* ------------------------------------------------------------------ */
/* entry construction and destruction                                  */
/* ------------------------------------------------------------------ */

/**
 * \brief Creates a blob entry and copies data into it.
 *
 * \param[in] ab    The allocator block. Must not be NULL.
 * \param[in] data  The source data. Must not be NULL.
 * \param[in] len   The number of bytes to copy. Must be greater than zero.
 *
 * \return A pointer to the new entry.
 * \return NULL on failure.
 *
 * \pre  ab != NULL
 * \pre  data != NULL
 * \pre  len > 0
 *
 * \note The entry is not owned by any vector. bv_append sets the owner.
 * \note The entry records ab. The block must outlive the entry.
 *
 * \warning If you do not add the entry to a vector, and you do not
 *          destroy it with bv_ent_destroy, the memory leaks.
 *
 * \example
 * const BlobAlloc* ab = ba_default();
 * const char msg[] = "hello";
 * BlobVecEnt* e = bv_ent_create(ab, msg, sizeof msg - 1);
 * if (e == NULL) {
 *     // handle failure
 * }
 */
BlobVecEnt*
bv_ent_create(const BlobAlloc* ab, const void* data, size_t len);

/**
 * \brief Creates a blob entry without initial data.
 *
 * \param[in] ab    The allocator block. Must not be NULL.
 * \param[in] len   The number of bytes to allocate. Must be greater than
 *                  zero.
 *
 * \return A pointer to the new entry.
 * \return NULL on failure.
 *
 * \pre  ab != NULL
 * \pre  len > 0
 *
 * \note The data buffer holds uninitialized bytes. Write to it with
 *       bv_ent_data_mut before you read it.
 * \note The entry is not owned by any vector.
 */
BlobVecEnt*
bv_ent_create_uninit(const BlobAlloc* ab, size_t len);

/**
 * \brief Destroys a blob entry and returns its memory to its allocator.
 *
 * \param[in] ent  The entry to destroy. May be NULL.
 *
 * \return BV_OK if ent is NULL, or on success.
 * \return BV_OWNED if ent has an owner.
 *
 * \note A NULL ent is a no-op.
 * \note This function does not run any vector destructor. If the entry
 *       has an owner, remove it with bv_deassignidx or destroy the whole
 *       vector with bv_destroy instead.
 * \note The entry returns to the allocator it was created from, not to
 *       any vector's allocator.
 *
 * \warning This function refuses to free an owned entry. That is a
 *          safety check, not a bug. To destroy an owned entry, first
 *          remove it from its vector with bv_deassignidx, which clears
 *          the owner and returns the entry to you.
 */
BlobVecRes
bv_ent_destroy(BlobVecEnt* ent);

/**
 * \brief Returns the allocator block that created an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The entry's allocator block.
 * \return NULL if ent is NULL.
 */
const BlobAlloc*
bv_ent_alloc(const BlobVecEnt* ent);

/**
 * \brief Returns the vector that owns an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The owning vector.
 * \return NULL if ent is NULL or the entry has no owner.
 */
const BlobVec*
bv_ent_owner(const BlobVecEnt* ent);

/**
 * \brief Returns the length of an entry, in bytes.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The entry length.
 * \return Zero if ent is NULL.
 */
size_t
bv_ent_len(const BlobVecEnt* ent);

/**
 * \brief Returns a read-only pointer to an entry's data.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return A pointer to the first byte of the data.
 * \return NULL if ent is NULL.
 */
const unsigned char*
bv_ent_data(const BlobVecEnt* ent);

/**
 * \brief Returns a writable pointer to an entry's data.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return A pointer to the first byte of the data.
 * \return NULL if ent is NULL.
 *
 * \note You can write up to bv_ent_len(ent) bytes.
 */
unsigned char*
bv_ent_data_mut(BlobVecEnt* ent);

/* ------------------------------------------------------------------ */
/* container construction and destruction                              */
/* ------------------------------------------------------------------ */

/**
 * \brief Creates an empty vector.
 *
 * \param[in] ab    The allocator block for the vector. Must not be NULL.
 * \param[in] init  The initial capacity, in entries. Zero means one.
 * \param[in] cmp   The comparator for bv_sort and bv_binarysearch. May
 *                  be NULL if you will not use those functions.
 * \param[in] dest  The destructor for entries. Must not be NULL.
 *
 * \return A pointer to the new vector.
 * \return NULL on failure.
 *
 * \pre  ab != NULL
 * \pre  dest != NULL
 *
 * \note The vector allocates from ab. It does not use ab to manage the
 *       entries you add later. Each entry keeps its own allocator.
 * \note dest may be a function that does nothing if your entries hold
 *       no resources requiring cleanup.
 * \note If cmp is NULL, bv_sort and bv_binarysearch return BV_NPARAM
 *       and NULL respectively.
 *
 * \example
 * static void noop_destroy(BlobVecEnt* ent) { (void)ent; }
 * BlobVec* v = bv_create(ba_default(), 8, cmp_bytes, noop_destroy);
 * if (v == NULL) {
 *     // handle failure
 * }
 */
BlobVec*
bv_create(const BlobAlloc* ab, size_t init, BlobVecCmpFn cmp, BlobVecDestFn dest);

/**
 * \brief Destroys every entry, then destroys the vector.
 *
 * \param[in] vec  The vector. May be NULL.
 *
 * \note A NULL vec is a no-op.
 * \note For each entry, the vector calls its destructor, then returns
 *       the entry to its own allocator. The destructor runs while the
 *       entry's data is still valid.
 */
void
bv_destroy(BlobVec* vec);

/* ------------------------------------------------------------------ */
/* container queries                                                   */
/* ------------------------------------------------------------------ */

/**
 * \brief Returns the number of entries in a vector.
 *
 * \param[in] vec  The vector. May be NULL.
 *
 * \return The entry count.
 * \return Zero if vec is NULL.
 */
size_t
bv_getlen(const BlobVec* vec);

/**
 * \brief Returns the capacity of a vector, in entries.
 *
 * \param[in] vec  The vector. May be NULL.
 *
 * \return The capacity.
 * \return Zero if vec is NULL.
 */
size_t
bv_getcap(const BlobVec* vec);

/**
 * \brief Returns a read-only pointer to the entry at an index.
 *
 * \param[in] vec  The vector. May be NULL.
 * \param[in] idx  The index.
 *
 * \return A pointer to the entry.
 * \return NULL if vec is NULL or idx is out of range.
 *
 * \pre  idx < bv_getlen(vec)
 */
const BlobVecEnt*
bv_getidx(const BlobVec* vec, size_t idx);

/**
 * \brief Returns a writable pointer to the entry at an index.
 *
 * \param[in] vec  The vector. May be NULL.
 * \param[in] idx  The index.
 *
 * \return A pointer to the entry.
 * \return NULL if vec is NULL or idx is out of range.
 *
 * \pre  idx < bv_getlen(vec)
 */
BlobVecEnt*
bv_getidx_mut(BlobVec* vec, size_t idx);

/* ------------------------------------------------------------------ */
/* capacity                                                            */
/* ------------------------------------------------------------------ */

/**
 * \brief Sets the capacity of a vector.
 *
 * \param[in,out] vec  The vector. Must not be NULL.
 * \param[in]     num  The new capacity, in entries. Must be greater than
 *                     zero, and not less than bv_getlen(vec).
 *
 * \return BV_OK on success.
 * \return BV_NPARAM if vec is NULL.
 * \return BV_RANGE if num is zero, if num is less than the current
 *         length, or if the size calculation overflowed.
 * \return BV_OOM if the allocator returned NULL.
 *
 * \pre  vec != NULL
 * \pre  num > 0
 * \pre  num >= bv_getlen(vec)
 *
 * \note If num is greater than the current capacity, the new slots are
 *       zeroed.
 * \note On failure, the vector is unchanged.
 */
BlobVecRes
bv_realloc(BlobVec* vec, size_t num);

/**
 * \brief Makes sure the capacity is at least a minimum value.
 *
 * \param[in,out] vec  The vector. Must not be NULL.
 * \param[in]     min  The minimum capacity, in entries.
 *
 * \return BV_OK on success.
 * \return BV_NPARAM if vec is NULL.
 * \return BV_RANGE if the capacity request cannot be met.
 * \return BV_OOM if the allocator returned NULL.
 *
 * \pre  vec != NULL
 *
 * \note If the current capacity is already at least min, this function
 *       does nothing.
 * \note The vector grows by doubling.
 */
BlobVecRes
bv_reserve(BlobVec* vec, size_t min);

/* ------------------------------------------------------------------ */
/* mutation                                                            */
/* ------------------------------------------------------------------ */

/**
 * \brief Adds an entry to the end of a vector.
 *
 * \param[in,out] vec  The vector. Must not be NULL.
 * \param[in]     ent  The entry. Must not be NULL, and must not already
 *                     have an owner.
 *
 * \return BV_OK on success.
 * \return BV_NPARAM if vec or ent is NULL.
 * \return BV_OWNED if ent already has an owner.
 * \return BV_RANGE if the capacity request cannot be met.
 * \return BV_OOM if the allocator returned NULL.
 *
 * \pre  vec != NULL
 * \pre  ent != NULL
 * \pre  bv_ent_owner(ent) == NULL
 *
 * \note On success, ent->owner is set to vec.
 * \note On failure, vec and ent are both unchanged.
 */
BlobVecRes
bv_append(BlobVec* vec, BlobVecEnt* ent);

/**
 * \brief Adds an entry at a given index.
 *
 * \param[in,out] vec  The vector. Must not be NULL.
 * \param[in]     ent  The entry. Must not be NULL, and must not already
 *                     have an owner.
 * \param[in]     idx  The index. Must be in [0, bv_getlen(vec)].
 *
 * \return BV_OK on success.
 * \return BV_NPARAM if vec or ent is NULL.
 * \return BV_RANGE if idx is out of range, or if the capacity request
 *         cannot be met.
 * \return BV_OWNED if ent already has an owner.
 * \return BV_OOM if the allocator returned NULL.
 *
 * \pre  vec != NULL
 * \pre  ent != NULL
 * \pre  idx <= bv_getlen(vec)
 * \pre  bv_ent_owner(ent) == NULL
 *
 * \note Entries at idx and above move one slot to the right.
 * \note If idx equals bv_getlen(vec), this function appends.
 * \note On success, ent->owner is set to vec.
 */
BlobVecRes
bv_appendidx(BlobVec* vec, BlobVecEnt* ent, size_t idx);

/**
 * \brief Removes the entry at an index and returns it.
 *
 * \param[in,out] vec  The vector. Must not be NULL.
 * \param[in]     idx  The index.
 *
 * \return A pointer to the removed entry.
 * \return NULL if vec is NULL or idx is out of range.
 *
 * \pre  idx < bv_getlen(vec)
 *
 * \note The entry is not destroyed. The entry's owner is cleared, and
 *       the caller owns it. It may be re-appended to another BlobVec.
 * \note Entries above idx move one slot to the left.
 */
BlobVecEnt*
bv_deassignidx(BlobVec* vec, size_t idx);

/**
 * \brief Removes the first occurrence of an entry and returns it.
 *
 * \param[in,out] vec  The vector. Must not be NULL.
 * \param[in]     ent  The entry to remove. Must not be NULL.
 *
 * \return A pointer to the removed entry.
 * \return NULL if vec or ent is NULL, if ent is not in the vector, or if
 *         ent's owner is not vec.
 *
 * \pre  vec != NULL
 * \pre  ent != NULL
 *
 * \note This function does a linear scan. The cost is O(n).
 * \note The entry's owner is cleared. The caller owns it.
 */
BlobVecEnt*
bv_deassignptr(BlobVec* vec, const BlobVecEnt* ent);

/**
 * \brief Destroys every entry and empties the vector.
 *
 * \param[in,out] vec  The vector. May be NULL.
 *
 * \note A NULL vec is a no-op.
 * \note The vector keeps its capacity. Reuse is cheap.
 * \note For each entry, the vector calls its destructor, then returns
 *       the entry to its own allocator.
 */
void
bv_clear(BlobVec* vec);

/* ------------------------------------------------------------------ */
/* sort and search                                                     */
/* ------------------------------------------------------------------ */

/**
 * \brief Sorts the entries of a vector.
 *
 * \param[in,out] vec  The vector. Must not be NULL and must have a
 *                     comparator.
 *
 * \return BV_OK on success.
 * \return BV_NPARAM if vec is NULL or vec has no comparator.
 * \return BV_RANGE if the size calculation overflowed.
 * \return BV_OOM if the scratch allocation returned NULL.
 *
 * \pre  vec != NULL
 * \pre  vec has a non-NULL comparator.
 *
 * \note The sort is stable. Equal entries keep their original order.
 * \note This function uses O(n) scratch memory from the vector's
 *       allocator.
 * \note A vector with zero or one entry is already sorted. This
 *       function returns BV_OK without allocating.
 * \note The comparator is the one supplied to bv_create. bv_binarysearch
 *       must use the same comparator.
 */
BlobVecRes
bv_sort(BlobVec* vec);

/**
 * \brief Searches for an entry that matches a byte buffer.
 *
 * \param[in] vec   The vector. Must not be NULL. Must be sorted with the
 *                  comparator supplied to bv_create.
 * \param[in] data  The buffer to match. Must not be NULL.
 * \param[in] len   The length of the buffer. Must be greater than zero.
 *
 * \return A pointer to a matching entry.
 * \return NULL if no entry matches, or if an argument is invalid.
 *
 * \pre  vec != NULL
 * \pre  data != NULL
 * \pre  len > 0
 * \pre  vec has a non-NULL comparator.
 * \pre  vec is sorted with that comparator.
 *
 * \note The vector is not changed. The returned entry is read-only.
 * \note If more than one entry matches, this function returns one of
 *       them. Which one is not defined.
 *
 * \warning If the vector is not sorted with its comparator, the result
 *          is undefined. This function can return NULL when a match is
 *          present, and can return a non-matching entry when the search
 *          key is not present.
 */
const BlobVecEnt*
bv_binarysearch(const BlobVec* vec, const void* data, size_t len);

#endif /* BLOBVEC_H */

#ifdef BV_IMPL
#ifndef BV_IMPL_DONE
#define BV_IMPL_DONE

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

struct BlobVecEnt_s {
    const BlobAlloc* ab;
    BlobVec*         owner;
    size_t           len;
    alignas(max_align_t) unsigned char data[];
};

struct BlobVec_s {
    BlobVecEnt**     array;
    size_t           len;
    size_t           cap;
    const BlobAlloc* ab;
    BlobVecCmpFn     cmp;
    BlobVecDestFn    dest;
};

BlobVecEnt*
bv_ent_create(const BlobAlloc* ab, const void* data, size_t len) {
    if (!ab || !data || len == 0) {
        return NULL;
    }

    size_t bytes = 0;
    if (__builtin_add_overflow(offsetof(BlobVecEnt, data), len, &bytes)) {
        return NULL;
    }

    BlobVecEnt* out = ba_alloc(ab, bytes);
    if (!out) {
        return NULL;
    }

    out->ab    = ab;
    out->owner = NULL;
    out->len   = len;
    memcpy(out->data, data, len);
    return out;
}

BlobVecEnt*
bv_ent_create_uninit(const BlobAlloc* ab, size_t len) {
    if (!ab || len == 0) {
        return NULL;
    }

    size_t bytes = 0;
    if (__builtin_add_overflow(offsetof(BlobVecEnt, data), len, &bytes)) {
        return NULL;
    }

    BlobVecEnt* out = ba_alloc(ab, bytes);
    if (!out) {
        return NULL;
    }

    out->ab    = ab;
    out->owner = NULL;
    out->len   = len;
    return out;
}

static inline size_t
bv_ent_bytes(size_t len) {
    return offsetof(BlobVecEnt, data) + len;
}

BlobVecRes
bv_ent_destroy(BlobVecEnt* ent) {
    if (ent && ent->owner) {
        return BV_OWNED;
    }

    if (ent) {
        ba_free(ent->ab, ent, bv_ent_bytes(ent->len));
    }
    return BV_OK;
}

const BlobAlloc*
bv_ent_alloc(const BlobVecEnt* ent) {
    return ent ? ent->ab : NULL;
}

const BlobVec*
bv_ent_owner(const BlobVecEnt* ent) {
    return ent ? ent->owner : NULL;
}

size_t
bv_ent_len(const BlobVecEnt* ent) {
    return ent ? ent->len : 0;
}

const unsigned char*
bv_ent_data(const BlobVecEnt* ent) {
    return ent ? ent->data : NULL;
}

unsigned char*
bv_ent_data_mut(BlobVecEnt* ent) {
    return ent ? ent->data : NULL;
}

BlobVec*
bv_create(const BlobAlloc* ab, size_t init, BlobVecCmpFn cmp, BlobVecDestFn dest) {
    if (!ab || !dest) {
        return NULL;
    }

    if (init == 0) {
        init = 1;
    }

    size_t array_bytes = 0;
    if (__builtin_mul_overflow(sizeof(BlobVecEnt*), init, &array_bytes)) {
        return NULL;
    }

    BlobVec* out = ba_alloc(ab, sizeof(*out));
    if (!out) {
        return NULL;
    }

    BlobVecEnt** array = ba_alloc(ab, array_bytes);
    if (!array) {
        ba_free(ab, out, sizeof(*out));
        return NULL;
    }
    memset(array, 0, array_bytes);

    out->array = array;
    out->len   = 0;
    out->cap   = init;
    out->ab    = ab;
    out->cmp   = cmp;
    out->dest  = dest;
    return out;
}

static void
bv_ent_destroy_unchecked(BlobVecEnt* ent) {
    ba_free(ent->ab, ent, bv_ent_bytes(ent->len));
}

void
bv_destroy(BlobVec* vec) {
    if (!vec) {
        return;
    }

    for (size_t i = 0; i < vec->len; i++) {
        BlobVecEnt* ent = vec->array[i];
        vec->dest(ent);
        bv_ent_destroy_unchecked(ent);
    }
    ba_free(vec->ab, vec->array, sizeof(BlobVecEnt*) * vec->cap);
    ba_free(vec->ab, vec, sizeof(*vec));
}

void
bv_clear(BlobVec* vec) {
    if (!vec) {
        return;
    }

    for (size_t i = 0; i < vec->len; i++) {
        BlobVecEnt* ent = vec->array[i];
        vec->dest(ent);
        bv_ent_destroy_unchecked(ent);
    }
    memset(vec->array, 0, vec->len * sizeof(*vec->array));
    vec->len = 0;
}

size_t
bv_getlen(const BlobVec* vec) {
    return vec ? vec->len : 0;
}

size_t
bv_getcap(const BlobVec* vec) {
    return vec ? vec->cap : 0;
}

const BlobVecEnt*
bv_getidx(const BlobVec* vec, size_t idx) {
    return (vec && idx < vec->len) ? vec->array[idx] : NULL;
}

BlobVecEnt*
bv_getidx_mut(BlobVec* vec, size_t idx) {
    return (vec && idx < vec->len) ? vec->array[idx] : NULL;
}

BlobVecRes
bv_realloc(BlobVec* vec, size_t num) {
    if (!vec) {
        return BV_NPARAM;
    }

    if (num < vec->len || num == 0) {
        return BV_RANGE;
    }

    if (num == vec->cap) {
        return BV_OK;
    }

    size_t new_bytes = 0;
    if (__builtin_mul_overflow(sizeof(BlobVecEnt*), num, &new_bytes)) {
        return BV_RANGE;
    }

    size_t       old_bytes = sizeof(BlobVecEnt*) * vec->cap;

    BlobVecEnt** new_arr   = ba_realloc(vec->ab, vec->array, old_bytes, new_bytes);
    if (!new_arr) {
        return BV_OOM;
    }

    if (new_bytes > old_bytes) {
        memset(new_arr + vec->cap, 0, (new_bytes - old_bytes));
    }

    vec->array = new_arr;
    vec->cap   = num;

    return BV_OK;
}

BlobVecRes
bv_reserve(BlobVec* vec, size_t min) {
    if (!vec) {
        return BV_NPARAM;
    }

    if (vec->cap >= min) {
        return BV_OK;
    }

    size_t new_cap = vec->cap ? vec->cap : 1;
    while (new_cap < min) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min;
            break;
        }
        new_cap *= 2;
    }
    return bv_realloc(vec, new_cap);
}

BlobVecRes
bv_append(BlobVec* vec, BlobVecEnt* ent) {
    if (!vec || !ent) {
        return BV_NPARAM;
    }

    if (ent->owner) {
        return BV_OWNED;
    }

    if (vec->len >= vec->cap) {
        BlobVecRes realloc_res = bv_reserve(vec, vec->len + 1);
        if (realloc_res != BV_OK) {
            return realloc_res;
        }
    }

    vec->array[vec->len] = ent;
    ent->owner           = vec;
    vec->len++;

    return BV_OK;
}

BlobVecRes
bv_appendidx(BlobVec* vec, BlobVecEnt* ent, size_t idx) {
    if (!vec || !ent) {
        return BV_NPARAM;
    }

    if (idx > vec->len) {
        return BV_RANGE;
    }

    if (ent->owner) {
        return BV_OWNED;
    }

    if (vec->len >= vec->cap) {
        BlobVecRes realloc_res = bv_reserve(vec, vec->len + 1);
        if (realloc_res != BV_OK) {
            return realloc_res;
        }
    }

    if (idx < vec->len) {
        memmove(&vec->array[idx + 1], &vec->array[idx], (vec->len - idx) * sizeof(*vec->array));
    }
    vec->array[idx] = ent;
    vec->len++;
    ent->owner = vec;
    return BV_OK;
}

BlobVecEnt*
bv_deassignidx(BlobVec* vec, size_t idx) {
    if (!vec || idx >= vec->len) {
        return NULL;
    }

    BlobVecEnt* out = vec->array[idx];

    memmove(&vec->array[idx], &vec->array[idx + 1], (vec->len - idx - 1) * sizeof(*vec->array));
    vec->array[vec->len - 1] = NULL;
    vec->len--;
    out->owner = NULL;
    return out;
}

BlobVecEnt*
bv_deassignptr(BlobVec* vec, const BlobVecEnt* ent) {
    if (!vec || !ent || ent->owner != vec) {
        return NULL;
    }

    for (size_t i = 0; i < vec->len; i++) {
        if (vec->array[i] == ent) {
            return bv_deassignidx(vec, i);
        }
    }
    return NULL;
}

static void
bv_merge(BlobVecEnt** a, BlobVecEnt** tmp, size_t lo, size_t mid, size_t hi, BlobVecCmpFn cmp) {
    size_t i = lo;
    size_t j = mid;
    size_t k = lo;

    while (i < mid && j < hi) {
        if (cmp(a[i]->data, a[j]->data, a[i]->len, a[j]->len) <= 0) {
            tmp[k++] = a[i++];
        } else {
            tmp[k++] = a[j++];
        }
    }

    while (i < mid) {
        tmp[k++] = a[i++];
    }

    while (j < hi) {
        tmp[k++] = a[j++];
    }

    memcpy(&a[lo], &tmp[lo], (hi - lo) * sizeof(*a));
}

static void
bv_msort(BlobVecEnt** a, BlobVecEnt** tmp, size_t lo, size_t hi, BlobVecCmpFn cmp) {
    if (hi - lo < 2) {
        return;
    }

    size_t mid = lo + ((hi - lo) / 2);
    bv_msort(a, tmp, lo, mid, cmp);
    bv_msort(a, tmp, mid, hi, cmp);
    bv_merge(a, tmp, lo, mid, hi, cmp);
}

BlobVecRes
bv_sort(BlobVec* vec) {
    if (!vec || !vec->cmp) {
        return BV_NPARAM;
    }

    if (vec->len < 2) {
        return BV_OK;
    }

    size_t bytes = 0;
    if (__builtin_mul_overflow(sizeof(*vec->array), vec->len, &bytes)) {
        return BV_RANGE;
    }

    BlobVecEnt** tmp = ba_alloc(vec->ab, bytes);
    if (!tmp) {
        return BV_OOM;
    }

    bv_msort(vec->array, tmp, 0, vec->len, vec->cmp);
    ba_free(vec->ab, tmp, bytes);
    return BV_OK;
}

const BlobVecEnt*
bv_binarysearch(const BlobVec* vec, const void* data, size_t len) {
    if (!vec || !data || !vec->cmp || len == 0) {
        return NULL;
    }

    if (vec->len == 0) {
        return NULL;
    }

    size_t lo = 0;
    size_t hi = vec->len;

    while (lo < hi) {
        size_t mid = lo + ((hi - lo) / 2);
        int    c   = vec->cmp(vec->array[mid]->data, data, vec->array[mid]->len, len);
        if (c == 0) {
            return vec->array[mid];
        }

        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

#endif /* BV_IMPL_DONE */
#endif /* BV_IMPL */