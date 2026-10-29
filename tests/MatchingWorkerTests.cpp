#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <new>
#include <numeric>
#include <random>
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

    // Queue a valid command without waiting for the failed one.
    auto recovery = worker.submit(
        Order(8002, Side::BUY, 10100, 5)
    );

    // Both accepted commands must reach a terminal future state.
    worker.shutdown();

    CHECK(failing.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    CHECK(recovery.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    bool caught_bad_alloc = false;

    try {
        (void)failing.get();
    } catch (const std::bad_alloc&) {
        caught_bad_alloc = true;
    }

    CHECK(caught_bad_alloc);

    const auto result = recovery.get();

    CHECK(result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 0);
    CHECK(result.remaining_quantity == 5);
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

#ifdef MATCHING_ENGINE_TESTING

void test_blocked_submission_wakes_on_shutdown() {
    MatchingWorker worker(1);

    worker.pause_before_next_command_for_test();

    // The worker removes this command and pauses.
    auto first = worker.submit(
        Order(56001, Side::BUY, 10000, 1)
    );

    worker.wait_until_paused_for_test();

    // The queue has capacity 1, so this fills it.
    auto second = worker.submit(
        Order(56002, Side::BUY, 10000, 1)
    );

    std::promise<void> producer_started;
    auto started = producer_started.get_future();

    std::promise<bool> producer_rejected;
    auto rejected = producer_rejected.get_future();

    std::thread producer([&] {
        producer_started.set_value();

        bool caught = false;

        try {
            auto future = worker.submit(
                Order(56003, Side::BUY, 10000, 1)
            );
            (void)future;
        } catch (const std::runtime_error&) {
            caught = true;
        }

        producer_rejected.set_value(caught);
    });

    started.wait();

    worker.wait_for_blocked_producers_for_test(1);

    // Shutdown closes the queue, waking the producer.
    worker.shutdown();

    CHECK(
        rejected.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready
    );

    CHECK(rejected.get());

    producer.join();

    // Both previously accepted commands must complete.
    CHECK(first.get().status == SubmissionStatus::ACCEPTED);
    CHECK(second.get().status == SubmissionStatus::ACCEPTED);
}

void test_blocked_cancellation_wakes_on_shutdown() {
    MatchingWorker worker(1);

    worker.pause_before_next_command_for_test();

    auto first = worker.submit(
        Order(57001, Side::SELL, 10500, 10)
    );

    worker.wait_until_paused_for_test();

    // Fill the queue with an accepted cancellation.
    auto accepted_cancel = worker.cancel(57001);

    std::promise<void> producer_started;
    auto started = producer_started.get_future();

    std::promise<bool> producer_rejected;
    auto rejected = producer_rejected.get_future();

    std::thread producer([&] {
        producer_started.set_value();

        bool caught = false;

        try {
            auto future = worker.cancel(57001);
            (void)future;
        } catch (const std::runtime_error&) {
            caught = true;
        }

        producer_rejected.set_value(caught);
    });

    started.wait();

    worker.wait_for_blocked_producers_for_test(1);

    worker.shutdown();

    CHECK(
        rejected.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready
    );

    CHECK(rejected.get());

    producer.join();

    CHECK(first.get().status == SubmissionStatus::ACCEPTED);
    CHECK(accepted_cancel.get());
}

void test_shutdown_drains_mixed_accepted_commands() {
    MatchingWorker worker(3);

    worker.pause_before_next_command_for_test();

    // First command is removed from the queue and paused.
    auto resting = worker.submit(
        Order(58001, Side::SELL, 10500, 10)
    );

    worker.wait_until_paused_for_test();

    // These three commands fill the queue.
    auto crossing = worker.submit(
        Order(58002, Side::BUY, 10500, 4)
    );

    auto cancellation = worker.cancel(58001);

    auto later_buy = worker.submit(
        Order(58003, Side::BUY, 10500, 6)
    );

    // Shutdown must process all four accepted commands.
    worker.shutdown();

    CHECK(resting.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    CHECK(crossing.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    CHECK(cancellation.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

    CHECK(later_buy.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);

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
}

#endif

void test_concurrent_mixed_commands() {
    constexpr int orders_per_producer = 50;

    MatchingWorker worker(8);

    // Preload independent liquidity for MARKET orders.
    // These SELL orders cannot cross the LIMIT BUY workload.
    for (int i = 0; i < orders_per_producer; ++i) {
        auto result = worker.submit(
            Order(60000 + i, Side::SELL, 15000, 1)
        ).get();

        CHECK(result.status == SubmissionStatus::ACCEPTED);
        CHECK(result.executed_quantity == 0);
        CHECK(result.remaining_quantity == 1);
    }

    // Preload separate orders reserved for cancellation.
    for (int i = 0; i < orders_per_producer; ++i) {
        auto result = worker.submit(
            Order(61000 + i, Side::SELL, 20000, 1)
        ).get();

        CHECK(result.status == SubmissionStatus::ACCEPTED);
    }

    std::atomic<int> completed{0};
    std::atomic<int> failed{0};

    std::vector<std::thread> producers;

    // Producer 1: non-crossing LIMIT BUY orders.
    producers.emplace_back([&] {
        for (int i = 0; i < orders_per_producer; ++i) {
            auto result = worker.submit(
                Order(62000 + i, Side::BUY, 10000, 1)
            ).get();

            if (result.status == SubmissionStatus::ACCEPTED &&
                result.executed_quantity == 0 &&
                result.remaining_quantity == 1) {
                ++completed;
            } else {
                ++failed;
            }
        }
    });

    // Producer 2: non-crossing LIMIT SELL orders.
    producers.emplace_back([&] {
        for (int i = 0; i < orders_per_producer; ++i) {
            auto result = worker.submit(
                Order(63000 + i, Side::SELL, 25000, 1)
            ).get();

            if (result.status == SubmissionStatus::ACCEPTED &&
                result.executed_quantity == 0 &&
                result.remaining_quantity == 1) {
                ++completed;
            } else {
                ++failed;
            }
        }
    });

    // Producer 3: MARKET BUY orders.
    producers.emplace_back([&] {
        for (int i = 0; i < orders_per_producer; ++i) {
            auto result = worker.submit(
                Order(
                    64000 + i,
                    Side::BUY,
                    OrderType::MARKET,
                    0,
                    1
                )
            ).get();

            if (result.status == SubmissionStatus::ACCEPTED &&
                result.executed_quantity == 1 &&
                result.remaining_quantity == 0) {
                ++completed;
            } else {
                ++failed;
            }
        }
    });

    // Producer 4: cancellations of independent resting orders.
    producers.emplace_back([&] {
        for (int i = 0; i < orders_per_producer; ++i) {
            const bool cancelled =
                worker.cancel(61000 + i).get();

            if (cancelled) {
                ++completed;
            } else {
                ++failed;
            }
        }
    });

    for (auto& producer : producers) {
        producer.join();
    }

    worker.shutdown();

    CHECK(completed.load() == 4 * orders_per_producer);
    CHECK(failed.load() == 0);
}

void test_concurrent_quantity_conservation() {
    constexpr int producer_count = 4;
    constexpr int orders_per_producer = 5;
    constexpr Quantity quantity_per_order = 5;

    constexpr Quantity total_quantity =
        producer_count *
        orders_per_producer *
        quantity_per_order;

    MatchingWorker worker(8);

    auto resting = worker.submit(
        Order(70000, Side::SELL, 15000, total_quantity)
    );

    const auto resting_result = resting.get();

    CHECK(resting_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(resting_result.executed_quantity == 0);
    CHECK(resting_result.remaining_quantity ==
          total_quantity);

    std::atomic<Quantity> executed_total{0};
    std::atomic<int> accepted_count{0};
    std::atomic<int> unexpected_count{0};

    std::vector<std::thread> producers;

    for (int producer_id = 0;
         producer_id < producer_count;
         ++producer_id) {

        producers.emplace_back([&, producer_id] {
            for (int i = 0;
                 i < orders_per_producer;
                 ++i) {

                const OrderId id =
                    71000 +
                    producer_id * orders_per_producer +
                    i;

                auto future = worker.submit(
                    Order(
                        id,
                        Side::BUY,
                        OrderType::MARKET,
                        0,
                        quantity_per_order
                    )
                );

                const auto result = future.get();

                if (result.status ==
                        SubmissionStatus::ACCEPTED &&
                    result.executed_quantity ==
                        quantity_per_order &&
                    result.remaining_quantity == 0) {

                    executed_total.fetch_add(
                        result.executed_quantity,
                        std::memory_order_relaxed
                    );

                    accepted_count.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                } else {
                    unexpected_count.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    CHECK(accepted_count.load() ==
          producer_count * orders_per_producer);

    CHECK(unexpected_count.load() == 0);

    CHECK(executed_total.load() == total_quantity);

    // The resting SELL must have been fully consumed.
    CHECK(!worker.cancel(70000).get());

    // A filled order's ID remains historically reserved.
    const auto reused = worker.submit(
        Order(70000, Side::SELL, 15000, 1)
    ).get();

    CHECK(reused.status ==
          SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_concurrent_price_time_priority() {
    MatchingWorker worker(8);

    const auto first = worker.submit(
        Order(72001, Side::SELL, 15000, 5)
    ).get();

    const auto second = worker.submit(
        Order(72002, Side::SELL, 15000, 5)
    ).get();

    CHECK(first.status == SubmissionStatus::ACCEPTED);
    CHECK(second.status == SubmissionStatus::ACCEPTED);

    std::atomic<int> accepted_count{0};
    std::atomic<Quantity> executed_total{0};

    std::vector<std::thread> producers;

    for (int i = 0; i < 2; ++i) {
        producers.emplace_back([&, i] {
            const auto result = worker.submit(
                Order(
                    73000 + i,
                    Side::BUY,
                    OrderType::MARKET,
                    0,
                    3
                )
            ).get();

            if (result.status == SubmissionStatus::ACCEPTED &&
                result.executed_quantity == 3 &&
                result.remaining_quantity == 0) {

                accepted_count.fetch_add(
                    1, std::memory_order_relaxed
                );

                executed_total.fetch_add(
                    result.executed_quantity,
                    std::memory_order_relaxed
                );
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    CHECK(accepted_count.load() == 2);
    CHECK(executed_total.load() == 6);

    // FIFO requires the first resting order to be
    // fully consumed before the second is completed.
    CHECK(!worker.cancel(72001).get());
    CHECK(worker.cancel(72002).get());

    // Both IDs must remain historically reserved.
    CHECK(worker.submit(
        Order(72001, Side::SELL, 15000, 1)
    ).get().status == SubmissionStatus::DUPLICATE_ID);

    CHECK(worker.submit(
        Order(72002, Side::SELL, 15000, 1)
    ).get().status == SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_worker_different_price_priority() {
    MatchingWorker worker(8);

    // Insert the worse-priced SELL first.
    // Price priority must override insertion order.
    const auto expensive = worker.submit(
        Order(74002, Side::SELL, 15000, 6)
    ).get();

    const auto cheaper = worker.submit(
        Order(74001, Side::SELL, 14000, 4)
    ).get();

    CHECK(expensive.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(cheaper.status ==
          SubmissionStatus::ACCEPTED);

    // The BUY must consume the cheaper SELL first.
    const auto market = worker.submit(
        Order(
            74003,
            Side::BUY,
            OrderType::MARKET,
            0,
            7
        )
    ).get();

    CHECK(market.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(market.executed_quantity == 7);
    CHECK(market.remaining_quantity == 0);

    // Cheaper order: fully consumed.
    CHECK(!worker.cancel(74001).get());

    // Expensive order: 3 units should remain.
    CHECK(worker.cancel(74002).get());

    // Historical IDs remain reserved after
    // both execution and cancellation.
    const auto reused_cheaper = worker.submit(
        Order(74001, Side::SELL, 14000, 1)
    ).get();

    const auto reused_expensive = worker.submit(
        Order(74002, Side::SELL, 15000, 1)
    ).get();

    CHECK(reused_cheaper.status ==
          SubmissionStatus::DUPLICATE_ID);
    CHECK(reused_expensive.status ==
          SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_cancellation_before_submission() {
    MatchingWorker worker(8);

    constexpr OrderId id = 75001;

    // Establish queue order explicitly:
    // CANCEL must be accepted before SUBMIT.
    auto cancellation = worker.cancel(id);

    auto submission = worker.submit(
        Order(id, Side::SELL, 10500, 10)
    );

    CHECK(!cancellation.get());

    const auto result = submission.get();

    CHECK(result.status == SubmissionStatus::ACCEPTED);
    CHECK(result.executed_quantity == 0);
    CHECK(result.remaining_quantity == 10);

    // The earlier failed cancellation must not
    // prevent the later submission from resting.
    CHECK(worker.cancel(id).get());

    // Cancellation must not release historical IDs.
    const auto reused = worker.submit(
        Order(id, Side::SELL, 10500, 1)
    ).get();

    CHECK(reused.status ==
          SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_crossing_before_cancellation() {
    MatchingWorker worker(8);

    auto resting = worker.submit(
        Order(76001, Side::SELL, 10500, 10)
    );

    auto crossing = worker.submit(
        Order(76002, Side::BUY, 10500, 10)
    );

    auto cancellation = worker.cancel(76001);

    const auto resting_result = resting.get();
    const auto crossing_result = crossing.get();
    const bool cancelled = cancellation.get();

    CHECK(resting_result.status ==
          SubmissionStatus::ACCEPTED);

    CHECK(crossing_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(crossing_result.executed_quantity == 10);
    CHECK(crossing_result.remaining_quantity == 0);

    // The resting order was fully filled before
    // the cancellation command was processed.
    CHECK(!cancelled);

    // A fully executed order ID remains reserved.
    CHECK(worker.submit(
        Order(76001, Side::SELL, 10500, 1)
    ).get().status == SubmissionStatus::DUPLICATE_ID);

    worker.shutdown();
}

void test_seeded_concurrent_stress() {
    constexpr int producer_count = 4;
    constexpr int commands_per_producer = 100;
    constexpr std::size_t queue_capacity = 8;

    const unsigned int seeds[] = {
        42u, 123u, 2026u, 7777u, 98765u
    };

    for (const unsigned int seed : seeds) {
        std::printf(
            "Concurrent stress: seed=%u, producers=%d, "
            "commands_per_producer=%d, queue_capacity=%zu\n",
            seed,
            producer_count,
            commands_per_producer,
            queue_capacity
        );
        std::fflush(stdout);

        MatchingWorker worker(queue_capacity);

        // Liquidity reserved for MARKET BUY commands.
        for (int i = 0; i < commands_per_producer; ++i) {
            const auto result = worker.submit(
                Order(80000 + i, Side::SELL, 15000, 1)
            ).get();

            CHECK(result.status == SubmissionStatus::ACCEPTED);
        }

        // Separate liquidity reserved for cancellations.
        for (int i = 0; i < commands_per_producer; ++i) {
            const auto result = worker.submit(
                Order(81000 + i, Side::SELL, 20000, 1)
            ).get();

            CHECK(result.status == SubmissionStatus::ACCEPTED);
        }

        std::atomic<int> completed{0};
        std::atomic<int> failed{0};
        std::atomic<int> ready_count{0};
        std::atomic<bool> start{false};

        std::vector<std::thread> producers;

        for (int producer_id = 0;
             producer_id < producer_count;
             ++producer_id) {

            producers.emplace_back([&, producer_id] {
                std::vector<int> indices(
                    commands_per_producer
                );

                std::iota(
                    indices.begin(),
                    indices.end(),
                    0
                );

                const unsigned int producer_seed =
                    seed + static_cast<unsigned int>(
                        producer_id * 1009
                    );

                std::mt19937 rng(producer_seed);

                std::shuffle(
                    indices.begin(),
                    indices.end(),
                    rng
                );

                ready_count.fetch_add(
                    1,
                    std::memory_order_release
                );

                while (!start.load(
                    std::memory_order_acquire
                )) {
                    std::this_thread::yield();
                }

                for (const int i : indices) {
                    bool valid = false;

                    if (producer_id == 0) {
                        const auto result = worker.submit(
                            Order(
                                82000 + i,
                                Side::BUY,
                                10000,
                                1
                            )
                        ).get();

                        valid =
                            result.status ==
                                SubmissionStatus::ACCEPTED &&
                            result.executed_quantity == 0 &&
                            result.remaining_quantity == 1;

                    } else if (producer_id == 1) {
                        const auto result = worker.submit(
                            Order(
                                83000 + i,
                                Side::SELL,
                                25000,
                                1
                            )
                        ).get();

                        valid =
                            result.status ==
                                SubmissionStatus::ACCEPTED &&
                            result.executed_quantity == 0 &&
                            result.remaining_quantity == 1;

                    } else if (producer_id == 2) {
                        const auto result = worker.submit(
                            Order(
                                84000 + i,
                                Side::BUY,
                                OrderType::MARKET,
                                0,
                                1
                            )
                        ).get();

                        valid =
                            result.status ==
                                SubmissionStatus::ACCEPTED &&
                            result.executed_quantity == 1 &&
                            result.remaining_quantity == 0;

                    } else {
                        valid = worker.cancel(
                            81000 + i
                        ).get();
                    }

                    if (valid) {
                        completed.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                    } else {
                        std::fprintf(
                            stderr,
                            "Stress failure: seed=%u, "
                            "producer=%d, operation=%d\n",
                            seed,
                            producer_id,
                            i
                        );
                        std::fflush(stderr);

                        failed.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                    }
                }
            });
        }

        while (ready_count.load(
            std::memory_order_acquire
        ) != producer_count) {
            std::this_thread::yield();
        }

        start.store(
            true,
            std::memory_order_release
        );

        for (auto& producer : producers) {
            producer.join();
        }

        CHECK(completed.load() ==
              producer_count * commands_per_producer);

        CHECK(failed.load() == 0);

        worker.shutdown();
    }
}

void test_concurrent_shutdown_calls() {
    MatchingWorker worker(8);

    auto first = worker.submit(
        Order(59001, Side::SELL, 10500, 10)
    );

    auto second = worker.submit(
        Order(59002, Side::BUY, 10500, 4)
    );

    constexpr int shutdown_callers = 4;

    std::atomic<int> ready_count{0};
    std::atomic<bool> start{false};

    std::vector<std::thread> callers;

    for (int i = 0; i < shutdown_callers; ++i) {
        callers.emplace_back([&] {
            ready_count.fetch_add(
                1, std::memory_order_release
            );

            while (!start.load(
                std::memory_order_acquire
            )) {
                std::this_thread::yield();
            }

            worker.shutdown();
        });
    }

    while (ready_count.load(
        std::memory_order_acquire
    ) != shutdown_callers) {
        std::this_thread::yield();
    }

    start.store(true, std::memory_order_release);

    for (auto& caller : callers) {
        caller.join();
    }

    CHECK(first.get().status ==
          SubmissionStatus::ACCEPTED);

    const auto second_result = second.get();

    CHECK(second_result.status ==
          SubmissionStatus::ACCEPTED);
    CHECK(second_result.executed_quantity == 4);

    // Repeated shutdown must remain safe.
    worker.shutdown();
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
    test_concurrent_mixed_commands();
    test_seeded_concurrent_stress();
    test_concurrent_quantity_conservation();
    test_concurrent_price_time_priority();
    test_worker_different_price_priority();
    test_multiple_outstanding_futures();
    test_concurrent_duplicate_order_ids();
    test_worker_preserves_queue_submission_order();
    test_worker_cancellation();
    test_cancellation_before_submission();
    test_crossing_before_cancellation();
    test_cancellation_after_full_fill();
    test_cancellation_prevents_future_match();
    test_cancel_partially_filled_order();
    test_cancelled_order_id_cannot_be_reused();
    test_cancellation_drains_during_shutdown();
    test_concurrent_shutdown_calls();

#ifdef MATCHING_ENGINE_TESTING
    test_matching_exception_reaches_future_and_worker_recovers();
    test_blocked_submission_wakes_on_shutdown();
    test_blocked_cancellation_wakes_on_shutdown();
    test_shutdown_drains_mixed_accepted_commands();
#endif

    std::printf("MatchingWorker tests passed\n");

    return 0;
}
