---
title: Update PROJECT.md for the New Design Direction
parent: ../mljit-design-map.md
labels:
  - wayfinder:task
status: closed
assignee: workflow-manager
blocked_by:
  - design-module-and-docs-layout.md
---

# Update PROJECT.md for the New Design Direction

## Question

How should `PROJECT.md` be revised so it no longer describes the older bytecode-template-JIT/C++20 plan and instead reflects the agreed MLJIT direction?

The update should capture C++26 modules, Clang/CMake/Ninja, vcpkg/Catch2, tiny generated frontend, block-parameter SSA IR, i64 interpreter, register-IR backend, internal x64 assembler, linear scan register allocation, fib/gcd demo, and architecture-heavy documentation strategy.

## Resolution

`PROJECT.md` was replaced by `README.md`.

The new README is intentionally short and punchy. It frames MLJIT as a hobby compiler/JIT project, emphasizes the backend-first design, states the current skeleton/design-stage status clearly, documents current build commands, and links to the Wayfinder design map for deeper planning.
