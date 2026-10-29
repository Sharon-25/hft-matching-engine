#include "matching_engine/MatchingWorker.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace matching_engine {

MatchingWorker::MatchingWorker(std::size_t queue_capacity)
    : engine_(),
      queue_(queue_capacity),
      worker_([this] {
          run();
      }) {}

MatchingWorker::~MatchingWorker() {
    shutdown();
}

std::future<SubmissionResult> MatchingWorker::submit(
    Order order
) {
    std::promise<SubmissionResult> promise;
    auto future = promise.get_future();

    WorkerCommand command(
        std::in_place_type<MatchingCommand>,
        std::move(order),
        std::move(promise)
    );

    if (!queue_.push(std::move(command))) {
        throw std::runtime_error(
            "Matching worker is shutting down"
        );
    }

    return future;
}

std::future<bool> MatchingWorker::cancel(OrderId id) {
    std::promise<bool> promise;
    auto future = promise.get_future();

    WorkerCommand command(
        std::in_place_type<CancelCommand>,
        id,
        std::move(promise)
    );

    if (!queue_.push(std::move(command))) {
        throw std::runtime_error(
            "Matching worker is shutting down"
        );
    }

    return future;
}

#ifdef MATCHING_ENGINE_TESTING

std::future<SubmissionResult>
MatchingWorker::submit_with_failure_for_test(
    Order order,
    InsertionFailurePoint point
) {
    std::promise<SubmissionResult> promise;
    auto future = promise.get_future();

    WorkerCommand command(
        std::in_place_type<MatchingCommand>,
        std::move(order),
        std::move(promise)
    );

    std::get<MatchingCommand>(command).failure_point = point;

    if (!queue_.push(std::move(command))) {
        throw std::runtime_error(
            "Matching worker is shutting down"
        );
    }

    return future;
}

#endif

#ifdef MATCHING_ENGINE_TESTING

void MatchingWorker::pause_before_next_command_for_test() {
    std::lock_guard<std::mutex> lock(test_gate_mutex_);

    test_pause_requested_ = true;
    test_worker_paused_ = false;
    test_resume_requested_ = false;
}

void MatchingWorker::wait_until_paused_for_test() {
    std::unique_lock<std::mutex> lock(test_gate_mutex_);

    test_gate_cv_.wait(lock, [this] {
        return test_worker_paused_;
    });
}

void MatchingWorker::resume_for_test() {
    {
        std::lock_guard<std::mutex> lock(test_gate_mutex_);
        test_resume_requested_ = true;
    }

    test_gate_cv_.notify_all();
}

void MatchingWorker::wait_at_test_gate() {
    std::unique_lock<std::mutex> lock(test_gate_mutex_);

    if (!test_pause_requested_) {
        return;
    }

    test_worker_paused_ = true;
    test_gate_cv_.notify_all();

    test_gate_cv_.wait(lock, [this] {
        return test_resume_requested_;
    });

    test_pause_requested_ = false;
    test_worker_paused_ = false;
    test_resume_requested_ = false;
}

void MatchingWorker::wait_for_blocked_producers_for_test(
    std::size_t expected_count
) {
    queue_.wait_for_blocked_producers_for_test(expected_count);
}

#endif

void MatchingWorker::run() {
    while (auto command = queue_.pop()) {
#ifdef MATCHING_ENGINE_TESTING
        wait_at_test_gate();
#endif

        if (auto* submission =
                std::get_if<MatchingCommand>(&*command)) {

            try {
#ifdef MATCHING_ENGINE_TESTING
                engine_.set_insertion_failure_for_test(
                    submission->failure_point.value_or(
                        InsertionFailurePoint::NONE
                    )
                );
#endif

                SubmissionResult result;

                if (submission->order.type() ==
                    OrderType::LIMIT) {

                    result = engine_.submit_limit_order(
                        submission->order
                    );

                } else if (
                    submission->order.type() ==
                    OrderType::MARKET
                ) {

                    result = engine_.submit_market_order(
                        submission->order
                    );

                } else {

                    result = {
                        SubmissionStatus::INVALID_ORDER,
                        0,
                        submission->order.remaining_quantity()
                    };
                }

                submission->result_promise.set_value(
                    result
                );

            } catch (...) {
                submission->result_promise.set_exception(
                    std::current_exception()
                );
            }

        } else if (auto* cancellation =
                       std::get_if<CancelCommand>(&*command)) {

            try {
                const bool cancelled =
                    engine_.cancel_order(
                        cancellation->order_id
                    );

                cancellation->result_promise.set_value(
                    cancelled
                );

            } catch (...) {
                cancellation->result_promise.set_exception(
                    std::current_exception()
                );
            }
        }
    }
}

void MatchingWorker::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(shutdown_mutex_);

    queue_.close();

#ifdef MATCHING_ENGINE_TESTING
    resume_for_test();
#endif

    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace matching_engine
