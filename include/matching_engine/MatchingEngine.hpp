#pragma once

#include <unordered_set>
#include <vector>
#include <cstddef>
#include <cstdint>

#include "matching_engine/OrderBook.hpp"
#include "matching_engine/Trade.hpp"

namespace matching_engine {

enum class SubmissionStatus {
    ACCEPTED,
    DUPLICATE_ID,
    INVALID_ORDER
};

struct SubmissionResult {
    SubmissionStatus status;
    Quantity executed_quantity;
    Quantity remaining_quantity;
};

struct MarketOrderAccounting {
    Quantity executed_quantity;
    Quantity cancelled_quantity;
};

#ifdef MATCHING_ENGINE_PROFILING
struct MarketProfilingStats {
    std::uint64_t preflight_ns = 0;
    std::uint64_t reservation_ns = 0;
    std::uint64_t execution_ns = 0;
    std::uint64_t submissions = 0;
};
#endif

class MatchingEngine {
private:
    OrderBook book_;
#ifdef MATCHING_ENGINE_TESTING
    using AcceptedOrderIds = std::unordered_set<
        OrderId,
        std::hash<OrderId>,
        std::equal_to<OrderId>,
        testing::TestingAllocator<OrderId>
    >;
#else
    using AcceptedOrderIds = std::unordered_set<OrderId>;
#endif

    AcceptedOrderIds accepted_order_ids_;
    std::vector<Trade> trades_;
#ifdef MATCHING_ENGINE_PROFILING
    bool exact_trade_growth_for_profiling_ = false;
    MarketProfilingStats market_profiling_stats_{};
#endif

public:
    using MarketOrderAccounting = matching_engine::MarketOrderAccounting;

    MatchingEngine() = default;

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    MatchingEngine(MatchingEngine&&) = delete;
    MatchingEngine& operator=(MatchingEngine&&) = delete;

    SubmissionResult submit_limit_order(const Order& order);
    SubmissionResult submit_market_order(const Order& order);
    bool cancel_order(OrderId id);

    MarketOrderAccounting market_order_accounting(
        const SubmissionResult& result
    ) const noexcept;
    bool was_order_id_accepted(OrderId id) const;

    const OrderBook& order_book() const;
    const std::vector<Trade>& trades() const noexcept;
#ifdef MATCHING_ENGINE_PROFILING
    void reserve_trade_capacity_for_profiling(
        std::size_t capacity
    );

    void enable_exact_trade_growth_for_profiling() noexcept;

    const MarketProfilingStats& market_profiling_stats() const noexcept;
#endif

#ifdef MATCHING_ENGINE_TESTING
    void set_insertion_failure_for_test(
        InsertionFailurePoint point
    ) noexcept {
        book_.set_insertion_failure_for_test(point);
    }
#endif
};

} // namespace matching_engine
