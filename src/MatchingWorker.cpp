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

void MatchingWorker::run() {
    while (auto command = queue_.pop()) {

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
    queue_.close();

    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace matching_engine
