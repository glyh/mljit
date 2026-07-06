---
title: Plan vcpkg and Catch2 Integration
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: open
assignee:
blocked_by: []
---

# Plan vcpkg and Catch2 Integration

## Question

What concrete project files and CMake changes are needed to use vcpkg manifest mode and Catch2 cleanly in MLJIT?

The answer should identify the `vcpkg.json` contents, CMake preset/toolchain expectations, Catch2 target wiring, CTest discovery, and how parser-generator/tool dependencies should be represented if vcpkg supports them. It should keep all test/tooling dependencies out of compiler runtime/core targets.
