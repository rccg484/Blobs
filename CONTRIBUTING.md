# Contributing

Thanks for taking a look at Blobs. This is a small project, and
contributions are welcome — whether bug reports, API feedback,
documentation fixes, or code.

## Before you start

For anything beyond a small fix, please open an issue first. It's
easier to agree on the shape of a change before writing code than to
rework a pull request afterwards.

The API is still settling (see the version in the README). Feedback on
naming, ergonomics, and missing operations is as useful as code at
this stage.

## Reporting issues

A good issue includes:

- What you were trying to do.
- The smallest code sample that reproduces the problem.
- What you expected to happen, and what happened instead.
- Your compiler, version, and platform.

If the issue involves a crash, a sanitizer report, or memory
corruption, please include the full output. It's often faster to
diagnose from the report than from a description of it.

## Building and testing

The project uses CMake with presets. To build and run tests:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset builds with AddressSanitizer and UndefinedBehaviorSanitizer
enabled and treats sanitizer findings as failures. Please run the tests
through this preset before submitting a change. A change that passes the
release build but trips ASan is not ready.

For a release build:

```sh
cmake --preset release
cmake --build --preset release
```

For coverage:

```sh
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage
```

See the README for the full coverage reporting workflow.

### A note on Windows and ASan

On Windows, clang's ASan runtime ships as a DLL next to `clang.exe` and
is not on `PATH` by default. The test CMakeLists copies it next to each
test executable so `ctest --preset dev` works without environment setup.
If you run a test executable directly and see `0xc0000135`, that is the
cause — use CTest, or copy the DLL by hand.

## Code style

Formatting is handled by `clang-format`, configured by the
`.clang-format` file in the repository root. Run it before committing:

```sh
clang-format -i BlobAlloc/*.h BlobAlloc/*.c \
                BlobBox/*.h    BlobBox/*.c \
                BlobVec/*.h    BlobVec/*.c \
                tests/*.c      tests/*.h
```

A `.clangd` file is provided for editor tooling. It expects the compile
database at `build/dev/compile_commands.json`, so configure the `dev`
preset once before opening the project in an IDE.

Beyond formatting:

- C11. No compiler extensions outside of `__builtin_*_overflow`.
- Compile clean under `-Wall -Wextra -Wpedantic`. The project targets
  zero warnings.
- Public functions use the module's short prefix (`ba_`, `bb_`, `bv_`).
  Types use full names (`BlobAlloc`, `BlobBoxEnt`, `BlobVec`).
- Opaque types stay opaque. Implementation details belong inside the
  `#ifdef XXX_IMPL` block.

## Tests

Every public function should have a test. Every error return path
reachable from the public API should have a test. The coverage report
shows what is covered; branches that cannot be reached from the public
API (defensive NULL guards, unreachable overflow checks) are fine to
leave uncovered.

Tests live in `tests/`, one executable per module, plus an integration
suite for cross-module behaviour. The shared helpers — the counting
arena, comparators, and the failure-injection allocator — are in
`test_common.c`.

When you add a test to one container's suite, check whether the same
test belongs in the sibling container's suite. The containers are
structurally parallel, and a gap in one usually means a gap in the
other.

## Submitting a change

For a pull request:

- Keep it focused. One change per PR is easier to review than five.
- Include a test that would have failed before the change.
- Update the relevant documentation (header comments, README if
  user-facing).
- Run the tests through the `dev` preset.

A short imperative subject line and a body explaining the *why* is
appreciated. If the change fixes an issue, reference it.

## AI-assisted contributions

The library itself was developed with LLM assistance — see the README
acknowledgements. AI-assisted contributions are welcome on the same
terms: the submitter is expected to understand the code, to have
reviewed it, and to be able to explain the reasoning behind it.

If a contribution is substantially AI-generated, please mention it in
the PR description. There is no penalty; it is useful context for
review.

## Questions

Open an issue, even for a question. If you are unsure whether something
is a bug or expected behaviour, that is worth knowing either way.