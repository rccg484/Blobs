/**
 * \file BlobList.h
 * \author Rory Graham (rorygraham2\@gmail.com)
 * \brief An owning, allocator-aware doubly-linked list of byte blobs.
 * \version 0.1
 *
 * A doubly-linked, non-cyclic, NULL-terminated list. Head's prev is NULL,
 * tail's next is NULL. Entries belong to one list at a time, and
 * destroying the list destroys the entries.
 *
 * Ownership semantics:
 *   - An entry can belong to at most one list at a time. bl_append,
 *     bl_prepend, bl_insert_before, and bl_insert_after refuse an entry
 *     that already has an owner, returning BL_OWNED.
 *   - bl_destroy destroys every entry, then the list.
 *   - bl_clear destroys every entry but keeps the list usable.
 *   - bl_remove, bl_remove_head, and bl_remove_tail remove an entry
 *     without destroying it. The entry's owner is cleared, and the
 *     caller owns it. It may be re-added to another list.
 *   - bl_ent_destroy refuses to free an owned entry. Remove it from its
 *     list first, or use bl_remove.
 *
 * Entry model:
 *   Entries are opaque handles to length-prefixed, max_align_t-aligned
 *   byte buffers. They are not typed; interpretation is the caller's.
 *   Entries are not indexable; access is by position in the list or by
 *   iteration.
 *
 * Complexity:
 *   Append, prepend, insert, and remove at a known position are O(1).
 *   Indexed access (bl_getidx) and iteration by index are O(n). Iteration
 *   via bl_first/bl_next is O(n) total for a full walk.
 *
 * Allocator lifetime:
 *   Each entry holds a const BlobAlloc* to the block that created it.
 *   That block must outlive every entry created from it. The list's own
 *   allocator (used for the list struct) is separate, and may differ
 *   from the entries' allocators.
 *
 * Single-header library. Define BLOBLIST_IMPL in exactly one translation
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
 *
 * Development of this file was assisted by LLM-based code generation.
 * See README for details.
 */

#ifndef BLOBLIST_H
#define BLOBLIST_H

#include "BlobAlloc.h"
#include <stddef.h>

/**
 * \brief Return codes for BlobList operations.
 */
typedef enum BlobList_res_e {
    BL_OK,     /**< The operation completed. */
    BL_OOM,    /**< The allocator returned NULL. */
    BL_OWNED,  /**< An ownership constraint prevented the operation. */
    BL_NPARAM, /**< A required pointer argument is NULL. */
} BlobListRes;

/**
 * \brief An opaque handle to a blob entry.
 *
 * \invariant The entry holds a non-NULL pointer to the BlobAlloc block
 *            that created it.
 * \invariant The entry length is greater than zero.
 * \invariant The owner is either NULL or a valid BlobList.
 * \invariant If the owner is not NULL, the entry is reachable from that
 *            list's head by following next pointers.
 *
 * \note The struct fields are an implementation detail and are not part of
 *       the public API. Use the accessor functions to inspect an entry or
 *       a container.
 */
typedef struct BlobListEnt_s BlobListEnt;

/**
 * \brief An opaque handle to an owning doubly-linked list of blob entries.
 *
 * \invariant If head is NULL, tail is NULL and len is zero.
 * \invariant If head is not NULL, head->prev is NULL and tail->next is
 *            NULL.
 * \invariant For every entry reachable from head, entry->owner is this
 *            list.
 * \invariant len equals the number of entries reachable from head.
 *
 * \note The struct fields are an implementation detail and are not part of
 *       the public API. Use the accessor functions to inspect an entry or
 *       a container.
 */
typedef struct BlobList_s    BlobList;

/**
 * \brief Called on each entry before it is destroyed.
 *
 * \param[in] ent  The entry about to be destroyed.
 *
 * \note The destructor runs before the entry's memory is returned to
 *       its allocator. The entry's data is still valid at this point.
 * \note Use this to release resources held inside the blob, such as
 *       nested allocations.
 *
 * \warning Do not call bl_ent_destroy from inside the destructor. The
 *          list will destroy the entry after the destructor returns.
 */
typedef void                 (*BlobListDestFn)(BlobListEnt* ent);

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
 * \note The entry is not owned by any list. bl_append sets the owner.
 * \note The entry's next and prev pointers are NULL on creation.
 * \note The entry records ab. The block must outlive the entry.
 *
 * \warning If you do not add the entry to a list, and you do not destroy
 *          it with bl_ent_destroy, the memory leaks.
 *
 * \example
 * const BlobAlloc* ab = ba_default();
 * BlobListEnt* e = bl_ent_create(ab, "hello", 5);
 * if (e == NULL) {
 *     // handle failure
 * }
 */
BlobListEnt*
bl_ent_create(const BlobAlloc* ab, const void* data, size_t len);

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
 *       bl_ent_data_mut before you read it.
 * \note The entry is not owned by any list.
 */
BlobListEnt*
bl_ent_create_uninit(const BlobAlloc* ab, size_t len);

/**
 * \brief Destroys a blob entry and returns its memory to its allocator.
 *
 * \param[in] ent  The entry to destroy. May be NULL.
 *
 * \return BL_OK if ent is NULL, or on success.
 * \return BL_OWNED if ent has an owner.
 *
 * \note A NULL ent is a no-op.
 * \note This function does not run any list destructor. If the entry has
 *       an owner, remove it with bl_remove or destroy the whole list
 *       with bl_destroy instead.
 * \note The entry returns to the allocator it was created from, not to
 *       any list's allocator.
 *
 * \warning This function refuses to free an owned entry. That is a
 *          safety check, not a bug. To destroy an owned entry, first
 *          remove it from its list with bl_remove, which clears the
 *          owner and returns the entry to you.
 */
BlobListRes
bl_ent_destroy(BlobListEnt* ent);

/**
 * \brief Returns the allocator block that created an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The entry's allocator block.
 * \return NULL if ent is NULL.
 */
const BlobAlloc*
bl_ent_alloc(const BlobListEnt* ent);

/**
 * \brief Returns the list that owns an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The owning list.
 * \return NULL if ent is NULL or the entry has no owner.
 */
const BlobList*
bl_ent_owner(const BlobListEnt* ent);

/**
 * \brief Returns the length of an entry, in bytes.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The entry length.
 * \return Zero if ent is NULL.
 */
size_t
bl_ent_len(const BlobListEnt* ent);

/**
 * \brief Returns a read-only pointer to an entry's data.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return A pointer to the first byte of the data.
 * \return NULL if ent is NULL.
 */
const unsigned char*
bl_ent_data(const BlobListEnt* ent);

/**
 * \brief Returns a writable pointer to an entry's data.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return A pointer to the first byte of the data.
 * \return NULL if ent is NULL.
 *
 * \note You can write up to bl_ent_len(ent) bytes.
 */
unsigned char*
bl_ent_data_mut(BlobListEnt* ent);

/* ------------------------------------------------------------------ */
/* container construction and destruction                              */
/* ------------------------------------------------------------------ */

/**
 * \brief Creates an empty list.
 *
 * \param[in] ab    The allocator block for the list. Must not be NULL.
 * \param[in] dest  The destructor for entries. Must not be NULL.
 *
 * \return A pointer to the new list.
 * \return NULL on failure.
 *
 * \pre  ab != NULL
 * \pre  dest != NULL
 *
 * \note The list allocates from ab. It does not use ab to manage the
 *       entries you add later. Each entry keeps its own allocator.
 * \note dest may be a function that does nothing if your entries hold
 *       no resources requiring cleanup.
 */
BlobList*
bl_create(const BlobAlloc* ab, BlobListDestFn dest);

/**
 * \brief Destroys every entry, then destroys the list.
 *
 * \param[in] list  The list. May be NULL.
 *
 * \note A NULL list is a no-op.
 * \note For each entry, the list calls its destructor, then returns the
 *       entry to its own allocator. The destructor runs while the
 *       entry's data is still valid.
 * \note Entries are destroyed in head-to-tail order.
 */
void
bl_destroy(BlobList* list);

/**
 * \brief Destroys every entry and empties the list.
 *
 * \param[in] list  The list. May be NULL.
 *
 * \note A NULL list is a no-op.
 * \note The list remains usable after this call.
 * \note For each entry, the list calls its destructor, then returns the
 *       entry to its own allocator.
 */
void
bl_clear(BlobList* list);

/* ------------------------------------------------------------------ */
/* container queries                                                   */
/* ------------------------------------------------------------------ */

/**
 * \brief Returns the number of entries in a list.
 *
 * \param[in] list  The list. May be NULL.
 *
 * \return The entry count.
 * \return Zero if list is NULL.
 */
size_t
bl_getlen(const BlobList* list);

/**
 * \brief Returns a read-only pointer to the entry at an index.
 *
 * \param[in] list  The list. May be NULL.
 * \param[in] idx   The index.
 *
 * \return A pointer to the entry.
 * \return NULL if list is NULL or idx is out of range.
 *
 * \pre  idx < bl_getlen(list)
 *
 * \note This function walks the list from the head. The cost is O(n).
 * \note For sequential access, prefer bl_first and bl_next.
 */
const BlobListEnt*
bl_getidx(const BlobList* list, size_t idx);

/**
 * \brief Returns a writable pointer to the entry at an index.
 *
 * \param[in] list  The list. May be NULL.
 * \param[in] idx   The index.
 *
 * \return A pointer to the entry.
 * \return NULL if list is NULL or idx is out of range.
 *
 * \pre  idx < bl_getlen(list)
 *
 * \note This function walks the list from the head. The cost is O(n).
 * \note For sequential access, prefer bl_first and bl_next.
 */
BlobListEnt*
bl_getidx_mut(BlobList* list, size_t idx);

/* ------------------------------------------------------------------ */
/* iteration                                                           */
/* ------------------------------------------------------------------ */

/**
 * \brief Returns the first entry of a list.
 *
 * \param[in] list  The list. May be NULL.
 *
 * \return A pointer to the first entry.
 * \return NULL if the list is NULL or empty.
 *
 * \note The returned handle is not const-qualified. Const-ness of the
 *       entry's data is enforced by bl_ent_data and bl_ent_data_mut.
 *
 * \example
 * for (BlobListEnt* e = bl_first(list); e; e = bl_next(e)) {
 *     // use e
 * }
 */
BlobListEnt*
bl_first(const BlobList* list);

/**
 * \brief Returns the last entry of a list.
 *
 * \param[in] list  The list. May be NULL.
 *
 * \return A pointer to the last entry.
 * \return NULL if the list is NULL or empty.
 *
 * \note The returned handle is not const-qualified. See bl_first.
 *
 * \example
 * for (BlobListEnt* e = bl_last(list); e; e = bl_prev(e)) {
 *     // use e
 * }
 */
BlobListEnt*
bl_last(const BlobList* list);

/**
 * \brief Returns the entry after an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The next entry.
 * \return NULL if ent is NULL or ent is the last entry.
 *
 * \note The returned handle is not const-qualified.
 *
 * \warning The result is only meaningful while ent is still in its
 *          list. Calling bl_next on an entry that has been removed
 *          reads stale pointers.
 *
 * \example
 * // Safe removal during iteration: capture next before removing.
 * for (BlobListEnt* e = bl_first(list); e; ) {
 *     BlobListEnt* next = bl_next(e);
 *     if (should_remove(e)) bl_remove(list, e);
 *     e = next;
 * }
 */
BlobListEnt*
bl_next(const BlobListEnt* ent);

/**
 * \brief Returns the entry before an entry.
 *
 * \param[in] ent  The entry. May be NULL.
 *
 * \return The previous entry.
 * \return NULL if ent is NULL or ent is the first entry.
 *
 * \note The returned handle is not const-qualified.
 *
 * \warning The result is only meaningful while ent is still in its
 *          list. See bl_next.
 */
BlobListEnt*
bl_prev(const BlobListEnt* ent);

/* ------------------------------------------------------------------ */
/* insertion                                                           */
/* ------------------------------------------------------------------ */

/**
 * \brief Adds an entry to the end of a list.
 *
 * \param[in,out] list  The list. Must not be NULL.
 * \param[in]     ent   The entry. Must not be NULL, and must not already
 *                      have an owner.
 *
 * \return BL_OK on success.
 * \return BL_NPARAM if list or ent is NULL.
 * \return BL_OWNED if ent already has an owner.
 *
 * \pre  list != NULL
 * \pre  ent != NULL
 * \pre  bl_ent_owner(ent) == NULL
 *
 * \note On success, ent->owner is set to list.
 * \note On failure, the list and the entry are unchanged.
 */
BlobListRes
bl_append(BlobList* list, BlobListEnt* ent);

/**
 * \brief Adds an entry to the front of a list.
 *
 * \param[in,out] list  The list. Must not be NULL.
 * \param[in]     ent   The entry. Must not be NULL, and must not already
 *                      have an owner.
 *
 * \return BL_OK on success.
 * \return BL_NPARAM if list or ent is NULL.
 * \return BL_OWNED if ent already has an owner.
 *
 * \pre  list != NULL
 * \pre  ent != NULL
 * \pre  bl_ent_owner(ent) == NULL
 *
 * \note On success, ent->owner is set to list.
 */
BlobListRes
bl_prepend(BlobList* list, BlobListEnt* ent);

/**
 * \brief Inserts an entry before another entry.
 *
 * \param[in,out] list    The list. Must not be NULL.
 * \param[in]     before  The position entry. Must not be NULL, and must
 *                        be owned by list.
 * \param[in]     ent     The entry to insert. Must not be NULL, and must
 *                        not already have an owner.
 *
 * \return BL_OK on success.
 * \return BL_NPARAM if list, before, or ent is NULL.
 * \return BL_OWNED if ent already has an owner, or if before is owned by
 *         a different list.
 *
 * \pre  list != NULL
 * \pre  before != NULL
 * \pre  ent != NULL
 * \pre  bl_ent_owner(before) == list
 * \pre  bl_ent_owner(ent) == NULL
 *
 * \note This function is O(1).
 * \note On success, ent->owner is set to list.
 */
BlobListRes
bl_insert_before(BlobList* list, BlobListEnt* before, BlobListEnt* ent);

/**
 * \brief Inserts an entry after another entry.
 *
 * \param[in,out] list   The list. Must not be NULL.
 * \param[in]     after  The position entry. Must not be NULL, and must
 *                       be owned by list.
 * \param[in]     ent    The entry to insert. Must not be NULL, and must
 *                       not already have an owner.
 *
 * \return BL_OK on success.
 * \return BL_NPARAM if list, after, or ent is NULL.
 * \return BL_OWNED if ent already has an owner, or if after is owned by
 *         a different list.
 *
 * \pre  list != NULL
 * \pre  after != NULL
 * \pre  ent != NULL
 * \pre  bl_ent_owner(after) == list
 * \pre  bl_ent_owner(ent) == NULL
 *
 * \note This function is O(1).
 * \note On success, ent->owner is set to list.
 */
BlobListRes
bl_insert_after(BlobList* list, BlobListEnt* after, BlobListEnt* ent);

/* ------------------------------------------------------------------ */
/* removal                                                             */
/* ------------------------------------------------------------------ */

/**
 * \brief Removes the first entry of a list and returns it.
 *
 * \param[in,out] list  The list. Must not be NULL.
 *
 * \return A pointer to the removed entry.
 * \return NULL if list is NULL or empty.
 *
 * \note The entry is not destroyed. The entry's owner is cleared, and
 *       the caller owns it.
 * \note This function is O(1).
 */
BlobListEnt*
bl_remove_head(BlobList* list);

/**
 * \brief Removes the last entry of a list and returns it.
 *
 * \param[in,out] list  The list. Must not be NULL.
 *
 * \return A pointer to the removed entry.
 * \return NULL if list is NULL or empty.
 *
 * \note The entry is not destroyed. The entry's owner is cleared, and
 *       the caller owns it.
 * \note This function is O(1).
 */
BlobListEnt*
bl_remove_tail(BlobList* list);

/**
 * \brief Removes a specific entry from a list and returns it.
 *
 * \param[in,out] list  The list. Must not be NULL.
 * \param[in]     ent   The entry to remove. Must not be NULL, and must
 *                      be owned by list.
 *
 * \return A pointer to the removed entry.
 * \return NULL if list or ent is NULL, or if ent is not owned by list.
 *
 * \pre  list != NULL
 * \pre  ent != NULL
 * \pre  bl_ent_owner(ent) == list
 *
 * \note The entry is not destroyed. The entry's owner is cleared, and
 *       the caller owns it.
 * \note This function is O(1).
 */
BlobListEnt*
bl_remove(BlobList* list, BlobListEnt* ent);

#endif /* BLOBLIST_H */

#ifdef BLOBLIST_IMPL
#ifndef BLOBLIST_IMPL_DONE
#define BLOBLIST_IMPL_DONE

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

struct BlobListEnt_s {
    const BlobAlloc* ab;
    BlobList*        owner;
    BlobListEnt*     next;
    BlobListEnt*     prev;
    size_t           len;
    alignas(max_align_t) unsigned char data[];
};

struct BlobList_s {
    const BlobAlloc* ab;
    BlobListEnt*     head;
    BlobListEnt*     tail;
    size_t           len;
    BlobListDestFn   dest;
};

BlobListEnt*
bl_ent_create(const BlobAlloc* ab, const void* data, size_t len) {
    if (!ab || !data || len == 0) {
        return NULL;
    }

    size_t bytes = 0;
    if (__builtin_add_overflow(offsetof(BlobListEnt, data), len, &bytes)) {
        return NULL;
    }

    BlobListEnt* out = ba_alloc(ab, bytes);
    if (!out) {
        return NULL;
    }

    out->ab    = ab;
    out->owner = NULL;
    out->len   = len;
    out->next  = NULL;
    out->prev  = NULL;
    memcpy(out->data, data, len);
    return out;
}

BlobListEnt*
bl_ent_create_uninit(const BlobAlloc* ab, size_t len) {
    if (!ab || len == 0) {
        return NULL;
    }

    size_t bytes = 0;
    if (__builtin_add_overflow(offsetof(BlobListEnt, data), len, &bytes)) {
        return NULL;
    }

    BlobListEnt* out = ba_alloc(ab, bytes);
    if (!out) {
        return NULL;
    }

    out->ab    = ab;
    out->owner = NULL;
    out->len   = len;
    return out;
}

static inline size_t
bl_ent_bytes(size_t len) {
    return offsetof(BlobListEnt, data) + len;
}

BlobListRes
bl_ent_destroy(BlobListEnt* ent) {
    if (ent && ent->owner) {
        return BL_OWNED;
    }

    if (ent) {
        ba_free(ent->ab, ent, bl_ent_bytes(ent->len));
    }
    return BL_OK;
}

const BlobAlloc*
bl_ent_alloc(const BlobListEnt* ent) {
    return ent ? ent->ab : NULL;
}

const BlobList*
bl_ent_owner(const BlobListEnt* ent) {
    return ent ? ent->owner : NULL;
}

size_t
bl_ent_len(const BlobListEnt* ent) {
    return ent ? ent->len : 0;
}

const unsigned char*
bl_ent_data(const BlobListEnt* ent) {
    return ent ? ent->data : NULL;
}

unsigned char*
bl_ent_data_mut(BlobListEnt* ent) {
    return ent ? ent->data : NULL;
}

BlobList*
bl_create(const BlobAlloc* ab, BlobListDestFn dest) {
    if (!ab || !dest) {
        return NULL;
    }

    BlobList* out = ba_alloc(ab, sizeof(*out));
    if (!out) {
        return NULL;
    }

    out->ab   = ab;
    out->head = NULL;
    out->tail = NULL;
    out->len  = 0;
    out->dest = dest;

    return out;
}

static void
bl_ent_destroy_unchecked(BlobListEnt* ent) {
    ba_free(ent->ab, ent, bl_ent_bytes(ent->len));
}

void
bl_destroy(BlobList* list) {
    if (!list) {
        return;
    }

    BlobListEnt* ptr = list->head;
    while (ptr) {
        BlobListEnt* next = ptr->next;
        list->dest(ptr);
        bl_ent_destroy_unchecked(ptr);
        ptr = next;
    }

    ba_free(list->ab, list, sizeof(BlobList));
}

void
bl_clear(BlobList* list) {
    if (!list) {
        return;
    }

    BlobListEnt* ptr = list->head;
    while (ptr) {
        BlobListEnt* next = ptr->next;
        list->dest(ptr);
        bl_ent_destroy_unchecked(ptr);
        ptr = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->len  = 0;
}

size_t
bl_getlen(const BlobList* list) {
    return list ? list->len : 0;
}

const BlobListEnt*
bl_getidx(const BlobList* list, size_t idx) {
    if (!list || idx >= list->len) {
        return NULL;
    }

    BlobListEnt* ptr = list->head;
    for (size_t i = 0; i < idx; i++) {
        ptr = ptr->next;
    }

    return ptr;
}

BlobListEnt*
bl_getidx_mut(BlobList* list, size_t idx) {
    if (!list || idx >= list->len) {
        return NULL;
    }

    BlobListEnt* ptr = list->head;
    for (size_t i = 0; i < idx; i++) {
        ptr = ptr->next;
    }

    return ptr;
}

BlobListRes
bl_append(BlobList* list, BlobListEnt* ent) {
    if (!list || !ent) {
        return BL_NPARAM;
    }

    if (ent->owner) {
        return BL_OWNED;
    }

    ent->next = NULL;
    ent->prev = list->tail;

    if (list->tail) {
        list->tail->next = ent;
    } else {
        list->head = ent;
    }

    ent->owner = list;
    list->tail = ent;
    list->len++;
    return BL_OK;
}

BlobListRes
bl_prepend(BlobList* list, BlobListEnt* ent) {
    if (!list || !ent) {
        return BL_NPARAM;
    }

    if (ent->owner) {
        return BL_OWNED;
    }

    ent->prev = NULL;
    ent->next = list->head;

    if (list->head) {
        list->head->prev = ent;
    } else {
        list->tail = ent;
    }

    list->head = ent;
    ent->owner = list;
    list->len++;
    return BL_OK;
}

BlobListEnt*
bl_remove_head(BlobList* list) {
    if (!list || !list->head) {
        return NULL;
    }

    BlobListEnt* out = list->head;
    if (list->head == list->tail) {
        list->tail = NULL;
        list->head = NULL;
    } else {
        list->head      = out->next;
        out->next->prev = NULL;
    }

    out->prev  = NULL;
    out->next  = NULL;
    out->owner = NULL;
    list->len--;

    return out;
}

BlobListEnt*
bl_remove_tail(BlobList* list) {
    if (!list || !list->head) {
        return NULL;
    }

    BlobListEnt* out = list->tail;
    if (list->head == list->tail) {
        list->tail = NULL;
        list->head = NULL;
    } else {
        list->tail      = out->prev;
        out->prev->next = NULL;
    }

    out->prev  = NULL;
    out->next  = NULL;
    out->owner = NULL;
    list->len--;

    return out;
}

BlobListEnt*
bl_remove(BlobList* list, BlobListEnt* ent) {
    if (!list || !ent || ent->owner != list) {
        return NULL;
    }

    if (list->head == ent) {
        return bl_remove_head(list);
    }

    if (list->tail == ent) {
        return bl_remove_tail(list);
    }

    ent->prev->next = ent->next;
    ent->next->prev = ent->prev;

    ent->prev       = NULL;
    ent->next       = NULL;
    ent->owner      = NULL;
    list->len--;
    return ent;
}

BlobListRes
bl_insert_before(BlobList* list, BlobListEnt* before, BlobListEnt* ent) {
    if (!list || !before || !ent) {
        return BL_NPARAM;
    }

    if (before->owner != list || ent->owner) {
        return BL_OWNED;
    }

    ent->prev = before->prev;
    ent->next = before;

    if (before->prev) {
        before->prev->next = ent;
    } else {
        list->head = ent;
    }

    before->prev = ent;

    ent->owner   = list;
    list->len++;
    return BL_OK;
}

BlobListRes
bl_insert_after(BlobList* list, BlobListEnt* after, BlobListEnt* ent) {
    if (!list || !after || !ent) {
        return BL_NPARAM;
    }

    if (after->owner != list || ent->owner) {
        return BL_OWNED;
    }

    ent->next = after->next;
    ent->prev = after;

    if (after->next) {
        after->next->prev = ent;
    } else {
        list->tail = ent;
    }

    after->next = ent;

    ent->owner  = list;
    list->len++;
    return BL_OK;
}

BlobListEnt*
bl_first(const BlobList* list) {
    return list ? list->head : NULL;
}

BlobListEnt*
bl_last(const BlobList* list) {
    return list ? list->tail : NULL;
}

BlobListEnt*
bl_next(const BlobListEnt* ent) {
    return ent ? ent->next : NULL;
}

BlobListEnt*
bl_prev(const BlobListEnt* ent) {
    return ent ? ent->prev : NULL;
}

#endif /* BLOBLIST_IMPL_DONE */
#endif /* BLOBLIST_IMPL */