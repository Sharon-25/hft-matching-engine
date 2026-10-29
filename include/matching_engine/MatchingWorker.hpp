#pragma once

#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <thread>

#include "matching_engine/BoundedBlockingQueue.hpp"
#include "matching_engine/MatchingCommand.hpp"
#include "matching_engine/MatchingEngine.hpp"

namespace matching_engine {

// Thread-safety and lifetime contract:
//
// - Multiple producer threads may call submit() and cancel().
// - Concurrent shutdown() calls are serialized.
// - Accepted commands are drained before shutdown() returns.
// - New commands are rejected once the queue closes.
// - Producers blocked on queue capacity wake when it closes.
// - The caller must ensure all producer threads have finished
//   using this object before its destruction begins.
// - shutdown() must not be called from the worker thread.
// - shutdown() is noexcept and assumes worker joining succeeds;
//   an unexpected join failure would terminate the process.
class MatchingWorker {
private:
    MatchingEngine engine_;
    BoundedBlockingQueue<WorkerCommand> queue_;

#ifdef MATCHING_ENGINE_TESTING
    std::mutex test_gate_mutex_;
    std::condition_variable test_gate_cv_;

    bool test_pause_requested_ = false;
    bool test_worker_paused_ = false;
    bool test_resume_requested_ = false;

    void wait_at_test_gate();
#endif

    std::mutex shutdown_mutex_;
    std::jthread worker_;

    void run();

public:
    explicit MatchingWorker(std::size_t queue_capacity);

    ~MatchingWorker();

    MatchingWorker(const MatchingWorker&) = delete;
    MatchingWorker& operator=(const MatchingWorker&) = delete;

    MatchingWorker(MatchingWorker&&) = delete;
    MatchingWorker& operator=(MatchingWorker&&) = delete;

    std::future<SubmissionResult> submit(Order order);
    std::future<bool> cancel(OrderId id);

#ifdef MATCHING_ENGINE_TESTING
    std::future<SubmissionResult> submit_with_failure_for_test(
        Order order,
        InsertionFailurePoint point
    );
    void pause_before_next_command_for_test();
    void wait_until_paused_for_test();
    void resume_for_test();
    void wait_for_blocked_producers_for_test(
        std::size_t expected_count
    );
#endif

    void shutdown() noexcept;
};

} // namespace matching_engine
