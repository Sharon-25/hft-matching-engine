#include <Windows.h>
#include <Psapi.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "matching_engine/MatchingEngine.hpp"
#include "matching_engine/Order.hpp"

namespace {

constexpr std::size_t kOperations = 10000;
constexpr matching_engine::Quantity kQuantity = 10;

std::size_t private_memory_bytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};

    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        throw std::runtime_error(
            "GetProcessMemoryInfo failed"
        );
    }

    return static_cast<std::size_t>(
        counters.PrivateUsage
    );
}

void verify_submission(
    const matching_engine::SubmissionResult& result,
    matching_engine::Quantity expected_executed,
    matching_engine::Quantity expected_remaining
) {
    if (result.status !=
            matching_engine::SubmissionStatus::ACCEPTED ||
        result.executed_quantity != expected_executed ||
        result.remaining_quantity != expected_remaining) {
        throw std::runtime_error(
            "Profiling workload verification failed"
        );
    }
}

void run_limit_profile() {
    using namespace matching_engine;

    MatchingEngine engine;

    const auto memory_before = private_memory_bytes();
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < kOperations; ++i) {
        const Order order(i + 1, Side::BUY, 9000, kQuantity);

        verify_submission(
            engine.submit_limit_order(order), 0, kQuantity
        );
    }

    const auto end = std::chrono::steady_clock::now();
    const auto memory_after = private_memory_bytes();

    std::cout << "LIMIT operations: " << kOperations << '\n';
    std::cout << "Active orders: "
              << engine.order_book().order_count() << '\n';
    std::cout << "Elapsed ms: "
              << std::chrono::duration<double, std::milli>(
                     end - start).count() << '\n';
    std::cout << "Private memory delta bytes: "
              << static_cast<std::int64_t>(memory_after) -
                     static_cast<std::int64_t>(memory_before)
              << '\n';
}

void run_market_profile(bool preallocate, bool geometric) {
    using namespace matching_engine;

    MatchingEngine engine;

    for (std::size_t i = 0; i < kOperations; ++i) {
        const Order resting(
            i + 1, Side::SELL, 10000, kQuantity
        );

        verify_submission(
            engine.submit_limit_order(resting), 0, kQuantity
        );
    }

    if (!geometric && !preallocate) {
        engine.enable_exact_trade_growth_for_profiling();
    }

    if (preallocate) {
        engine.reserve_trade_capacity_for_profiling(kOperations);
    }

    const auto memory_before = private_memory_bytes();
    std::size_t trade_capacity_growth_events = 0;
    std::size_t cumulative_trade_capacity_increase = 0;
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < kOperations; ++i) {
        const Order incoming(
            20001 + i,
            Side::BUY,
            OrderType::MARKET,
            0,
            kQuantity
        );

        const std::size_t capacity_before =
            engine.trades().capacity();

        verify_submission(
            engine.submit_market_order(incoming), kQuantity, 0
        );

        const std::size_t capacity_after =
            engine.trades().capacity();

        if (capacity_after > capacity_before) {
            ++trade_capacity_growth_events;

            cumulative_trade_capacity_increase +=
                capacity_after - capacity_before;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto memory_after = private_memory_bytes();

    std::cout << "Mode: "
              << (geometric ? "geometric" :
                  preallocate ? "preallocated" : "baseline")
              << '\n';
    std::cout << "MARKET operations: " << kOperations << '\n';
    std::cout << "Trades: " << engine.trades().size() << '\n';
    std::cout << "Trade capacity growth events: "
              << trade_capacity_growth_events << '\n';

    std::cout << "Final trade vector capacity: "
              << engine.trades().capacity() << '\n';

    std::cout << "Cumulative capacity increase: "
              << cumulative_trade_capacity_increase << '\n';

    const auto& stats = engine.market_profiling_stats();

    std::cout << "Preflight total ms: "
              << stats.preflight_ns / 1'000'000.0 << '\n';

    std::cout << "Reservation total ms: "
              << stats.reservation_ns / 1'000'000.0 << '\n';

    std::cout << "Execution total ms: "
              << stats.execution_ns / 1'000'000.0 << '\n';

    std::cout << "Profiled submissions: "
              << stats.submissions << '\n';

    std::cout << "Elapsed ms: "
              << std::chrono::duration<double, std::milli>(
                     end - start).count() << '\n';
    std::cout << "Private memory delta bytes: "
              << static_cast<std::int64_t>(memory_after) -
                     static_cast<std::int64_t>(memory_before)
              << '\n';
}

void run_depth_profile(
    std::size_t resting_count,
    std::size_t match_count
) {
    using namespace matching_engine;

    if (match_count == 0 || match_count > resting_count) {
        throw std::invalid_argument("Invalid match count");
    }

    MatchingEngine engine;

    // Load resting orders before starting measurement.
    for (std::size_t i = 0; i < resting_count; ++i) {
        const Order resting(
            i + 1,
            Side::SELL,
            10000,
            kQuantity
        );

        verify_submission(
            engine.submit_limit_order(resting),
            0,
            kQuantity
        );
    }

    const Order incoming(
        resting_count + 1,
        Side::BUY,
        OrderType::MARKET,
        0,
        static_cast<Quantity>(match_count) * kQuantity
    );

    const auto start = std::chrono::steady_clock::now();

    verify_submission(
        engine.submit_market_order(incoming),
        static_cast<Quantity>(match_count) * kQuantity,
        0
    );

    const auto end = std::chrono::steady_clock::now();

    const auto& stats = engine.market_profiling_stats();

    std::cout << "Resting orders: "
              << resting_count << '\n';

    std::cout << "Matched orders: "
              << match_count << '\n';

    std::cout << "Preflight ns: "
              << stats.preflight_ns << '\n';

    std::cout << "Reservation ns: "
              << stats.reservation_ns << '\n';

    std::cout << "Execution ns: "
              << stats.execution_ns << '\n';

    std::cout << "Total submission ns: "
              << std::chrono::duration_cast<
                     std::chrono::nanoseconds>(
                     end - start
                 ).count()
              << '\n';

    std::cout << "Remaining resting orders: "
              << engine.order_book().order_count()
              << '\n';
}

void run_cancel_profile() {
    using namespace matching_engine;

    MatchingEngine engine;

    for (std::size_t i = 0; i < kOperations; ++i) {
        const Order order(
            i + 1, Side::BUY, 8000, kQuantity
        );

        verify_submission(
            engine.submit_limit_order(order), 0, kQuantity
        );
    }

    const auto memory_before = private_memory_bytes();
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < kOperations; ++i) {
        if (!engine.cancel_order(i + 1)) {
            throw std::runtime_error(
                "Cancellation verification failed"
            );
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto memory_after = private_memory_bytes();

    std::cout << "CANCEL operations: " << kOperations << '\n';
    std::cout << "Active orders: "
              << engine.order_book().order_count() << '\n';
    std::cout << "Elapsed ms: "
              << std::chrono::duration<double, std::milli>(
                     end - start).count() << '\n';
    std::cout << "Private memory delta bytes: "
              << static_cast<std::int64_t>(memory_after) -
                     static_cast<std::int64_t>(memory_before)
              << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr
            << "Usage: matching_allocation_profile "
               "<limit|market|market-preallocated|market-geometric|"
               "depth-shallow|depth-deep-single|depth-deep-multi|cancel>\n";
        return 1;
    }

    try {
        const std::string_view workload(argv[1]);

        if (workload == "limit") {
            run_limit_profile();
        } else if (workload == "market") {
            run_market_profile(false, false);
        } else if (workload == "market-preallocated") {
            run_market_profile(true, false);
        } else if (workload == "market-geometric") {
            run_market_profile(false, true);
        } else if (workload == "depth-shallow") {
            run_depth_profile(100, 1);
        } else if (workload == "depth-deep-single") {
            run_depth_profile(10000, 1);
        } else if (workload == "depth-deep-multi") {
            run_depth_profile(10000, 100);
        } else if (workload == "cancel") {
            run_cancel_profile();
        } else {
            std::cerr << "Unknown workload\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "Profiling error: "
                  << error.what() << '\n';
        return 1;
    }

    return 0;
}
