#pragma once

#include <cstddef>
#include <future>
#include <thread>

#include "matching_engine/BoundedBlockingQueue.hpp"
#include "matching_engine/MatchingCommand.hpp"
#include "matching_engine/MatchingEngine.hpp"

namespace matching_engine {

class MatchingWorker {
private:
    MatchingEngine engine_;
    BoundedBlockingQueue<MatchingCommand> queue_;
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

#ifdef MATCHING_ENGINE_TESTING
    std::future<SubmissionResult> submit_with_failure_for_test(
        Order order,
        InsertionFailurePoint point
    );
#endif

    void shutdown() noexcept;
};

} // namespace matching_engine
