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

    MatchingCommand command(
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

#ifdef MATCHING_ENGINE_TESTING

std::future<SubmissionResult>
MatchingWorker::submit_with_failure_for_test(
    Order order,
    InsertionFailurePoint point
) {
    std::promise<SubmissionResult> promise;
    auto future = promise.get_future();

    MatchingCommand command(
        std::move(order),
        std::move(promise)
    );

    command.failure_point = point;

    if (!queue_.push(std::move(command))) {
        throw std::runtime_error(
            "Matching worker is shutting down"
        );
    }

    return future;
}

#endif

void MatchingWorker::run() {
    while (auto command = queue_.pop()) {
        try {
#ifdef MATCHING_ENGINE_TESTING
            engine_.set_insertion_failure_for_test(
                command->failure_point.value_or(
                    InsertionFailurePoint::NONE
                )
            );
#endif
            SubmissionResult result;

            if (command->order.type() == OrderType::LIMIT) {
                result = engine_.submit_limit_order(
                    command->order
                );
            } else if (
                command->order.type() == OrderType::MARKET
            ) {
                result = engine_.submit_market_order(
                    command->order
                );
            } else {
                result = {
                    SubmissionStatus::INVALID_ORDER,
                    0,
                    command->order.remaining_quantity()
                };
            }

            command->result_promise.set_value(result);

        } catch (...) {
            command->result_promise.set_exception(
                std::current_exception()
            );
        }
    }
}

void MatchingWorker::shutdown() noexcept {
    queue_.close();

    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace matching_engine
