# Concurrency Architecture

## Overview

The matching engine uses a multiple-producer, single-consumer architecture implemented in C++20.

Multiple producer threads submit LIMIT orders, MARKET orders and cancellation requests through a bounded blocking FIFO command queue. One dedicated matching worker processes commands sequentially.

The design separates concurrent command submission from matching-engine state mutation.

## Components and Ownership

| Component | Responsibility |
|---|---|
| `MatchingWorker` | Owns the matching engine, command queue and worker thread |
| `MatchingEngine` | Processes order submissions, matching and cancellations |
| `BoundedBlockingQueue` | Synchronizes concurrent producers and the worker |
| `WorkerCommand` | Represents a submission or cancellation request |
| `std::promise` / `std::future` | Communicates command results or exceptions to callers |

The matching engine is a private member of `MatchingWorker`. Producers cannot access it through the worker's public interface.

Only the worker thread invokes matching-engine operations within the asynchronous worker architecture. This avoids concurrent mutation of the order book through that interface.

## Command Ordering

Commands are processed in FIFO queue-insertion order.

Each successfully enqueued command is processed after commands already ahead of it in the queue.

Commands submitted by different producer threads do not have a predetermined global ordering. Their order depends on which producer successfully inserts its command into the queue first.

The queue's FIFO ordering is separate from the order book's price-time priority:

- Queue FIFO determines the order in which commands reach the matching engine.
- Price priority determines which resting price level is matched first.
- Time priority determines execution order among resting orders at the same price.

## Backpressure

The command queue has a fixed positive capacity.

When the queue reaches capacity, additional producers block until space becomes available or the queue closes.

The worker removes commands from the queue and processes them sequentially.

This prevents unlimited growth of pending commands, but introduces potentially variable producer blocking latency.

Queue synchronization uses a mutex and condition variables with predicate-based waits.

## Results and Exceptions

Submission commands return `std::future<SubmissionResult>`.

Cancellation commands return `std::future<bool>`.

The worker delivers successful results through the corresponding promises.

Exceptions raised during matching or cancellation are captured and propagated to callers through their futures. An individual command failure does not ordinarily terminate the worker's processing loop.

## Shutdown

`MatchingWorker::shutdown()` closes the command queue and joins the worker thread.

The shutdown contract provides:

- Rejection of commands after queue closure.
- Wakeup of producers blocked by queue capacity.
- Processing of previously accepted commands before worker termination.
- Serialization of concurrent shutdown calls.
- Safe repeated shutdown calls under the documented lifetime assumptions.

The destructor invokes `shutdown()`.

## Lifetime Requirements

Callers must ensure that all producer threads have finished using a `MatchingWorker` object before its destruction begins.

Concurrent object destruction and API calls are not supported.

`shutdown()` must not be called from the worker thread itself.

`shutdown()` is declared `noexcept`; an unexpected thread-join failure would terminate the process.

## Verification

The existing test suite covers concurrent producers, command ordering, price-time priority, cancellations, shutdown rejection, accepted-command draining, blocked-producer wakeup, concurrent shutdown and exception propagation.

Deterministic synchronization gates are used for selected concurrency scenarios.

Seeded concurrent stress tests use reproducible command generation, but actual cross-thread scheduling is not deterministic.

Passing these tests does not establish the absence of all possible concurrency defects.

## Diagnostic Limitations

The current development environment uses Windows 11, MSVC 14.43.34808 and CMake 3.30.5.

ThreadSanitizer has not been executed because it is not available through the detected MSVC toolchain.

Clang and GCC were not found on the configured PATH during the Phase 7.8 environment check.

AddressSanitizer may be evaluated separately for memory-safety diagnostics, but it is not a substitute for data-race detection.

A compatible Linux/WSL2 ThreadSanitizer build remains a possible future validation step.

## Performance Considerations

The single-worker architecture serializes matching operations, avoiding concurrent order-book mutation and associated locking within the worker execution path.

However, the design does not guarantee a particular latency or throughput.

Potential performance costs include queue synchronization, producer contention, backpressure, command allocation and future/promise communication.

These costs will be measured during Phase 8 rather than estimated as performance claims.

## Known Limitations

- Matching operations execute on one worker thread rather than in parallel.
- Producer submission may block when the bounded queue is full.
- Cross-producer command ordering depends on runtime scheduling.
- Callers must coordinate object lifetime externally.
- ThreadSanitizer validation has not been performed.
- Latency distributions and throughput have not yet been benchmarked.
