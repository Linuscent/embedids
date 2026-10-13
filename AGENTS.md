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
- `BUILD_EDGEML=OFF` (default) — EdgeML agent, simulator and tests (see below)
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
- `embedids_analyze_metric_detailed()` evaluates every algorithm on a metric (unlike `embedids_analyze_metric()`, which stops at the first anomaly).

## EdgeML Runtime Security MVP (`edgeml/`)

C++17 agent, simulator, REST API and dashboard built on the core library. Off by default; enable with `-DBUILD_EDGEML=ON`. See `edgeml/README.md`.

```bash
cmake -S . -B build -DBUILD_EDGEML=ON -DBUILD_TESTS=ON && cmake --build build -j
cd build && ctest -E e2e          # edgeml unit + integration (~5 s)
ctest -R e2e                      # real-process attack scenarios (~3 min)
cd edgeml/deployment && docker compose up --build   # demo at http://localhost:8080
```

- Dependencies (nlohmann/json, yaml-cpp, cpp-httplib, SQLite amalgamation) are fetched via FetchContent with pinned hashes in `edgeml/cmake/Dependencies.cmake`; the core C library stays dependency-free.
- Library layering: `edgeml_core` (no collector/hardware deps) ← `edgeml_sim` ← `edgeml_collectors` ← `edgeml_agent`. Keep detection code free of platform APIs.
- New collectors implement `MetricCollector` (`edgeml/collectors/common/collector.h`) and are wired in `collector_factory.cpp`.
- Integration tests drive `Agent::run_cycle()` with a `ManualClock` (`edgeml/tests/support/test_support.h`); prefer that over sleeps.
- The simulator's approved model SHA-256 is pinned in `edgeml/configs/*.yaml` and `test_simulator.cpp`; changing the model generator breaks both.

## Docs

Python/MkDocs tooling (docs only, not library code). Managed by Poetry:
```bash
poetry install
poetry run mkdocs serve   # local preview
```
CI deploys versioned docs via `mike` on push to `main` or `v*.*` branches.

- Case studies live in `docs/case-studies/`; their charts under `docs/assets/case-studies/` are rendered from recorded telemetry (`edgeml/tools/case-study-data/`, deliberately outside `docs/` so it is not published) by `edgeml/tools/case_study_charts.py` (see `edgeml/docs/demo-guide.md`). Don't hand-edit the `*-timeline.svg` / `detection-latency.svg` files.

## CI

- No CI workflow builds or tests the C library — only docs are CI-tested.
- PR titles must follow conventional commits, types `fix` and `feat` only (enforced on PRs to `main`, `feat/*`, `fix/*`).
