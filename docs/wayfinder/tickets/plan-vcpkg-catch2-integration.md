---
title: Plan vcpkg and Catch2 Integration
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: closed
assignee: workflow-manager
blocked_by: []
---

# Plan vcpkg and Catch2 Integration

## Question

What concrete project files and CMake changes are needed to use vcpkg manifest mode and Catch2 cleanly in MLJIT?

The answer should identify the `vcpkg.json` contents, CMake preset/toolchain expectations, Catch2 target wiring, CTest discovery, and how parser-generator/tool dependencies should be represented if vcpkg supports them. It should keep all test/tooling dependencies out of compiler runtime/core targets.

---

## Resolution

### Files created or modified

| File                         | Change |
|------------------------------|--------|
| `vcpkg.json`                 | Added vcpkg manifest with project `mljit` v0.1.0, dependency `catch2`. |
| `CMakePresets.json`          | Added `CMAKE_TOOLCHAIN_FILE` pointing to `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` in base preset. |
| `test/CMakeLists.txt`        | Replaced placeholder with full Catch2 wiring: `find_package(Catch2 3)`, test exe `mljit-tests`, links `mljit::lib` + `Catch2::Catch2WithMain`, enables CTest discovery via `catch_discover_tests`. |
| `test/test_main.cpp`         | New Catch2 smoke test importing `mljit.util` and asserting `version() == "0.1.0"` and `greet` doesn't throw. |

### Key decisions

1. **vcpkg manifest mode** — Declared via root `vcpkg.json` (no `vcpkg-configuration.json` needed for a single dependency). The toolchain path is set in `CMakePresets.json` using `$env{VCPKG_ROOT}` so each developer only needs `VCPKG_ROOT` in their environment.
2. **Catch2 v3** — Using `Catch2::Catch2WithMain` (provides `main()`) to keep tests minimal. `catch_discover_tests()` auto-registers each `TEST_CASE` with CTest.
3. **Test target hygiene** — The test executable `mljit-tests` applies the same `target_compile_features`, `set_project_warnings`, and `enable_sanitizers` as core targets. It links `mljit::lib` (not the `mljit` executable) to test the public module API directly.
4. **Dependency isolation** — Catch2 is a test-only dependency declared only in the manifest; it never leaks into the `mljit-lib` target. Future tooling dependencies should follow the same pattern: added to `vcpkg.json` and `find_package`'d only where needed, while compiler core/runtime dependencies still require separate justification.

### Commands

```bash
cmake --preset debug          # installs Catch2 via vcpkg, then configures
cmake --build --preset debug  # builds mljit, mljit-lib, and mljit-tests
ctest --preset debug          # runs all discovered Catch2 test cases
```

### Status

**closed** — verified by Orchestrator with `ctest --preset debug`.
