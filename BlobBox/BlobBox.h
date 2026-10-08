/**
 * \file BlobBox.h
 * \author Rory Graham (rorygraham2\@gmail.com)
 * \brief A non-owning, allocator-aware dynamic container of byte blobs.
 * \version 0.1
 *
 * A sibling to BlobVec with a different ownership contract. Where BlobVec
 * owns its entries and enforces single-ownership, BlobBox does neither:
 * every entry carries a pointer to the BlobAlloc it was created from, and
 * may be referenced by any number of containers simultaneously.
 *
 * Ownership semantics:
 *   - bb_destroy     frees the container and its pointer array;
 *                    entries are left untouched.
 *   - bb_destroy_all destroys every entry, then the container.
 *   - The same entry may appear in multiple BlobBoxes. Calling
 *     bb_destroy_all on two boxes that share an entry is a double-free.
 *
 * Entry model:
 *   Entries are opaque handles to length-prefixed, max_align_t-aligned
 *   byte buffers. They are not typed; interpretation is the caller's.
 *
 * Allocator lifetime:
 *   Each entry holds a const BlobAlloc* to the block that created it.
 *   That block must outlive every entry created from it. Stack-local
 *   blocks that escape their scope are the primary way to misuse this
 *   library.
 *
 * Single-header library. Define BLOBBOX_IMPL in exactly one translation
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

#ifndef BLOBBOX_H
#define BLOBBOX_H

#include "BlobAlloc.h"
#include <stddef.h>

/**
 * \brief Return codes for BlobBox operations.
 */
typedef enum BlobBoxRes_s {
    BB_OK,     /**< The operation completed. */
    BB_OOM,    /**< The allocator returned NULL. */
    BB_RANGE,  /**< An argument is out of range. */
    BB_NPARAM, /**< A required pointer argument is NULL. */
} BlobBoxRes;

/**
 * \brief An opaque handle to a blob entry.
 *
 * You can hold, pass, and compare this handle. You cannot read or write
 * the fields. Use the bb_ent_* accessors to read the entry, and
 * bb_ent_data_mut to write the blob data.
 *
 * \invariant The entry holds a non-NULL pointer to the BlobAlloc block
 *            that created it.
 * \invariant The entry length is greater than zero.
 * \invariant The entry data buffer holds at least bb_ent_len bytes.
 */
typedef struct BB_entry_s BlobBoxEnt;

/**
 * \brief An opaque handle to a dynamic container of blob entries.
 *
 * The container holds pointers to entries. It does not own the entries.
 *
 * \invariant The pointer array is not NULL.
 * \invariant The capacity is greater than zero.
 * \invariant The length is less than or equal to the capacity.
 * \invariant The pointer array has space for bb_getcap(box) pointers.
 * \invariant For i in [0, bb_getlen(box)), array[i] is not NULL.
 * \invariant The container holds a non-NULL pointer to its BlobAlloc
 *            block.
 */
typedef struct BlobBox_s  BlobBox;

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
 *       bb_sort and bb_binarysearch both rely on this.
 * \note To sort by more than one key, do all comparisons inside this
 *       function. Do not sort by length first and compare bytes second.
 */
typedef int               (*BlobBoxCmpFn)(const void* a, const void* b, size_t alen, size_t blen);

/* ------------------------------------------------------------------ */
/* entry construction and destruction                                  */
/* ------------------------------------------------------------------ */

/**
 * \brief Creates a blob entry and copies data into it.
 *
 * \param[in] ab    The allocator block to use. Must not be NULL.
 * \param[in] data  The source data. Must not be NULL.
 * \param[in] len   The number of bytes to copy. Must be greater than zero.
 *
 * \return A pointer to the new entry.
 * \return NULL on failure.
 *
 * \retval NULL ab is NULL, data is NULL, or len is zero.
 * \retval NULL the size calculation overflowed.
 * \retval NULL the allocator returned NULL.
 *
 * \pre  ab != NULL
 * \pre  data != NULL
 * \pre  len > 0
 *
 * \note The entry copies the data. You can free or change the source
 *       buffer after this call.
 * \note The entry records ab. The block must outlive the entry.
 * \note The entry is not in any container. You must either add it to a
 *       container or destroy it with bb_ent_destroy.
 *
 * \warning If you do not destroy the entry, and you do not add it to a
 *          container that calls bb_destroy_all, the memory leaks.
 *
 * \example
 * const BlobAlloc* ab = ba_default();
 * const char msg[] = "hello";
 * BlobBoxEnt* e = bb_ent_create(ab, msg, sizeof msg - 1);
 * if (e == NULL) {
 *     // handle failure
 * }
 */
BlobBoxEnt*
bb_ent_create(const BlobAlloc* ab, const void* data, size_t len);

/**
 * \brief Creates a blob entry without initial data.
 *
 * \param[in] ab    The allocator block to use. Must not be NULL.
 * \param[in] len   The number of bytes to allocate. Must be greater than
 *                  zero.
 *
 * \return A pointer to the new entry.
 * \return NULL on failure.
 *
 * \retval NULL ab is NULL or len is zero.
 * \retval NULL the size calculation overflowed.
 * \retval NULL the allocator returned NULL.
 *
 * \pre  ab != NULL
 * \pre  len > 0
 *
 * \note The data buffer holds uninitialized bytes. Write to it with
 *       bb_ent_data_mut before you read it.
 * \note Use this function when you will fill the buffer with a single
 *       read or memcpy. The zero-fill in bb_ent_create costs time you
 *       do not need to spend.
 *
 * \warning If you do not destroy the entry, and you do not add it to a
 *          container that calls bb_destroy_all, the memory leaks.
 *
 * \example
 * BlobBoxEnt* e = bb_ent_create_uninit(ab, 4096);
 * if (e != NULL) {
 *     ssize_t n = read(fd, bb_ent_data_mut(e), bb_ent_len(e));
 *     // ...
 * }
 */
BlobBoxEnt*
bb_ent_create_uninit(const BlobAlloc* ab, size_t len);

/**
 * \brief Destroys a blob entry and returns its memory to its allocator.
 *
 * \param[in] ent  The entry to destroy. May be NULL.
 *
 * \pre  If ent is not NULL, the entry's BlobAlloc block must still be
 *       alive.
 * \pre  If ent is not NULL, ent must not also be held by a container
 *       that will later call bb_destroy_all or bb_clear_all.
 *
 * \note A NULL ent is a no-op.
 * \note The entry returns to the allocator it was created from, not to
 *       any container's allocator.
 *
 * \warning If the same entry is referenced by more than one container,
 *          destroy it once and remove it from all containers. A second
 *          destroy is a double-free.
 */
void
bb_ent_destroy(BlobBoxEnt* ent);

/**
 * \brief Returns the allocator block that created an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The entry's allocator block.
 * \return NULL if ent is NULL.
 */
const BlobAlloc*
bb_ent_alloc(const BlobBoxEnt* ent);

/**
 * \brief Returns the length of an entry, in bytes.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The entry length.
 * \return Zero if ent is NULL.
 */
size_t
bb_ent_len(const BlobBoxEnt* ent);

/**
 * \brief Returns a read-only pointer to an entry's data.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return A pointer to the first byte of the data.
 * \return NULL if ent is NULL.
 *
 * \note The pointer is valid while the entry is alive. Destroying the
 *       entry invalidates the pointer.
 */
const unsigned char*
bb_ent_data(const BlobBoxEnt* ent);

/**
 * \brief Returns a writable pointer to an entry's data.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return A pointer to the first byte of the data.
 * \return NULL if ent is NULL.
 *
 * \note You can write up to bb_ent_len(ent) bytes. Do not write past
 *       that limit.
 * \note The pointer is valid while the entry is alive. Destroying the
 *       entry invalidates the pointer.
 */
unsigned char*
bb_ent_data_mut(BlobBoxEnt* ent);

/* ------------------------------------------------------------------ */
/* container construction and destruction                              */
/* ------------------------------------------------------------------ */

/**
 * \brief Creates an empty container.
 *
 * \param[in] ab    The allocator block for the container. Must not be
 *                  NULL.
 * \param[in] init  The initial capacity, in entries.
 *
 * \return A pointer to the new container.
 * \return NULL on failure.
 *
 * \retval NULL ab is NULL.
 * \retval NULL the size calculation overflowed.
 * \retval NULL an allocation returned NULL.
 *
 * \pre  ab != NULL
 *
 * \note If init is zero, the container starts with capacity one.
 * \note The container allocates from ab. It does not use ab to manage
 *       the entries you add later.
 *
 * \example
 * BlobBox* box = bb_create(ba_default(), 8);
 * if (box == NULL) {
 *     // handle failure
 * }
 */
BlobBox*
bb_create(const BlobAlloc* ab, size_t init);

/**
 * \brief Destroys a container and its pointer array.
 *
 * \param[in] box  The container. May be NULL.
 *
 * \note A NULL box is a no-op.
 * \note The entries are not destroyed. This function frees only the
 *       container and its pointer array.
 * \note Use bb_destroy_all to destroy the entries as well.
 *
 * \warning The entries are left in memory. If you do not hold a pointer
 *          to each entry, they leak.
 */
void
bb_destroy(BlobBox* box);

/**
 * \brief Destroys every entry, then destroys the container.
 *
 * \param[in] box  The container. May be NULL.
 *
 * \note A NULL box is a no-op.
 * \note Each entry returns to its own allocator, not to the container's
 *       allocator.
 *
 * \warning If the same entry is in more than one container, this
 *          function destroys it once. A later bb_destroy_all on the
 *          other container is a double-free.
 */
void
bb_destroy_all(BlobBox* box);

/* ------------------------------------------------------------------ */
/* container queries                                                   */
/* ------------------------------------------------------------------ */

/**
 * \brief Returns the number of entries in a container.
 *
 * \param[in] box  The container. May be NULL.
 *
 * \return The entry count.
 * \return Zero if box is NULL.
 */
size_t
bb_getlen(const BlobBox* box);

/**
 * \brief Returns the capacity of a container, in entries.
 *
 * \param[in] box  The container. May be NULL.
 *
 * \return The capacity.
 * \return Zero if box is NULL.
 */
size_t
bb_getcap(const BlobBox* box);

/**
 * \brief Returns a read-only pointer to the entry at an index.
 *
 * \param[in] box  The container. May be NULL.
 * \param[in] idx  The index.
 *
 * \return A pointer to the entry.
 * \return NULL if box is NULL or idx is out of range.
 *
 * \pre  idx < bb_getlen(box)
 */
const BlobBoxEnt*
bb_getidx(const BlobBox* box, size_t idx);

/**
 * \brief Returns a writable pointer to the entry at an index.
 *
 * \param[in] box  The container. May be NULL.
 * \param[in] idx  The index.
 *
 * \return A pointer to the entry.
 * \return NULL if box is NULL or idx is out of range.
 *
 * \note The returned handle is mutable. You can write to the entry's
 *       data with bb_ent_data_mut.
 *
 * \pre  idx < bb_getlen(box)
 */
BlobBoxEnt*
bb_getidx_mut(BlobBox* box, size_t idx);

/* ------------------------------------------------------------------ */
/* capacity                                                            */
/* ------------------------------------------------------------------ */

/**
 * \brief Sets the capacity of a container.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     num  The new capacity, in entries. Must be greater than
 *                     zero, and not less than bb_getlen(box).
 *
 * \return BB_OK on success.
 * \return BB_NPARAM if box is NULL.
 * \return BB_RANGE if num is zero, if num is less than the current
 *         length, or if the size calculation overflowed.
 * \return BB_OOM if the allocator returned NULL.
 *
 * \pre  box != NULL
 * \pre  num > 0
 * \pre  num >= bb_getlen(box)
 *
 * \note If num is greater than the current capacity, the new slots are
 *       zeroed.
 * \note If num is less than the current capacity, the container shrinks.
 *       Entries at indices >= num must not exist.
 * \note On failure, the container is unchanged.
 */
BlobBoxRes
bb_realloc(BlobBox* box, size_t num);

/**
 * \brief Makes sure the capacity is at least a minimum value.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     min  The minimum capacity, in entries.
 *
 * \return BB_OK on success.
 * \return BB_NPARAM if box is NULL.
 * \return BB_RANGE if the capacity request cannot be met.
 * \return BB_OOM if the allocator returned NULL.
 *
 * \pre  box != NULL
 *
 * \note If the current capacity is already at least min, this function
 *       does nothing.
 * \note The container grows by doubling. This keeps the cost of a
 *       sequence of appends low.
 */
BlobBoxRes
bb_reserve(BlobBox* box, size_t min);

/* ------------------------------------------------------------------ */
/* mutation                                                            */
/* ------------------------------------------------------------------ */

/**
 * \brief Adds an entry to the end of a container.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     ent  The entry to add. Must not be NULL.
 *
 * \return BB_OK on success.
 * \return BB_NPARAM if box or ent is NULL.
 * \return BB_RANGE if the capacity request cannot be met.
 * \return BB_OOM if the allocator returned NULL.
 *
 * \pre  box != NULL
 * \pre  ent != NULL
 *
 * \note The container grows if it is full.
 * \note The container does not copy the entry. It stores the pointer.
 * \note The same entry can be in more than one container.
 *
 * \warning The container does not own the entry. You must destroy the
 *          entry separately.
 */
BlobBoxRes
bb_append(BlobBox* box, BlobBoxEnt* ent);

/**
 * \brief Adds an entry at a given index.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     ent  The entry to add. Must not be NULL.
 * \param[in]     idx  The index. Must be in [0, bb_getlen(box)].
 *
 * \return BB_OK on success.
 * \return BB_NPARAM if box or ent is NULL.
 * \return BB_RANGE if idx is out of range, or if the capacity request
 *         cannot be met.
 * \return BB_OOM if the allocator returned NULL.
 *
 * \pre  box != NULL
 * \pre  ent != NULL
 * \pre  idx <= bb_getlen(box)
 *
 * \note Entries at idx and above move one slot to the right.
 * \note If idx equals bb_getlen(box), this function appends.
 *
 * \warning The container does not own the entry. You must destroy the
 *          entry separately.
 */
BlobBoxRes
bb_appendidx(BlobBox* box, BlobBoxEnt* ent, size_t idx);

/**
 * \brief Removes the entry at an index and returns it.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     idx  The index.
 *
 * \return A pointer to the removed entry.
 * \return NULL if box is NULL or idx is out of range.
 *
 * \pre  idx < bb_getlen(box)
 *
 * \note The entry is not destroyed. The caller owns it.
 * \note Entries above idx move one slot to the left.
 *
 * \example
 * BlobBoxEnt* e = bb_deassignidx(box, 3);
 * if (e != NULL) {
 *     // use e, then destroy it or add it elsewhere
 * }
 */
BlobBoxEnt*
bb_deassignidx(BlobBox* box, size_t idx);

/**
 * \brief Removes the first occurrence of an entry and returns it.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     ent  The entry to remove. Must not be NULL.
 *
 * \return A pointer to the removed entry.
 * \return NULL if box or ent is NULL, or if ent is not in the container.
 *
 * \pre  box != NULL
 * \pre  ent != NULL
 *
 * \note This function does a linear scan. The cost is O(n).
 * \note If the same pointer is in the container more than once, this
 *       function removes the first occurrence.
 * \note The entry is not destroyed. The caller owns it.
 */
BlobBoxEnt*
bb_deassignptr(BlobBox* box, const BlobBoxEnt* ent);

/**
 * \brief Destroys every entry and empties the container.
 *
 * \param[in,out] box  The container. May be NULL.
 *
 * \note A NULL box is a no-op.
 * \note The container keeps its capacity. Reuse is cheap.
 * \note Each entry returns to its own allocator.
 *
 * \warning If an entry is in more than one container, this function
 *          destroys it once. A later bb_destroy_all or bb_clear_all on
 *          the other container is a double-free.
 */
void
bb_clear_all(BlobBox* box);

/* ------------------------------------------------------------------ */
/* sort and search                                                     */
/* ------------------------------------------------------------------ */

/**
 * \brief Sorts the entries of a container.
 *
 * \param[in,out] box  The container. Must not be NULL.
 * \param[in]     cmp  The comparator. Must not be NULL.
 *
 * \return BB_OK on success.
 * \return BB_NPARAM if box or cmp is NULL.
 * \return BB_RANGE if the size calculation overflowed.
 * \return BB_OOM if the scratch allocation returned NULL.
 *
 * \pre  box != NULL
 * \pre  cmp != NULL
 *
 * \note The sort is stable. Equal entries keep their original order.
 * \note This function uses O(n) scratch memory from the container's
 *       allocator.
 * \note A container with zero or one entry is already sorted. This
 *       function returns BB_OK without allocating.
 * \note bb_binarysearch must use the same comparator as bb_sort.
 */
BlobBoxRes
bb_sort(BlobBox* box, BlobBoxCmpFn cmp);

/**
 * \brief Searches for an entry that matches a byte buffer.
 *
 * \param[in] box   The container. Must not be NULL. Must be sorted with
 *                  cmp.
 * \param[in] data  The buffer to match. Must not be NULL.
 * \param[in] len   The length of the buffer. Must be greater than zero.
 * \param[in] cmp   The comparator. Must not be NULL. Must be the same
 *                  comparator you gave to bb_sort.
 *
 * \return A pointer to a matching entry.
 * \return NULL if no entry matches, or if an argument is invalid.
 *
 * \pre  box != NULL
 * \pre  data != NULL
 * \pre  len > 0
 * \pre  cmp != NULL
 * \pre  box is sorted with cmp.
 *
 * \note The box is not changed. The returned entry is read-only.
 * \note If more than one entry matches, this function returns one of
 *       them. Which one is not defined.
 *
 * \warning If the box is not sorted with cmp, the result is undefined.
 *          This function can return NULL when a match is present, and
 *          can return a non-matching entry when the search key is not
 *          present.
 *
 * \example
 * // find an entry whose bytes are exactly {'f','o','o'}
 * const BlobBoxEnt* e = bb_binarysearch(box, "foo", 3, cmp_bytes);
 */
const BlobBoxEnt*
bb_binarysearch(const BlobBox* box, const void* data, size_t len, BlobBoxCmpFn cmp);

#endif /* BLOBBOX_H */

#ifdef BLOBBOX_IMPL
#ifndef BLOBBOX_IMPL_DONE
#define BLOBBOX_IMPL_DONE

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

struct BB_entry_s {
    const BlobAlloc* ab;
    size_t           len;
    alignas(max_align_t) unsigned char data[];
};

struct BlobBox_s {
    BlobBoxEnt**     array;
    size_t           len;
    size_t           cap;
    const BlobAlloc* ab;
};

BlobBoxEnt*
bb_ent_create(const BlobAlloc* ab, const void* data, size_t len) {
    if (!ab || !data || len == 0) {
        return NULL;
    }

    size_t bytes = 0;
    if (__builtin_add_overflow(offsetof(BlobBoxEnt, data), len, &bytes)) {
        return NULL;
    }

    BlobBoxEnt* out = ba_alloc(ab, bytes);
    if (!out) {
        return NULL;
    }

    out->ab  = ab;
    out->len = len;
    memcpy(out->data, data, len);
    return out;
}

BlobBoxEnt*
bb_ent_create_uninit(const BlobAlloc* ab, size_t len) {
    if (!ab || len == 0) {
        return NULL;
    }

    size_t bytes = 0;
    if (__builtin_add_overflow(offsetof(BlobBoxEnt, data), len, &bytes)) {
        return NULL;
    }

    BlobBoxEnt* out = ba_alloc(ab, bytes);
    if (!out) {
        return NULL;
    }

    out->ab  = ab;
    out->len = len;
    return out;
}

static inline size_t
bb_ent_bytes(size_t len) {
    return offsetof(BlobBoxEnt, data) + len;
}

void
bb_ent_destroy(BlobBoxEnt* ent) {
    if (ent) {
        ba_free(ent->ab, ent, bb_ent_bytes(ent->len));
    }
}

const BlobAlloc*
bb_ent_alloc(const BlobBoxEnt* ent) {
    return ent ? ent->ab : NULL;
}

size_t
bb_ent_len(const BlobBoxEnt* ent) {
    return ent ? ent->len : 0;
}

const unsigned char*
bb_ent_data(const BlobBoxEnt* ent) {
    return ent ? ent->data : NULL;
}

unsigned char*
bb_ent_data_mut(BlobBoxEnt* ent) {
    return ent ? ent->data : NULL;
}

BlobBox*
bb_create(const BlobAlloc* ab, size_t init) {
    if (!ab) {
        return NULL;
    }

    if (init == 0) {
        init = 1;
    }

    size_t bytes = 0;
    if (__builtin_mul_overflow(sizeof(BlobBoxEnt*), init, &bytes)) {
        return NULL;
    }

    BlobBox* out = ba_alloc(ab, sizeof(*out));
    if (!out) {
        return NULL;
    }

    BlobBoxEnt** array = ba_alloc(ab, bytes);
    if (!array) {
        ba_free(ab, out, sizeof(*out));
        return NULL;
    }
    memset(array, 0, bytes);

    out->array = array;
    out->len   = 0;
    out->cap   = init;
    out->ab    = ab;
    return out;
}

void
bb_destroy(BlobBox* box) {
    if (!box) {
        return;
    }

    const BlobAlloc* ab    = box->ab;
    BlobBoxEnt**     array = box->array;
    size_t           cap   = box->cap;

    ba_free(ab, array, cap * sizeof(*array));
    ba_free(ab, box, sizeof(*box));
}

void
bb_destroy_all(BlobBox* box) {
    if (!box) {
        return;
    }

    for (size_t i = 0; i < box->len; i++) {
        bb_ent_destroy(box->array[i]);
    }
    bb_destroy(box);
}

size_t
bb_getlen(const BlobBox* box) {
    return box ? box->len : 0;
}

size_t
bb_getcap(const BlobBox* box) {
    return box ? box->cap : 0;
}

const BlobBoxEnt*
bb_getidx(const BlobBox* box, size_t idx) {
    return (box && idx < box->len) ? box->array[idx] : NULL;
}

BlobBoxEnt*
bb_getidx_mut(BlobBox* box, size_t idx) {
    return (box && idx < box->len) ? box->array[idx] : NULL;
}

BlobBoxRes
bb_realloc(BlobBox* box, size_t num) {
    if (!box) {
        return BB_NPARAM;
    }

    if (num < box->len || num == 0) {
        return BB_RANGE;
    }

    if (num == box->cap) {
        return BB_OK;
    }

    size_t new_bytes = 0;
    if (__builtin_mul_overflow(sizeof(BlobBoxEnt*), num, &new_bytes)) {
        return BB_RANGE;
    }

    size_t       old_bytes = sizeof(BlobBoxEnt*) * box->cap;

    BlobBoxEnt** new_arr   = ba_realloc(box->ab, box->array, old_bytes, new_bytes);
    if (!new_arr) {
        return BB_OOM;
    }

    if (new_bytes > old_bytes) {
        memset(new_arr + box->cap, 0, (new_bytes - old_bytes));
    }

    box->array = new_arr;
    box->cap   = num;

    return BB_OK;
}

BlobBoxRes
bb_reserve(BlobBox* box, size_t min) {
    if (!box) {
        return BB_NPARAM;
    }

    if (box->cap >= min) {
        return BB_OK;
    }

    size_t new_cap = box->cap ? box->cap : 1;
    while (new_cap < min) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min;
            break;
        }
        new_cap *= 2;
    }
    return bb_realloc(box, new_cap);
}

BlobBoxRes
bb_append(BlobBox* box, BlobBoxEnt* ent) {
    if (!box || !ent) {
        return BB_NPARAM;
    }

    if (box->len >= box->cap) {
        BlobBoxRes realloc_res = bb_reserve(box, box->len + 1);
        if (realloc_res != BB_OK) {
            return realloc_res;
        }
    }

    box->array[box->len] = ent;
    box->len++;

    return BB_OK;
}

BlobBoxRes
bb_appendidx(BlobBox* box, BlobBoxEnt* ent, size_t idx) {
    if (!box || !ent) {
        return BB_NPARAM;
    }

    if (idx > box->len) {
        return BB_RANGE;
    }

    if (box->len >= box->cap) {
        BlobBoxRes realloc_res = bb_reserve(box, box->len + 1);
        if (realloc_res != BB_OK) {
            return realloc_res;
        }
    }

    if (idx < box->len) {
        memmove(&box->array[idx + 1], &box->array[idx], (box->len - idx) * sizeof(*box->array));
    }
    box->array[idx] = ent;
    box->len++;
    return BB_OK;
}

BlobBoxEnt*
bb_deassignidx(BlobBox* box, size_t idx) {
    if (!box || idx >= box->len) {
        return NULL;
    }

    BlobBoxEnt* out = box->array[idx];

    memmove(&box->array[idx], &box->array[idx + 1], (box->len - idx - 1) * sizeof(*box->array));
    box->array[box->len - 1] = NULL;
    box->len--;
    return out;
}

BlobBoxEnt*
bb_deassignptr(BlobBox* box, const BlobBoxEnt* ent) {
    if (!box || !ent) {
        return NULL;
    }

    for (size_t i = 0; i < box->len; i++) {
        if (box->array[i] == ent) {
            return bb_deassignidx(box, i);
        }
    }
    return NULL;
}

void
bb_clear_all(BlobBox* box) {
    if (!box) {
        return;
    }

    for (size_t i = 0; i < box->len; i++) {
        bb_ent_destroy(box->array[i]);
    }
    box->len = 0;
}

static void
bb_merge(BlobBoxEnt** a, BlobBoxEnt** tmp, size_t lo, size_t mid, size_t hi, BlobBoxCmpFn cmp) {
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
bb_msort(BlobBoxEnt** a, BlobBoxEnt** tmp, size_t lo, size_t hi, BlobBoxCmpFn cmp) {
    if (hi - lo < 2) {
        return;
    }

    size_t mid = lo + ((hi - lo) / 2);
    bb_msort(a, tmp, lo, mid, cmp);
    bb_msort(a, tmp, mid, hi, cmp);
    bb_merge(a, tmp, lo, mid, hi, cmp);
}

BlobBoxRes
bb_sort(BlobBox* box, BlobBoxCmpFn cmp) {
    if (!box || !cmp) {
        return BB_NPARAM;
    }

    if (box->len < 2) {
        return BB_OK;
    }

    size_t bytes = 0;
    if (__builtin_mul_overflow(sizeof(*box->array), box->len, &bytes)) {
        return BB_RANGE;
    }

    BlobBoxEnt** tmp = ba_alloc(box->ab, bytes);
    if (!tmp) {
        return BB_OOM;
    }

    bb_msort(box->array, tmp, 0, box->len, cmp);
    ba_free(box->ab, tmp, bytes);
    return BB_OK;
}

const BlobBoxEnt*
bb_binarysearch(const BlobBox* box, const void* data, size_t len, BlobBoxCmpFn cmp) {
    if (!box || !data || !cmp || len == 0) {
        return NULL;
    }

    if (box->len == 0) {
        return NULL;
    }

    size_t lo = 0;
    size_t hi = box->len; /* half-open: valid indices are [lo, hi) */

    while (lo < hi) {
        size_t mid = lo + ((hi - lo) / 2);
        int    c   = cmp(box->array[mid]->data, data, box->array[mid]->len, len);
        if (c == 0) {
            return box->array[mid];
        }

        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

#endif /* BLOBBOX_IMPL_DONE */
#endif /* BLOBBOX_IMPL */
