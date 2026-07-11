---
title: Choose the Generated Frontend Stack
parent: ../mljit-design-map.md
labels:
  - wayfinder:research
status: open
assignee:
blocked_by: []
---

# Choose the Generated Frontend Stack

## Question

Which well-established parser/lexer stack should MLJIT use for its tiny frontend, given the project goals of x86-64 Linux portability, Nix-based dependency acquisition, CMake/Ninja/Clang integration, C++26 module boundaries, good diagnostics, and a resume signal focused on systems/JIT plus modern C++ engineering rather than parser novelty?

The answer should compare mature generated-parser options and any small, production-friendly alternatives worth considering. It should recommend one stack, explain generated-code/module-boundary strategy, and identify CMake/Nix integration consequences.
