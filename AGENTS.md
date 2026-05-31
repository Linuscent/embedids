# EmbedIDS Agent Guide

C11 static library for anomaly detection on embedded systems. No heap allocation. Zero external dependencies for the library itself (GoogleTest fetched for tests only).

## Build

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON
make -j$(nproc)
```

Key CMake options:
- `BUILD_TESTS=OFF` (default) — must enable to build tests
- `BUILD_EXAMPLES=OFF` (default)
- `ENABLE_COVERAGE=OFF` — adds `--coverage` and a `coverage` target

## Test

```bash
ctest                                   # run all suites
ctest --verbose --output-on-failure     # see failures
./tests/embedids_tests --gtest_filter="EmbedIDSAlgorithmsTest.*"  # single suite
```

Five CTest suites: `core_tests`, `metrics_tests`, `algorithms_tests`, `analysis_tests`, `extensible_tests`.

## Public Header

`include/embedids.h.in` is a CMake template — the real header is generated at `<build>/include/embedids.h`. Edit `.h.in`, not the generated file.

## Architecture

- **No malloc anywhere.** All buffers (`embedids_metric_datapoint_t[]`) are user-provided static arrays.
- Compile-time limits (overridable via `-D`): `EMBEDIDS_MAX_METRICS=32`, `EMBEDIDS_MAX_METRIC_NAME_LEN=32`, `EMBEDIDS_MAX_ALGORITHMS_PER_METRIC=4`.
- Floating point is optional: guarded by `EMBEDIDS_ENABLE_FLOATING_POINT` / `EMBEDIDS_ENABLE_DOUBLE_PRECISION`.
- Library is C11; tests are C++17 with `extern "C"` wrapper.
- `run_trend_algorithm()` in `src/embedids.c` is a stub with a TODO — trend analysis is incomplete.

## Docs

Python/MkDocs tooling (docs only, not library code). Managed by Poetry:
```bash
poetry install
poetry run mkdocs serve   # local preview
```
CI deploys versioned docs via `mike` on push to `main` or `v*.*` branches.

## CI

- No CI workflow builds or tests the C library — only docs are CI-tested.
- PR titles must follow conventional commits, types `fix` and `feat` only (enforced on PRs to `main`, `feat/*`, `fix/*`).
