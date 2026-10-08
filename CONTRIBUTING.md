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