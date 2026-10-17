# Blobs

Allocator-aware, single-header blob containers for C11.

Four small libraries providing dynamic containers for arbitrary byte
blobs, with pluggable allocators. No build step, no dependencies, no
globals.

## Modules

| Module | Purpose |
|---|---|
| `BlobAlloc` | Allocator substrate: hooks, context, and a default malloc-backed block |
| `BlobBox` | Non-owning dynamic array. Entries may be referenced by multiple boxes |
| `BlobVec` | Owning dynamic array. Entries belong to one vector at a time |
| `BlobList` | Owning doubly-linked list. O(1) insert and remove at a known position |

Each module is a single header. Each is usable on its own, though the
containers all sit on top of `BlobAlloc`.

## The allocator block

Every allocation in the family goes through a `BlobAlloc`:

```c
typedef struct BlobAlloc_s {
    void*         ctx;
    BlobAllocFn   alloc;
    BlobFreeFn    free;
    BlobReallocFn realloc;
} BlobAlloc;
```

The hooks take a leading `void* ctx` that the block carries, which allows
multiple allocators to coexist in one process without global state. The
`free` and `realloc` hooks take the size of the allocation, so allocators
that do not track sizes internally — bump arenas, pool allocators — can
free correctly.

`ba_default()` returns a shared, static, malloc-backed block. A custom
block wraps anything that fits the hook signature: an arena, a pool, a
tracking allocator. The block must outlive every allocation made through
it.

## Getting started

See the [README](https://github.com/rccg484/Blobs) for installation
instructions, a worked example, and the design notes. The
[Contributing](https://github.com/rccg484/Blobs/blob/main/CONTRIBUTING.md)
guide covers the build and test workflow.

## Quick navigation

- \ref BlobAlloc.h — the allocator substrate
- \ref BlobBox.h — the non-owning dynamic array
- \ref BlobVec.h — the owning dynamic array
- \ref BlobList.h — the owning doubly-linked list

## Status

Version 0.1. The API is stable enough to use, but has not been extensively
tested and may change before 1.0. Tested on Windows 11 / MSYS2 ucrt64 with
clang. Function coverage is 100%, line coverage approximately 91%, branch
coverage approximately 81%.

## License

GPL-3.0-or-later. See the file header in each module for the full notice.