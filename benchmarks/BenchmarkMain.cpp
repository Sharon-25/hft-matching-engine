#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "matching_engine/MatchingEngine.hpp"
#include "matching_engine/MatchingWorker.hpp"
#include "matching_engine/Order.hpp"
#include "matching_engine/Types.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

struct BenchmarkStats {
    std::int64_t minimum_ns;
    std::int64_t p50_ns;
    std::int64_t p95_ns;
    std::int64_t p99_ns;
    std::int64_t maximum_ns;
};

std::int64_t percentile(
    const std::vector<std::int64_t>& sorted,
    double fraction
) {
    if (sorted.empty()) {
        throw std::invalid_argument("No latency samples");
    }

    const auto index = static_cast<std::size_t>(
        fraction * static_cast<double>(sorted.size() - 1)
    );

    return sorted[index];
}

BenchmarkStats calculate_stats(
    std::vector<std::int64_t> samples
) {
    if (samples.empty()) {
        throw std::invalid_argument("No latency samples");
    }

    std::sort(samples.begin(), samples.end());

    return {
        samples.front(),
        percentile(samples, 0.50),
        percentile(samples, 0.95),
        percentile(samples, 0.99),
        samples.back()
    };
}

void print_stats(
    const char* name,
    const BenchmarkStats& stats
) {
    std::cout
        << name << '\n'
        << "  Min: " << stats.minimum_ns << " ns\n"
        << "  p50: " << stats.p50_ns << " ns\n"
        << "  p95: " << stats.p95_ns << " ns\n"
        << "  p99: " << stats.p99_ns << " ns\n"
        << "  Max: " << stats.maximum_ns << " ns\n";
}

void run_timer_baseline() {
    constexpr std::size_t warmup_iterations = 10000;
    constexpr std::size_t measured_iterations = 100000;

    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        const auto start = Clock::now();
        const auto end = Clock::now();
        (void)std::chrono::duration_cast<Nanoseconds>(
            end - start
        ).count();
    }

    std::vector<std::int64_t> samples;
    samples.reserve(measured_iterations);

    for (std::size_t i = 0; i < measured_iterations; ++i) {
        const auto start = Clock::now();
        const auto end = Clock::now();

        const auto elapsed =
            std::chrono::duration_cast<Nanoseconds>(
                end - start
            ).count();

        samples.push_back(elapsed);
    }

    const auto stats = calculate_stats(std::move(samples));

    std::cout << "Clock steady: "
              << std::boolalpha << Clock::is_steady << '\n';

    print_stats("Timer baseline", stats);
}

void run_b1_non_crossing_limit() {
    constexpr std::size_t warmup_iterations = 1000;
    constexpr std::size_t measured_iterations = 10000;

    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId id =
            static_cast<matching_engine::OrderId>(i + 1);
        const matching_engine::Price price = 10000;
        const matching_engine::Quantity quantity = 10;
        const matching_engine::Order order(
            id,
            matching_engine::Side::BUY,
            price,
            quantity
        );

        const auto result = engine.submit_limit_order(order);

        if (result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            result.executed_quantity != 0 ||
            result.remaining_quantity != quantity ||
            !engine.trades().empty()) {
            throw std::runtime_error("B1 warmup verification failed");
        }
    }

    std::vector<std::int64_t> samples;
    samples.reserve(measured_iterations);

    for (std::size_t i = 0; i < measured_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId id =
            static_cast<matching_engine::OrderId>(i + 1);
        const matching_engine::Price price = 10000;
        const matching_engine::Quantity quantity = 10;
        const matching_engine::Order order(
            id,
            matching_engine::Side::BUY,
            price,
            quantity
        );

        const auto start = Clock::now();
        const auto result = engine.submit_limit_order(order);
        const auto end = Clock::now();

        const auto elapsed =
            std::chrono::duration_cast<Nanoseconds>(
                end - start
            ).count();

        samples.push_back(elapsed);

        if (result.status != matching_engine::SubmissionStatus::ACCEPTED) {
            throw std::runtime_error("B1: order was not accepted");
        }
        if (result.executed_quantity != 0) {
            throw std::runtime_error("B1: executed quantity is not zero");
        }
        if (result.remaining_quantity != quantity) {
            throw std::runtime_error(
                "B1: remaining quantity does not match submitted quantity"
            );
        }
        if (!engine.trades().empty()) {
            throw std::runtime_error("B1: trades were unexpectedly generated");
        }
    }

    const auto stats = calculate_stats(std::move(samples));

    std::cout << '\n';
    print_stats("B1 - Non-crossing LIMIT insertion", stats);
}

void run_b2_crossing_limit() {
    constexpr std::size_t warmup_iterations = 1000;
    constexpr std::size_t measured_iterations = 10000;

    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId resting_id =
            static_cast<matching_engine::OrderId>(2 * i + 1);
        const matching_engine::OrderId incoming_id =
            static_cast<matching_engine::OrderId>(2 * i + 2);
        const matching_engine::Price sell_price = 10000;
        const matching_engine::Price buy_price = 10500;
        const matching_engine::Quantity quantity = 10;

        const matching_engine::Order resting_sell(
            resting_id,
            matching_engine::Side::SELL,
            sell_price,
            quantity
        );
        const auto preload_result = engine.submit_limit_order(resting_sell);
        if (preload_result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            preload_result.executed_quantity != 0 ||
            preload_result.remaining_quantity != quantity) {
            throw std::runtime_error("B2 warmup: resting order preload failed");
        }

        const matching_engine::Order incoming_buy(
            incoming_id,
            matching_engine::Side::BUY,
            buy_price,
            quantity
        );

        const auto result = engine.submit_limit_order(incoming_buy);

        if (result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            result.executed_quantity != quantity ||
            result.remaining_quantity != 0 ||
            engine.trades().size() != 1) {
            throw std::runtime_error("B2 warmup verification failed");
        }
    }

    std::vector<std::int64_t> samples;
    samples.reserve(measured_iterations);

    for (std::size_t i = 0; i < measured_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId resting_id =
            static_cast<matching_engine::OrderId>(2 * i + 1);
        const matching_engine::OrderId incoming_id =
            static_cast<matching_engine::OrderId>(2 * i + 2);
        const matching_engine::Price sell_price = 10000;
        const matching_engine::Price buy_price = 10500;
        const matching_engine::Quantity quantity = 10;

        const matching_engine::Order resting_sell(
            resting_id,
            matching_engine::Side::SELL,
            sell_price,
            quantity
        );
        const auto preload_result = engine.submit_limit_order(resting_sell);
        if (preload_result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            preload_result.executed_quantity != 0 ||
            preload_result.remaining_quantity != quantity) {
            throw std::runtime_error("B2: resting order preload failed");
        }

        const matching_engine::Order incoming_buy(
            incoming_id,
            matching_engine::Side::BUY,
            buy_price,
            quantity
        );

        const auto start = Clock::now();
        const auto result = engine.submit_limit_order(incoming_buy);
        const auto end = Clock::now();

        const auto elapsed =
            std::chrono::duration_cast<Nanoseconds>(
                end - start
            ).count();

        samples.push_back(elapsed);

        if (result.status != matching_engine::SubmissionStatus::ACCEPTED) {
            throw std::runtime_error("B2: incoming order was not accepted");
        }
        if (result.executed_quantity != quantity) {
            throw std::runtime_error(
                "B2: executed quantity does not match full quantity"
            );
        }
        if (result.remaining_quantity != 0) {
            throw std::runtime_error("B2: remaining quantity is not zero");
        }
        if (engine.trades().size() != 1) {
            throw std::runtime_error("B2: expected exactly one trade");
        }

        const auto& trade = engine.trades().front();
        if (trade.incoming_order_id != incoming_id ||
            trade.resting_order_id != resting_id ||
            trade.execution_price_ticks != sell_price ||
            trade.executed_quantity != quantity) {
            throw std::runtime_error("B2: trade attributes mismatch");
        }
    }

    const auto stats = calculate_stats(std::move(samples));

    std::cout << '\n';
    print_stats("B2 - Crossing LIMIT execution", stats);
}

void run_b3_market_execution() {
    constexpr std::size_t warmup_iterations = 1000;
    constexpr std::size_t measured_iterations = 10000;

    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId resting_id =
            static_cast<matching_engine::OrderId>(2 * i + 1);
        const matching_engine::OrderId incoming_id =
            static_cast<matching_engine::OrderId>(2 * i + 2);
        const matching_engine::Price sell_price = 10000;
        const matching_engine::Quantity quantity = 10;

        const matching_engine::Order resting_sell(
            resting_id,
            matching_engine::Side::SELL,
            sell_price,
            quantity
        );
        const auto preload_result = engine.submit_limit_order(resting_sell);
        if (preload_result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            preload_result.executed_quantity != 0 ||
            preload_result.remaining_quantity != quantity) {
            throw std::runtime_error("B3 warmup: resting order preload failed");
        }

        const matching_engine::Order incoming_buy(
            incoming_id,
            matching_engine::Side::BUY,
            matching_engine::OrderType::MARKET,
            0,
            quantity
        );

        const auto result = engine.submit_market_order(incoming_buy);

        if (result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            result.executed_quantity != quantity ||
            result.remaining_quantity != 0 ||
            engine.trades().size() != 1) {
            throw std::runtime_error("B3 warmup verification failed");
        }

        const auto& trade = engine.trades().front();
        if (trade.incoming_order_id != incoming_id ||
            trade.resting_order_id != resting_id ||
            trade.execution_price_ticks != sell_price ||
            trade.executed_quantity != quantity) {
            throw std::runtime_error("B3 warmup: trade attributes mismatch");
        }
    }

    std::vector<std::int64_t> samples;
    samples.reserve(measured_iterations);

    for (std::size_t i = 0; i < measured_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId resting_id =
            static_cast<matching_engine::OrderId>(2 * i + 1);
        const matching_engine::OrderId incoming_id =
            static_cast<matching_engine::OrderId>(2 * i + 2);
        const matching_engine::Price sell_price = 10000;
        const matching_engine::Quantity quantity = 10;

        const matching_engine::Order resting_sell(
            resting_id,
            matching_engine::Side::SELL,
            sell_price,
            quantity
        );
        const auto preload_result = engine.submit_limit_order(resting_sell);
        if (preload_result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            preload_result.executed_quantity != 0 ||
            preload_result.remaining_quantity != quantity) {
            throw std::runtime_error("B3: resting order preload failed");
        }

        const matching_engine::Order incoming_buy(
            incoming_id,
            matching_engine::Side::BUY,
            matching_engine::OrderType::MARKET,
            0,
            quantity
        );

        const auto start = Clock::now();
        const auto result = engine.submit_market_order(incoming_buy);
        const auto end = Clock::now();

        const auto elapsed =
            std::chrono::duration_cast<Nanoseconds>(
                end - start
            ).count();

        samples.push_back(elapsed);

        if (result.status != matching_engine::SubmissionStatus::ACCEPTED) {
            throw std::runtime_error("B3: market order was not accepted");
        }
        if (result.executed_quantity != quantity) {
            throw std::runtime_error(
                "B3: executed quantity does not match full quantity"
            );
        }
        if (result.remaining_quantity != 0) {
            throw std::runtime_error("B3: remaining quantity is not zero");
        }
        if (engine.trades().size() != 1) {
            throw std::runtime_error("B3: expected exactly one trade");
        }

        const auto& trade = engine.trades().front();
        if (trade.incoming_order_id != incoming_id ||
            trade.resting_order_id != resting_id ||
            trade.execution_price_ticks != sell_price ||
            trade.executed_quantity != quantity) {
            throw std::runtime_error("B3: trade attributes mismatch");
        }
    }

    const auto stats = calculate_stats(std::move(samples));

    std::cout << '\n';
    print_stats("B3 - MARKET execution", stats);
}

void run_b4_order_cancellation() {
    constexpr std::size_t warmup_iterations = 1000;
    constexpr std::size_t measured_iterations = 10000;

    for (std::size_t i = 0; i < warmup_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId id =
            static_cast<matching_engine::OrderId>(i + 1);
        const matching_engine::Price price = 10000;
        const matching_engine::Quantity quantity = 10;
        const matching_engine::Order order(
            id,
            matching_engine::Side::BUY,
            price,
            quantity
        );

        const auto preload_result = engine.submit_limit_order(order);
        if (preload_result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            preload_result.executed_quantity != 0 ||
            preload_result.remaining_quantity != quantity) {
            throw std::runtime_error("B4 warmup: preload failed");
        }

        const bool cancelled = engine.cancel_order(id);

        if (!cancelled ||
            engine.order_book().contains(id) ||
            !engine.trades().empty()) {
            throw std::runtime_error("B4 warmup verification failed");
        }
    }

    std::vector<std::int64_t> samples;
    samples.reserve(measured_iterations);

    for (std::size_t i = 0; i < measured_iterations; ++i) {
        matching_engine::MatchingEngine engine;
        const matching_engine::OrderId id =
            static_cast<matching_engine::OrderId>(i + 1);
        const matching_engine::Price price = 10000;
        const matching_engine::Quantity quantity = 10;
        const matching_engine::Order order(
            id,
            matching_engine::Side::BUY,
            price,
            quantity
        );

        const auto preload_result = engine.submit_limit_order(order);
        if (preload_result.status != matching_engine::SubmissionStatus::ACCEPTED ||
            preload_result.executed_quantity != 0 ||
            preload_result.remaining_quantity != quantity) {
            throw std::runtime_error("B4: preload failed");
        }

        const auto start = Clock::now();
        const bool cancelled = engine.cancel_order(id);
        const auto end = Clock::now();

        const auto elapsed =
            std::chrono::duration_cast<Nanoseconds>(
                end - start
            ).count();

        samples.push_back(elapsed);

        if (!cancelled) {
            throw std::runtime_error("B4: cancel_order returned false");
        }
        if (engine.order_book().contains(id)) {
            throw std::runtime_error(
                "B4: order is still present in active book"
            );
        }
        if (!engine.trades().empty()) {
            throw std::runtime_error("B4: trades were unexpectedly generated");
        }
    }

    const auto stats = calculate_stats(std::move(samples));

    std::cout << '\n';
    print_stats("B4 - Order cancellation", stats);
}

enum class ConcurrentWorkload {
    NON_CROSSING_LIMIT,
    MARKET_EXECUTION,
    CANCELLATION,
    MIXED
};

constexpr std::size_t kCommandsPerRun = 10000;
constexpr matching_engine::Quantity kOrderQuantity = 10;

struct BenchmarkCommand {
    bool is_cancel = false;
    matching_engine::OrderId id = 0;
    matching_engine::Side side = matching_engine::Side::BUY;
    matching_engine::OrderType type = matching_engine::OrderType::LIMIT;
    matching_engine::Price price = 0;
};

std::vector<BenchmarkCommand> prepare_workload(
    matching_engine::MatchingWorker& worker,
    ConcurrentWorkload workload,
    matching_engine::OrderId base_id
) {
    using namespace matching_engine;

    std::vector<BenchmarkCommand> commands;
    commands.reserve(kCommandsPerRun);

    const auto preload = [&](OrderId id, Side side, Price price) {
        const Order order(id, side, price, kOrderQuantity);
        const auto result = worker.submit(order).get();

        if (result.status != SubmissionStatus::ACCEPTED ||
            result.executed_quantity != 0 ||
            result.remaining_quantity != kOrderQuantity) {
            throw std::runtime_error("P8-08 preload failed");
        }
    };

    const auto add_limit = [&](OrderId id, Price price) {
        commands.push_back({
            false, id, Side::BUY, OrderType::LIMIT, price
        });
    };

    const auto add_market = [&](OrderId id) {
        commands.push_back({
            false, id, Side::BUY, OrderType::MARKET, 0
        });
    };

    const auto add_cancel = [&](OrderId id) {
        commands.push_back({
            true, id, Side::BUY, OrderType::LIMIT, 0
        });
    };

    switch (workload) {
    case ConcurrentWorkload::NON_CROSSING_LIMIT:
        for (std::size_t i = 0; i < kCommandsPerRun; ++i) {
            add_limit(base_id + 20001 + i, 9000);
        }
        break;

    case ConcurrentWorkload::MARKET_EXECUTION:
        for (std::size_t i = 0; i < kCommandsPerRun; ++i) {
            preload(base_id + 1 + i, Side::SELL, 10000);
            add_market(base_id + 20001 + i);
        }
        break;

    case ConcurrentWorkload::CANCELLATION:
        for (std::size_t i = 0; i < kCommandsPerRun; ++i) {
            const OrderId id = base_id + 1 + i;
            preload(id, Side::BUY, 8000);
            add_cancel(id);
        }
        break;

    case ConcurrentWorkload::MIXED:
        // 3000 MARKET executions.
        for (std::size_t i = 0; i < 3000; ++i) {
            preload(base_id + 1 + i, Side::SELL, 10000);
        }

        // 3000 independent cancellation targets.
        for (std::size_t i = 0; i < 3000; ++i) {
            preload(base_id + 5001 + i, Side::BUY, 8000);
        }

        // 4000 new non-crossing LIMIT orders.
        for (std::size_t i = 0; i < 4000; ++i) {
            add_limit(base_id + 20001 + i, 9000);
        }

        // 3000 MARKET orders.
        for (std::size_t i = 0; i < 3000; ++i) {
            add_market(base_id + 25001 + i);
        }

        // 3000 cancellations.
        for (std::size_t i = 0; i < 3000; ++i) {
            add_cancel(base_id + 5001 + i);
        }
        break;
    }

    if (workload == ConcurrentWorkload::MIXED) {
        std::vector<BenchmarkCommand> mixed;
        mixed.reserve(kCommandsPerRun);

        std::size_t limit_index = 0;
        std::size_t market_index = 4000;
        std::size_t cancel_index = 7000;

        for (std::size_t i = 0; i < 1000; ++i) {
            for (int j = 0; j < 4; ++j) {
                mixed.push_back(commands[limit_index++]);
            }
            for (int j = 0; j < 3; ++j) {
                mixed.push_back(commands[market_index++]);
                mixed.push_back(commands[cancel_index++]);
            }
        }

        commands = std::move(mixed);
    }

    if (commands.size() != kCommandsPerRun) {
        throw std::runtime_error("P8-08 command count mismatch");
    }

    return commands;
}

struct SingleRunResult {
    double throughput_ops_sec;
    std::int64_t elapsed_ms;
    std::vector<std::int64_t> submit_api_durations;
    std::vector<std::int64_t> observation_latencies;
#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
    matching_engine::QueueBackpressureStats queue_stats;
#endif
};

using BenchmarkFuture = std::variant<
    std::future<matching_engine::SubmissionResult>,
    std::future<bool>
>;

struct OutstandingCommand {
    Clock::time_point submitted_at;
    BenchmarkFuture future;
    BenchmarkCommand command;
};

SingleRunResult execute_concurrent_producer_run(
    std::size_t producer_count,
    std::size_t queue_capacity,
    matching_engine::OrderId run_base_id,
    bool record_latencies,
    ConcurrentWorkload workload =
        ConcurrentWorkload::NON_CROSSING_LIMIT
) {
    constexpr std::size_t total_commands = 10000;
    constexpr std::size_t batch_size = 32;

    if (total_commands % producer_count != 0) {
        throw std::invalid_argument(
            "Total commands not evenly divisible by producer count"
        );
    }

    const std::size_t commands_per_producer =
        total_commands / producer_count;

    matching_engine::MatchingWorker worker(queue_capacity);

    const auto commands = prepare_workload(
        worker, workload, run_base_id
    );

    std::atomic<std::size_t> ready_count{0};
    std::atomic<bool> start_signal{false};
    std::atomic<std::size_t> successful_submissions{0};

    std::vector<std::thread> producers;
    producers.reserve(producer_count);

    std::vector<std::exception_ptr> exceptions(producer_count, nullptr);
    std::vector<std::vector<std::int64_t>> thread_submit_durations(producer_count);
    std::vector<std::vector<std::int64_t>> thread_obs_latencies(producer_count);

    for (std::size_t p = 0; p < producer_count; ++p) {
        if (record_latencies) {
            thread_submit_durations[p].reserve(commands_per_producer);
            thread_obs_latencies[p].reserve(commands_per_producer);
        }

        producers.emplace_back([&, p] {
            try {
                auto& local_submit = thread_submit_durations[p];
                auto& local_obs = thread_obs_latencies[p];

                std::vector<OutstandingCommand> outstanding;
                outstanding.reserve(batch_size);

                ready_count.fetch_add(1, std::memory_order_release);

                while (!start_signal.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                const auto collect_results = [&] {
                    for (auto& entry : outstanding) {
                        bool valid = false;

                        if (entry.command.is_cancel) {
                            valid = std::get<std::future<bool>>(
                                entry.future
                            ).get();
                        } else {
                            const auto result =
                                std::get<std::future<matching_engine::SubmissionResult>>(
                                    entry.future
                                ).get();

                            const bool is_market =
                                entry.command.type ==
                                matching_engine::OrderType::MARKET;

                            valid =
                                result.status ==
                                    matching_engine::SubmissionStatus::ACCEPTED &&
                                result.executed_quantity ==
                                    (is_market ? kOrderQuantity : 0) &&
                                result.remaining_quantity ==
                                    (is_market ? 0 : kOrderQuantity);
                        }

                        const auto observed_at = Clock::now();

                        if (!valid) {
                            throw std::runtime_error(
                                "P8-08 command correctness verification failed"
                            );
                        }

                        if (record_latencies) {
                            local_obs.push_back(
                                std::chrono::duration_cast<Nanoseconds>(
                                    observed_at - entry.submitted_at
                                ).count()
                            );
                        }

                        successful_submissions.fetch_add(
                            1, std::memory_order_relaxed
                        );
                    }

                    outstanding.clear();
                };

                for (std::size_t i = 0; i < commands_per_producer; ++i) {
                    const auto& command =
                        commands[p * commands_per_producer + i];

                    std::optional<matching_engine::Order> order;

                    if (!command.is_cancel) {
                        order.emplace(
                            command.id,
                            command.side,
                            command.type,
                            command.price,
                            kOrderQuantity
                        );
                    }

                    const auto before_submit = Clock::now();

                    BenchmarkFuture future = command.is_cancel
                        ? BenchmarkFuture(worker.cancel(command.id))
                        : BenchmarkFuture(worker.submit(*order));

                    const auto after_submit = Clock::now();

                    if (record_latencies) {
                        local_submit.push_back(
                            std::chrono::duration_cast<Nanoseconds>(
                                after_submit - before_submit
                            ).count()
                        );
                    }

                    outstanding.push_back({
                        before_submit,
                        std::move(future),
                        command
                    });

                    if (outstanding.size() >= batch_size) {
                        collect_results();
                    }
                }

                collect_results();
            } catch (...) {
                exceptions[p] = std::current_exception();
            }
        });
    }

    // Wait for all producers to reach barrier
    while (ready_count.load(std::memory_order_acquire) < producer_count) {
        std::this_thread::yield();
    }

    // Measure elapsed wall-clock duration from coordinated release
    const auto wall_start = Clock::now();
    start_signal.store(true, std::memory_order_release);

    for (auto& t : producers) {
        t.join();
    }
    const auto wall_end = Clock::now();

    SingleRunResult run_result;
    const auto total_elapsed_ns =
        std::chrono::duration_cast<Nanoseconds>(wall_end - wall_start).count();
    const double elapsed_seconds =
        static_cast<double>(total_elapsed_ns) / 1'000'000'000.0;
    const double throughput =
        static_cast<double>(total_commands) / elapsed_seconds;

    run_result.throughput_ops_sec = throughput;
    run_result.elapsed_ms = total_elapsed_ns / 1'000'000;

#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
    run_result.queue_stats = worker.queue_backpressure_stats();
#endif

    // Clean worker shutdown after all producers finished and futures collected
    worker.shutdown();

    // Propagate captured exceptions
    for (const auto& ep : exceptions) {
        if (ep) {
            std::rethrow_exception(ep);
        }
    }

    if (successful_submissions.load(std::memory_order_relaxed) != total_commands) {
        throw std::runtime_error("Successful submission count mismatch");
    }

    if (record_latencies) {
        run_result.submit_api_durations.reserve(total_commands);
        run_result.observation_latencies.reserve(total_commands);
        for (std::size_t p = 0; p < producer_count; ++p) {
            run_result.submit_api_durations.insert(
                run_result.submit_api_durations.end(),
                thread_submit_durations[p].begin(),
                thread_submit_durations[p].end()
            );
            run_result.observation_latencies.insert(
                run_result.observation_latencies.end(),
                thread_obs_latencies[p].begin(),
                thread_obs_latencies[p].end()
            );
        }
    }

    return run_result;
}

void run_concurrent_producer_scenario(
    const char* name,
    std::size_t producer_count,
    std::size_t queue_capacity,
    matching_engine::OrderId scenario_base_id,
    ConcurrentWorkload workload =
        ConcurrentWorkload::NON_CROSSING_LIMIT
) {
    constexpr std::size_t warmup_runs = 2;
    constexpr std::size_t measured_runs = 5;

    // 2 unreported warm-up runs (full correctness verification, fresh worker, unique IDs)
    for (std::size_t w = 0; w < warmup_runs; ++w) {
        const auto warmup_base_id = static_cast<matching_engine::OrderId>(
            scenario_base_id + (w + 1) * 100000
        );
        (void)execute_concurrent_producer_run(
            producer_count,
            queue_capacity,
            warmup_base_id,
            false,
            workload
        );
    }

    std::vector<double> throughputs;
    throughputs.reserve(measured_runs);

    std::vector<std::int64_t> all_submit_durations;
    all_submit_durations.reserve(measured_runs * 10000);

    std::vector<std::int64_t> all_obs_latencies;
    all_obs_latencies.reserve(measured_runs * 10000);

#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
    std::size_t total_pushes_all = 0;
    std::size_t blocked_pushes_all = 0;
    std::size_t rejected_pushes_all = 0;
    std::int64_t cumulative_wait_ns_all = 0;
    std::vector<std::int64_t> all_queue_wait_durations;
#endif

    std::cout
        << '\n'
        << name << '\n'
        << "  Producers: " << producer_count
        << ", Queue capacity: " << queue_capacity
        << ", Batch size: 32\n"
        << "  Warm-up runs: " << warmup_runs << " (unreported)\n"
        << "  Measured runs: " << measured_runs << " (10000 commands each)\n";

    for (std::size_t m = 0; m < measured_runs; ++m) {
        const auto run_base_id = static_cast<matching_engine::OrderId>(
            scenario_base_id + (warmup_runs + m + 1) * 100000
        );
        auto result = execute_concurrent_producer_run(
            producer_count,
            queue_capacity,
            run_base_id,
            true,
            workload
        );

        throughputs.push_back(result.throughput_ops_sec);

#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
        const double run_blocked_pct = result.queue_stats.total_pushes > 0
            ? (static_cast<double>(result.queue_stats.blocked_pushes) * 100.0 /
               static_cast<double>(result.queue_stats.total_pushes))
            : 0.0;
        std::cout << "    Run " << (m + 1) << ": "
                  << static_cast<std::int64_t>(result.throughput_ops_sec)
                  << " ops/sec (" << result.elapsed_ms << " ms) "
                  << "[Blocked: " << result.queue_stats.blocked_pushes
                  << "/" << result.queue_stats.total_pushes
                  << " (" << run_blocked_pct << "%)]\n";

        total_pushes_all += result.queue_stats.total_pushes;
        blocked_pushes_all += result.queue_stats.blocked_pushes;
        rejected_pushes_all += result.queue_stats.rejected_pushes;
        cumulative_wait_ns_all += result.queue_stats.cumulative_wait_ns;
        all_queue_wait_durations.insert(
            all_queue_wait_durations.end(),
            result.queue_stats.wait_durations_ns.begin(),
            result.queue_stats.wait_durations_ns.end()
        );
#else
        std::cout << "    Run " << (m + 1) << ": "
                  << static_cast<std::int64_t>(result.throughput_ops_sec)
                  << " ops/sec (" << result.elapsed_ms << " ms)\n";
#endif

        all_submit_durations.insert(
            all_submit_durations.end(),
            result.submit_api_durations.begin(),
            result.submit_api_durations.end()
        );
        all_obs_latencies.insert(
            all_obs_latencies.end(),
            result.observation_latencies.begin(),
            result.observation_latencies.end()
        );
    }

    std::sort(throughputs.begin(), throughputs.end());
    const double min_tp = throughputs.front();
    const double median_tp = throughputs[measured_runs / 2];
    const double max_tp = throughputs.back();

    const auto submit_stats = calculate_stats(std::move(all_submit_durations));
    const auto obs_stats = calculate_stats(std::move(all_obs_latencies));

    std::cout
        << "  Throughput (ops/sec):\n"
        << "    Min:    " << static_cast<std::int64_t>(min_tp) << "\n"
        << "    Median: " << static_cast<std::int64_t>(median_tp) << "\n"
        << "    Max:    " << static_cast<std::int64_t>(max_tp) << "\n"
        << "  Submission API duration (includes queue blocking, future creation, synchronization):\n"
        << "    Min: " << submit_stats.minimum_ns << " ns\n"
        << "    p50: " << submit_stats.p50_ns << " ns\n"
        << "    p95: " << submit_stats.p95_ns << " ns\n"
        << "    p99: " << submit_stats.p99_ns << " ns\n"
        << "    Max: " << submit_stats.maximum_ns << " ns\n"
        << "  Submission-to-observation latency (submit to future collection):\n"
        << "    Min: " << obs_stats.minimum_ns << " ns\n"
        << "    p50: " << obs_stats.p50_ns << " ns\n"
        << "    p95: " << obs_stats.p95_ns << " ns\n"
        << "    p99: " << obs_stats.p99_ns << " ns\n"
        << "    Max: " << obs_stats.maximum_ns << " ns\n";

#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
    const double blocked_pct = total_pushes_all > 0
        ? (static_cast<double>(blocked_pushes_all) * 100.0 /
           static_cast<double>(total_pushes_all))
        : 0.0;
    const double cumulative_wait_ms =
        static_cast<double>(cumulative_wait_ns_all) / 1'000'000.0;

    std::cout
        << "  Queue Backpressure Metrics (across 5 measured runs):\n"
        << "    Total push attempts: " << total_pushes_all << "\n"
        << "    Blocked submissions: " << blocked_pushes_all
        << " (" << blocked_pct << "%)\n"
        << "    Rejected submissions: " << rejected_pushes_all << "\n"
        << "    Cumulative queue wait time: " << cumulative_wait_ms << " ms\n";

    if (!all_queue_wait_durations.empty()) {
        const auto queue_wait_stats =
            calculate_stats(std::move(all_queue_wait_durations));
        std::cout
            << "    Wait duration percentiles (when blocked):\n"
            << "      Min: " << queue_wait_stats.minimum_ns << " ns\n"
            << "      p50: " << queue_wait_stats.p50_ns << " ns\n"
            << "      p95: " << queue_wait_stats.p95_ns << " ns\n"
            << "      p99: " << queue_wait_stats.p99_ns << " ns\n"
            << "      Max: " << queue_wait_stats.maximum_ns << " ns\n";
    } else {
        std::cout
            << "    Wait duration percentiles: N/A (no blocked submissions)\n";
    }
#endif
}

void run_p8_06_concurrent_workloads() {
#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
    std::cout << "\n=== P8-06 Concurrent Producer Throughput and Backpressure (INSTRUMENTED) ===\n";
    run_concurrent_producer_scenario(
        "P8-06 (Instrumented) - 1 producer, queue capacity 64",
        1, 64, 10000000
    );
    run_concurrent_producer_scenario(
        "P8-06 (Instrumented) - 2 producers, queue capacity 64",
        2, 64, 20000000
    );
    run_concurrent_producer_scenario(
        "P8-06 (Instrumented) - 4 producers, queue capacity 64",
        4, 64, 30000000
    );
    run_concurrent_producer_scenario(
        "P8-06 (Instrumented) - 4 producers, queue capacity 8",
        4, 8, 40000000
    );
    run_concurrent_producer_scenario(
        "P8-06 (Instrumented) - 4 producers, queue capacity 1024",
        4, 1024, 50000000
    );
#else
    std::cout << "\n=== P8-06 Concurrent Producer Throughput and Backpressure ===\n";
    run_concurrent_producer_scenario(
        "P8-06 - 1 producer, queue capacity 64",
        1, 64, 10000000
    );
    run_concurrent_producer_scenario(
        "P8-06 - 2 producers, queue capacity 64",
        2, 64, 20000000
    );
    run_concurrent_producer_scenario(
        "P8-06 - 4 producers, queue capacity 64",
        4, 64, 30000000
    );
    run_concurrent_producer_scenario(
        "P8-06 - 4 producers, queue capacity 8",
        4, 8, 40000000
    );
    run_concurrent_producer_scenario(
        "P8-06 - 4 producers, queue capacity 1024",
        4, 1024, 50000000
    );
#endif
}

void run_p8_08_concurrent_workloads() {
    std::cout
        << "\n=== P8-08 Concurrent Trading Workloads ===\n";

    struct Configuration {
        std::size_t producers;
        std::size_t capacity;
    };

    const Configuration configurations[] = {
        {1, 64},
        {2, 64},
        {4, 64},
        {4, 8},
        {4, 1024}
    };

    struct WorkloadDefinition {
        const char* name;
        ConcurrentWorkload workload;
    };

    const WorkloadDefinition workloads[] = {
        {"B6 - MARKET execution",
         ConcurrentWorkload::MARKET_EXECUTION},
        {"B7 - Cancellation",
         ConcurrentWorkload::CANCELLATION},
        {"B8 - Mixed trading commands",
         ConcurrentWorkload::MIXED}
    };

    matching_engine::OrderId scenario_id = 100000000;

    for (const auto& workload : workloads) {
        for (const auto& config : configurations) {
            run_concurrent_producer_scenario(
                workload.name,
                config.producers,
                config.capacity,
                scenario_id,
                workload.workload
            );

            scenario_id += 1000000;
        }
    }
}

} // namespace

int main() {
    try {
#ifdef MATCHING_ENGINE_QUEUE_INSTRUMENTATION
        std::cout << "=== Low-Latency Matching Engine Benchmarks (INSTRUMENTED QUEUE BUILD) ===\n"
                  << "[INSTRUMENTATION ACTIVE: Queue wait time and backpressure metrics enabled]\n"
                  << "[NOTE: Throughput and latency in this executable include queue instrumentation overhead;]\n"
                  << "[do NOT compare directly against the uninstrumented production throughput baseline.]\n\n";
#else
        std::cout << "=== Low-Latency Matching Engine Benchmarks ===\n\n";
#endif
        run_timer_baseline();
        run_b1_non_crossing_limit();
        run_b2_crossing_limit();
        run_b3_market_execution();
        run_b4_order_cancellation();
        run_p8_06_concurrent_workloads();
        run_p8_08_concurrent_workloads();
        std::cout << "\nNote: Measurements include container allocations and API overhead;\n"
                  << "they do not represent isolated algorithmic matching latency.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark error: "
                  << error.what() << '\n';
        return 1;
    }
}
