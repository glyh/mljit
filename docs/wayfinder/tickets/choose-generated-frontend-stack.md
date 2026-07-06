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

Which well-established parser/lexer stack should MLJIT use for its tiny frontend, given the project goals of x86-64 Linux portability, vcpkg-based dependency acquisition where feasible, CMake/Ninja/Clang integration, C++26 module boundaries, good diagnostics, and a resume signal focused on systems/JIT plus modern C++ engineering rather than parser novelty?

The answer should compare Bison C++ skeleton plus re2c/Flex, ANTLR4 C++ target/runtime, tree-sitter, Boost.Spirit/X3, PEGTL, and any mature alternatives worth considering. It should recommend one stack, explain generated-code/module-boundary strategy, and identify CMake/vcpkg integration consequences.
