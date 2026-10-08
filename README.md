# Blobs

Allocator-aware, single-header blob containers for C11.

Three small libraries providing dynamic containers for arbitrary byte
blobs, with pluggable allocators. No build step, no dependencies, no
globals. Add the headers to your project, define one macro, and use
them.

- **BlobAlloc** — the allocator substrate: hooks, context, and a default
  malloc-backed block.
- **BlobBox** — a non-owning container. Entries carry their own allocator
  and may be referenced by any number of boxes.
- **BlobVec** — an owning container. Entries belong to one vector at a
  time, and destroying the vector destroys them.

Each is a single header. Each is usable on its own, though in practice
BlobBox and BlobVec both sit on top of BlobAlloc.

---

## Overview

C dynamic-array code commonly assumes that the element type is known at
compile time and that `malloc` and `free` are the allocators. Blobs
relaxes both assumptions.

Entries are opaque, length-prefixed, `max_align_t`-aligned byte buffers,
and every allocation goes through a function-pointer block supplied by
the caller. The same container can be used with the libc allocator, a
bump arena, a pool allocator, or anything else that fits the hook
signature. Multiple allocators can coexist in the same process with no
global state.

---

## Quick start

```c
#include <stdio.h>
#include <string.h>
#include "BlobVec.h"

#define BV_IMPL
#include "BlobVec.h"

static void noop_destroy(BlobVecEnt* e) { (void)e; }

static int cmp_bytes(const void* a, const void* b, size_t alen, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    int    r = memcmp(a, b, n);
    if (r) return r;
    return (alen > blen) - (alen < blen);
}

int main(void) {
    const BlobAlloc* ab = ba_default();

    BlobVec* v = bv_create(ab, 8, cmp_bytes, noop_destroy);

    const char* words[] = { "banana", "apple", "cherry" };
    for (size_t i = 0; i < 3; i++) {
        bv_append(v, bv_ent_create(ab, words[i], strlen(words[i])));
    }

    bv_sort(v);

    for (size_t i = 0; i < bv_getlen(v); i++) {
        const BlobVecEnt* e = bv_getidx(v, i);
        fwrite(bv_ent_data(e), 1, bv_ent_len(e), stdout);
        putchar('\n');
    }

    bv_destroy(v);
    return 0;
}
```

The `#define BV_IMPL` line goes in exactly one translation unit in your
program. Every other file that includes `BlobVec.h` receives only the
declarations.

---

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

The hooks take a leading `void* ctx` that the block carries. This allows
multiple allocators to coexist in one process without global state: one
`arena_alloc` function can serve every arena, with each block carrying
which arena it belongs to.

`free` and `realloc` take the size of the allocation. This is required
by allocators that do not track sizes internally, such as bump arenas
and pool allocators.

**`ba_default()`** returns a shared, static, malloc-backed block:

```c
const BlobAlloc* ab = ba_default();
```

**A custom block** wraps anything you like:

```c
typedef struct { unsigned char* base; size_t used; size_t cap; } Arena;

void* arena_alloc  (void* ctx, size_t size);
void  arena_free   (void* ctx, void* ptr, size_t size);
void* arena_realloc(void* ctx, void* ptr, size_t old_size, size_t new_size);

Arena a = { .base = buffer, .cap = sizeof buffer };
BlobAlloc ab = {
    .ctx     = &a,
    .alloc   = arena_alloc,
    .free    = arena_free,
    .realloc = arena_realloc,
};
```

The block must outlive every allocation made through it. Stack-local
blocks that escape their scope are the primary way to misuse the
library.

---

## Ownership: Box vs Vec

The two containers differ in their ownership contract.

| | `BlobBox` | `BlobVec` |
|---|---|---|
| Owns its entries | no | yes |
| Same entry in multiple containers | yes | no |
| `destroy` frees entries | no | yes |
| Entry has an owner pointer | no | yes |
| Append refuses a shared entry | no | yes (`BV_OWNED`) |
| Container stores comparator / destructor | no | yes |

**BlobBox** is suitable when entries have their own lifetimes and the
container holds references to them: a registry, an index, a work queue,
a set of views. Destroying the box leaves the entries untouched.

**BlobVec** is suitable when the entries belong to the container and
should be freed with it, as in a dynamic array that owns its contents.
`bv_destroy` frees every entry, runs the destructor on each, then frees
the vector.

Both containers can be backed by the same allocator, or by different
ones. Entries and containers do not have to share an `BlobAlloc`.

---

## Installation

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    blobs
    GIT_REPOSITORY https://github.com/yourname/blobs.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(blobs)

target_link_libraries(myapp PRIVATE BlobVec::BlobVec)
```

`BlobVec::BlobVec` transitively pulls in `BlobAlloc::BlobAlloc`, so one
link line is sufficient. Use `BlobBox::BlobBox` for the non-owning
container, or both if you need both.

### Vendored

Add the three module directories to your source tree and:

```cmake
add_subdirectory(third_party/blobs)
target_link_libraries(myapp PRIVATE BlobVec::BlobVec)
```

### Manual

Copy the header. Define the impl macro in one `.c` file:

```c
/* blob_impl.c */
#define BLOBALLOC_IMPL
#define BLOBBOX_IMPL
#define BV_IMPL
#include "BlobAlloc.h"
#include "BlobBox.h"
#include "BlobVec.h"
```

Then include the headers from anywhere else. No CMake required.

---

## Building and testing

Three CMake presets:

```sh
# Development: ASan + UBSan, optimised debug
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

# Release
cmake --preset release
cmake --build --preset release

# Coverage (clang source-based)
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
```

Tests use [Unity](https://github.com/ThrowTheSwitch/Unity) via
FetchContent and are registered with CTest. `ctest --preset dev -j 8`
runs them in parallel.

For a coverage report, after running the coverage preset:

```sh
cd build/coverage
llvm-profdata merge -sparse profraw/*.profraw -o merged.profdata
llvm-cov report \
    -object=tests/test_bloballoc.exe \
    -object=tests/test_blobbox.exe \
    -object=tests/test_blobvec.exe \
    -object=tests/test_integration.exe \
    -instr-profile=merged.profdata \
    -ignore-filename-regex='(_deps|test_)'
```

Add `-format=html -output-dir=html` for a browsable report.

---

## Design notes

**Entries are opaque.** You hold, pass, and compare `BlobBoxEnt*` and
`BlobVecEnt*` handles, but cannot read or write the fields. Access is
via `*_ent_data`, `*_ent_len`, and the mut variants. The struct layout
is an implementation detail and may change.

**`BlobBoxEnt` and `BlobVecEnt` are not interchangeable.** They have
different layouts, and casting one to the other is undefined behaviour.
If you need data in both kinds of container, copy it with the
destination's `*_ent_create`; do not transfer pointers.

**Zero-length entries are prohibited.** Since entries cannot be resized,
a zero-length entry would be permanently unusable. Both `*_ent_create`
and `*_ent_create_uninit` return NULL for `len == 0`.

**The allocator block carries no lifetime.** It is a value type and
must outlive every allocation made through it. A block built on the
stack causes every entry created from it to become a dangling pointer
when the function returns.

**`free(NULL)` is a no-op.** Every custom block must guarantee this.
`*_ent_destroy(NULL)` relies on it.

**Recursive allocation is not supported.** A block whose hooks call
into another block's hooks works at the top level, but is not intended
as a general facility. Keep blocks flat.

**The default block follows libc semantics.** In particular,
`ba_default_realloc(ptr, 0)` frees `ptr` and returns NULL, as libc's
`realloc` does. The containers guard against this by refusing `num == 0`
in their own realloc functions. If you write a custom block, document
whether your realloc shares this behaviour.

---

## Platform notes

**Windows + clang + ASan.** The ASan runtime ships as a DLL next to
`clang.exe` and is not on `PATH` by default. The test CMakeLists copies
it next to each test executable so `ctest --preset dev` works without
environment setup. If you encounter `0xc0000135`
(`STATUS_DLL_NOT_FOUND`) when running tests directly, that is the cause —
copy the DLL by hand, or run the tests through CTest.

**MSYS2 vs Program Files LLVM.** The two toolchains have separate
installations and separate sanitizer runtimes. If ASan behaves
unexpectedly, `grep CMAKE_C_COMPILER build/dev/CMakeCache.txt` shows
which compiler CMake picked up.

---

## Status

Version 0.1. The API is stable enough to use, but has not been
extensively tested and may change before 1.0. Function coverage is
100%, line and region coverage are approximately 90%, and branch
coverage is approximately 81%. The residual uncovered branches are
defensive NULL guards and unreachable overflow checks.

Tested on:

- Windows 11 / MSYS2 ucrt64 (clang)

Linux and macOS have not been tested. The library is standard C11 with
no platform-specific code; the only Windows-specific handling is in the
test infrastructure, which copies the sanitizer runtime DLL next to
each test executable.

---

## Acknowledgements

Development of this library was assisted by LLM-based code generation
and review. All code has been human-reviewed, tested, and is understood
by the maintainer.

---

## License

GPL-3.0-or-later. See the file header in each module for the full notice.
