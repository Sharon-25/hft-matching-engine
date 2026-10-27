#include <cstdio>
#include <cstdlib>
#include <future>
#include <new>
#include <stdexcept>

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

int main() {
    test_limit_orders_through_worker();
    test_market_orders_through_worker();
    test_fifo_submission_order();
    test_duplicate_order_id();
    test_shutdown_rejects_new_submissions();
    test_shutdown_drains_accepted_commands();
    test_destructor_drains_accepted_commands();

#ifdef MATCHING_ENGINE_TESTING
    test_matching_exception_reaches_future_and_worker_recovers();
#endif

    std::printf("MatchingWorker tests passed\n");

    return 0;
}
