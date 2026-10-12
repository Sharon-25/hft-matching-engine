# C++ Low-Latency Order Matching Engine

A C++20 project to design, implement, test and benchmark a price-time-priority order matching engine.

## Planned Features

- Limit and market orders
- Bid and ask order books
- Price-time priority
- Partial and full executions
- Order cancellation
- Trade generation
- Concurrent order processing
- Latency and throughput benchmarking

## Current Status

Phase 1 — Development environment and project scaffolding.

The matching engine has not yet been implemented or benchmarked.

## Build Requirements

- CMake 3.20 or newer
- C++20-capable compiler
- MSVC development tools on Windows

## Build on Windows

Run from an MSVC developer terminal:

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
.\build\matching_engine.exe
```

## Development Approach

Correctness first, followed by automated testing, concurrency design, reproducible benchmarking and measured optimization.
