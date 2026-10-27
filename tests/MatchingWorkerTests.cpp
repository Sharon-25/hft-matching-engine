#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

#include "matching_engine/MatchingWorker.hpp"

using namespace matching_engine;

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            std::fprintf(                                     \
                stderr,                                       \
                "CHECK failed: %s (%s:%d)\n",                  \
                #condition, __FILE__, __LINE__                \
            );                                                \
            std::abort();                                     \
        }                                                     \
    } while (false)

void test_limit_orders_through_worker() {
    MatchingWorker worker(4);

    auto sell = worker.submit(
        Order(1001, Side::SELL, 10500, 10)
    );

    auto buy = worker.submit(
        Order(1002, Side::BUY, 10600, 6)
    );

    const auto sell_result = sell.get();
    const auto buy_result = buy.get();

    CHECK(sell_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(sell_result.executed_quantity == 0);
    CHECK(sell_result.remaining_quantity == 10);

    CHECK(buy_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(buy_result.executed_quantity == 6);
    CHECK(buy_result.remaining_quantity == 0);

    worker.shutdown();
}

void test_market_orders_through_worker() {
    MatchingWorker worker(4);

    auto resting = worker.submit(
        Order(2001, Side::SELL, 10500, 10)
    );

    CHECK(resting.get().status ==
          SubmissionStatus::ACCEPTED);

    auto market = worker.submit(
        Order(
            2002,
            Side::BUY,
            OrderType::MARKET,
            0,
            15
        )
    );

    const auto result = market.get();

    CHECK(result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 10);
    CHECK(result.remaining_quantity == 5);

    worker.shutdown();
}

void test_fifo_submission_order() {
    MatchingWorker worker(4);

    auto first = worker.submit(
        Order(3001, Side::SELL, 10500, 5)
    );

    auto second = worker.submit(
        Order(3002, Side::SELL, 10500, 5)
    );

    auto incoming = worker.submit(
        Order(3003, Side::BUY, 10500, 6)
    );

    CHECK(first.get().status ==
          SubmissionStatus::ACCEPTED);
    CHECK(second.get().status ==
          SubmissionStatus::ACCEPTED);

    const auto result = incoming.get();

    CHECK(result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 6);
    CHECK(result.remaining_quantity == 0);

    worker.shutdown();
}

void test_duplicate_order_id() {
    MatchingWorker worker(2);

    auto first = worker.submit(
        Order(4001, Side::BUY, 10000, 10)
    );

    CHECK(first.get().status ==
          SubmissionStatus::ACCEPTED);

    auto duplicate = worker.submit(
        Order(4001, Side::SELL, 11000, 5)
    );

    CHECK(duplicate.get().status ==
          SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_shutdown_rejects_new_submissions() {
    MatchingWorker worker(2);

    worker.shutdown();

    bool rejected = false;

    try {
        auto future = worker.submit(
            Order(5001, Side::BUY, 10000, 10)
        );
        (void)future;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    CHECK(rejected);
}

void test_shutdown_drains_accepted_commands() {
    MatchingWorker worker(4);

    auto first = worker.submit(
        Order(6001, Side::SELL, 10500, 10)
    );

    auto second = worker.submit(
        Order(6002, Side::BUY, 10500, 4)
    );

    worker.shutdown();

    const auto first_result = first.get();
    const auto second_result = second.get();

    CHECK(first_result.status ==
          SubmissionStatus::ACCEPTED);

    CHECK(second_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(second_result.executed_quantity == 4);
}

void test_destructor_drains_accepted_commands() {
    std::future<SubmissionResult> first;
    std::future<SubmissionResult> second;

    {
        MatchingWorker worker(4);

        first = worker.submit(
            Order(7001, Side::SELL, 10500, 10)
        );

        second = worker.submit(
            Order(7002, Side::BUY, 10500, 3)
        );
    }

    CHECK(first.get().status ==
          SubmissionStatus::ACCEPTED);

    const auto second_result = second.get();

    CHECK(second_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(second_result.executed_quantity == 3);
}

#ifdef MATCHING_ENGINE_TESTING

void test_matching_exception_reaches_future_and_worker_recovers() {
    MatchingWorker worker(4);

    auto failing = worker.submit_with_failure_for_test(
        Order(8001, Side::BUY, 10000, 10),
        InsertionFailurePoint::AFTER_LIST_INSERT
    );

    bool caught_bad_alloc = false;

    try {
        (void)failing.get();
    } catch (const std::bad_alloc&) {
        caught_bad_alloc = true;
    }

    CHECK(caught_bad_alloc);

    // The worker must remain alive after the exception.
    auto recovery = worker.submit(
        Order(8002, Side::BUY, 10100, 5)
    );

    const auto result = recovery.get();

    CHECK(result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 0);
    CHECK(result.remaining_quantity == 5);

    worker.shutdown();
}

#endif

void test_multiple_concurrent_producers() {
    constexpr int producer_count = 4;
    constexpr int orders_per_producer = 250;
    constexpr int total_orders =
        producer_count * orders_per_producer;

    MatchingWorker worker(32);

    std::atomic<int> accepted_count{0};
    std::atomic<int> failed_count{0};

    std::vector<std::thread> producers;

    for (int producer_id = 0;
         producer_id < producer_count;
         ++producer_id) {

        producers.emplace_back([&, producer_id] {
            for (int i = 0;
                 i < orders_per_producer;
                 ++i) {

                const OrderId id =
                    10000 + producer_id * orders_per_producer + i;

                auto future = worker.submit(
                    Order(
                        id,
                        Side::BUY,
                        10000,
                        1
                    )
                );

                const auto result = future.get();

                if (result.status ==
                    SubmissionStatus::ACCEPTED &&
                    result.executed_quantity == 0 &&
                    result.remaining_quantity == 1) {
                    accepted_count.fetch_add(
                        1, std::memory_order_relaxed
                    );
                } else {
                    failed_count.fetch_add(
                        1, std::memory_order_relaxed
                    );
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    worker.shutdown();

    CHECK(accepted_count.load() == total_orders);
    CHECK(failed_count.load() == 0);
}

void test_multiple_outstanding_futures() {
    constexpr int producer_count = 4;
    constexpr int orders_per_producer = 250;
    constexpr int total_orders =
        producer_count * orders_per_producer;

    MatchingWorker worker(16);

    std::atomic<int> accepted_count{0};
    std::atomic<int> failed_count{0};

    std::vector<std::thread> producers;

    for (int producer_id = 0;
         producer_id < producer_count;
         ++producer_id) {

        producers.emplace_back([&, producer_id] {
            std::vector<std::future<SubmissionResult>> futures;
            futures.reserve(orders_per_producer);

            // Submit all orders before waiting for results.
            for (int i = 0;
                 i < orders_per_producer;
                 ++i) {

                const OrderId id =
                    20000 + producer_id * orders_per_producer + i;

                futures.push_back(
                    worker.submit(
                        Order(
                            id,
                            Side::BUY,
                            10000,
                            1
                        )
                    )
                );
            }

            // Retrieve results after submission.
            for (auto& future : futures) {
                const auto result = future.get();

                if (result.status ==
                        SubmissionStatus::ACCEPTED &&
                    result.executed_quantity == 0 &&
                    result.remaining_quantity == 1) {

                    accepted_count.fetch_add(
                        1, std::memory_order_relaxed
                    );

                } else {
                    failed_count.fetch_add(
                        1, std::memory_order_relaxed
                    );
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    worker.shutdown();

    CHECK(accepted_count.load() == total_orders);
    CHECK(failed_count.load() == 0);
}

void test_concurrent_duplicate_order_ids() {
    constexpr int producer_count = 4;
    constexpr OrderId shared_order_id = 30000;

    MatchingWorker worker(4);

    std::atomic<int> accepted_count{0};
    std::atomic<int> duplicate_count{0};
    std::atomic<int> unexpected_count{0};

    std::atomic<int> ready_count{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> producers;

    for (int i = 0; i < producer_count; ++i) {
        producers.emplace_back([&] {
            ready_count.fetch_add(
                1, std::memory_order_release
            );

            while (!start.load(
                std::memory_order_acquire
            )) {
                std::this_thread::yield();
            }

            auto future = worker.submit(
                Order(
                    shared_order_id,
                    Side::BUY,
                    10000,
                    1
                )
            );

            const auto result = future.get();

            if (result.status ==
                SubmissionStatus::ACCEPTED) {
                accepted_count.fetch_add(
                    1, std::memory_order_relaxed
                );
            } else if (
                result.status ==
                SubmissionStatus::DUPLICATE_ID
            ) {
                duplicate_count.fetch_add(
                    1, std::memory_order_relaxed
                );
            } else {
                unexpected_count.fetch_add(
                    1, std::memory_order_relaxed
                );
            }
        });
    }

    while (ready_count.load(
        std::memory_order_acquire
    ) != producer_count) {
        std::this_thread::yield();
    }

    start.store(true, std::memory_order_release);

    for (auto& producer : producers) {
        producer.join();
    }

    worker.shutdown();

    CHECK(accepted_count.load() == 1);
    CHECK(duplicate_count.load() == producer_count - 1);
    CHECK(unexpected_count.load() == 0);
}

void test_worker_preserves_queue_submission_order() {
    MatchingWorker worker(4);

    // First command: resting sell liquidity.
    auto resting = worker.submit(
        Order(40001, Side::SELL, 10500, 10)
    );

    // Second command: market buy consumes the liquidity.
    auto market = worker.submit(
        Order(
            40002,
            Side::BUY,
            OrderType::MARKET,
            0,
            10
        )
    );

    // Third command: limit buy arrives after the market buy.
    auto limit = worker.submit(
        Order(40003, Side::BUY, 10500, 5)
    );

    const auto resting_result = resting.get();
    const auto market_result = market.get();
    const auto limit_result = limit.get();

    CHECK(resting_result.status ==
          SubmissionStatus::ACCEPTED);

    CHECK(market_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(market_result.executed_quantity == 10);
    CHECK(market_result.remaining_quantity == 0);

    CHECK(limit_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(limit_result.executed_quantity == 0);
    CHECK(limit_result.remaining_quantity == 5);

    worker.shutdown();
}

void test_worker_cancellation() {
    MatchingWorker worker(8);

    // Submit an order that should rest in the book.
    auto submission = worker.submit(
        Order(50001, Side::SELL, 10500, 10)
    );

    // Queue cancellation after submission.
    auto cancellation = worker.cancel(50001);

    const auto submit_result = submission.get();
    const bool cancelled = cancellation.get();

    CHECK(submit_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(submit_result.executed_quantity == 0);
    CHECK(submit_result.remaining_quantity == 10);

    CHECK(cancelled);

    // Cancelling the same order twice must fail.
    auto second_cancellation = worker.cancel(50001);
    CHECK(!second_cancellation.get());

    // An unknown order ID must also fail.
    auto unknown_cancellation = worker.cancel(99999);
    CHECK(!unknown_cancellation.get());

    worker.shutdown();
}

void test_cancellation_after_full_fill() {
    MatchingWorker worker(8);

    auto resting = worker.submit(
        Order(51001, Side::SELL, 10500, 10)
    );

    auto crossing = worker.submit(
        Order(51002, Side::BUY, 10500, 10)
    );

    auto cancellation = worker.cancel(51001);

    const auto resting_result = resting.get();
    const auto crossing_result = crossing.get();

    CHECK(resting_result.status ==
          SubmissionStatus::ACCEPTED);

    CHECK(crossing_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(crossing_result.executed_quantity == 10);
    CHECK(crossing_result.remaining_quantity == 0);

    // Fully filled orders are no longer active.
    CHECK(!cancellation.get());

    worker.shutdown();
}

void test_cancellation_prevents_future_match() {
    MatchingWorker worker(8);

    auto resting = worker.submit(
        Order(52001, Side::SELL, 10500, 10)
    );

    auto cancellation = worker.cancel(52001);

    // This BUY would have matched if the SELL still existed.
    auto crossing = worker.submit(
        Order(52002, Side::BUY, 10600, 10)
    );

    const auto resting_result = resting.get();
    const bool cancelled = cancellation.get();
    const auto crossing_result = crossing.get();

    CHECK(resting_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(cancelled);

    CHECK(crossing_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(crossing_result.executed_quantity == 0);
    CHECK(crossing_result.remaining_quantity == 10);

    worker.shutdown();
}

void test_cancel_partially_filled_order() {
    MatchingWorker worker(8);

    auto resting = worker.submit(
        Order(53001, Side::SELL, 10500, 10)
    );

    auto crossing = worker.submit(
        Order(53002, Side::BUY, 10500, 4)
    );

    auto cancellation = worker.cancel(53001);

    // After cancellation, no remaining SELL liquidity
    // should be available for the next BUY.
    auto later_buy = worker.submit(
        Order(53003, Side::BUY, 10500, 6)
    );

    const auto resting_result = resting.get();
    const auto crossing_result = crossing.get();
    const bool cancelled = cancellation.get();
    const auto later_result = later_buy.get();

    CHECK(resting_result.status ==
          SubmissionStatus::ACCEPTED);

    CHECK(crossing_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(crossing_result.executed_quantity == 4);
    CHECK(crossing_result.remaining_quantity == 0);

    CHECK(cancelled);

    CHECK(later_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(later_result.executed_quantity == 0);
    CHECK(later_result.remaining_quantity == 6);

    worker.shutdown();
}

void test_cancelled_order_id_cannot_be_reused() {
    MatchingWorker worker(8);

    auto original = worker.submit(
        Order(54001, Side::BUY, 10000, 10)
    );

    auto cancellation = worker.cancel(54001);

    auto reused = worker.submit(
        Order(54001, Side::BUY, 10100, 5)
    );

    const auto original_result = original.get();
    const bool cancelled = cancellation.get();
    const auto reused_result = reused.get();

    CHECK(original_result.status ==
          SubmissionStatus::ACCEPTED);

    CHECK(cancelled);

    CHECK(reused_result.status ==
          SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_cancellation_drains_during_shutdown() {
    MatchingWorker worker(8);

    auto submission = worker.submit(
        Order(55001, Side::SELL, 10500, 10)
    );

    auto cancellation = worker.cancel(55001);

    // Shutdown must drain both accepted commands.
    worker.shutdown();

    const auto submission_result = submission.get();
    const bool cancelled = cancellation.get();

    CHECK(submission_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(cancelled);

    // No new cancellation may be accepted.
    bool rejected = false;

    try {
        auto future = worker.cancel(55001);
        (void)future;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    CHECK(rejected);
}

int main() {
    test_limit_orders_through_worker();
    test_market_orders_through_worker();
    test_fifo_submission_order();
    test_duplicate_order_id();
    test_shutdown_rejects_new_submissions();
    test_shutdown_drains_accepted_commands();
    test_destructor_drains_accepted_commands();
    test_multiple_concurrent_producers();
    test_multiple_outstanding_futures();
    test_concurrent_duplicate_order_ids();
    test_worker_preserves_queue_submission_order();
    test_worker_cancellation();
    test_cancellation_after_full_fill();
    test_cancellation_prevents_future_match();
    test_cancel_partially_filled_order();
    test_cancelled_order_id_cannot_be_reused();
    test_cancellation_drains_during_shutdown();

#ifdef MATCHING_ENGINE_TESTING
    test_matching_exception_reaches_future_and_worker_recovers();
#endif

    std::printf("MatchingWorker tests passed\n");

    return 0;
}
